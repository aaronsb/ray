// scenecheck - Scene file parser and validator
// Usage: scenecheck <file.scene> [--dump]

#include "../../parametric/scene/scene_loader.h"
#include <iostream>
#include <fstream>
#include <sstream>

using namespace parametric;

void printUsage(const char* prog) {
    std::cerr << "Usage: " << prog << " <file.scene> [--dump]\n";
    std::cerr << "  --dump  Print parsed scene structure\n";
}

void dumpMaterial(const Material& m, size_t idx) {
    const char* typeNames[] = {"diffuse", "metal", "glass", "emissive"};
    std::cout << "  [" << idx << "] type=" << typeNames[m.type]
              << " rgb(" << m.r << ", " << m.g << ", " << m.b << ")"
              << " roughness=" << m.roughness
              << " ior=" << m.ior << "\n";
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
        std::cout << "  Materials:  " << data.materials.count() << "\n";
        std::cout << "  Primitives: " << data.csg.primitiveCount() << "\n";
        std::cout << "  Nodes:      " << data.csg.nodeCount() << "\n";
        std::cout << "  Roots:      " << data.csg.rootCount() << "\n";
        std::cout << "  Patches:    " << data.patchGroups.size() << " groups\n";
        std::cout << "  Instances:  " << data.patchInstances.size() << "\n";

        if (dump) {
            std::cout << "\nMaterials:\n";
            for (size_t i = 0; i < data.materials.materials().size(); i++) {
                dumpMaterial(data.materials.materials()[i], i);
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
        }

        return 0;
    } else {
        std::cerr << "  Scene load: FAILED\n";
        return 1;
    }
}
