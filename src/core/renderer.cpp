#include "renderer.h"
#include <QFile>
#include <QVulkanFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QImage>
#include <QDateTime>
#include <QThread>
#include <cstring>

RayTracingRenderer::RayTracingRenderer(QVulkanWindow* window, Scene scene)
    : m_window(window)
    , m_scene(std::move(scene))
{
    m_camera.distance = 12.0f;
    m_camera.elevation = 0.4f;
    m_camera.target = {0, 1.5f, 0};
}

void RayTracingRenderer::initResources() {
    m_devFuncs = m_window->vulkanInstance()->deviceFunctions(m_window->device());
    m_frameTimer.start();

    createSceneBuffers();
    createComputePipeline();
}

void RayTracingRenderer::initSwapChainResources() {
    createStorageImages();
    createDescriptorSet();
    m_frameIndex = 0;
    m_needsImageTransition = true;
}

void RayTracingRenderer::releaseSwapChainResources() {
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

void RayTracingRenderer::releaseResources() {
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

    if (m_sphereBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_sphereBuffer, nullptr);
        m_sphereBuffer = VK_NULL_HANDLE;
    }
    if (m_sphereBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_sphereBufferMemory, nullptr);
        m_sphereBufferMemory = VK_NULL_HANDLE;
    }
    if (m_boxBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_boxBuffer, nullptr);
        m_boxBuffer = VK_NULL_HANDLE;
    }
    if (m_boxBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_boxBufferMemory, nullptr);
        m_boxBufferMemory = VK_NULL_HANDLE;
    }
    if (m_spotLightBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_spotLightBuffer, nullptr);
        m_spotLightBuffer = VK_NULL_HANDLE;
    }
    if (m_spotLightBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_spotLightBufferMemory, nullptr);
        m_spotLightBufferMemory = VK_NULL_HANDLE;
    }
    if (m_materialBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_materialBuffer, nullptr);
        m_materialBuffer = VK_NULL_HANDLE;
    }
    if (m_materialBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_materialBufferMemory, nullptr);
        m_materialBufferMemory = VK_NULL_HANDLE;
    }
    if (m_cameraBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_cameraBuffer, nullptr);
        m_cameraBuffer = VK_NULL_HANDLE;
    }
    if (m_cameraBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_cameraBufferMemory, nullptr);
        m_cameraBufferMemory = VK_NULL_HANDLE;
    }
}

void RayTracingRenderer::startNextFrame() {
    // Track frame time (vsync handles rate limiting)
    qint64 now = m_frameTimer.nsecsElapsed();
    m_lastFrameTimeMs = (now - m_lastFrameNs) / 1000000.0f;
    m_lastFrameNs = now;

    // Detect if camera is stationary (200ms since last motion)
    constexpr qint64 stationaryThresholdNs = 200 * 1000000LL;  // 200ms
    bool isStationary = (now - m_lastMotionNs) > stationaryThresholdNs;

    // Reset to 1 (not 0) when becoming stationary - blends 50/50 with rolling average
    // instead of discarding it completely (which causes a flash)
    if (isStationary && !m_wasStationary) {
        m_frameIndex = 1;
    }
    m_wasStationary = isStationary;

    // Check if converged - stop sampling after 200 frames to let GPU rest
    constexpr uint32_t convergenceFrames = 200;
    bool isConverged = isStationary && m_frameIndex > convergenceFrames;

    VkDevice dev = m_window->device();
    QSize sz = m_window->swapChainImageSize();
    float aspect = float(sz.width()) / float(sz.height());

    // Update camera uniform buffer
    CameraData camData = m_camera.getCameraData(aspect);
    memcpy(m_cameraMapped, &camData, sizeof(CameraData));

    VkCommandBuffer cmdBuf = m_window->currentCommandBuffer();

    recordComputeCommands(cmdBuf, isStationary);

    m_frameIndex++;

    m_window->frameReady();

    // Only request next frame if not converged - lets GPU rest
    if (!isConverged) {
        m_window->requestUpdate();
    }
}

