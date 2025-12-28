#include "ray_renderer.h"
#include "../../parametric/scene/scene_loader.h"
#include <QFile>
#include <QFileInfo>
#include <QVulkanFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QImage>
#include <QDateTime>
#include <QTimer>
#include <algorithm>
#include <cstring>

// Feature flag for GPU caustics (area-ratio method with caustic map)
// Based on Evan Wallace's technique adapted for compute shader
#define FEATURE_GPU_CAUSTICS 1

RayRenderer::RayRenderer(QVulkanWindow* window,
                               const QString& scenePath)
    : m_window(window), m_scenePath(scenePath)
{
}

void RayRenderer::initResources() {
    m_devFuncs = m_window->vulkanInstance()->deviceFunctions(m_window->device());

    // Guard against repeated initialization - only load scene once
    if (m_resourcesInitialized) {
        // Already initialized - don't reload scene or recreate buffers
        m_frameTimer.start();
        return;
    }
    m_resourcesInitialized = true;
    m_frameTimer.start();

    // Try loading scene from file (empty path = empty scene)
    bool sceneLoaded = false;
    if (!m_scenePath.isEmpty() && QFile::exists(m_scenePath)) {
        parametric::SceneData sceneData;
        if (parametric::SceneLoader::loadFile(m_scenePath.toStdString(), sceneData)) {
            printf("Loaded scene from %s\n", qPrintable(m_scenePath));

            // Build instances BEFORE moving materials (buildInstances uses materials.find)
            m_instances = sceneData.buildInstances();

            // Transfer data to renderer members
            m_csgScene = std::move(sceneData.csg);
            m_materials = std::move(sceneData.materials);
            m_lights = std::move(sceneData.lights);
            m_floor = sceneData.floor;
            m_background = sceneData.background;

            // Build patches from scene data
            auto patches = sceneData.allPatches();
            if (!patches.empty()) {
                m_patchGroup.build(patches);
                printf("  Patches:    %zu groups, %zu total patches\n",
                       sceneData.patchGroups.size(), patches.size());
            }

            printf("  Materials:  %u\n", m_materials.count());
            printf("  Primitives: %u\n", m_csgScene.primitiveCount());
            printf("  Nodes:      %u\n", m_csgScene.nodeCount());
            printf("  Roots:      %u\n", m_csgScene.rootCount());
            printf("  Instances:  %zu\n", m_instances.size());
            printf("  Sun:        az=%.1f° el=%.1f°\n", m_lights.sun.azimuth, m_lights.sun.elevation);
            if (m_lights.pointLightCount() > 0) {
                printf("  Point:      %u point lights\n", m_lights.pointLightCount());
            }
            if (m_lights.spotLightCount() > 0) {
                printf("  Spot:       %u spotlights\n", m_lights.spotLightCount());
            }

            // Build BVH for CSG roots
            m_csgBVH.build(m_csgScene);
            if (!m_csgBVH.empty()) {
                printf("  CSG BVH:    %zu nodes\n", m_csgBVH.nodeCount());
            }

            // Find emissive CSG objects for area light sampling
            findEmissiveLights();
            if (!m_lights.emissiveLights.empty()) {
                printf("  Emissive:   %u area lights\n", m_lights.emissiveCount());
            }

            // Build GI Gaussians from CSG primitives (for indirect illumination)
            // Caustics are handled separately by GPU caustic map
            m_giGaussians.placeOnCSG(m_csgScene, m_materials);

            // Cap total gaussians to prevent GPU timeout
            constexpr uint32_t MAX_GAUSSIANS = 4096;
            if (m_giGaussians.count() > MAX_GAUSSIANS) {
                m_giGaussians.truncate(MAX_GAUSSIANS);
            }

            if (m_giGaussians.count() > 0) {
                // Compute direct lighting for Gaussians
                m_giGaussians.computeDirectLighting(m_lights.sun,
                                                     m_lights.pointLights,
                                                     m_lights.spotLights);
                // Propagate light between Gaussians (radiosity)
                m_giGaussians.propagate(3);
                printf("  GI Gauss:   %u gaussians\n", m_giGaussians.count());
            }

            sceneLoaded = true;
        } else {
            fprintf(stderr, "Failed to parse scene file: %s\n", qPrintable(m_scenePath));
        }
    } else if (!m_scenePath.isEmpty()) {
        fprintf(stderr, "Scene file not found: %s\n", qPrintable(m_scenePath));
    }

    // Empty scene if no file loaded
    if (!sceneLoaded) {
        printf("No scene file loaded (empty scene)\n");
    }

    createPatchBuffers();
    createComputePipeline();
#if FEATURE_GPU_CAUSTICS
    createCausticsPipeline();
    runCausticsPass();
#endif
}

void RayRenderer::initSwapChainResources() {
    createStorageImage();
    createDescriptorSet();
    m_frameIndex = 0;
    m_needsImageTransition = true;
}

void RayRenderer::releaseSwapChainResources() {
    VkDevice dev = m_window->device();

    if (m_storageImageView) {
        m_devFuncs->vkDestroyImageView(dev, m_storageImageView, nullptr);
        m_storageImageView = VK_NULL_HANDLE;
    }
    if (m_storageImage) {
        m_devFuncs->vkDestroyImage(dev, m_storageImage, nullptr);
        m_storageImage = VK_NULL_HANDLE;
    }
    if (m_storageImageMemory) {
        m_devFuncs->vkFreeMemory(dev, m_storageImageMemory, nullptr);
        m_storageImageMemory = VK_NULL_HANDLE;
    }

    // Accumulation buffer cleanup
    if (m_accumImageView) {
        m_devFuncs->vkDestroyImageView(dev, m_accumImageView, nullptr);
        m_accumImageView = VK_NULL_HANDLE;
    }
    if (m_accumImage) {
        m_devFuncs->vkDestroyImage(dev, m_accumImage, nullptr);
        m_accumImage = VK_NULL_HANDLE;
    }
    if (m_accumImageMemory) {
        m_devFuncs->vkFreeMemory(dev, m_accumImageMemory, nullptr);
        m_accumImageMemory = VK_NULL_HANDLE;
    }

    if (m_descriptorPool) {
        m_devFuncs->vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}

void RayRenderer::releaseResources() {
    VkDevice dev = m_window->device();

    if (m_computePipeline) {
        m_devFuncs->vkDestroyPipeline(dev, m_computePipeline, nullptr);
        m_computePipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout) {
        m_devFuncs->vkDestroyPipelineLayout(dev, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    if (m_descriptorSetLayout) {
        m_devFuncs->vkDestroyDescriptorSetLayout(dev, m_descriptorSetLayout, nullptr);
        m_descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (m_patchBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_patchBuffer, nullptr);
        m_patchBuffer = VK_NULL_HANDLE;
    }
    if (m_patchBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_patchBufferMemory, nullptr);
        m_patchBufferMemory = VK_NULL_HANDLE;
    }
    if (m_bvhBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_bvhBuffer, nullptr);
        m_bvhBuffer = VK_NULL_HANDLE;
    }
    if (m_bvhBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_bvhBufferMemory, nullptr);
        m_bvhBufferMemory = VK_NULL_HANDLE;
    }
    if (m_patchIndexBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_patchIndexBuffer, nullptr);
        m_patchIndexBuffer = VK_NULL_HANDLE;
    }
    if (m_patchIndexBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_patchIndexBufferMemory, nullptr);
        m_patchIndexBufferMemory = VK_NULL_HANDLE;
    }
    if (m_instanceBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_instanceBuffer, nullptr);
        m_instanceBuffer = VK_NULL_HANDLE;
    }
    if (m_instanceBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_instanceBufferMemory, nullptr);
        m_instanceBufferMemory = VK_NULL_HANDLE;
    }

    // CSG buffers
    if (m_csgPrimitiveBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_csgPrimitiveBuffer, nullptr);
        m_csgPrimitiveBuffer = VK_NULL_HANDLE;
    }
    if (m_csgPrimitiveBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_csgPrimitiveBufferMemory, nullptr);
        m_csgPrimitiveBufferMemory = VK_NULL_HANDLE;
    }
    if (m_csgNodeBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_csgNodeBuffer, nullptr);
        m_csgNodeBuffer = VK_NULL_HANDLE;
    }
    if (m_csgNodeBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_csgNodeBufferMemory, nullptr);
        m_csgNodeBufferMemory = VK_NULL_HANDLE;
    }
    if (m_csgRootBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_csgRootBuffer, nullptr);
        m_csgRootBuffer = VK_NULL_HANDLE;
    }
    if (m_csgRootBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_csgRootBufferMemory, nullptr);
        m_csgRootBufferMemory = VK_NULL_HANDLE;
    }
    if (m_csgBVHBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_csgBVHBuffer, nullptr);
        m_csgBVHBuffer = VK_NULL_HANDLE;
    }
    if (m_csgBVHBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_csgBVHBufferMemory, nullptr);
        m_csgBVHBufferMemory = VK_NULL_HANDLE;
    }
    if (m_csgTransformBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_csgTransformBuffer, nullptr);
        m_csgTransformBuffer = VK_NULL_HANDLE;
    }
    if (m_csgTransformBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_csgTransformBufferMemory, nullptr);
        m_csgTransformBufferMemory = VK_NULL_HANDLE;
    }

    // Material buffer
    if (m_materialBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_materialBuffer, nullptr);
        m_materialBuffer = VK_NULL_HANDLE;
    }
    if (m_materialBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_materialBufferMemory, nullptr);
        m_materialBufferMemory = VK_NULL_HANDLE;
    }

    // Light buffer
    if (m_lightBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_lightBuffer, nullptr);
        m_lightBuffer = VK_NULL_HANDLE;
    }
    if (m_lightBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_lightBufferMemory, nullptr);
        m_lightBufferMemory = VK_NULL_HANDLE;
    }

    // Emissive light buffer
    if (m_emissiveLightBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_emissiveLightBuffer, nullptr);
        m_emissiveLightBuffer = VK_NULL_HANDLE;
    }
    if (m_emissiveLightBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_emissiveLightBufferMemory, nullptr);
        m_emissiveLightBufferMemory = VK_NULL_HANDLE;
    }

    // Gaussian buffer
    if (m_gaussianBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_gaussianBuffer, nullptr);
        m_gaussianBuffer = VK_NULL_HANDLE;
    }
    if (m_gaussianBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_gaussianBufferMemory, nullptr);
        m_gaussianBufferMemory = VK_NULL_HANDLE;
    }

    // Hardware ray tracing acceleration structures
    if (m_tlas && m_vkDestroyAccelerationStructureKHR) {
        m_vkDestroyAccelerationStructureKHR(dev, m_tlas, nullptr);
        m_tlas = VK_NULL_HANDLE;
    }
    if (m_tlasBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_tlasBuffer, nullptr);
        m_tlasBuffer = VK_NULL_HANDLE;
    }
    if (m_tlasBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_tlasBufferMemory, nullptr);
        m_tlasBufferMemory = VK_NULL_HANDLE;
    }
    if (m_blas && m_vkDestroyAccelerationStructureKHR) {
        m_vkDestroyAccelerationStructureKHR(dev, m_blas, nullptr);
        m_blas = VK_NULL_HANDLE;
    }
    if (m_blasBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_blasBuffer, nullptr);
        m_blasBuffer = VK_NULL_HANDLE;
    }
    if (m_blasBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_blasBufferMemory, nullptr);
        m_blasBufferMemory = VK_NULL_HANDLE;
    }
    if (m_aabbBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_aabbBuffer, nullptr);
        m_aabbBuffer = VK_NULL_HANDLE;
    }
    if (m_aabbBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_aabbBufferMemory, nullptr);
        m_aabbBufferMemory = VK_NULL_HANDLE;
    }

