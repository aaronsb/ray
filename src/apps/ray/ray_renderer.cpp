#include "ray_renderer.h"
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
#include <cstring>

RayRenderer::RayRenderer(QVulkanWindow* window,
                               std::vector<Patch> patches)
    : m_window(window)
{
    m_patchGroup.build(patches);
}

void RayRenderer::initResources() {
    m_devFuncs = m_window->vulkanInstance()->deviceFunctions(m_window->device());
    m_frameTimer.start();

    const float PI = 3.14159265f;

    // Teapot instances - reduced to showcase alongside CSG scene
    // Format: {posX, posY, posZ, scale, rotX, rotY, rotZ, materialId}
    m_instances = {
        {-10.0f, 0.0f, 0.0f, 0.5f, 0.0f, PI * 0.5f, 0.0f, 1},  // Polished silver
        { 10.0f, 0.0f, 0.0f, 0.5f, 0.0f, -PI * 0.5f, 0.0f, 4}, // Clear glass
    };

    createPatchBuffers();
    createComputePipeline();
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
}

void RayRenderer::startNextFrame() {
    // Calculate FPS
    qint64 now = m_frameTimer.nsecsElapsed();
    if (m_lastFrameTime > 0) {
        float frameMs = (now - m_lastFrameTime) / 1000000.0f;
        m_fps = m_fps * 0.9f + (1000.0f / frameMs) * 0.1f;  // Smoothed
    }
    m_lastFrameTime = now;

    // Update window title with FPS
    m_window->setTitle(QString("Utah Teapot - %1 fps").arg(static_cast<int>(m_fps)));

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

    size_t patchDataSize = patchData.size() * sizeof(float);
    size_t bvhSize = bvhNodes.size() * sizeof(BVHNode);
    size_t indexSize = patchIndices.size() * sizeof(uint32_t);

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

    // Upload patch control points
    void* data;
    m_devFuncs->vkMapMemory(dev, m_patchBufferMemory, 0, patchDataSize, 0, &data);
    memcpy(data, patchData.data(), patchDataSize);
    m_devFuncs->vkUnmapMemory(dev, m_patchBufferMemory);

    // Upload BVH nodes
    m_devFuncs->vkMapMemory(dev, m_bvhBufferMemory, 0, bvhSize, 0, &data);
    memcpy(data, bvhNodes.data(), bvhSize);
    m_devFuncs->vkUnmapMemory(dev, m_bvhBufferMemory);

    // Upload patch indices
    m_devFuncs->vkMapMemory(dev, m_patchIndexBufferMemory, 0, indexSize, 0, &data);
    memcpy(data, patchIndices.data(), indexSize);
    m_devFuncs->vkUnmapMemory(dev, m_patchIndexBufferMemory);

    // Instance buffer
    size_t instanceSize = m_instances.size() * sizeof(BezierInstance);
    createBuffer(instanceSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_instanceBuffer, m_instanceBufferMemory);

    m_devFuncs->vkMapMemory(dev, m_instanceBufferMemory, 0, instanceSize, 0, &data);
    memcpy(data, m_instances.data(), instanceSize);
    m_devFuncs->vkUnmapMemory(dev, m_instanceBufferMemory);

    printf("Uploaded %u patches + %u BVH nodes + %zu instances (%.1f KB total)\n",
           m_patchGroup.subPatchCount(), m_patchGroup.bvhNodeCount(), m_instances.size(),
           (patchDataSize + bvhSize + indexSize + instanceSize) / 1024.0f);
}

void RayRenderer::createDescriptorSet() {
    VkDevice dev = m_window->device();

    // Create descriptor pool (2 images + 4 buffers)
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2};  // output + accumulation
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 4};  // patches, BVH, indices, instances

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

    std::array<VkWriteDescriptorSet, 6> writes{};

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

    m_devFuncs->vkUpdateDescriptorSets(dev, writes.size(), writes.data(), 0, nullptr);
}

void RayRenderer::createComputePipeline() {
    VkDevice dev = m_window->device();

    // Descriptor set layout (6 bindings: output image, 4 buffers, accum image)
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};

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
    m_renderer = new RayRenderer(this, std::move(m_patches));
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
