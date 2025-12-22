// Light sampling and spotlight functions

// Light sampling for NEE (Next Event Estimation)
// Returns light position and emission for emissive spheres
// Requires: spheres[], materials[], pc.sphereCount, rand()
void sampleLight(out vec3 lightPos, out vec3 lightEmission, out float lightRadius, out uint lightCount) {
    // Count emissive spheres and randomly select one
    uint lightIndices[8];  // max 8 lights
    lightCount = 0u;

    for (uint i = 0; i < pc.sphereCount && lightCount < 8u; i++) {
        Material mat = materials[spheres[i].materialId];
        if (mat.type == 3) {
            lightIndices[lightCount] = i;
            lightCount++;
        }
    }

    if (lightCount == 0u) {
        // Fallback
        lightPos = vec3(0, 8, 0);
        lightRadius = 1.0;
        lightEmission = vec3(15.0);
        lightCount = 1u;
        return;
    }

    // Randomly select one light
    uint selected = uint(rand() * float(lightCount)) % lightCount;
    uint idx = lightIndices[selected];

    lightPos = spheres[idx].center;
    lightRadius = spheres[idx].radius;
    lightEmission = materials[spheres[idx].materialId].emission;
}

// Sample random point on sphere surface
// Requires: randomUnitVector()
vec3 sampleSpherePoint(vec3 center, float radius) {
    vec3 dir = randomUnitVector();
    return center + dir * radius;
}

// Check if ray hits anything before maxDist
// Requires: Ray, HitRecord, hitScene(), materials[], EPSILON
bool shadowRay(vec3 origin, vec3 dir, float maxDist) {
    Ray ray = Ray(origin, dir);
    HitRecord rec;
    if (hitScene(ray, EPSILON, maxDist - EPSILON, rec)) {
        // Hit something - but is it the light itself?
        Material mat = materials[rec.materialId];
        if (mat.type == 3) return false; // Hit the light, not shadowed
        return true; // Hit something else, shadowed
    }
    return false; // Nothing hit, light is visible
}

// ============ SPOTLIGHT FUNCTIONS ============

// Evaluate gobo pattern at a 2D coordinate
// Returns 0.0 (blocked) to 1.0 (full light)
float evaluateGobo(uint goboType, vec2 uv, float scale, float rotation) {
    // Apply rotation
    float c = cos(rotation);
    float s = sin(rotation);
    vec2 ruv = vec2(c * uv.x - s * uv.y, s * uv.x + c * uv.y);
    ruv *= scale;

    if (goboType == 0u) {
        // No gobo - full light
        return 1.0;
    } else if (goboType == 1u) {
        // Stripes - vertical bars
        return step(0.5, fract(ruv.x));
    } else if (goboType == 2u) {
        // Grid - window pattern
        float bars = step(0.3, fract(ruv.x)) * step(0.3, fract(ruv.y));
        return bars;
    } else if (goboType == 3u) {
        // Circles - concentric rings
        float r = length(ruv);
        return step(0.5, fract(r));
    } else if (goboType == 4u) {
        // Dots - array of circles
        vec2 cell = fract(ruv) - 0.5;
        return 1.0 - smoothstep(0.2, 0.25, length(cell));
    } else if (goboType == 5u) {
        // Star - radial burst pattern
        float angle = atan(ruv.y, ruv.x);
        float rays = abs(sin(angle * 6.0));
        float r = length(ruv);
        return smoothstep(0.0, 0.5, rays) * smoothstep(2.0, 0.0, r);
    } else if (goboType == 6u) {
        // Off - light completely disabled
        return 0.0;
    }
    return 1.0;
}

// Evaluate spotlight contribution at a given point
// Returns the light color/intensity multiplied by cone falloff and gobo pattern
// Requires: SpotLight struct
vec3 evaluateSpotLight(SpotLight light, vec3 point, vec3 normal, out vec3 lightDir, out float distance) {
    vec3 toLight = light.position - point;
    distance = length(toLight);
    lightDir = toLight / distance;

    // Check if point is in front of the spotlight
    float cosAngle = dot(-lightDir, light.direction);
    if (cosAngle <= cos(light.outerAngle)) {
        return vec3(0.0);  // Outside outer cone
    }

    // Cone falloff - smooth transition from inner to outer
    float cosInner = cos(light.innerAngle);
    float cosOuter = cos(light.outerAngle);
    float spotFalloff = smoothstep(cosOuter, cosInner, cosAngle);

    // Distance attenuation (inverse square law)
    float attenuation = 1.0 / (distance * distance);

    // Compute UV for gobo pattern
    // Project point onto plane perpendicular to light direction
    vec3 lightRight, lightUp;
    if (abs(light.direction.y) < 0.99) {
        lightRight = normalize(cross(light.direction, vec3(0, 1, 0)));
    } else {
        lightRight = normalize(cross(light.direction, vec3(1, 0, 0)));
    }
    lightUp = cross(light.direction, lightRight);

    // Project the light-to-point vector onto the plane
    vec3 toPointLocal = -toLight;
    vec2 goboUV = vec2(dot(toPointLocal, lightRight), dot(toPointLocal, lightUp));
    // Normalize by distance to get angular coordinates
    goboUV /= distance;

    float goboMask = evaluateGobo(light.goboType, goboUV, light.goboScale, light.goboRotation);

    return light.color * spotFalloff * attenuation * goboMask;
}