#if FEATURE_GPU_CAUSTICS
    // Caustics pipeline resources
    if (m_causticsPipeline) {
        m_devFuncs->vkDestroyPipeline(dev, m_causticsPipeline, nullptr);
        m_causticsPipeline = VK_NULL_HANDLE;
    }
    if (m_causticsPipelineLayout) {
        m_devFuncs->vkDestroyPipelineLayout(dev, m_causticsPipelineLayout, nullptr);
        m_causticsPipelineLayout = VK_NULL_HANDLE;
    }
    if (m_causticsDescriptorSetLayout) {
        m_devFuncs->vkDestroyDescriptorSetLayout(dev, m_causticsDescriptorSetLayout, nullptr);
        m_causticsDescriptorSetLayout = VK_NULL_HANDLE;
    }
    if (m_causticsDescriptorPool) {
        m_devFuncs->vkDestroyDescriptorPool(dev, m_causticsDescriptorPool, nullptr);
        m_causticsDescriptorPool = VK_NULL_HANDLE;
    }
    if (m_causticsPrimMaterialBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_causticsPrimMaterialBuffer, nullptr);
        m_causticsPrimMaterialBuffer = VK_NULL_HANDLE;
    }
    if (m_causticsPrimMaterialBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_causticsPrimMaterialBufferMemory, nullptr);
        m_causticsPrimMaterialBufferMemory = VK_NULL_HANDLE;
    }
    // Caustic hash buffer
    if (m_causticHashBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_causticHashBuffer, nullptr);
        m_causticHashBuffer = VK_NULL_HANDLE;
    }
    if (m_causticHashBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_causticHashBufferMemory, nullptr);
        m_causticHashBufferMemory = VK_NULL_HANDLE;
    }
#endif
}

void RayRenderer::startNextFrame() {
    // Calculate FPS
    qint64 now = m_frameTimer.nsecsElapsed();
    if (m_lastFrameTime > 0) {
        float frameMs = (now - m_lastFrameTime) / 1000000.0f;
        m_fps = m_fps * 0.9f + (1000.0f / frameMs) * 0.1f;  // Smoothed
    }
    m_lastFrameTime = now;

    // Update window title with resolution and FPS
    QSize sz = m_window->swapChainImageSize();
    m_window->setTitle(QString("Ray's Bouncy Castle - %1x%2 - %3 fps")
        .arg(sz.width()).arg(sz.height()).arg(static_cast<int>(m_fps)));

#if FEATURE_GPU_CAUSTICS
    // Real-time caustics update (when sun moves, etc.)
    if (m_causticsRealTime && m_causticsNeedUpdate) {
        runCausticsPass();
    }
#endif

    VkCommandBuffer cmdBuf = m_window->currentCommandBuffer();
    recordComputeCommands(cmdBuf);

    m_frameIndex++;

    m_window->frameReady();
    m_window->requestUpdate();
}

VkShaderModule RayRenderer::createShaderModule(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qFatal("Failed to open shader file: %s", qPrintable(path));
    }
    QByteArray code = file.readAll();

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule shaderModule;
    if (m_devFuncs->vkCreateShaderModule(m_window->device(), &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        qFatal("Failed to create shader module");
    }
    return shaderModule;
}

void RayRenderer::createStorageImage() {
    QSize sz = m_window->swapChainImageSize();

    // Output image (8-bit for display)
    createImage(sz.width(), sz.height(), VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                m_storageImage, m_storageImageMemory);
    m_storageImageView = createImageView(m_storageImage, VK_FORMAT_R8G8B8A8_UNORM);

    // Accumulation buffer (32-bit float for precision during progressive rendering)
    createImage(sz.width(), sz.height(), VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT,
                m_accumImage, m_accumImageMemory);
    m_accumImageView = createImageView(m_accumImage, VK_FORMAT_R32G32B32A32_SFLOAT);

    m_needsImageTransition = true;
}

void RayRenderer::createPatchBuffers() {
    VkDevice dev = m_window->device();

    // Get data from patch group
    std::vector<float> patchData = m_patchGroup.packPatchData();
    const auto& bvhNodes = m_patchGroup.bvhNodes();
    const auto& patchIndices = m_patchGroup.patchIndices();

    // Minimum sizes to avoid empty buffer issues
    size_t patchDataSize = std::max(patchData.size() * sizeof(float), size_t(64));
    size_t bvhSize = std::max(bvhNodes.size() * sizeof(BVHNode), size_t(32));
    size_t indexSize = std::max(patchIndices.size() * sizeof(uint32_t), size_t(4));
    size_t instanceSize = std::max(m_instances.size() * sizeof(BezierInstance), size_t(32));

    // Patch data: 16 vec4s per patch (control points)
    createBuffer(patchDataSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_patchBuffer, m_patchBufferMemory);

    // BVH nodes
    createBuffer(bvhSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_bvhBuffer, m_bvhBufferMemory);

    // Patch indices (reordered by BVH)
    createBuffer(indexSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_patchIndexBuffer, m_patchIndexBufferMemory);

    // Instance buffer
    createBuffer(instanceSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_instanceBuffer, m_instanceBufferMemory);

    // Upload data (zero-initialize if empty)
    void* data;

    m_devFuncs->vkMapMemory(dev, m_patchBufferMemory, 0, patchDataSize, 0, &data);
    if (!patchData.empty()) {
        memcpy(data, patchData.data(), patchData.size() * sizeof(float));
    } else {
        memset(data, 0, patchDataSize);
    }
    m_devFuncs->vkUnmapMemory(dev, m_patchBufferMemory);

    m_devFuncs->vkMapMemory(dev, m_bvhBufferMemory, 0, bvhSize, 0, &data);
    if (!bvhNodes.empty()) {
        memcpy(data, bvhNodes.data(), bvhNodes.size() * sizeof(BVHNode));
    } else {
        memset(data, 0, bvhSize);
    }
    m_devFuncs->vkUnmapMemory(dev, m_bvhBufferMemory);

    m_devFuncs->vkMapMemory(dev, m_patchIndexBufferMemory, 0, indexSize, 0, &data);
    if (!patchIndices.empty()) {
        memcpy(data, patchIndices.data(), patchIndices.size() * sizeof(uint32_t));
    } else {
        memset(data, 0, indexSize);
    }
    m_devFuncs->vkUnmapMemory(dev, m_patchIndexBufferMemory);

    m_devFuncs->vkMapMemory(dev, m_instanceBufferMemory, 0, instanceSize, 0, &data);
    if (!m_instances.empty()) {
        memcpy(data, m_instances.data(), m_instances.size() * sizeof(BezierInstance));
    } else {
        memset(data, 0, instanceSize);
    }
    m_devFuncs->vkUnmapMemory(dev, m_instanceBufferMemory);

    printf("Uploaded %u patches + %u BVH nodes + %zu instances (%.1f KB total)\n",
           m_patchGroup.subPatchCount(), m_patchGroup.bvhNodeCount(), m_instances.size(),
           (patchDataSize + bvhSize + indexSize + instanceSize) / 1024.0f);

    // Create CSG, material, and light buffers
    createCSGBuffers();
    createMaterialBuffer();
    createLightBuffer();
    createEmissiveLightBuffer();
    createGaussianBuffer();

    // Hardware ray tracing acceleration structures
    loadRayTracingFunctions();
    buildAccelerationStructures();
}

