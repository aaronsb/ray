// Ray's Bouncy Castle - GPU Path Tracer
// Scene-driven ray tracing with CSG primitives and Bezier patches

#include <QApplication>
#include <QVulkanInstance>
#include <QLoggingCategory>
#include <QCommandLineParser>
#include <QTimer>
#include <cstdio>

#include "ray_renderer.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ray");
    app.setApplicationVersion("1.0");

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Ray's Bouncy Castle - GPU Path Tracer");
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption screenshotOption(
        QStringList() << "screenshot",
        "Take screenshot on start and exit. Optionally specify filename.",
        "filename", "screenshot.png");
    parser.addOption(screenshotOption);

    QCommandLineOption framesOption(
        QStringList() << "frames",
        "Number of frames to accumulate before screenshot (default: 30).",
        "count", "30");
    parser.addOption(framesOption);

    QCommandLineOption noExitOption(
        QStringList() << "no-exit",
        "Don't exit after taking screenshot (keep window open).");
    parser.addOption(noExitOption);

    QCommandLineOption sceneOption(
        QStringList() << "s" << "scene",
        "Scene file to load (.scene format). Non-existent file = empty scene.",
        "file");
    parser.addOption(sceneOption);

    QCommandLineOption debugOption(
        QStringList() << "d" << "debug",
        "Enable Vulkan validation layers (verbose, for debugging).");
    parser.addOption(debugOption);

    parser.process(app);

    // No arguments = show help
    if (argc == 1) {
        parser.showHelp(0);
        return 0;
    }

    printf("=== Ray's Bouncy Castle ===\n\n");

    // Vulkan setup
    QVulkanInstance inst;
    inst.setApiVersion(QVersionNumber(1, 2));  // Match shader target
    if (parser.isSet(debugOption)) {
        inst.setLayers({"VK_LAYER_KHRONOS_validation"});
        printf("Vulkan validation layers enabled\n");
    }

    if (!inst.create()) {
        qFatal("Failed to create Vulkan instance: %d", inst.errorCode());
    }

    printf("Controls: Left-drag to orbit, Right-drag to pan, scroll to zoom, S to save, Esc to quit\n\n");

    // Create window and renderer
    RayWindow window;
    window.setVulkanInstance(&inst);

    // Enable ray tracing extensions for hardware-accelerated BVH traversal
    window.setDeviceExtensions({
        "VK_KHR_acceleration_structure",
        "VK_KHR_ray_query",
        "VK_KHR_deferred_host_operations",  // Required by acceleration_structure
        "VK_KHR_buffer_device_address"      // Required by acceleration_structure
    });

    // Set scene file if specified
    if (parser.isSet(sceneOption)) {
        window.setScenePath(parser.value(sceneOption));
    }

    window.setTitle("Ray's Bouncy Castle");
    window.resize(800, 600);

    // Handle screenshot on start
    if (parser.isSet(screenshotOption)) {
        QString filename = parser.value(screenshotOption);
        uint32_t frames = parser.value(framesOption).toUInt();
        bool exitAfter = !parser.isSet(noExitOption);

        printf("Screenshot mode: %s after %u frames%s\n",
               qPrintable(filename), frames,
               exitAfter ? " (will exit)" : " (staying open)");

        window.setScreenshotOnStart(filename, frames, exitAfter);

        // Start checking after window is shown
        QTimer::singleShot(100, &window, &RayWindow::checkScreenshotReady);
    }

    window.show();

    return app.exec();
}
