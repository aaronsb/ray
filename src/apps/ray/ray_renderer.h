#pragma once

// Ray - Bezier Patch Ray Tracer
// Direct ray tracing of parametric surfaces (no tessellation)

#include <QVulkanWindow>
#include <QElapsedTimer>
#include <vector>
#include "../../parametric/bezier/patch_group.h"

using parametric::BezierInstance;
using parametric::BezierPatchGroup;
using parametric::Patch;
using parametric::SubPatch;
using parametric::BVHNode;

// Push constants matching ray.comp
struct RayPushConstants {
    uint32_t width;
    uint32_t height;
    uint32_t numPatches;
    uint32_t numBVHNodes;
    float camPosX, camPosY, camPosZ;
    uint32_t frameIndex;
    float camTargetX, camTargetY, camTargetZ;
    uint32_t numInstances;
};

// Simple orbit camera
struct RayCamera {
    float distance = 12.0f;
    float azimuth = 0.5f;
    float elevation = 0.4f;
    float targetX = 0.0f;
    float targetY = 0.5f;
    float targetZ = 1.5f;

    void rotate(float dAzimuth, float dElevation) {
        azimuth += dAzimuth;
        elevation += dElevation;
        elevation = std::clamp(elevation, -1.5f, 1.5f);
    }

    void zoom(float delta) {
        distance *= (1.0f - delta * 0.1f);
        distance = std::clamp(distance, 1.0f, 50.0f);
    }

    void getPosition(float& x, float& y, float& z) const {
        x = targetX + distance * std::cos(elevation) * std::sin(azimuth);
        y = targetY + distance * std::sin(elevation);
        z = targetZ + distance * std::cos(elevation) * std::cos(azimuth);
    }
};

class RayRenderer : public QVulkanWindowRenderer {
public:
    explicit RayRenderer(QVulkanWindow* window,
                         std::vector<Patch> patches);

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;

    RayCamera& camera() { return m_camera; }
    float fps() const { return m_fps; }
    void markCameraMotion() {
        m_frameIndex = 0;
        m_window->requestUpdate();
    }

private:
    QVulkanWindow* m_window;
    QVulkanDeviceFunctions* m_devFuncs = nullptr;

    // Patch data
    BezierPatchGroup m_patchGroup;
    std::vector<BezierInstance> m_instances;
    RayCamera m_camera;

    // Vulkan resources
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Output image
    VkImage m_storageImage = VK_NULL_HANDLE;
    VkDeviceMemory m_storageImageMemory = VK_NULL_HANDLE;
    VkImageView m_storageImageView = VK_NULL_HANDLE;

    // Patch data buffer (16 vec4s per patch)
    VkBuffer m_patchBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_patchBufferMemory = VK_NULL_HANDLE;

    // BVH node buffer
    VkBuffer m_bvhBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_bvhBufferMemory = VK_NULL_HANDLE;

    // Patch index buffer (reordered by BVH)
    VkBuffer m_patchIndexBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_patchIndexBufferMemory = VK_NULL_HANDLE;

    // Instance buffer
    VkBuffer m_instanceBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_instanceBufferMemory = VK_NULL_HANDLE;

    // Frame tracking
    uint32_t m_frameIndex = 0;
    bool m_needsImageTransition = true;
    QElapsedTimer m_frameTimer;
    float m_fps = 0.0f;
    qint64 m_lastFrameTime = 0;

    // Helpers
    VkShaderModule createShaderModule(const QString& path);
    void createStorageImage();
    void createPatchBuffers();
    void createDescriptorSet();
    void createComputePipeline();
    void recordComputeCommands(VkCommandBuffer cmdBuf);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                      VkMemoryPropertyFlags properties, VkBuffer& buffer,
                      VkDeviceMemory& memory);
    void createImage(uint32_t width, uint32_t height, VkFormat format,
                     VkImageUsageFlags usage, VkImage& image, VkDeviceMemory& memory);
    VkImageView createImageView(VkImage image, VkFormat format);
    void transitionImageLayout(VkCommandBuffer cmdBuf, VkImage image,
                               VkImageLayout oldLayout, VkImageLayout newLayout,
                               VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                               VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
};

class RayWindow : public QVulkanWindow {
    Q_OBJECT
public:
    void setPatches(std::vector<Patch> patches) {
        m_patches = std::move(patches);
    }
    QVulkanWindowRenderer* createRenderer() override;

    RayRenderer* renderer() const { return m_renderer; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    std::vector<Patch> m_patches;
    RayRenderer* m_renderer = nullptr;
    QPointF m_lastMousePos;
    bool m_leftMousePressed = false;
};