void RayRenderer::createDescriptorSet() {
    VkDevice dev = m_window->device();

    // Create descriptor pool (2 storage images + 14 buffers)
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};  // output + accumulation
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 14};  // patches, BVH, indices, instances, CSG prims/nodes/roots/bvh/transforms, materials, lights, emissive, gaussians, caustic hash

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (m_devFuncs->vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        qFatal("Failed to create descriptor pool");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (m_devFuncs->vkAllocateDescriptorSets(dev, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        qFatal("Failed to allocate descriptor set");
    }

    // Descriptor infos
    VkDescriptorImageInfo outputImageInfo{};
    outputImageInfo.imageView = m_storageImageView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo accumImageInfo{};
    accumImageInfo.imageView = m_accumImageView;
    accumImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo patchBufferInfo{};
    patchBufferInfo.buffer = m_patchBuffer;
    patchBufferInfo.offset = 0;
    patchBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo bvhBufferInfo{};
    bvhBufferInfo.buffer = m_bvhBuffer;
    bvhBufferInfo.offset = 0;
    bvhBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo indexBufferInfo{};
    indexBufferInfo.buffer = m_patchIndexBuffer;
    indexBufferInfo.offset = 0;
    indexBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo instanceBufferInfo{};
    instanceBufferInfo.buffer = m_instanceBuffer;
    instanceBufferInfo.offset = 0;
    instanceBufferInfo.range = VK_WHOLE_SIZE;

    // CSG buffer infos
    VkDescriptorBufferInfo csgPrimBufferInfo{};
    csgPrimBufferInfo.buffer = m_csgPrimitiveBuffer;
    csgPrimBufferInfo.offset = 0;
    csgPrimBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo csgNodeBufferInfo{};
    csgNodeBufferInfo.buffer = m_csgNodeBuffer;
    csgNodeBufferInfo.offset = 0;
    csgNodeBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo csgRootBufferInfo{};
    csgRootBufferInfo.buffer = m_csgRootBuffer;
    csgRootBufferInfo.offset = 0;
    csgRootBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo materialBufferInfo{};
    materialBufferInfo.buffer = m_materialBuffer;
    materialBufferInfo.offset = 0;
    materialBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo csgBVHBufferInfo{};
    csgBVHBufferInfo.buffer = m_csgBVHBuffer;
    csgBVHBufferInfo.offset = 0;
    csgBVHBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo csgTransformBufferInfo{};
    csgTransformBufferInfo.buffer = m_csgTransformBuffer;
    csgTransformBufferInfo.offset = 0;
    csgTransformBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo lightBufferInfo{};
    lightBufferInfo.buffer = m_lightBuffer;
    lightBufferInfo.offset = 0;
    lightBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo emissiveBufferInfo{};
    emissiveBufferInfo.buffer = m_emissiveLightBuffer;
    emissiveBufferInfo.offset = 0;
    emissiveBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo gaussianBufferInfo{};
    gaussianBufferInfo.buffer = m_gaussianBuffer;
    gaussianBufferInfo.offset = 0;
    gaussianBufferInfo.range = VK_WHOLE_SIZE;

#if FEATURE_GPU_CAUSTICS
    VkDescriptorBufferInfo causticHashBufInfo{};
    causticHashBufInfo.buffer = m_causticHashBuffer;
    causticHashBufInfo.offset = 0;
    causticHashBufInfo.range = VK_WHOLE_SIZE;
#endif

    std::array<VkWriteDescriptorSet, 16> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &outputImageInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &patchBufferInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &bvhBufferInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &indexBufferInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = m_descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &instanceBufferInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = m_descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[5].descriptorCount = 1;
    writes[5].pImageInfo = &accumImageInfo;

    // CSG buffers (bindings 6, 7, 8)
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = m_descriptorSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    writes[6].pBufferInfo = &csgPrimBufferInfo;

    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = m_descriptorSet;
    writes[7].dstBinding = 7;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    writes[7].pBufferInfo = &csgNodeBufferInfo;

    writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[8].dstSet = m_descriptorSet;
    writes[8].dstBinding = 8;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[8].descriptorCount = 1;
    writes[8].pBufferInfo = &csgRootBufferInfo;

    // Material buffer (binding 9)
    writes[9].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[9].dstSet = m_descriptorSet;
    writes[9].dstBinding = 9;
    writes[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[9].descriptorCount = 1;
    writes[9].pBufferInfo = &materialBufferInfo;

    // CSG BVH buffer (binding 10)
    writes[10].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[10].dstSet = m_descriptorSet;
    writes[10].dstBinding = 10;
    writes[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[10].descriptorCount = 1;
    writes[10].pBufferInfo = &csgBVHBufferInfo;

    // Light buffer (binding 11)
    writes[11].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[11].dstSet = m_descriptorSet;
    writes[11].dstBinding = 11;
    writes[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[11].descriptorCount = 1;
    writes[11].pBufferInfo = &lightBufferInfo;

    // Emissive light buffer (binding 12)
    writes[12].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[12].dstSet = m_descriptorSet;
    writes[12].dstBinding = 12;
    writes[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[12].descriptorCount = 1;
    writes[12].pBufferInfo = &emissiveBufferInfo;

    // GI Gaussian buffer (binding 13)
    writes[13].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[13].dstSet = m_descriptorSet;
    writes[13].dstBinding = 13;
    writes[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[13].descriptorCount = 1;
    writes[13].pBufferInfo = &gaussianBufferInfo;

    // CSG Transform buffer (binding 14)
    writes[14].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[14].dstSet = m_descriptorSet;
    writes[14].dstBinding = 14;
    writes[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[14].descriptorCount = 1;
    writes[14].pBufferInfo = &csgTransformBufferInfo;

    // Caustic hash buffer (binding 15) - only write if caustics enabled
#if FEATURE_GPU_CAUSTICS
    writes[15].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[15].dstSet = m_descriptorSet;
    writes[15].dstBinding = 15;
    writes[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[15].descriptorCount = 1;
    writes[15].pBufferInfo = &causticHashBufInfo;
    m_devFuncs->vkUpdateDescriptorSets(dev, writes.size(), writes.data(), 0, nullptr);
#else
    // Skip binding 15 when caustics disabled
    m_devFuncs->vkUpdateDescriptorSets(dev, 15, writes.data(), 0, nullptr);
#endif
}

void RayRenderer::createComputePipeline() {
    VkDevice dev = m_window->device();

    // Descriptor set layout (16 bindings: output image, 4 buffers, accum image, 4 CSG buffers, materials, lights, emissive, gaussians, transforms, caustic map)
    std::array<VkDescriptorSetLayoutBinding, 16> bindings{};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // CSG buffers (bindings 6, 7, 8)
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Material buffer (binding 9)
    bindings[9].binding = 9;
    bindings[9].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[9].descriptorCount = 1;
    bindings[9].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // CSG BVH buffer (binding 10)
    bindings[10].binding = 10;
    bindings[10].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[10].descriptorCount = 1;
    bindings[10].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Light buffer (binding 11)
    bindings[11].binding = 11;
    bindings[11].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[11].descriptorCount = 1;
    bindings[11].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Emissive light buffer (binding 12)
    bindings[12].binding = 12;
    bindings[12].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[12].descriptorCount = 1;
    bindings[12].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // GI Gaussian buffer (binding 13)
    bindings[13].binding = 13;
    bindings[13].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[13].descriptorCount = 1;
    bindings[13].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // CSG Transform buffer (binding 14)
    bindings[14].binding = 14;
    bindings[14].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[14].descriptorCount = 1;
    bindings[14].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Caustic hash buffer (binding 15)
    bindings[15].binding = 15;
    bindings[15].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[15].descriptorCount = 1;
    bindings[15].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    if (m_devFuncs->vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        qFatal("Failed to create descriptor set layout");
    }

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(RayPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        qFatal("Failed to create pipeline layout");
    }

    QString shaderPath = QCoreApplication::applicationDirPath() + "/shaders/ray.spv";
    VkShaderModule shaderModule = createShaderModule(shaderPath);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_pipelineLayout;

    if (m_devFuncs->vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_computePipeline) != VK_SUCCESS) {
        qFatal("Failed to create compute pipeline");
    }

    m_devFuncs->vkDestroyShaderModule(dev, shaderModule, nullptr);
}

#if FEATURE_GPU_CAUSTICS
// Push constants for caustics shader (area-ratio method)
// Must match caustics.comp exactly!
struct CausticsPushConstants {
    uint32_t gridWidth;
    uint32_t gridHeight;
    uint32_t numPrimitives;
    uint32_t numMaterials;
    float lightPosX, lightPosY, lightPosZ;
    float lightDirX, lightDirY, lightDirZ;
    float lightIntensity;
    uint32_t lightType;  // 0=directional, 1=point, 2=spot
    float lightRadius;
    float floorY;
    // Patch data (for dielectric teapots, etc.)
    uint32_t numPatches;
    uint32_t numBVHNodes;
    uint32_t numInstances;
    uint32_t _pad0;
    // Camera position for focusing ray grid on visible area
    float camPosX, camPosY, camPosZ;
    float _pad1;
};

void RayRenderer::createCausticsPipeline() {
    VkDevice dev = m_window->device();

    // Create caustic hash buffer (spatial hash for atomic accumulation)
    VkDeviceSize hashBufSize = CAUSTIC_HASH_SIZE * sizeof(uint32_t);
    createBuffer(hashBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                 m_causticHashBuffer, m_causticHashBufferMemory);
    printf("  Created caustic hash buffer: %u cells, RGB (%.1f KB)\n", CAUSTIC_HASH_CELLS, hashBufSize / 1024.0f);

    // Create descriptor set layout for caustics (9 bindings)
    // 0: Primitives, 1: Transforms, 2: Materials, 3: CausticHash, 4: PrimToMaterial
    // 5: Patches, 6: BVH, 7: Instances, 8: TLAS (acceleration structure)
    std::array<VkDescriptorSetLayoutBinding, 9> bindings{};

    // Binding 0: CSG Primitives
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: CSG Transforms
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: Materials
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: Caustic hash buffer (storage buffer for atomic writes)
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: Primitive-to-material mapping
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 5: Bezier Patches (control points)
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 6: Patch BVH nodes
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 7: Bezier Instances (transforms + material)
    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 8: TLAS (acceleration structure for hardware ray queries)
    bindings[8].binding = 8;
    bindings[8].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    bindings[8].descriptorCount = 1;
    bindings[8].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (m_devFuncs->vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_causticsDescriptorSetLayout) != VK_SUCCESS) {
        qWarning("Failed to create caustics descriptor set layout");
        return;
    }

    // Create descriptor pool (8 storage buffers + 1 acceleration structure)
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 8;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    if (m_devFuncs->vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_causticsDescriptorPool) != VK_SUCCESS) {
        qWarning("Failed to create caustics descriptor pool");
        return;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_causticsDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_causticsDescriptorSetLayout;

    if (m_devFuncs->vkAllocateDescriptorSets(dev, &allocInfo, &m_causticsDescriptorSet) != VK_SUCCESS) {
        qWarning("Failed to allocate caustics descriptor set");
        return;
    }

    // Create primitive-to-material mapping buffer
    // Map each primitive to its material ID by scanning root nodes
    size_t numPrims = m_csgScene.primitiveCount();
    std::vector<uint32_t> primToMaterial(numPrims, 0xFFFFFFFF);  // Invalid = no material

    const auto& nodes = m_csgScene.nodes();
    const auto& roots = m_csgScene.roots();
    for (uint32_t rootIdx : roots) {
        const auto& node = nodes[rootIdx];
        // For primitive nodes, left is the primitive index
        if (node.type == static_cast<uint32_t>(parametric::CSGNodeType::Primitive)) {
            uint32_t primIdx = node.left;
            if (primIdx < numPrims) {
                primToMaterial[primIdx] = node.materialId;
            }
        }
    }

    size_t primMatBufSize = std::max(numPrims, size_t(1)) * sizeof(uint32_t);
    createBuffer(primMatBufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_causticsPrimMaterialBuffer, m_causticsPrimMaterialBufferMemory);

    // Upload mapping
    if (numPrims > 0) {
        void* primMatData;
        m_devFuncs->vkMapMemory(dev, m_causticsPrimMaterialBufferMemory, 0, primMatBufSize, 0, &primMatData);
        memcpy(primMatData, primToMaterial.data(), primToMaterial.size() * sizeof(uint32_t));
        m_devFuncs->vkUnmapMemory(dev, m_causticsPrimMaterialBufferMemory);
    }

    // Update descriptor set
    VkDescriptorBufferInfo primInfo{};
    primInfo.buffer = m_csgPrimitiveBuffer;
    primInfo.offset = 0;
    primInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo xformInfo{};
    xformInfo.buffer = m_csgTransformBuffer;
    xformInfo.offset = 0;
    xformInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo matInfo{};
    matInfo.buffer = m_materialBuffer;
    matInfo.offset = 0;
    matInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo causticHashInfo{};
    causticHashInfo.buffer = m_causticHashBuffer;
    causticHashInfo.offset = 0;
    causticHashInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo primMatInfo{};
    primMatInfo.buffer = m_causticsPrimMaterialBuffer;
    primMatInfo.offset = 0;
    primMatInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo patchInfo{};
    patchInfo.buffer = m_patchBuffer;
    patchInfo.offset = 0;
    patchInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo bvhInfo{};
    bvhInfo.buffer = m_bvhBuffer;
    bvhInfo.offset = 0;
    bvhInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo instanceInfo{};
    instanceInfo.buffer = m_instanceBuffer;
    instanceInfo.offset = 0;
    instanceInfo.range = VK_WHOLE_SIZE;

    // Acceleration structure write info
    VkWriteDescriptorSetAccelerationStructureKHR asWriteInfo{};
    asWriteInfo.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;
    asWriteInfo.accelerationStructureCount = 1;
    asWriteInfo.pAccelerationStructures = &m_tlas;

    std::array<VkWriteDescriptorSet, 9> writes{};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_causticsDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &primInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_causticsDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &xformInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_causticsDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &matInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_causticsDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &causticHashInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = m_causticsDescriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &primMatInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = m_causticsDescriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    writes[5].pBufferInfo = &patchInfo;

    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = m_causticsDescriptorSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    writes[6].pBufferInfo = &bvhInfo;

    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = m_causticsDescriptorSet;
    writes[7].dstBinding = 7;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    writes[7].pBufferInfo = &instanceInfo;

    writes[8].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[8].dstSet = m_causticsDescriptorSet;
    writes[8].dstBinding = 8;
    writes[8].descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    writes[8].descriptorCount = 1;
    writes[8].pNext = &asWriteInfo;  // AS uses pNext instead of pBufferInfo

    m_devFuncs->vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    // Create pipeline layout
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(CausticsPushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_causticsDescriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_causticsPipelineLayout) != VK_SUCCESS) {
        qWarning("Failed to create caustics pipeline layout");
        return;
    }

    // Create compute pipeline
    QString shaderPath = QCoreApplication::applicationDirPath() + "/shaders/caustics.spv";
    VkShaderModule shaderModule = createShaderModule(shaderPath);

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = m_causticsPipelineLayout;

    if (m_devFuncs->vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_causticsPipeline) != VK_SUCCESS) {
        qWarning("Failed to create caustics compute pipeline");
    }

    m_devFuncs->vkDestroyShaderModule(dev, shaderModule, nullptr);
    printf("  Caustics pipeline created (area-ratio method)\n");
}

void RayRenderer::runCausticsPass() {
    if (!m_causticsPipeline || !m_causticsNeedUpdate) return;
    if (m_csgScene.primitiveCount() == 0) {
        if (!m_causticsRealTime) printf("  Caustics: no CSG primitives, skipping\n");
        return;
    }
    // Note: floor is optional - caustics now trace to any opaque geometry

    // Only print verbose output when not in real-time mode
    bool verbose = !m_causticsRealTime;

    // Count refractive primitives - dielectrics with real/rational IOR > 1.0
    // This includes glass, water, plastic, jello, gems, etc. - any transparent material
    // Metals have complex IOR (real + imaginary parts) and only reflect, not refract
    int refractiveCount = 0;
    const auto& prims = m_csgScene.primitives();
    const auto& nodes = m_csgScene.nodes();
    const auto& roots = m_csgScene.roots();
    for (uint32_t rootIdx : roots) {
        const auto& node = nodes[rootIdx];
        if (node.type == static_cast<uint32_t>(parametric::CSGNodeType::Primitive)) {
            uint32_t primIdx = node.left;
            uint32_t matId = node.materialId;
            if (primIdx < prims.size() && matId < m_materials.count()) {
                const auto& mat = m_materials.materials()[matId];
                // Check for dielectric materials (type 2) with real IOR > 1.0
                // These have rational/real IOR (refractive) vs metals with complex IOR (reflective only)
                if (mat.type == 2 && mat.ior > 1.001f) {
                    refractiveCount++;
                    if (verbose) printf("  Caustics: refractive prim %u (IOR=%.2f) at (%.1f, %.1f, %.1f)\n",
                           primIdx, mat.ior, prims[primIdx].x, prims[primIdx].y, prims[primIdx].z);
                }
            }
        }
    }
    if (verbose) {
        if (m_floor.enabled) {
            printf("  Caustics: %d refractive primitives out of %zu total, floor Y=%.1f\n",
                   refractiveCount, prims.size(), m_floor.y);
        } else {
            printf("  Caustics: %d refractive primitives out of %zu total (no floor, using geometry)\n",
                   refractiveCount, prims.size());
        }
    }

    // Count refractive Bezier instances (dielectric teapots, etc.)
    int refractiveInstCount = 0;
    const auto& mats = m_materials.materials();
    for (size_t i = 0; i < m_instances.size(); i++) {
        const auto& inst = m_instances[i];
        if (inst.materialId < mats.size()) {
            const auto& mat = mats[inst.materialId];
            if (mat.type == 2 && mat.ior > 1.001f) {
                refractiveInstCount++;
                if (verbose) printf("  Caustics: refractive instance %zu (IOR=%.2f) at (%.1f, %.1f, %.1f)\n",
                       i, mat.ior, inst.posX, inst.posY, inst.posZ);
            }
        }
    }
    if (verbose && !m_instances.empty()) {
        printf("  Caustics: %d refractive instances out of %zu total\n",
               refractiveInstCount, m_instances.size());
    }

    // Skip caustic pass if no refractive materials (IOR > 1.0)
    // Metals have complex IOR (reflect, don't refract) - only dielectrics cause caustics
    int totalRefractive = refractiveCount + refractiveInstCount;
    if (totalRefractive == 0) {
        if (verbose) printf("  Caustics: no refractive materials (IOR > 1.0), skipping pass\n");
        m_causticsNeedUpdate = false;
        return;
    }

    VkDevice dev = m_window->device();

    // Get sun direction (direction TO sun, so negate for direction FROM sun)
    float sunDirX, sunDirY, sunDirZ;
    m_lights.sun.getDirection(sunDirX, sunDirY, sunDirZ);
    if (verbose) printf("  Caustics: sun dir FROM sun = (%.3f, %.3f, %.3f)\n",
           -sunDirX, -sunDirY, -sunDirZ);

    // Compute world bounds for caustic map coverage
    // Use CSG primitive bounds
    float worldMinX = -10.0f, worldMaxX = 10.0f;
    float worldMinZ = -10.0f, worldMaxZ = 10.0f;
    if (!prims.empty()) {
        worldMinX = worldMaxX = prims[0].x;
        worldMinZ = worldMaxZ = prims[0].z;
        for (const auto& prim : prims) {
            float extent = std::max({prim.param0, prim.param1, prim.param2}) * 2.0f;
            worldMinX = std::min(worldMinX, prim.x - extent);
            worldMaxX = std::max(worldMaxX, prim.x + extent);
            worldMinZ = std::min(worldMinZ, prim.z - extent);
            worldMaxZ = std::max(worldMaxZ, prim.z + extent);
        }
        // Expand bounds to catch refracted rays
        float margin = 5.0f;
        worldMinX -= margin;
        worldMaxX += margin;
        worldMinZ -= margin;
        worldMaxZ += margin;
    }

    // Set up push constants for sun light (directional)
    CausticsPushConstants pc{};
    // Photon grid: 512² for real-time (speed), 1024² for static (quality)
    uint32_t gridSize = m_causticsRealTime ? 512 : 1024;
    pc.gridWidth = gridSize;
    pc.gridHeight = gridSize;
    pc.numPrimitives = m_csgScene.primitiveCount();
    pc.numMaterials = m_materials.count();
    // Light position (not used for directional, but set high above scene)
    pc.lightPosX = 0.0f;
    pc.lightPosY = 100.0f;
    pc.lightPosZ = 0.0f;
    // Light direction (FROM sun, so negate getDirection which gives TO sun)
    pc.lightDirX = -sunDirX;
    pc.lightDirY = -sunDirY;
    pc.lightDirZ = -sunDirZ;
    pc.lightIntensity = m_lights.sun.intensity;
    pc.lightType = 0;  // Directional
    pc.lightRadius = 0.0f;
    pc.floorY = m_floor.y;
    // Patch data for glass teapots, etc.
    pc.numPatches = m_patchGroup.subPatchCount();
    pc.numBVHNodes = m_patchGroup.bvhNodeCount();
    pc.numInstances = static_cast<uint32_t>(m_instances.size());
    pc._pad0 = 0;
    // Camera position - focus ray grid near where camera is looking
    m_camera.getPosition(pc.camPosX, pc.camPosY, pc.camPosZ);
    pc._pad1 = 0;

    // Create command buffer for caustics pass
    VkCommandBufferAllocateInfo cmdAllocInfo{};
    cmdAllocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdAllocInfo.commandPool = m_window->graphicsCommandPool();
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    m_devFuncs->vkAllocateCommandBuffers(dev, &cmdAllocInfo, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    m_devFuncs->vkBeginCommandBuffer(cmdBuf, &beginInfo);

    // Clear caustic hash buffer to zero
    m_devFuncs->vkCmdFillBuffer(cmdBuf, m_causticHashBuffer, 0, VK_WHOLE_SIZE, 0);

    // Memory barrier before compute
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &barrier, 0, nullptr, 0, nullptr);

    m_devFuncs->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_causticsPipeline);
    m_devFuncs->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
                                         m_causticsPipelineLayout, 0, 1, &m_causticsDescriptorSet, 0, nullptr);
    m_devFuncs->vkCmdPushConstants(cmdBuf, m_causticsPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
                                    0, sizeof(CausticsPushConstants), &pc);

    // Dispatch ray grid (16x16 workgroup size in shader)
    m_devFuncs->vkCmdDispatch(cmdBuf, (pc.gridWidth + 15) / 16, (pc.gridHeight + 15) / 16, 1);

    // Memory barrier for buffer to be readable
    VkMemoryBarrier readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    readBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 1, &readBarrier, 0, nullptr, 0, nullptr);

    m_devFuncs->vkEndCommandBuffer(cmdBuf);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    VkFence fence;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    m_devFuncs->vkCreateFence(dev, &fenceInfo, nullptr, &fence);

    m_devFuncs->vkQueueSubmit(m_window->graphicsQueue(), 1, &submitInfo, fence);
    m_devFuncs->vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

    m_devFuncs->vkDestroyFence(dev, fence, nullptr);
    m_devFuncs->vkFreeCommandBuffers(dev, m_window->graphicsCommandPool(), 1, &cmdBuf);

    if (verbose) {
        printf("  Caustics pass complete (%ux%u photons -> %u cell 3D hash)\n", pc.gridWidth, pc.gridHeight, CAUSTIC_HASH_CELLS);
    }
    if (!m_causticsRealTime) {
        m_causticsNeedUpdate = false;
    }
}
#endif // FEATURE_GPU_CAUSTICS

// ============================================================================
// Hardware Ray Tracing (VK_KHR_ray_query)
// ============================================================================

void RayRenderer::loadRayTracingFunctions() {
    VkDevice dev = m_window->device();
    auto inst = m_window->vulkanInstance();
    auto getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(
        inst->getInstanceProcAddr("vkGetDeviceProcAddr"));

    m_vkCreateAccelerationStructureKHR = reinterpret_cast<PFN_vkCreateAccelerationStructureKHR>(
        getDeviceProcAddr(dev, "vkCreateAccelerationStructureKHR"));
    m_vkDestroyAccelerationStructureKHR = reinterpret_cast<PFN_vkDestroyAccelerationStructureKHR>(
        getDeviceProcAddr(dev, "vkDestroyAccelerationStructureKHR"));
    m_vkGetAccelerationStructureBuildSizesKHR = reinterpret_cast<PFN_vkGetAccelerationStructureBuildSizesKHR>(
        getDeviceProcAddr(dev, "vkGetAccelerationStructureBuildSizesKHR"));
    m_vkCmdBuildAccelerationStructuresKHR = reinterpret_cast<PFN_vkCmdBuildAccelerationStructuresKHR>(
        getDeviceProcAddr(dev, "vkCmdBuildAccelerationStructuresKHR"));
    m_vkGetBufferDeviceAddressKHR = reinterpret_cast<PFN_vkGetBufferDeviceAddressKHR>(
        getDeviceProcAddr(dev, "vkGetBufferDeviceAddressKHR"));
    m_vkGetAccelerationStructureDeviceAddressKHR = reinterpret_cast<PFN_vkGetAccelerationStructureDeviceAddressKHR>(
        getDeviceProcAddr(dev, "vkGetAccelerationStructureDeviceAddressKHR"));

    if (!m_vkCreateAccelerationStructureKHR || !m_vkGetAccelerationStructureBuildSizesKHR) {
        printf("Warning: Ray tracing functions not available\n");
        return;
    }
    printf("  Ray tracing functions loaded (VK_KHR_ray_query ready)\n");
}

void RayRenderer::buildAccelerationStructures() {
    if (!m_vkCreateAccelerationStructureKHR) {
        printf("  Skipping AS build - ray tracing not available\n");
        return;
    }

    const auto& prims = m_csgScene.primitives();
    if (prims.empty()) {
        printf("  Skipping AS build - no CSG primitives\n");
        return;
    }

    VkDevice dev = m_window->device();

    // Build AABBs for each CSG primitive
    std::vector<VkAabbPositionsKHR> aabbs;
    aabbs.reserve(prims.size());

    for (size_t i = 0; i < prims.size(); i++) {
        // Get AABB from CSG scene
        parametric::AABB box = m_csgScene.computePrimitiveAABB(static_cast<uint32_t>(i));

        VkAabbPositionsKHR aabb{};
        aabb.minX = box.min.x;
        aabb.minY = box.min.y;
        aabb.minZ = box.min.z;
        aabb.maxX = box.max.x;
        aabb.maxY = box.max.y;
        aabb.maxZ = box.max.z;
        aabbs.push_back(aabb);
    }

    // Create AABB buffer
    VkDeviceSize aabbBufferSize = sizeof(VkAabbPositionsKHR) * aabbs.size();
    createBuffer(aabbBufferSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_aabbBuffer, m_aabbBufferMemory);

    // Upload AABBs
    void* data;
    m_devFuncs->vkMapMemory(dev, m_aabbBufferMemory, 0, aabbBufferSize, 0, &data);
    memcpy(data, aabbs.data(), aabbBufferSize);
    m_devFuncs->vkUnmapMemory(dev, m_aabbBufferMemory);

    // Get AABB buffer device address
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = m_aabbBuffer;
    VkDeviceAddress aabbAddress = m_vkGetBufferDeviceAddressKHR(dev, &addrInfo);

    // BLAS geometry description
    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_AABBS_KHR;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.geometry.aabbs.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_AABBS_DATA_KHR;
    geometry.geometry.aabbs.data.deviceAddress = aabbAddress;
    geometry.geometry.aabbs.stride = sizeof(VkAabbPositionsKHR);

    // Get build sizes
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;

    uint32_t primitiveCount = static_cast<uint32_t>(aabbs.size());
    VkAccelerationStructureBuildSizesInfoKHR sizeInfo{};
    sizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &primitiveCount, &sizeInfo);

    // Create BLAS buffer
    createBuffer(sizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_blasBuffer, m_blasBufferMemory);

    // Create BLAS
    VkAccelerationStructureCreateInfoKHR createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    createInfo.buffer = m_blasBuffer;
    createInfo.size = sizeInfo.accelerationStructureSize;
    createInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    m_vkCreateAccelerationStructureKHR(dev, &createInfo, nullptr, &m_blas);

    // Create scratch buffer for build
    VkBuffer scratchBuffer;
    VkDeviceMemory scratchMemory;
    createBuffer(sizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        scratchBuffer, scratchMemory);

    VkBufferDeviceAddressInfo scratchAddrInfo{};
    scratchAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchAddrInfo.buffer = scratchBuffer;
    VkDeviceAddress scratchAddress = m_vkGetBufferDeviceAddressKHR(dev, &scratchAddrInfo);

    // Build BLAS
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.dstAccelerationStructure = m_blas;
    buildInfo.scratchData.deviceAddress = scratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = primitiveCount;
    rangeInfo.primitiveOffset = 0;
    rangeInfo.firstVertex = 0;
    rangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &rangeInfo;

    // Execute build on command buffer
    VkCommandBuffer cmdBuf;
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_window->graphicsCommandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    m_devFuncs->vkAllocateCommandBuffers(dev, &allocInfo, &cmdBuf);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_devFuncs->vkBeginCommandBuffer(cmdBuf, &beginInfo);

    m_vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &buildInfo, &pRangeInfo);

    m_devFuncs->vkEndCommandBuffer(cmdBuf);

    // Submit and wait
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence;
    m_devFuncs->vkCreateFence(dev, &fenceInfo, nullptr, &fence);
    m_devFuncs->vkQueueSubmit(m_window->graphicsQueue(), 1, &submitInfo, fence);
    m_devFuncs->vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

    // Cleanup
    m_devFuncs->vkDestroyFence(dev, fence, nullptr);
    m_devFuncs->vkFreeCommandBuffers(dev, m_window->graphicsCommandPool(), 1, &cmdBuf);
    m_devFuncs->vkDestroyBuffer(dev, scratchBuffer, nullptr);
    m_devFuncs->vkFreeMemory(dev, scratchMemory, nullptr);

    printf("  Built BLAS: %zu CSG primitives, %.1f KB\n",
           aabbs.size(), sizeInfo.accelerationStructureSize / 1024.0f);

    // Build TLAS with single instance referencing BLAS
    // Get BLAS device address
    VkAccelerationStructureDeviceAddressInfoKHR blasAddrInfo{};
    blasAddrInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    blasAddrInfo.accelerationStructure = m_blas;
    VkDeviceAddress blasAddress = m_vkGetAccelerationStructureDeviceAddressKHR(dev, &blasAddrInfo);

    // Create instance (identity transform, all primitives in world space)
    VkAccelerationStructureInstanceKHR instance{};
    // Identity transform (3x4 row-major)
    instance.transform.matrix[0][0] = 1.0f;
    instance.transform.matrix[1][1] = 1.0f;
    instance.transform.matrix[2][2] = 1.0f;
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
    instance.accelerationStructureReference = blasAddress;

    // Create instance buffer
    VkBuffer instanceBuffer;
    VkDeviceMemory instanceBufferMemory;
    VkDeviceSize instanceBufferSize = sizeof(VkAccelerationStructureInstanceKHR);
    createBuffer(instanceBufferSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceBuffer, instanceBufferMemory);

    // Upload instance
    m_devFuncs->vkMapMemory(dev, instanceBufferMemory, 0, instanceBufferSize, 0, &data);
    memcpy(data, &instance, instanceBufferSize);
    m_devFuncs->vkUnmapMemory(dev, instanceBufferMemory);

    // Get instance buffer device address
    VkBufferDeviceAddressInfo instanceAddrInfo{};
    instanceAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    instanceAddrInfo.buffer = instanceBuffer;
    VkDeviceAddress instanceAddress = m_vkGetBufferDeviceAddressKHR(dev, &instanceAddrInfo);

    // TLAS geometry description
    VkAccelerationStructureGeometryKHR tlasGeometry{};
    tlasGeometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    tlasGeometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    tlasGeometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    tlasGeometry.geometry.instances.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    tlasGeometry.geometry.instances.arrayOfPointers = VK_FALSE;
    tlasGeometry.geometry.instances.data.deviceAddress = instanceAddress;

    // Get TLAS build sizes
    VkAccelerationStructureBuildGeometryInfoKHR tlasBuildInfo{};
    tlasBuildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    tlasBuildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    tlasBuildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    tlasBuildInfo.geometryCount = 1;
    tlasBuildInfo.pGeometries = &tlasGeometry;

    uint32_t instanceCount = 1;
    VkAccelerationStructureBuildSizesInfoKHR tlasSizeInfo{};
    tlasSizeInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    m_vkGetAccelerationStructureBuildSizesKHR(dev, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &tlasBuildInfo, &instanceCount, &tlasSizeInfo);

    // Create TLAS buffer
    createBuffer(tlasSizeInfo.accelerationStructureSize,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        m_tlasBuffer, m_tlasBufferMemory);

    // Create TLAS
    VkAccelerationStructureCreateInfoKHR tlasCreateInfo{};
    tlasCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    tlasCreateInfo.buffer = m_tlasBuffer;
    tlasCreateInfo.size = tlasSizeInfo.accelerationStructureSize;
    tlasCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    m_vkCreateAccelerationStructureKHR(dev, &tlasCreateInfo, nullptr, &m_tlas);

    // Create scratch buffer for TLAS build
    VkBuffer tlasScratchBuffer;
    VkDeviceMemory tlasScratchMemory;
    createBuffer(tlasSizeInfo.buildScratchSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        tlasScratchBuffer, tlasScratchMemory);

    VkBufferDeviceAddressInfo tlasScratchAddrInfo{};
    tlasScratchAddrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    tlasScratchAddrInfo.buffer = tlasScratchBuffer;
    VkDeviceAddress tlasScratchAddress = m_vkGetBufferDeviceAddressKHR(dev, &tlasScratchAddrInfo);

    // Build TLAS
    tlasBuildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    tlasBuildInfo.dstAccelerationStructure = m_tlas;
    tlasBuildInfo.scratchData.deviceAddress = tlasScratchAddress;

    VkAccelerationStructureBuildRangeInfoKHR tlasRangeInfo{};
    tlasRangeInfo.primitiveCount = instanceCount;
    tlasRangeInfo.primitiveOffset = 0;
    tlasRangeInfo.firstVertex = 0;
    tlasRangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pTlasRangeInfo = &tlasRangeInfo;

    // Execute TLAS build
    m_devFuncs->vkAllocateCommandBuffers(dev, &allocInfo, &cmdBuf);
    m_devFuncs->vkBeginCommandBuffer(cmdBuf, &beginInfo);
    m_vkCmdBuildAccelerationStructuresKHR(cmdBuf, 1, &tlasBuildInfo, &pTlasRangeInfo);
    m_devFuncs->vkEndCommandBuffer(cmdBuf);

    m_devFuncs->vkCreateFence(dev, &fenceInfo, nullptr, &fence);
    m_devFuncs->vkQueueSubmit(m_window->graphicsQueue(), 1, &submitInfo, fence);
    m_devFuncs->vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX);

    // Cleanup TLAS build resources
    m_devFuncs->vkDestroyFence(dev, fence, nullptr);
    m_devFuncs->vkFreeCommandBuffers(dev, m_window->graphicsCommandPool(), 1, &cmdBuf);
    m_devFuncs->vkDestroyBuffer(dev, tlasScratchBuffer, nullptr);
    m_devFuncs->vkFreeMemory(dev, tlasScratchMemory, nullptr);
    m_devFuncs->vkDestroyBuffer(dev, instanceBuffer, nullptr);
    m_devFuncs->vkFreeMemory(dev, instanceBufferMemory, nullptr);

    printf("  Built TLAS: 1 instance, %.1f KB\n",
           tlasSizeInfo.accelerationStructureSize / 1024.0f);
}