VkShaderModule RayTracingRenderer::createShaderModule(const QString& path) {
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

void RayTracingRenderer::createStorageImages() {
    QSize sz = m_window->swapChainImageSize();

    // Output image (what we blit to swapchain)
    createImage(sz.width(), sz.height(), VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                m_storageImage, m_storageImageMemory);
    m_storageImageView = createImageView(m_storageImage, VK_FORMAT_R8G8B8A8_UNORM);

    // Accumulation buffer (higher precision for progressive rendering)
    createImage(sz.width(), sz.height(), VK_FORMAT_R32G32B32A32_SFLOAT,
                VK_IMAGE_USAGE_STORAGE_BIT,
                m_accumImage, m_accumImageMemory);
    m_accumImageView = createImageView(m_accumImage, VK_FORMAT_R32G32B32A32_SFLOAT);

    // Mark that we need to transition images on first frame
    m_needsImageTransition = true;
}

void RayTracingRenderer::createSceneBuffers() {
    const auto& spheres = m_scene.spheres();
    const auto& boxes = m_scene.boxes();
    const auto& spotLights = m_scene.spotLights();
    const auto& materials = m_scene.materials();

    VkDeviceSize sphereSize = sizeof(Sphere) * spheres.size();
    VkDeviceSize boxSize = sizeof(Box) * std::max(boxes.size(), size_t(1));  // At least 1 to avoid 0-size buffer
    VkDeviceSize spotLightSize = sizeof(SpotLight) * std::max(spotLights.size(), size_t(1));
    VkDeviceSize materialSize = sizeof(Material) * materials.size();
    VkDeviceSize cameraSize = sizeof(CameraData);

    createBuffer(sphereSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_sphereBuffer, m_sphereBufferMemory);

    createBuffer(boxSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_boxBuffer, m_boxBufferMemory);

    createBuffer(spotLightSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_spotLightBuffer, m_spotLightBufferMemory);

    createBuffer(materialSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_materialBuffer, m_materialBufferMemory);

    createBuffer(cameraSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_cameraBuffer, m_cameraBufferMemory);

    // Upload scene data
    VkDevice dev = m_window->device();
    void* data;

    m_devFuncs->vkMapMemory(dev, m_sphereBufferMemory, 0, sphereSize, 0, &data);
    memcpy(data, spheres.data(), sphereSize);
    m_devFuncs->vkUnmapMemory(dev, m_sphereBufferMemory);

    if (!boxes.empty()) {
        m_devFuncs->vkMapMemory(dev, m_boxBufferMemory, 0, boxSize, 0, &data);
        memcpy(data, boxes.data(), sizeof(Box) * boxes.size());
        m_devFuncs->vkUnmapMemory(dev, m_boxBufferMemory);
    }

    if (!spotLights.empty()) {
        m_devFuncs->vkMapMemory(dev, m_spotLightBufferMemory, 0, spotLightSize, 0, &data);
        memcpy(data, spotLights.data(), sizeof(SpotLight) * spotLights.size());
        m_devFuncs->vkUnmapMemory(dev, m_spotLightBufferMemory);
    }

    m_devFuncs->vkMapMemory(dev, m_materialBufferMemory, 0, materialSize, 0, &data);
    memcpy(data, materials.data(), materialSize);
    m_devFuncs->vkUnmapMemory(dev, m_materialBufferMemory);

    // Keep camera buffer mapped for updates each frame
    m_devFuncs->vkMapMemory(dev, m_cameraBufferMemory, 0, cameraSize, 0, &m_cameraMapped);
}

void RayTracingRenderer::createDescriptorSet() {
    VkDevice dev = m_window->device();

    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 3> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};  // spheres, materials, boxes, spotlights
    poolSizes[2] = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = poolSizes.size();
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (m_devFuncs->vkCreateDescriptorPool(dev, &poolInfo, nullptr, &m_descriptorPool) != VK_SUCCESS) {
        qFatal("Failed to create descriptor pool");
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = m_descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &m_descriptorSetLayout;

    if (m_devFuncs->vkAllocateDescriptorSets(dev, &allocInfo, &m_descriptorSet) != VK_SUCCESS) {
        qFatal("Failed to allocate descriptor set");
    }

    // Update descriptor set
    VkDescriptorImageInfo outputImageInfo{};
    outputImageInfo.imageView = m_storageImageView;
    outputImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo accumImageInfo{};
    accumImageInfo.imageView = m_accumImageView;
    accumImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorBufferInfo sphereBufferInfo{};
    sphereBufferInfo.buffer = m_sphereBuffer;
    sphereBufferInfo.offset = 0;
    sphereBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo materialBufferInfo{};
    materialBufferInfo.buffer = m_materialBuffer;
    materialBufferInfo.offset = 0;
    materialBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo cameraBufferInfo{};
    cameraBufferInfo.buffer = m_cameraBuffer;
    cameraBufferInfo.offset = 0;
    cameraBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo boxBufferInfo{};
    boxBufferInfo.buffer = m_boxBuffer;
    boxBufferInfo.offset = 0;
    boxBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo spotLightBufferInfo{};
    spotLightBufferInfo.buffer = m_spotLightBuffer;
    spotLightBufferInfo.offset = 0;
    spotLightBufferInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 7> writes{};

    // Binding 0: output image
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &outputImageInfo;

    // Binding 1: accumulation image
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &accumImageInfo;

    // Binding 2: spheres buffer
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &sphereBufferInfo;

    // Binding 3: materials buffer
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = m_descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &materialBufferInfo;

    // Binding 4: camera uniform
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = m_descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &cameraBufferInfo;

    // Binding 5: boxes buffer
    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = m_descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[5].descriptorCount = 1;
    writes[5].pBufferInfo = &boxBufferInfo;

    // Binding 6: spotlights buffer
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = m_descriptorSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[6].descriptorCount = 1;
    writes[6].pBufferInfo = &spotLightBufferInfo;

    m_devFuncs->vkUpdateDescriptorSets(dev, writes.size(), writes.data(), 0, nullptr);
}

void RayTracingRenderer::createComputePipeline() {
    VkDevice dev = m_window->device();

    // Create descriptor set layout
    std::array<VkDescriptorSetLayoutBinding, 7> bindings{};

    // Binding 0: output image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: accumulation image
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: spheres SSBO
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 3: materials SSBO
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 4: camera UBO
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 5: boxes SSBO
    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[5].descriptorCount = 1;
    bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 6: spotlights SSBO
    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[6].descriptorCount = 1;
    bindings[6].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindings.size();
    layoutInfo.pBindings = bindings.data();

    if (m_devFuncs->vkCreateDescriptorSetLayout(dev, &layoutInfo, nullptr, &m_descriptorSetLayout) != VK_SUCCESS) {
        qFatal("Failed to create descriptor set layout");
    }

    // Push constant range
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstants);

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;

    if (m_devFuncs->vkCreatePipelineLayout(dev, &pipelineLayoutInfo, nullptr, &m_pipelineLayout) != VK_SUCCESS) {
        qFatal("Failed to create pipeline layout");
    }

    // Load shader
    QString shaderPath = QCoreApplication::applicationDirPath() + "/shaders/raytrace.spv";
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

void RayTracingRenderer::recordComputeCommands(VkCommandBuffer cmdBuf, bool isStationary) {
    QSize sz = m_window->swapChainImageSize();

    // Transition images to general layout on first use
    if (m_needsImageTransition) {
        transitionImageLayout(cmdBuf, m_storageImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        transitionImageLayout(cmdBuf, m_accumImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        m_needsImageTransition = false;
    }

    // Bind pipeline and descriptors
    m_devFuncs->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    m_devFuncs->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // Adaptive sample count: target 60fps, scale samples to use available headroom
    constexpr float targetFrameMs = 16.67f;  // 60fps
    if (m_lastFrameTimeMs > 0.1f) {
        float scale = targetFrameMs / m_lastFrameTimeMs;
        float idealSamples = 32.0f * scale;
        // Smooth to avoid jitter, clamp to [2, 64]
        m_smoothedSamples = m_smoothedSamples * 0.8f + idealSamples * 0.2f;
        m_smoothedSamples = std::clamp(m_smoothedSamples, 2.0f, 64.0f);
    }

    // Push constants
    PushConstants pc{};
    pc.frameIndex = m_frameIndex;
    pc.sampleCount = static_cast<uint32_t>(m_smoothedSamples);
    pc.maxBounces = 5;
    pc.sphereCount = m_scene.sphereCount();
    pc.boxCount = m_scene.boxCount();
    pc.spotLightCount = m_scene.spotLightCount();
    pc.width = sz.width();
    pc.height = sz.height();
    pc.useNEE = 1;
    pc.accumulate = isStationary ? 1 : 0;  // Proper accumulation when still, rolling avg when moving
    pc.sunElevation = m_sunElevation;
    pc.sunAzimuth = m_sunAzimuth;

    m_devFuncs->vkCmdPushConstants(cmdBuf, m_pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstants), &pc);

    // Dispatch compute shader (16x16 workgroups)
    uint32_t groupCountX = (sz.width() + 15) / 16;
    uint32_t groupCountY = (sz.height() + 15) / 16;
    m_devFuncs->vkCmdDispatch(cmdBuf, groupCountX, groupCountY, 1);

    // Barrier: compute write -> transfer read
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

    // Transition swapchain image to transfer dst
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

    // Blit storage image to swapchain
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

    // Transition swapchain to present
    swapBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    swapBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    swapBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    swapBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0, 0, nullptr, 0, nullptr, 1, &swapBarrier);

    // Transition storage image back to general for next frame
    barrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    barrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;

    m_devFuncs->vkCmdPipelineBarrier(cmdBuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, 0, nullptr, 0, nullptr, 1, &barrier);
}

uint32_t RayTracingRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
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

void RayTracingRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
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

void RayTracingRenderer::createImage(uint32_t width, uint32_t height, VkFormat format,
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

VkImageView RayTracingRenderer::createImageView(VkImage image, VkFormat format) {
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

void RayTracingRenderer::transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image,
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

// RayTracingWindow implementation

QVulkanWindowRenderer* RayTracingWindow::createRenderer() {
    m_renderer = new RayTracingRenderer(this, std::move(m_scene));
    return m_renderer;
}

void RayTracingWindow::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = true;
    } else if (event->button() == Qt::RightButton) {
        m_rightMousePressed = true;
    }
}

void RayTracingWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = false;
    } else if (event->button() == Qt::RightButton) {
        m_rightMousePressed = false;
    }
    // No reset needed - hybrid mode transitions smoothly from rolling avg to proper accumulation
}

void RayTracingWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!m_renderer) return;

    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();

    if (m_leftMousePressed) {
        // Left drag: orbit camera
        float sensitivity = 0.005f;
        m_renderer->camera().rotate(-delta.x() * sensitivity, -delta.y() * sensitivity);
        m_renderer->markCameraMotion();
    } else if (m_rightMousePressed) {
        // Right drag: pan camera target
        m_renderer->camera().pan(delta.x(), delta.y());
        m_renderer->markCameraMotion();
    }
}

void RayTracingWindow::wheelEvent(QWheelEvent* event) {
    if (m_renderer) {
        float delta = event->angleDelta().y() / 120.0f;
        m_renderer->camera().zoom(delta);
        m_renderer->markCameraMotion();
    }
}

void RayTracingRenderer::cycleGobos(int direction) {
    auto& spotLights = m_scene.spotLightsMut();
    if (spotLights.empty()) return;

    constexpr uint32_t numGoboTypes = 7;  // None, Stripes, Grid, Circles, Dots, Star, Off

    // Cycle all spotlights together
    for (auto& light : spotLights) {
        int newType = static_cast<int>(light.goboType) + direction;
        if (newType < 0) newType = numGoboTypes - 1;
        if (newType >= static_cast<int>(numGoboTypes)) newType = 0;
        light.goboType = static_cast<uint32_t>(newType);
    }

    // Re-upload spotlight buffer
    VkDevice dev = m_window->device();
    VkDeviceSize size = sizeof(SpotLight) * spotLights.size();
    void* data;
    m_devFuncs->vkMapMemory(dev, m_spotLightBufferMemory, 0, size, 0, &data);
    memcpy(data, spotLights.data(), size);
    m_devFuncs->vkUnmapMemory(dev, m_spotLightBufferMemory);

    markCameraMotion();  // Reset accumulation to see change immediately
}

