#include "test_scene.h"
#include "../teapot/teapot_patches.h"  // Teapot data (TODO: move to shared location)

// Load teapot patches into parametric::Patch format
static std::vector<parametric::Patch> loadTeapotPatches() {
    std::vector<parametric::Patch> patches;
    patches.reserve(teapot::numPatches);
    for (int p = 0; p < teapot::numPatches; p++) {
        parametric::Patch patch;
        for (int i = 0; i < 16; i++) {
            int vertIdx = teapot::patches[p][i];
            patch.cp[i] = parametric::Vec3(
                teapot::vertices[vertIdx][0],
                teapot::vertices[vertIdx][1],
                teapot::vertices[vertIdx][2]
            );
        }
        patches.push_back(patch);
    }
    return patches;
}

Scene createTestScene() {
    Scene scene;

    // === MATERIALS ===
    // 0: Checker floor
    scene.addMaterial(MaterialType::Checker, {0.9f, 0.9f, 0.9f}, {0.2f, 0.2f, 0.2f}, 1.0f);
    // 1: Metal
    scene.addMaterial(MaterialType::Metal, {0.8f, 0.8f, 0.9f}, {0,0,0}, 0.05f);
    // 2: Glass
    scene.addMaterial(MaterialType::Dielectric, {1, 1, 1}, {0,0,0}, 1.5f);
    // 3: Diffuse
    scene.addMaterial(MaterialType::Diffuse, {0.8f, 0.4f, 0.4f});
    // 4: Marble
    scene.addMaterial(MaterialType::Marble, {0.0f, 0.25f, 0.2f}, {0.95f, 0.98f, 0.95f}, 8.0f);

    // === GEOMETRY: One of each primitive ===

    // Ground plane (large sphere)
    scene.addSphere({0, -100.0f, 0}, 100.0f, 0);

    // Sphere
    scene.addSphere({0, 1.0f, 0}, 1.0f, 1);

    // Box
    scene.addBox({-2.5f, 0.75f, 0}, {0.75f, 0.75f, 0.75f}, 2);

    // Cylinder
    scene.addCylinder({2.5f, 0.0f, 0}, {0.0f, 1.0f, 0.0f}, 0.5f, 2.0f, 3, true);

    // Cone
    scene.addCone({0, 0.0f, -2.5f}, {0.0f, 1.0f, 0.0f}, 0.6f, 1.5f, 4, true);

    // Torus
    scene.addTorus({0, 1.0f, 2.5f}, {0.0f, 1.0f, 0.0f}, 0.8f, 0.25f, 2);

    // Bezier patch group (teapot)
    scene.buildBezierGroup(loadTeapotPatches());
    scene.addBezierInstance(-3.0f, 0.5f, 3.0f, 0.3f, 0.0f, 0.0f, 0.0f, 1);  // Metal teapot

    return scene;
}