void RayRenderer::recordComputeCommands(VkCommandBuffer cmdBuf) {
    QSize sz = m_window->swapChainImageSize();

    if (m_needsImageTransition) {
        // Output image transition
        transitionImageLayout(cmdBuf, m_storageImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        // Accumulation image transition
        transitionImageLayout(cmdBuf, m_accumImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        m_needsImageTransition = false;
    }

    m_devFuncs->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    m_devFuncs->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    RayPushConstants pc{};
    pc.width = sz.width();
    pc.height = sz.height();
    pc.numPatches = m_patchGroup.subPatchCount();
    pc.numBVHNodes = m_patchGroup.bvhNodeCount();
    pc.frameIndex = m_frameIndex;

    m_camera.getPosition(pc.camPosX, pc.camPosY, pc.camPosZ);
    pc.camTargetX = m_camera.targetX;
    pc.camTargetY = m_camera.targetY;
    pc.camTargetZ = m_camera.targetZ;
    pc.numInstances = static_cast<uint32_t>(m_instances.size());
    pc.numCSGPrimitives = m_csgScene.primitiveCount();
    pc.numCSGNodes = m_csgScene.nodeCount();
    pc.numCSGRoots = m_csgScene.rootCount();
    pc.numCSGBVHNodes = static_cast<uint32_t>(m_csgBVH.nodeCount());
    pc.numMaterials = m_materials.count();
    pc.numLights = m_lights.totalCount();
    pc.sunAngularRadius = m_lights.sunAngularRadius() * 3.14159265f / 180.0f;  // Convert to radians
    pc.floorEnabled = m_floor.enabled ? 1 : 0;
    pc.floorY = m_floor.y;
    pc.floorMaterialId = m_materials.find(m_floor.materialName);
    pc.numEmissiveLights = m_lights.emissiveCount();
    pc.numSpotLights = m_lights.spotLightCount();
    pc.bgR = m_background.r;
    pc.bgG = m_background.g;
    pc.bgB = m_background.b;
    pc.skyAmbient = m_lights.skyAmbient();
    pc.qualityLevel = m_qualityLevel;
    pc.numGaussians = m_giGaussians.count();  // GI gaussians only (caustics use separate buffer)

    m_devFuncs->vkCmdPushConstants(cmdBuf, m_pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(RayPushConstants), &pc);

    uint32_t groupCountX = (sz.width() + 15) / 16;
    uint32_t groupCountY = (sz.height() + 15) / 16;
    m_devFuncs->vkCmdDispatch(cmdBuf, groupCountX, groupCountY, 1);

    // Barriers and blit to swapchain
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.image = m_storageImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.layerCount = 1;

    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);

    VkImage swapchainImage = m_window->swapChainImage(m_window->currentSwapChainImageIndex());

    VkImageMemoryBarrier swapBarrier{};
    swapBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    swapBarrier.srcAccessMask = 0;
    swapBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    swapBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapBarrier.image = swapchainImage;
    swapBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    swapBarrier.subresourceRange.levelCount = 1;
    swapBarrier.subresourceRange.layerCount = 1;

    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &swapBarrier);

    VkImageBlit blit{};
    blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.srcSubresource.layerCount = 1;
    blit.srcOffsets[1] = {static_cast<int32_t>(sz.width()), static_cast<int32_t>(sz.height()), 1};
    blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    blit.dstSubresource.layerCount = 1;
    blit.dstOffsets[1] = {static_cast<int32_t>(sz.width()), static_cast<int32_t>(sz.height()), 1};

    m_devFuncs->vkCmdBlitImage(cmdBuf,
        m_storageImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        swapchainImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1, &blit, VK_FILTER_NEAREST);

    swapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    swapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &swapBarrier);

    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

