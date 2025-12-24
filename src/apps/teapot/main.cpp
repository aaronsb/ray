// Utah Teapot - Direct Bezier Patch Ray Tracing
// No tessellation. Pure mathematical surfaces.
// De Casteljau subdivision for GPU-friendly sub-patches.

#include <QApplication>
#include <QVulkanInstance>
#include <QLoggingCategory>
#include <cstdio>
#include <vector>

#include "teapot_patches.h"
#include "bezier_subdiv.h"
#include "renderer.h"

// Convert raw teapot data to Patch structs
std::vector<bezier::Patch> loadTeapotPatches() {
    std::vector<bezier::Patch> patches;
    patches.reserve(teapot::numPatches);

    for (int p = 0; p < teapot::numPatches; p++) {
        bezier::Patch patch;
        for (int i = 0; i < 16; i++) {
            int vertIdx = teapot::patches[p][i];
            patch.cp[i] = bezier::Vec3(
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

    printf("=== Utah Teapot Bezier Patch Ray Tracer ===\n\n");

    // Load original patches
    auto patches = loadTeapotPatches();
    printf("Original: %zu patches\n", patches.size());

    // Compute bounds of original teapot
    bezier::Vec3 teapotMin = patches[0].cp[0];
    bezier::Vec3 teapotMax = patches[0].cp[0];
    for (const auto& p : patches) {
        for (const auto& cp : p.cp) {
            teapotMin = bezier::Vec3::min(teapotMin, cp);
            teapotMax = bezier::Vec3::max(teapotMax, cp);
        }
    }
    printf("Teapot bounds: (%.2f, %.2f, %.2f) to (%.2f, %.2f, %.2f)\n",
           teapotMin.x, teapotMin.y, teapotMin.z,
           teapotMax.x, teapotMax.y, teapotMax.z);

    // Subdivide for GPU
    int maxDepth = 3;  // 4^3 = 64x subdivision per patch max
    float flatnessThreshold = 0.15f;  // Stop if AABB diagonal < this

    printf("\nSubdividing (maxDepth=%d, flatness=%.2f)...\n", maxDepth, flatnessThreshold);

    auto subPatches = bezier::subdividePatches(patches, maxDepth, flatnessThreshold);

    printf("After subdivision: %zu sub-patches\n", subPatches.size());
    printf("Expansion ratio: %.1fx\n", (float)subPatches.size() / patches.size());

    // Analyze sub-patch sizes
    float minDiag = 1e10f, maxDiag = 0, avgDiag = 0;
    for (const auto& sp : subPatches) {
        float d = sp.bounds.diagonal();
        minDiag = std::min(minDiag, d);
        maxDiag = std::max(maxDiag, d);
        avgDiag += d;
    }
    avgDiag /= subPatches.size();

    printf("\nSub-patch AABB diagonals:\n");
    printf("  Min: %.4f\n", minDiag);
    printf("  Max: %.4f\n", maxDiag);
    printf("  Avg: %.4f\n", avgDiag);

    // GPU memory estimate
    size_t patchBytes = subPatches.size() * 16 * sizeof(float) * 4;  // 16 vec4s per patch
    size_t aabbBytes = subPatches.size() * 6 * sizeof(float);         // min + max vec3
    printf("\nGPU memory estimate:\n");
    printf("  Patches: %.1f KB\n", patchBytes / 1024.0f);
    printf("  AABBs:   %.1f KB\n", aabbBytes / 1024.0f);
    printf("  Total:   %.1f KB\n", (patchBytes + aabbBytes) / 1024.0f);

    // Vulkan setup check
    QVulkanInstance inst;
    inst.setLayers({"VK_LAYER_KHRONOS_validation"});

    if (!inst.create()) {
        qFatal("Failed to create Vulkan instance: %d", inst.errorCode());
    }

    printf("\n=== Ready for GPU ray tracing ===\n");
    printf("Sub-patches are small enough for reliable Newton convergence.\n");
    printf("Next: Upload to GPU buffer and render.\n");

    return 0;
}
