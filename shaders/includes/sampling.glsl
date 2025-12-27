// ============================================================================
// PRIMITIVE SURFACE SAMPLING
// Uniform sampling of CSG primitive surfaces for area light sampling
// Each function returns a point on the surface and its normal
// PDF is 1/area for uniform sampling
// ============================================================================

// Sample a point uniformly on a sphere surface
// Returns position and outward normal
vec3 sampleSpherePoint(vec3 center, float radius, out vec3 normal) {
    // Uniform sphere sampling using two random numbers
    float u = rand();
    float v = rand();

    float theta = 2.0 * PI * u;
    float phi = acos(2.0 * v - 1.0);  // Maps [0,1] to [0,π]

    float sinPhi = sin(phi);
    normal = vec3(
        sinPhi * cos(theta),
        sinPhi * sin(theta),
        cos(phi)
    );

    return center + radius * normal;
}

// Sample a point uniformly on a box surface
// Box defined by center and half-extents
vec3 sampleBoxPoint(vec3 center, vec3 halfExt, out vec3 normal) {
    // Calculate face areas
    float areaXY = 4.0 * halfExt.x * halfExt.y;  // +Z and -Z faces
    float areaXZ = 4.0 * halfExt.x * halfExt.z;  // +Y and -Y faces
    float areaYZ = 4.0 * halfExt.y * halfExt.z;  // +X and -X faces
    float totalArea = 2.0 * (areaXY + areaXZ + areaYZ);

    // Choose face based on area-weighted probability
    float r = rand() * totalArea;
    float u = rand() * 2.0 - 1.0;  // [-1, 1]
    float v = rand() * 2.0 - 1.0;  // [-1, 1]

    vec3 localPos;

    if (r < areaYZ) {
        // -X face
        localPos = vec3(-halfExt.x, u * halfExt.y, v * halfExt.z);
        normal = vec3(-1.0, 0.0, 0.0);
    } else if (r < 2.0 * areaYZ) {
        // +X face
        localPos = vec3(halfExt.x, u * halfExt.y, v * halfExt.z);
        normal = vec3(1.0, 0.0, 0.0);
    } else if (r < 2.0 * areaYZ + areaXZ) {
        // -Y face
        localPos = vec3(u * halfExt.x, -halfExt.y, v * halfExt.z);
        normal = vec3(0.0, -1.0, 0.0);
    } else if (r < 2.0 * areaYZ + 2.0 * areaXZ) {
        // +Y face
        localPos = vec3(u * halfExt.x, halfExt.y, v * halfExt.z);
        normal = vec3(0.0, 1.0, 0.0);
    } else if (r < 2.0 * areaYZ + 2.0 * areaXZ + areaXY) {
        // -Z face
        localPos = vec3(u * halfExt.x, v * halfExt.y, -halfExt.z);
        normal = vec3(0.0, 0.0, -1.0);
    } else {
        // +Z face
        localPos = vec3(u * halfExt.x, v * halfExt.y, halfExt.z);
        normal = vec3(0.0, 0.0, 1.0);
    }

    return center + localPos;
}

// Sample a point uniformly on a cylinder surface (caps + lateral)
// Cylinder: center is base center, radius, height extends in +Y
vec3 sampleCylinderPoint(vec3 center, float radius, float height, out vec3 normal) {
    float capArea = PI * radius * radius;
    float lateralArea = 2.0 * PI * radius * height;
    float totalArea = 2.0 * capArea + lateralArea;

    float r = rand() * totalArea;

    if (r < capArea) {
        // Bottom cap (Y = 0)
        float u = rand();
        float v = rand();
        float rSample = radius * sqrt(u);
        float theta = 2.0 * PI * v;
        normal = vec3(0.0, -1.0, 0.0);
        return center + vec3(rSample * cos(theta), 0.0, rSample * sin(theta));
    } else if (r < 2.0 * capArea) {
        // Top cap (Y = height)
        float u = rand();
        float v = rand();
        float rSample = radius * sqrt(u);
        float theta = 2.0 * PI * v;
        normal = vec3(0.0, 1.0, 0.0);
        return center + vec3(rSample * cos(theta), height, rSample * sin(theta));
    } else {
        // Lateral surface
        float u = rand();
        float v = rand();
        float theta = 2.0 * PI * u;
        float y = v * height;
        normal = vec3(cos(theta), 0.0, sin(theta));
        return center + vec3(radius * cos(theta), y, radius * sin(theta));
    }
}

