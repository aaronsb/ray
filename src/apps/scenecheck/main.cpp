// scenecheck - Scene file parser and validator
// Usage: scenecheck <file.scene> [--dump]

#include "../../parametric/scene/scene_loader.h"
#include "../../parametric/lights/lights.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>

using namespace parametric;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <file.scene> [--dump]\n";
    std::cerr << "  --dump  Print parsed scene structure\n";
}

const char* goboPatternName(uint32_t pattern) {
    switch (pattern) {
        case 0: return "none";
        case 1: return "bars";
        case 2: return "grid";
        case 3: return "dots";
        case 4: return "radial";
        case 5: return "noise";
        default: return "unknown";
    }
}

void dumpMaterial(const Material& m, size_t idx, const std::string& name) {
    const char* typeNames[] = {"diffuse", "metal", "glass", "emissive", "checker"};
    uint32_t type = m.type < 5 ? m.type : 0;
    std::cout << "  [" << idx << "] ";
    if (!name.empty()) std::cout << "\"" << name << "\" ";
    std::cout << typeNames[type];
    std::cout << " rgb(" << m.r << ", " << m.g << ", " << m.b << ")";
    if (m.type == 1) { // metal
        std::cout << " roughness=" << m.roughness;
        if (m.metallic > 0) std::cout << " metallic=" << m.metallic;
    } else if (m.type == 2) { // glass
        std::cout << " ior=" << m.ior;
    } else if (m.type == 3) { // emissive
        std::cout << " emissive=" << m.emissive;
    } else if (m.type == 4) { // checker
        std::cout << " scale=" << m.emissive;
    }
    std::cout << "\n";
}

void dumpPrimitive(const CSGPrimitive& p, size_t idx) {
    const char* typeNames[] = {"sphere", "box", "cylinder", "cone", "torus"};
    std::cout << "  [" << idx << "] " << typeNames[p.type]
              << " at(" << p.x << ", " << p.y << ", " << p.z << ")";

    switch (p.type) {
        case 0: // sphere
            std::cout << " r=" << p.param0;
            break;
        case 1: // box
            std::cout << " half(" << p.param0 << ", " << p.param1 << ", " << p.param2 << ")";
            break;
        case 2: // cylinder
        case 3: // cone
            std::cout << " r=" << p.param0 << " h=" << p.param1;
            break;
        case 4: // torus
            std::cout << " major=" << p.param0 << " minor=" << p.param1;
            break;
    }
    std::cout << "\n";
}

