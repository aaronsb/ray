// Utah Teapot - Direct Bezier Patch Ray Tracing
// No tessellation. Pure mathematical surfaces.

#include <QApplication>
#include <QVulkanInstance>
#include <QLoggingCategory>
#include <cstdio>

#include "teapot_patches.h"
#include "renderer.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);

    // Verify teapot data loaded
    printf("Utah Teapot: %d patches, %d vertices\n",
           teapot::numPatches, teapot::numVertices);

    // Print first patch's control points as sanity check
    printf("Patch 0 (rim) control points:\n");
    for (int row = 0; row < 4; row++) {
        printf("  ");
        for (int col = 0; col < 4; col++) {
            int idx = teapot::patches[0][row * 4 + col];
            printf("(%.2f, %.2f, %.2f) ",
                   teapot::vertices[idx][0],
                   teapot::vertices[idx][1],
                   teapot::vertices[idx][2]);
        }
        printf("\n");
    }

    // TODO: Set up Vulkan renderer
    // For now, just verify the data is correct

    QVulkanInstance inst;
    inst.setLayers({"VK_LAYER_KHRONOS_validation"});

    if (!inst.create()) {
        qFatal("Failed to create Vulkan instance: %d", inst.errorCode());
    }

    printf("\nVulkan initialized. Ready for Bezier patch ray tracing.\n");
    printf("Next step: implement ray-bicubic Bezier intersection via Newton iteration.\n");

    // For now, just exit successfully after verifying setup
    // TODO: Create window with patch renderer

    return 0;
}