// Sample a point uniformly on a cone surface (base cap + lateral)
// Cone: center is base center, radius at base, height extends in +Y, apex at top
vec3 sampleConePoint(vec3 center, float radius, float height, out vec3 normal) {
    float baseArea = PI * radius * radius;
    float slantHeight = sqrt(radius * radius + height * height);
    float lateralArea = PI * radius * slantHeight;
    float totalArea = baseArea + lateralArea;

    float r = rand() * totalArea;

    if (r < baseArea) {
        // Base cap (Y = 0)
        float u = rand();
        float v = rand();
        float rSample = radius * sqrt(u);
        float theta = 2.0 * PI * v;
        normal = vec3(0.0, -1.0, 0.0);
        return center + vec3(rSample * cos(theta), 0.0, rSample * sin(theta));
    } else {
        // Lateral surface
        // For uniform sampling on cone lateral surface:
        // Sample uniformly in (theta, h) where h is height from base
        // The radius at height h is: r(h) = radius * (1 - h/height)
        float u = rand();
        float v = rand();
        float theta = 2.0 * PI * u;

        // For uniform area sampling on cone, use sqrt for radial distribution
        float h = (1.0 - sqrt(1.0 - v)) * height;
        float rAtH = radius * (1.0 - h / height);

        // Normal on cone surface
        float cosAlpha = radius / slantHeight;
        float sinAlpha = height / slantHeight;
        normal = normalize(vec3(cosAlpha * cos(theta), sinAlpha, cosAlpha * sin(theta)));

        return center + vec3(rAtH * cos(theta), h, rAtH * sin(theta));
    }
}

// Sample a point uniformly on a torus surface
// Torus: center, major radius R (ring), minor radius r (tube)
// Oriented with ring in XZ plane
vec3 sampleTorusPoint(vec3 center, float majorR, float minorR, out vec3 normal) {
    // Torus parametric surface:
    // x = (R + r*cos(v)) * cos(u)
    // y = r * sin(v)
    // z = (R + r*cos(v)) * sin(u)
    // where u is angle around the ring, v is angle around the tube

    // For uniform sampling, we need to account for the fact that
    // the surface area element varies with v (outer part has more area)
    // dA = r * (R + r*cos(v)) du dv

    // Sample u uniformly
    float u = rand() * 2.0 * PI;

    // For v, use rejection sampling or inverse CDF
    // P(v) ∝ (R + r*cos(v))
    // We can use: sample v uniformly, accept with probability (R + r*cos(v))/(R + r)
    float v;
    for (int i = 0; i < 10; i++) {  // Max iterations
        v = rand() * 2.0 * PI;
        float prob = (majorR + minorR * cos(v)) / (majorR + minorR);
        if (rand() < prob) break;
    }

    float cosU = cos(u);
    float sinU = sin(u);
    float cosV = cos(v);
    float sinV = sin(v);

    // Position on torus surface
    float tubeCenter = majorR + minorR * cosV;
    vec3 pos = vec3(
        tubeCenter * cosU,
        minorR * sinV,
        tubeCenter * sinU
    );

    // Normal points outward from tube center
    normal = normalize(vec3(
        cosV * cosU,
        sinV,
        cosV * sinU
    ));

    return center + pos;
}

// Sample a point on an emissive CSG primitive by index
// Returns the sampled point, normal, and pdf (1/area)
vec3 sampleEmissivePrimitive(uint primIndex, out vec3 lightNormal, out float pdf, float area) {
    CSGPrimitive prim = csgPrimitives[primIndex];
    vec3 center = vec3(prim.x, prim.y, prim.z);

    vec3 sampledPoint;

    switch (prim.type) {
        case CSG_PRIM_SPHERE:
            sampledPoint = sampleSpherePoint(center, prim.param0, lightNormal);
            break;
        case CSG_PRIM_BOX:
            sampledPoint = sampleBoxPoint(center, vec3(prim.param0, prim.param1, prim.param2), lightNormal);
            break;
        case CSG_PRIM_CYLINDER:
            sampledPoint = sampleCylinderPoint(center, prim.param0, prim.param1, lightNormal);
            break;
        case CSG_PRIM_CONE:
            sampledPoint = sampleConePoint(center, prim.param0, prim.param1, lightNormal);
            break;
        case CSG_PRIM_TORUS:
            sampledPoint = sampleTorusPoint(center, prim.param0, prim.param1, lightNormal);
            break;
        default:
            // Fallback - shouldn't happen
            sampledPoint = center;
            lightNormal = vec3(0.0, 1.0, 0.0);
            break;
    }

    // PDF is 1/area for uniform sampling
    pdf = 1.0 / area;

    return sampledPoint;
}
