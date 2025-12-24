#include "teapot_renderer.h"
#include <QFile>
#include <QVulkanFunctions>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <cstring>

TeapotRenderer::TeapotRenderer(QVulkanWindow* window,
                               std::vector<bezier::SubPatch> patches)
    : m_window(window)
    , m_patches(std::move(patches))
{
}

void TeapotRenderer::initResources() {
    m_devFuncs = m_window->vulkanInstance()->deviceFunctions(m_window->device());

    createPatchBuffers();
    createComputePipeline();
}

void TeapotRenderer::initSwapChainResources() {
    createStorageImage();
    createDescriptorSet();
    m_frameIndex = 0;
    m_needsImageTransition = true;
}

void TeapotRenderer::releaseSwapChainResources() {
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

    if (m_descriptorPool) {
        m_devFuncs->vkDestroyDescriptorPool(dev, m_descriptorPool, nullptr);
        m_descriptorPool = VK_NULL_HANDLE;
    }
}

void TeapotRenderer::releaseResources() {
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
    if (m_aabbBuffer) {
        m_devFuncs->vkDestroyBuffer(dev, m_aabbBuffer, nullptr);
        m_aabbBuffer = VK_NULL_HANDLE;
    }
    if (m_aabbBufferMemory) {
        m_devFuncs->vkFreeMemory(dev, m_aabbBufferMemory, nullptr);
        m_aabbBufferMemory = VK_NULL_HANDLE;
    }
}

void TeapotRenderer::startNextFrame() {
    VkCommandBuffer cmdBuf = m_window->currentCommandBuffer();

    recordComputeCommands(cmdBuf);

    m_frameIndex++;

    m_window->frameReady();
    m_window->requestUpdate();
}

VkShaderModule TeapotRenderer::createShaderModule(const QString& path) {
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

void TeapotRenderer::createStorageImage() {
    QSize sz = m_window->swapChainImageSize();

    createImage(sz.width(), sz.height(), VK_FORMAT_R8G8B8A8_UNORM,
                VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                m_storageImage, m_storageImageMemory);
    m_storageImageView = createImageView(m_storageImage, VK_FORMAT_R8G8B8A8_UNORM);

    m_needsImageTransition = true;
}

void TeapotRenderer::createPatchBuffers() {
    // Pack patch control points into vec4s (16 vec4s per patch, w unused)
    // Each patch has 16 control points, each control point is xyz
    size_t numPatches = m_patches.size();
    size_t patchDataSize = numPatches * 16 * sizeof(float) * 4;  // 16 vec4s per patch
    size_t aabbDataSize = numPatches * 2 * sizeof(float) * 4;    // 2 vec4s per patch (min, max)

    createBuffer(patchDataSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_patchBuffer, m_patchBufferMemory);

    createBuffer(aabbDataSize, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                 m_aabbBuffer, m_aabbBufferMemory);

    // Upload patch data
    VkDevice dev = m_window->device();

    // Pack control points as vec4s (w = 0)
    std::vector<float> patchData(numPatches * 16 * 4);
    for (size_t p = 0; p < numPatches; p++) {
        for (int i = 0; i < 16; i++) {
            size_t idx = (p * 16 + i) * 4;
            patchData[idx + 0] = m_patches[p].cp[i].x;
            patchData[idx + 1] = m_patches[p].cp[i].y;
            patchData[idx + 2] = m_patches[p].cp[i].z;
            patchData[idx + 3] = 0.0f;  // padding
        }
    }

    void* data;
    m_devFuncs->vkMapMemory(dev, m_patchBufferMemory, 0, patchDataSize, 0, &data);
    memcpy(data, patchData.data(), patchDataSize);
    m_devFuncs->vkUnmapMemory(dev, m_patchBufferMemory);

    // Pack AABBs as vec4s (w = 0)
    std::vector<float> aabbData(numPatches * 2 * 4);
    for (size_t p = 0; p < numPatches; p++) {
        size_t idx = p * 2 * 4;
        aabbData[idx + 0] = m_patches[p].bounds.min.x;
        aabbData[idx + 1] = m_patches[p].bounds.min.y;
        aabbData[idx + 2] = m_patches[p].bounds.min.z;
        aabbData[idx + 3] = 0.0f;
        aabbData[idx + 4] = m_patches[p].bounds.max.x;
        aabbData[idx + 5] = m_patches[p].bounds.max.y;
        aabbData[idx + 6] = m_patches[p].bounds.max.z;
        aabbData[idx + 7] = 0.0f;
    }

    m_devFuncs->vkMapMemory(dev, m_aabbBufferMemory, 0, aabbDataSize, 0, &data);
    memcpy(data, aabbData.data(), aabbDataSize);
    m_devFuncs->vkUnmapMemory(dev, m_aabbBufferMemory);

    printf("Uploaded %zu patches to GPU (%.1f KB patches + %.1f KB AABBs)\n",
           numPatches, patchDataSize / 1024.0f, aabbDataSize / 1024.0f);
}

void TeapotRenderer::createDescriptorSet() {
    VkDevice dev = m_window->device();

    // Create descriptor pool
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0] = {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1};
    poolSizes[1] = {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};  // patches + AABBs

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

    VkDescriptorBufferInfo patchBufferInfo{};
    patchBufferInfo.buffer = m_patchBuffer;
    patchBufferInfo.offset = 0;
    patchBufferInfo.range = VK_WHOLE_SIZE;

    VkDescriptorBufferInfo aabbBufferInfo{};
    aabbBufferInfo.buffer = m_aabbBuffer;
    aabbBufferInfo.offset = 0;
    aabbBufferInfo.range = VK_WHOLE_SIZE;

    std::array<VkWriteDescriptorSet, 3> writes{};

    // Binding 0: output image
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = m_descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &outputImageInfo;

    // Binding 1: patch buffer
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = m_descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &patchBufferInfo;

    // Binding 2: AABB buffer
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = m_descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &aabbBufferInfo;

    m_devFuncs->vkUpdateDescriptorSets(dev, writes.size(), writes.data(), 0, nullptr);
}