void dumpNode(const CSGNode& n, size_t idx) {
    const char* typeNames[] = {"primitive", "union", "intersect", "subtract"};
    std::cout << "  [" << idx << "] " << typeNames[n.type];

    if (n.type == 0) {
        std::cout << " -> prim[" << n.left << "]";
    } else {
        std::cout << " left=" << n.left << " right=" << n.right;
    }
    std::cout << " mat=" << n.materialId << "\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string filepath = argv[1];
    bool dump = (argc > 2 && std::string(argv[2]) == "--dump");

    // Read file
    std::ifstream file(filepath);
    if (!file) {
        std::cerr << "Error: Cannot open file: " << filepath << "\n";
        return 1;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();

    // Parse S-expressions first (syntax check)
    std::cout << "Parsing " << filepath << "...\n";

    std::vector<SExp> expressions;
    try {
        expressions = parseSExp(source);
        std::cout << "  S-expression parse: OK (" << expressions.size() << " top-level forms)\n";
    } catch (const std::exception& e) {
        std::cerr << "  S-expression parse: FAILED\n";
        std::cerr << "  Error: " << e.what() << "\n";
        return 1;
    }

    // Load into scene structures (uses loadFile to resolve includes relative to scene file)
    SceneData data;

    if (SceneLoader::loadFile(filepath, data)) {
        std::cout << "  Scene load: OK\n";
        std::cout << "\nScene summary:\n";
        std::cout << "  Materials:    " << data.materials.count() << "\n";
        std::cout << "  Primitives:   " << data.csg.primitiveCount() << "\n";
        std::cout << "  Nodes:        " << data.csg.nodeCount() << "\n";
        std::cout << "  Roots:        " << data.csg.rootCount() << "\n";
        std::cout << "  Patches:      " << data.patchGroups.size() << " groups\n";
        std::cout << "  Instances:    " << data.patchInstances.size() << "\n";
        std::cout << "  Point lights: " << data.lights.pointLightCount() << "\n";
        std::cout << "  Spotlights:   " << data.lights.spotLightCount() << "\n";

        std::cout << "\nSun:\n";
        std::cout << "  Azimuth:    " << data.lights.sun.azimuth << " deg\n";
        std::cout << "  Elevation:  " << data.lights.sun.elevation << " deg\n";
        std::cout << "  Color:      (" << data.lights.sun.r << ", " << data.lights.sun.g << ", " << data.lights.sun.b << ")\n";
        std::cout << "  Intensity:  " << data.lights.sun.intensity << "\n";
        std::cout << "  Ambient:    " << data.lights.sun.ambient << "\n";

        std::cout << "\nFloor:\n";
        std::cout << "  Enabled:  " << (data.floor.enabled ? "yes" : "no") << "\n";
        if (data.floor.enabled) {
            std::cout << "  Y:        " << data.floor.y << "\n";
            std::cout << "  Material: " << data.floor.materialName << "\n";
        }

        std::cout << "\nBackground:\n";
        std::cout << "  Color: (" << data.background.r << ", " << data.background.g << ", " << data.background.b << ")\n";

        if (dump) {
            std::cout << "\nMaterials:\n";
            for (size_t i = 0; i < data.materials.materials().size(); i++) {
                std::string name = data.materials.nameForIndex(static_cast<uint32_t>(i));
                dumpMaterial(data.materials.materials()[i], i, name);
            }

            std::cout << "\nPrimitives:\n";
            for (size_t i = 0; i < data.csg.primitives().size(); i++) {
                dumpPrimitive(data.csg.primitives()[i], i);
            }

            std::cout << "\nNodes:\n";
            for (size_t i = 0; i < data.csg.nodes().size(); i++) {
                dumpNode(data.csg.nodes()[i], i);
            }

            std::cout << "\nRoots: ";
            for (auto r : data.csg.roots()) {
                std::cout << r << " ";
            }
            std::cout << "\n";

            std::cout << "\nPatch Groups:\n";
            for (const auto& [name, patches] : data.patchGroups) {
                std::cout << "  " << name << ": " << patches.size() << " patches\n";
            }

            std::cout << "\nInstances:\n";
            for (const auto& inst : data.patchInstances) {
                std::cout << "  " << inst.patchGroupName
                          << " at(" << inst.x << ", " << inst.y << ", " << inst.z << ")"
                          << " scale=" << inst.scale
                          << " mat=" << inst.materialName << "\n";
            }

            if (!data.lights.pointLights.empty()) {
                std::cout << "\nPoint Lights:\n";
                for (size_t i = 0; i < data.lights.pointLights.size(); i++) {
                    const auto& l = data.lights.pointLights[i];
                    // Note: dirX/Y/Z stores position for point lights
                    std::cout << "  [" << i << "] at(" << l.dirX << ", " << l.dirY << ", " << l.dirZ << ")"
                              << " color(" << l.r << ", " << l.g << ", " << l.b << ")"
                              << " intensity=" << l.intensity << "\n";
                }
            }

            if (!data.lights.spotLights.empty()) {
                std::cout << "\nSpotlights:\n";
                for (size_t i = 0; i < data.lights.spotLights.size(); i++) {
                    const auto& s = data.lights.spotLights[i];
                    std::cout << "  [" << i << "] at(" << s.posX << ", " << s.posY << ", " << s.posZ << ")"
                              << " dir(" << s.dirX << ", " << s.dirY << ", " << s.dirZ << ")\n";
                    std::cout << "       color(" << s.r << ", " << s.g << ", " << s.b << ")"
                              << " intensity=" << s.intensity << "\n";
                    std::cout << "       angles: inner=" << std::acos(s.cosInner) * 180.0f / 3.14159265f
                              << " outer=" << std::acos(s.cosOuter) * 180.0f / 3.14159265f << " deg\n";
                    std::cout << "       gobo: " << goboPatternName(s.goboPattern)
                              << " scale=" << s.goboScale << "\n";
                }
            }
        }

        return 0;
    } else {
        std::cerr << "  Scene load: FAILED\n";
        return 1;
    }
}