void RayRenderer::createMaterialBuffer() {
    VkDevice dev = m_window->device();

    const auto& mats = m_materials.materials();
    size_t matSize = std::max(mats.size() * sizeof(Material), size_t(32));

    createBuffer(matSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_materialBuffer, m_materialBufferMemory);

    if (!mats.empty()) {
        void* data;
        m_devFuncs->vkMapMemory(dev, m_materialBufferMemory, 0, matSize, 0, &data);
        memcpy(data, mats.data(), mats.size() * sizeof(Material));
        m_devFuncs->vkUnmapMemory(dev, m_materialBufferMemory);
    }
}

void RayRenderer::createLightBuffer() {
    VkDevice dev = m_window->device();

    std::vector<Light> lights = m_lights.buildBuffer();
    size_t lightSize = std::max(lights.size() * sizeof(Light), size_t(32));

    createBuffer(lightSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_lightBuffer, m_lightBufferMemory);

    if (!lights.empty()) {
        void* data;
        m_devFuncs->vkMapMemory(dev, m_lightBufferMemory, 0, lightSize, 0, &data);
        memcpy(data, lights.data(), lights.size() * sizeof(Light));
        m_devFuncs->vkUnmapMemory(dev, m_lightBufferMemory);
    }
}

void RayRenderer::updateLightBuffer() {
    if (!m_lightBuffer || !m_lightBufferMemory) return;

    VkDevice dev = m_window->device();
    std::vector<Light> lights = m_lights.buildBuffer();

    if (!lights.empty()) {
        void* data;
        m_devFuncs->vkMapMemory(dev, m_lightBufferMemory, 0, lights.size() * sizeof(Light), 0, &data);
        memcpy(data, lights.data(), lights.size() * sizeof(Light));
        m_devFuncs->vkUnmapMemory(dev, m_lightBufferMemory);
    }
}

