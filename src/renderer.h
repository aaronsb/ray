#pragma once

#include <QVulkanWindow>
#include <QElapsedTimer>
#include <vector>
#include <array>
#include "scene.h"

class RayTracingRenderer : public QVulkanWindowRenderer {
public:
    explicit RayTracingRenderer(QVulkanWindow* window);

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;

    OrbitCamera& camera() { return m_camera; }
    void resetAccumulation() { m_frameIndex = 0; }
    void markCameraMotion() { m_lastMotionNs = m_frameTimer.nsecsElapsed(); }

    // Stats
    float lastFrameTimeMs() const { return m_lastFrameTimeMs; }
    uint32_t frameIndex() const { return m_frameIndex; }

private:
    QVulkanWindow* m_window;
    QVulkanDeviceFunctions* m_devFuncs = nullptr;

    // Scene data
    OrbitCamera m_camera;
    std::vector<Sphere> m_spheres;
    std::vector<Material> m_materials;

    // Vulkan resources
    VkPipeline m_computePipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    // Storage image for ray tracing output
    VkImage m_storageImage = VK_NULL_HANDLE;
    VkDeviceMemory m_storageImageMemory = VK_NULL_HANDLE;
    VkImageView m_storageImageView = VK_NULL_HANDLE;

    // Accumulation buffer for progressive rendering
    VkImage m_accumImage = VK_NULL_HANDLE;
    VkDeviceMemory m_accumImageMemory = VK_NULL_HANDLE;
    VkImageView m_accumImageView = VK_NULL_HANDLE;

    // Scene buffers
    VkBuffer m_sphereBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_sphereBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_materialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_materialBufferMemory = VK_NULL_HANDLE;

    // Camera uniform buffer
    VkBuffer m_cameraBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_cameraBufferMemory = VK_NULL_HANDLE;
    void* m_cameraMapped = nullptr;

    // Frame tracking
    uint32_t m_frameIndex = 0;
    QElapsedTimer m_frameTimer;
    float m_lastFrameTimeMs = 0;
    bool m_needsImageTransition = true;
    qint64 m_lastFrameNs = 0;
    qint64 m_lastMotionNs = 0;  // When camera last moved
    bool m_wasStationary = false;  // Track state change

    // Helpers
    VkShaderModule createShaderModule(const QString& path);
    void createStorageImages();
    void createSceneBuffers();
    void createDescriptorSet();
    void createComputePipeline();
    void recordComputeCommands(VkCommandBuffer cmdBuf, bool isStationary);

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

class RayTracingWindow : public QVulkanWindow {
    Q_OBJECT
public:
    QVulkanWindowRenderer* createRenderer() override;

    RayTracingRenderer* renderer() const { return m_renderer; }

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    RayTracingRenderer* m_renderer = nullptr;
    QPointF m_lastMousePos;
    bool m_leftMousePressed = false;
    bool m_rightMousePressed = false;
};
