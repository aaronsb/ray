#pragma once

// Material data structures for GPU upload
// Supports diffuse, metal, glass, and emissive materials

#include <vector>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace parametric {

// Material types (matches shader)
enum class MaterialType : uint32_t {
    Diffuse = 0,
    Metal = 1,
    Glass = 2,
    Emissive = 3,
};

// GPU Material - 32 bytes
struct alignas(16) Material {
    float r, g, b;          // albedo
    uint32_t type;          // MaterialType
    float roughness;        // 0=mirror, 1=matte
    float metallic;         // 0=dielectric, 1=conductor
    float ior;              // index of refraction
    float emissive;         // emission multiplier
};

// Material library builder
class MaterialLibrary {
public:
    // Add a material, returns its index
    uint32_t add(const Material& mat);

    // Add with name for lookup
    uint32_t add(const std::string& name, const Material& mat);

    // Lookup by name (returns 0 if not found)
    uint32_t find(const std::string& name) const;

    // Data accessor for GPU upload
    const std::vector<Material>& materials() const { return m_materials; }
    uint32_t count() const { return static_cast<uint32_t>(m_materials.size()); }

    void clear();

private:
    std::vector<Material> m_materials;
    std::unordered_map<std::string, uint32_t> m_nameIndex;
};

// Inline implementations

inline uint32_t MaterialLibrary::add(const Material& mat) {
    uint32_t idx = count();
    m_materials.push_back(mat);
    return idx;
}

inline uint32_t MaterialLibrary::add(const std::string& name, const Material& mat) {
    uint32_t idx = add(mat);
    m_nameIndex[name] = idx;
    return idx;
}

inline uint32_t MaterialLibrary::find(const std::string& name) const {
    auto it = m_nameIndex.find(name);
    return (it != m_nameIndex.end()) ? it->second : 0;
}

inline void MaterialLibrary::clear() {
    m_materials.clear();
    m_nameIndex.clear();
}

} // namespace parametric