void TeapotRenderer::createComputePipeline() {
    VkDevice dev = m_window->device();

    // Create descriptor set layout (3 bindings)
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};

    // Binding 0: output image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 1: patch buffer
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // Binding 2: AABB buffer
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

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
    pushConstantRange.size = sizeof(TeapotPushConstants);

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

    // Load teapot shader
    QString shaderPath = QCoreApplication::applicationDirPath() + "/shaders/teapot.spv";
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

void TeapotRenderer::recordComputeCommands(VkCommandBuffer cmdBuf) {
    QSize sz = m_window->swapChainImageSize();

    // Transition image to general layout on first use
    if (m_needsImageTransition) {
        transitionImageLayout(cmdBuf, m_storageImage,
            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
            0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        m_needsImageTransition = false;
    }

    // Bind pipeline and descriptors
    m_devFuncs->vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, m_computePipeline);
    m_devFuncs->vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE,
        m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

    // Push constants
    TeapotPushConstants pc{};
    pc.width = sz.width();
    pc.height = sz.height();
    pc.numPatches = static_cast<uint32_t>(m_patches.size());
    pc.frameIndex = m_frameIndex;

    m_camera.getPosition(pc.camPosX, pc.camPosY, pc.camPosZ);
    pc.camTargetX = m_camera.targetX;
    pc.camTargetY = m_camera.targetY;
    pc.camTargetZ = m_camera.targetZ;

    m_devFuncs->vkCmdPushConstants(cmdBuf, m_pipelineLayout,
        VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(TeapotPushConstants), &pc);

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

uint32_t TeapotRenderer::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
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

void TeapotRenderer::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
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

void TeapotRenderer::createImage(uint32_t width, uint32_t height, VkFormat format,
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

VkImageView TeapotRenderer::createImageView(VkImage image, VkFormat format) {
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

void TeapotRenderer::transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image,
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

// TeapotWindow implementation

QVulkanWindowRenderer* TeapotWindow::createRenderer() {
    m_renderer = new TeapotRenderer(this, std::move(m_patches));
    return m_renderer;
}

void TeapotWindow::mousePressEvent(QMouseEvent* event) {
    m_lastMousePos = event->position();
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = true;
    }
}

void TeapotWindow::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        m_leftMousePressed = false;
    }
}

void TeapotWindow::mouseMoveEvent(QMouseEvent* event) {
    if (!m_renderer) return;

    QPointF delta = event->position() - m_lastMousePos;
    m_lastMousePos = event->position();

    if (m_leftMousePressed) {
        float sensitivity = 0.005f;
        m_renderer->camera().rotate(-delta.x() * sensitivity, -delta.y() * sensitivity);
        m_renderer->markCameraMotion();
    }
}

void TeapotWindow::wheelEvent(QWheelEvent* event) {
    if (m_renderer) {
        float delta = event->angleDelta().y() / 120.0f;
        m_renderer->camera().zoom(delta);
        m_renderer->markCameraMotion();
    }
}

void TeapotWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        close();
    }
}