bool RayTracingRenderer::saveScreenshot(const QString& filename) {
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

void RayTracingWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
    } else if (event->key() == Qt::Key_R && m_renderer) {
        m_renderer->resetAccumulation();
    } else if (event->key() == Qt::Key_S && m_renderer) {
        QString filename = QString("raytrace_%1.png")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss"));
        if (m_renderer->saveScreenshot(filename)) {
            qDebug("Saved screenshot: %s", qPrintable(filename));
        } else {
            qWarning("Failed to save screenshot");
        }
    } else if (event->key() == Qt::Key_BracketLeft && m_renderer) {
        // [ - rotate sun counterclockwise (azimuth)
        m_renderer->adjustSunAzimuth(-0.1f);
    } else if (event->key() == Qt::Key_BracketRight && m_renderer) {
        // ] - rotate sun clockwise (azimuth)
        m_renderer->adjustSunAzimuth(0.1f);
    } else if (event->key() == Qt::Key_BraceLeft && m_renderer) {
        // { - lower sun (elevation)
        m_renderer->adjustSunElevation(-0.1f);
    } else if (event->key() == Qt::Key_BraceRight && m_renderer) {
        // } - raise sun (elevation)
        m_renderer->adjustSunElevation(0.1f);
    } else if (event->key() == Qt::Key_Comma && m_renderer) {
        // , - previous gobo pattern
        m_renderer->cycleGobos(-1);
    } else if (event->key() == Qt::Key_Period && m_renderer) {
        // . - next gobo pattern
        m_renderer->cycleGobos(1);
    }
}
