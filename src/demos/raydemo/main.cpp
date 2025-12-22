#include <QApplication>
#include <QVulkanInstance>
#include <QLoggingCategory>
#include <QTimer>
#include "renderer.h"
#include "test_scene.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

#ifndef NDEBUG
    QLoggingCategory::setFilterRules(QStringLiteral("qt.vulkan=true"));
#endif

    QVulkanInstance inst;
    inst.setApiVersion(QVersionNumber(1, 2));

#ifndef NDEBUG
    inst.setLayers({"VK_LAYER_KHRONOS_validation"});
#endif

    if (!inst.create()) {
        qFatal("Failed to create Vulkan instance: %d", inst.errorCode());
    }

    RayTracingWindow window;
    window.setScene(createTestScene());
    window.setVulkanInstance(&inst);
    window.resize(1920, 1080);
    window.show();

    // Update window title with stats
    QTimer statsTimer;
    QObject::connect(&statsTimer, &QTimer::timeout, [&]() {
        if (window.renderer()) {
            float ms = window.renderer()->lastFrameTimeMs();
            float fps = ms > 0.001f ? 1000.0f / ms : 0;
            uint32_t samples = window.renderer()->frameIndex();
            window.setTitle(QString("Ray Tracing Demo - %1 fps - %2 samples | LMB:Orbit RMB:Pan Scroll:Zoom S:Save R:Reset")
                .arg(fps, 0, 'f', 1)
                .arg(samples));
        }
    });
    statsTimer.start(100);

    return app.exec();
}
