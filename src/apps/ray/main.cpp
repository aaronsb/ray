// Ray - Bezier Patch Ray Tracer
// Direct ray tracing of parametric surfaces (no tessellation)
// BezierPatchGroup handles subdivision and BVH construction.

#include <QApplication>
#include <QVulkanInstance>
#include <QLoggingCategory>
#include <QCommandLineParser>
#include <QTimer>
#include <cstdio>
#include <vector>

#include "teapot_patches.h"  // Utah teapot bezier patch data
#include "ray_renderer.h"

using parametric::Vec3;
using parametric::Patch;

// Convert raw teapot data to Patch structs
std::vector<Patch> loadTeapotPatches() {
    std::vector<Patch> patches;
    patches.reserve(teapot::numPatches);

    for (int p = 0; p < teapot::numPatches; p++) {
        Patch patch;
        for (int i = 0; i < 16; i++) {
            int vertIdx = teapot::patches[p][i];
            patch.cp[i] = Vec3(
                teapot::vertices[vertIdx][0],
                teapot::vertices[vertIdx][1],
                teapot::vertices[vertIdx][2]
            );
        }
        patches.push_back(patch);
    }

    return patches;
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("ray");
    app.setApplicationVersion("1.0");

    // Parse command line arguments
    QCommandLineParser parser;
    parser.setApplicationDescription("Utah Teapot Bezier Patch Ray Tracer");
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

    parser.process(app);

    printf("=== Utah Teapot Bezier Patch Ray Tracer ===\n\n");

    // Load original patches
    auto patches = loadTeapotPatches();
    printf("Loaded %zu original patches\n", patches.size());

    // Compute and display bounds
    Vec3 boundsMin = patches[0].cp[0];
    Vec3 boundsMax = patches[0].cp[0];
    for (const auto& p : patches) {
        for (const auto& cp : p.cp) {
            boundsMin.x = std::min(boundsMin.x, cp.x);
            boundsMin.y = std::min(boundsMin.y, cp.y);
            boundsMin.z = std::min(boundsMin.z, cp.z);
            boundsMax.x = std::max(boundsMax.x, cp.x);
            boundsMax.y = std::max(boundsMax.y, cp.y);
            boundsMax.z = std::max(boundsMax.z, cp.z);
        }
    }
    printf("Bounds: (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n",
           boundsMin.x, boundsMin.y, boundsMin.z,
           boundsMax.x, boundsMax.y, boundsMax.z);

    // Vulkan setup
    QVulkanInstance inst;
    inst.setLayers({"VK_LAYER_KHRONOS_validation"});

    if (!inst.create()) {
        qFatal("Failed to create Vulkan instance: %d", inst.errorCode());
    }

    printf("\n=== Launching GPU ray tracer ===\n");
    printf("Controls: Left-drag to orbit, Right-drag to pan, scroll to zoom, S to save, Esc to quit\n\n");

    // Create window and renderer
    // BezierPatchGroup handles subdivision and BVH internally
    RayWindow window;
    window.setVulkanInstance(&inst);
    window.setPatches(std::move(patches));
    window.setTitle("Ray - Bezier Patch Ray Tracing");
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