void RayRenderer::toggleRealTimeCaustics() {
    m_causticsRealTime = !m_causticsRealTime;
    m_causticsNeedUpdate = true;
    if (m_causticsRealTime) {
        printf("Real-time caustics: ON\n");
    } else {
        printf("Real-time caustics: OFF\n");
        // Run high-quality pass immediately when turning off real-time mode
        runCausticsPass();
    }
    m_window->requestUpdate();
}

void RayRenderer::adjustSunAzimuth(float deltaDegrees) {
    if (!hasSun()) return;
    m_lights.sun.azimuth += deltaDegrees;
    // Wrap around 0-360
    if (m_lights.sun.azimuth < 0.0f) m_lights.sun.azimuth += 360.0f;
    if (m_lights.sun.azimuth >= 360.0f) m_lights.sun.azimuth -= 360.0f;
    updateLightBuffer();
    m_causticsNeedUpdate = true;
    m_frameIndex = 0;  // Reset accumulation
    printf("Sun: az=%.1f° el=%.1f°\n", m_lights.sun.azimuth, m_lights.sun.elevation);
    m_window->requestUpdate();
}

void RayRenderer::adjustSunElevation(float deltaDegrees) {
    if (!hasSun()) return;
    m_lights.sun.elevation += deltaDegrees;
    // Clamp to 0-90 (below horizon doesn't make sense for caustics)
    m_lights.sun.elevation = std::max(0.0f, std::min(90.0f, m_lights.sun.elevation));
    updateLightBuffer();
    m_causticsNeedUpdate = true;
    m_frameIndex = 0;  // Reset accumulation
    printf("Sun: az=%.1f° el=%.1f°\n", m_lights.sun.azimuth, m_lights.sun.elevation);
    m_window->requestUpdate();
}

