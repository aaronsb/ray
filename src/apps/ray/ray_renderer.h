#pragma once

// Ray - Bezier Patch Ray Tracer
// Direct ray tracing of parametric surfaces (no tessellation)

#include <QVulkanWindow>
#include <QElapsedTimer>
#include <QString>
#include <vector>
#include "../../parametric/bezier/patch_group.h"
#include "../../parametric/csg/csg.h"
#include "../../parametric/materials/material.h"
#include "../../parametric/scene/scene_loader.h"

using parametric::BezierInstance;
using parametric::BezierPatchGroup;
using parametric::Patch;
using parametric::SubPatch;
using parametric::BVHNode;
using parametric::CSGScene;
using parametric::CSGPrimitive;
using parametric::CSGNode;
using parametric::Material;
using parametric::MaterialLibrary;

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
    uint32_t numCSGPrimitives;
    uint32_t numCSGNodes;
    uint32_t numCSGRoots;
    uint32_t numMaterials;
};

// Simple orbit camera
struct RayCamera {
    float distance = 18.0f;
    float azimuth = 0.3f;
    float elevation = 0.5f;
    float targetX = 0.0f;
    float targetY = 1.0f;
    float targetZ = 0.0f;

    void rotate(float dAzimuth, float dElevation) {
        azimuth += dAzimuth;
        elevation += dElevation;
        elevation = std::clamp(elevation, -1.5f, 1.5f);
    }

    void zoom(float delta) {
        distance *= (1.0f - delta * 0.1f);
        distance = std::clamp(distance, 1.0f, 50.0f);
    }

    // Move camera origin (target point) - dolly (X) and truck (Y)
    void pan(float dx, float dy) {
        // Forward vector (from camera to target, in XZ plane)
        float forwardX = -std::sin(azimuth);
        float forwardZ = -std::cos(azimuth);

        // Right vector (perpendicular to forward, in XZ plane)
        float rightX = std::cos(azimuth);
        float rightZ = -std::sin(azimuth);

        // dx: move along forward (towards/away from camera view direction)
        // dy: move along right (perpendicular to view)
        float speed = distance * 0.01f;  // Scale with distance for consistent feel
        targetX += (forwardX * dx + rightX * dy) * speed;
        targetZ += (forwardZ * dx + rightZ * dy) * speed;
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
                         const QString& scenePath = QString());

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;

    RayCamera& camera() { return m_camera; }
    float fps() const { return m_fps; }
    uint32_t frameIndex() const { return m_frameIndex; }
    void markCameraMotion() {
        m_frameIndex = 0;
        m_window->requestUpdate();
    }

    bool saveScreenshot(const QString& filename);

private:
    QVulkanWindow* m_window;
    QVulkanDeviceFunctions* m_devFuncs = nullptr;
    QString m_scenePath;  // Scene file path (empty = use default)

    // Patch data
    BezierPatchGroup m_patchGroup;
    std::vector<BezierInstance> m_instances;
    RayCamera m_camera;

    // CSG scene data
    CSGScene m_csgScene;

    // Material library
    MaterialLibrary m_materials;

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

    // Accumulation buffer (high precision for progressive rendering)
    VkImage m_accumImage = VK_NULL_HANDLE;
    VkDeviceMemory m_accumImageMemory = VK_NULL_HANDLE;
    VkImageView m_accumImageView = VK_NULL_HANDLE;

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

    // CSG buffers
    VkBuffer m_csgPrimitiveBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_csgPrimitiveBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_csgNodeBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_csgNodeBufferMemory = VK_NULL_HANDLE;
    VkBuffer m_csgRootBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_csgRootBufferMemory = VK_NULL_HANDLE;

    // Material buffer
    VkBuffer m_materialBuffer = VK_NULL_HANDLE;
    VkDeviceMemory m_materialBufferMemory = VK_NULL_HANDLE;

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
    void createCSGBuffers();
    void buildCSGScene();
    void buildMaterialLibrary();
    void createMaterialBuffer();
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
    QVulkanWindowRenderer* createRenderer() override;

    RayRenderer* renderer() const { return m_renderer; }

    // Screenshot on start: takes screenshot after N frames then optionally exits
    void setScreenshotOnStart(const QString& filename, uint32_t waitFrames = 30, bool exitAfter = true) {
        m_screenshotFilename = filename;
        m_screenshotWaitFrames = waitFrames;
        m_screenshotExitAfter = exitAfter;
    }

    // Scene file path (empty = use default scenes/demo.scene)
    void setScenePath(const QString& path) { m_scenePath = path; }
    const QString& scenePath() const { return m_scenePath; }

public slots:
    void checkScreenshotReady();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    RayRenderer* m_renderer = nullptr;
    QPointF m_lastMousePos;
    bool m_leftMousePressed = false;
    bool m_rightMousePressed = false;

    // Screenshot on start
    QString m_screenshotFilename;
    uint32_t m_screenshotWaitFrames = 0;
    bool m_screenshotExitAfter = false;

    // Scene file
    QString m_scenePath;
};