void RayRenderer::createEmissiveLightBuffer() {
    VkDevice dev = m_window->device();

    const auto& emissive = m_lights.emissiveBuffer();
    size_t bufSize = std::max(emissive.size() * sizeof(parametric::EmissiveLight), size_t(32));

    createBuffer(bufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_emissiveLightBuffer, m_emissiveLightBufferMemory);

    if (!emissive.empty()) {
        void* data;
        m_devFuncs->vkMapMemory(dev, m_emissiveLightBufferMemory, 0, bufSize, 0, &data);
        memcpy(data, emissive.data(), emissive.size() * sizeof(parametric::EmissiveLight));
        m_devFuncs->vkUnmapMemory(dev, m_emissiveLightBufferMemory);
    }
}

void RayRenderer::findEmissiveLights() {
    // Scan CSG roots for emissive materials
    // For now, only support single-primitive roots (not CSG combinations)
    const auto& nodes = m_csgScene.nodes();
    const auto& roots = m_csgScene.roots();
    const auto& mats = m_materials.materials();

    m_lights.emissiveLights.clear();

    for (uint32_t rootIdx : roots) {
        const auto& node = nodes[rootIdx];

        // Only support primitive nodes for now
        if (node.type != static_cast<uint32_t>(parametric::CSGNodeType::Primitive)) {
            continue;
        }

        // Check if material is emissive
        if (node.materialId >= mats.size()) continue;
        const auto& mat = mats[node.materialId];

        if (mat.type == static_cast<uint32_t>(parametric::MaterialType::Emissive) &&
            mat.emissive > 0.0f) {
            // Found an emissive primitive
            parametric::EmissiveLight light;
            light.primitiveIndex = node.left;  // For primitive nodes, left = prim index
            light.nodeIndex = rootIdx;
            light.area = m_csgScene.computePrimitiveSurfaceArea(node.left);
            m_lights.emissiveLights.push_back(light);
        }
    }
}

void RayRenderer::createGaussianBuffer() {
    VkDevice dev = m_window->device();

    const auto& gaussians = m_giGaussians.gaussians();
    // GI Gaussians only - caustics use separate spatial hash buffer
    size_t bufSize = std::max(gaussians.size() * sizeof(GIGaussian), size_t(48));

    createBuffer(bufSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_gaussianBuffer, m_gaussianBufferMemory);

    if (!gaussians.empty()) {
        void* data;
        m_devFuncs->vkMapMemory(dev, m_gaussianBufferMemory, 0, bufSize, 0, &data);
        memcpy(data, gaussians.data(), gaussians.size() * sizeof(GIGaussian));
        m_devFuncs->vkUnmapMemory(dev, m_gaussianBufferMemory);
    }
}

void RayRenderer::createCSGBuffers() {
    VkDevice dev = m_window->device();

    const auto& prims = m_csgScene.primitives();
    const auto& transforms = m_csgScene.transforms();
    const auto& nodes = m_csgScene.nodes();
    const auto& roots = m_csgScene.roots();
    const auto& bvhNodes = m_csgBVH.nodes;
    const auto& bvhRootIndices = m_csgBVH.rootIndices;

    // Minimum size to avoid empty buffer issues
    size_t primSize = std::max(prims.size() * sizeof(CSGPrimitive), size_t(32));
    size_t transformSize = std::max(transforms.size() * sizeof(CSGTransform), size_t(16));
    size_t nodeSize = std::max(nodes.size() * sizeof(CSGNode), size_t(16));
    size_t rootSize = std::max(bvhRootIndices.size() * sizeof(uint32_t), size_t(4));
    size_t bvhSize = std::max(bvhNodes.size() * sizeof(CSGBVHNode), size_t(32));

    // Create buffers
    createBuffer(primSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_csgPrimitiveBuffer, m_csgPrimitiveBufferMemory);

    createBuffer(nodeSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_csgNodeBuffer, m_csgNodeBufferMemory);

    createBuffer(rootSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_csgRootBuffer, m_csgRootBufferMemory);

    createBuffer(bvhSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_csgBVHBuffer, m_csgBVHBufferMemory);

    createBuffer(transformSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_csgTransformBuffer, m_csgTransformBufferMemory);

    // Upload data
    void* data;

    if (!prims.empty()) {
        m_devFuncs->vkMapMemory(dev, m_csgPrimitiveBufferMemory, 0, primSize, 0, &data);
        memcpy(data, prims.data(), prims.size() * sizeof(CSGPrimitive));
        m_devFuncs->vkUnmapMemory(dev, m_csgPrimitiveBufferMemory);
    }

    if (!nodes.empty()) {
        m_devFuncs->vkMapMemory(dev, m_csgNodeBufferMemory, 0, nodeSize, 0, &data);
        memcpy(data, nodes.data(), nodes.size() * sizeof(CSGNode));
        m_devFuncs->vkUnmapMemory(dev, m_csgNodeBufferMemory);
    }

    // Upload BVH-reordered root indices (or original roots if no BVH)
    // Note: bvhRootIndices contains indices INTO roots[], not actual node indices
    // We need to dereference: reorderedRoots[i] = roots[bvhRootIndices[i]]
    if (!bvhRootIndices.empty()) {
        std::vector<uint32_t> reorderedRoots(bvhRootIndices.size());
        for (size_t i = 0; i < bvhRootIndices.size(); i++) {
            reorderedRoots[i] = roots[bvhRootIndices[i]];
        }
        m_devFuncs->vkMapMemory(dev, m_csgRootBufferMemory, 0, rootSize, 0, &data);
        memcpy(data, reorderedRoots.data(), reorderedRoots.size() * sizeof(uint32_t));
        m_devFuncs->vkUnmapMemory(dev, m_csgRootBufferMemory);
    } else if (!roots.empty()) {
        m_devFuncs->vkMapMemory(dev, m_csgRootBufferMemory, 0, rootSize, 0, &data);
        memcpy(data, roots.data(), roots.size() * sizeof(uint32_t));
        m_devFuncs->vkUnmapMemory(dev, m_csgRootBufferMemory);
    }

    if (!bvhNodes.empty()) {
        m_devFuncs->vkMapMemory(dev, m_csgBVHBufferMemory, 0, bvhSize, 0, &data);
        memcpy(data, bvhNodes.data(), bvhNodes.size() * sizeof(CSGBVHNode));
        m_devFuncs->vkUnmapMemory(dev, m_csgBVHBufferMemory);
    }

    if (!transforms.empty()) {
        m_devFuncs->vkMapMemory(dev, m_csgTransformBufferMemory, 0, transformSize, 0, &data);
        memcpy(data, transforms.data(), transforms.size() * sizeof(CSGTransform));
        m_devFuncs->vkUnmapMemory(dev, m_csgTransformBufferMemory);
    }

    printf("Uploaded CSG: %zu primitives (%.1f KB), %zu nodes, %zu roots, %zu BVH nodes\n",
           prims.size(), primSize / 1024.0f, nodes.size(), bvhRootIndices.size(), bvhNodes.size());
}

uint32_t RayRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    m_window->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(
        m_window->physicalDevice(), &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    qFatal("Failed to find suitable memory type");
    return 0;
}

void RayRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags properties,
                                   VkBuffer& buffer, VkDeviceMemory& memory) {
    VkDevice dev = m_window->device();

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (m_devFuncs->vkCreateBuffer(dev, &bufferInfo, nullptr, &buffer) != VK_SUCCESS) {
        qFatal("Failed to create buffer");
    }

    VkMemoryRequirements memRequirements;
    m_devFuncs->vkGetBufferMemoryRequirements(dev, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (m_devFuncs->vkAllocateMemory(dev, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        qFatal("Failed to allocate buffer memory");
    }

    m_devFuncs->vkBindBufferMemory(dev, buffer, memory, 0);
}

void RayRenderer::createImage(uint32_t width, uint32_t height, VkFormat format,
                                  VkImageUsageFlags usage,
                                  VkImage& image, VkDeviceMemory& memory) {
    VkDevice dev = m_window->device();

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (m_devFuncs->vkCreateImage(dev, &imageInfo, nullptr, &image) != VK_SUCCESS) {
        qFatal("Failed to create image");
    }

    VkMemoryRequirements memRequirements;
    m_devFuncs->vkGetImageMemoryRequirements(dev, image, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (m_devFuncs->vkAllocateMemory(dev, &allocInfo, nullptr, &memory) != VK_SUCCESS) {
        qFatal("Failed to allocate image memory");
    }

    m_devFuncs->vkBindImageMemory(dev, image, memory, 0);
}

VkImageView RayRenderer::createImageView(VkImage image, VkFormat format) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView;
    if (m_devFuncs->vkCreateImageView(m_window->device(), &viewInfo, nullptr, &imageView) != VK_SUCCESS) {
        qFatal("Failed to create image view");
    }
    return imageView;
}

void RayRenderer::transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image,
                                            VkImageLayout oldLayout, VkImageLayout newLayout,
                                            VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                            VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;

    m_devFuncs->vkCmdPipelineBarrier(cmdBuf, srcStage, dstStage,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

bool RayRenderer::saveScreenshot(const QString& filename) {
    VkDevice dev = m_window->device();
    uint32_t width = m_window->swapChainImageSize().width();
    uint32_t height = m_window->swapChainImageSize().height();

    // Wait for any in-flight rendering to complete
    m_devFuncs->vkDeviceWaitIdle(dev);

    // Create staging buffer
    VkDeviceSize bufferSize = width * height * 4;
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    createBuffer(bufferSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 stagingBuffer, stagingMemory);

    // Create a temporary command pool and buffer
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_window->graphicsQueueFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

    VkCommandPool cmdPool;
    if (m_devFuncs->vkCreateCommandPool(dev, &poolInfo, nullptr, &cmdPool) != VK_SUCCESS) {
        qWarning("Failed to create command pool for screenshot");
        m_devFuncs->vkDestroyBuffer(dev, stagingBuffer, nullptr);
        m_devFuncs->vkFreeMemory(dev, stagingMemory, nullptr);
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer cmdBuf;
    if (m_devFuncs->vkAllocateCommandBuffers(dev, &allocInfo, &cmdBuf) != VK_SUCCESS) {
        qWarning("Failed to allocate command buffer for screenshot");
        m_devFuncs->vkDestroyCommandPool(dev, cmdPool, nullptr);
        m_devFuncs->vkDestroyBuffer(dev, stagingBuffer, nullptr);
        m_devFuncs->vkFreeMemory(dev, stagingMemory, nullptr);
        return false;
    }

    // Begin command buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_devFuncs->vkBeginCommandBuffer(cmdBuf, &beginInfo);

    // Transition storage image for transfer (it's in GENERAL layout)
    transitionImageLayout(cmdBuf, m_storageImage,
                          VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                          VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    // Copy image to buffer
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    m_devFuncs->vkCmdCopyImageToBuffer(cmdBuf, m_storageImage,
                                        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                        stagingBuffer, 1, &region);

    // Transition back to general
    transitionImageLayout(cmdBuf, m_storageImage,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
                          VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
                          VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    // End and submit command buffer
    m_devFuncs->vkEndCommandBuffer(cmdBuf);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuf;

    VkQueue queue = m_window->graphicsQueue();
    m_devFuncs->vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
    m_devFuncs->vkQueueWaitIdle(queue);

    // Map and read
    void* data;
    m_devFuncs->vkMapMemory(dev, stagingMemory, 0, bufferSize, 0, &data);

    // Create QImage - storage image is RGBA8
    QImage image(width, height, QImage::Format_RGBA8888);
    memcpy(image.bits(), data, bufferSize);

    m_devFuncs->vkUnmapMemory(dev, stagingMemory);

    // Cleanup
    m_devFuncs->vkDestroyCommandPool(dev, cmdPool, nullptr);
    m_devFuncs->vkDestroyBuffer(dev, stagingBuffer, nullptr);
    m_devFuncs->vkFreeMemory(dev, stagingMemory, nullptr);

    return image.save(filename);
}

// RayWindow implementation

QVulkanWindowRenderer* RayWindow::createRenderer() {
    m_renderer = new RayRenderer(this, m_scenePath);
    return m_renderer;
}

void RayWindow::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = true;
    } else if (event->button() == Qt::RightButton) {
        m_rightMousePressed = true;
    }
}

void RayWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = false;
    } else if (event->button() == Qt::RightButton) {
        m_rightMousePressed = false;
    }
}

void RayWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!m_renderer) return;

    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();

    if (m_leftMousePressed) {
        // Orbit camera around target
        float sensitivity = 0.005f;
        m_renderer->camera().rotate(-delta.x() * sensitivity, -delta.y() * sensitivity);
        m_renderer->markCameraMotion();
    } else if (m_rightMousePressed) {
        // Pan camera origin (dolly/truck)
        // Mouse forward (up) = move towards target, back (down) = away
        // Mouse left/right = truck left/right
        m_renderer->camera().pan(-delta.y(), delta.x());
        m_renderer->markCameraMotion();
    }
}

void RayWindow::wheelEvent(QWheelEvent* event) {
    if (m_renderer) {
        float delta = event->angleDelta().y() / 120.0f;
        m_renderer->camera().zoom(delta);
        m_renderer->markCameraMotion();
    }
}

void RayWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
    } else if (event->key() == Qt::Key_S && m_renderer) {
        QString filename = QString("raytrace_%1.png")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        if (m_renderer->saveScreenshot(filename)) {
            QString absPath = QFileInfo(filename).absoluteFilePath();
            printf("Saved screenshot: %s\n", qPrintable(absPath));
        } else {
            fprintf(stderr, "Failed to save screenshot\n");
        }
    } else if (event->key() == Qt::Key_R && m_renderer) {
        // Reset accumulation (start fresh)
        m_renderer->markCameraMotion();
    } else if (event->key() == Qt::Key_1 && m_renderer) {
        m_renderer->setQualityLevel(0);
        printf("Quality: Draft (3 bounces)\n");
    } else if (event->key() == Qt::Key_2 && m_renderer) {
        m_renderer->setQualityLevel(1);
        printf("Quality: Preview (5 bounces)\n");
    } else if (event->key() == Qt::Key_3 && m_renderer) {
        m_renderer->setQualityLevel(2);
        printf("Quality: Final (8 bounces)\n");
    } else if (event->key() == Qt::Key_C && m_renderer) {
        m_renderer->toggleRealTimeCaustics();
    } else if (event->key() == Qt::Key_BracketLeft && m_renderer) {
        // [ - rotate sun counterclockwise (azimuth)
        m_renderer->adjustSunAzimuth(-5.0f);
    } else if (event->key() == Qt::Key_BracketRight && m_renderer) {
        // ] - rotate sun clockwise (azimuth)
        m_renderer->adjustSunAzimuth(5.0f);
    } else if (event->key() == Qt::Key_BraceLeft && m_renderer) {
        // { - lower sun (elevation)
        m_renderer->adjustSunElevation(-5.0f);
    } else if (event->key() == Qt::Key_BraceRight && m_renderer) {
        // } - raise sun (elevation)
        m_renderer->adjustSunElevation(5.0f);
    }
}

void RayWindow::checkScreenshotReady() {
    if (m_screenshotFilename.isEmpty() || !m_renderer) {
        return;
    }

    if (m_renderer->frameIndex() >= m_screenshotWaitFrames) {
        // Take screenshot
        if (m_renderer->saveScreenshot(m_screenshotFilename)) {
            QString absPath = QFileInfo(m_screenshotFilename).absoluteFilePath();
            printf("Saved screenshot: %s\n", qPrintable(absPath));
        } else {
            fprintf(stderr, "Failed to save screenshot\n");
        }

        // Clear to prevent retaking
        m_screenshotFilename.clear();

        if (m_screenshotExitAfter) {
            // Close window properly to trigger Vulkan cleanup
            close();
        }
    } else {
        // Check again after next frame
        QTimer::singleShot(16, this, &RayWindow::checkScreenshotReady);
    }
}
