// Material evaluation and scattering functions
// Requires: Ray, HitRecord, Material, materials[], rand(), rand2(),
//           randomUnitVector(), randomInUnitSphere(), fbm(), gradientNoise()
//           EPSILON, PI

// Schlick approximation for Fresnel
float schlick(float cosine, float refIdx) {
    float r0 = (1.0 - refIdx) / (1.0 + refIdx);
    r0 = r0 * r0;
    return r0 + (1.0 - r0) * pow(1.0 - cosine, 5.0);
}

// Custom refract (avoiding name conflict with builtin)
vec3 refractRay(vec3 uv, vec3 n, float etaiOverEtat) {
    float cosTheta = min(dot(-uv, n), 1.0);
    vec3 rOutPerp = etaiOverEtat * (uv + cosTheta * n);
    vec3 rOutParallel = -sqrt(abs(1.0 - dot(rOutPerp, rOutPerp))) * n;
    return rOutPerp + rOutParallel;
}

// Material scattering - returns true if ray should continue, false if absorbed
bool scatter(Ray rIn, HitRecord rec, out vec3 attenuation, out Ray scattered) {
    Material mat = materials[rec.materialId];

    if (mat.type == 0) {
        // Diffuse (Lambertian)
        vec3 scatterDir = rec.normal + randomUnitVector();
        if (dot(scatterDir, scatterDir) < EPSILON)
            scatterDir = rec.normal;
        scattered = Ray(rec.point, normalize(scatterDir));
        attenuation = mat.albedo;
        return true;
    }
    else if (mat.type == 1) {
        // Metal
        vec3 reflected = reflect(normalize(rIn.direction), rec.normal);
        reflected += mat.param * randomInUnitSphere(); // roughness
        scattered = Ray(rec.point, normalize(reflected));
        attenuation = mat.albedo;
        return dot(scattered.direction, rec.normal) > 0.0;
    }
    else if (mat.type == 2) {
        // Dielectric (glass)
        attenuation = vec3(1.0);
        float refractionRatio = rec.frontFace ? (1.0 / mat.param) : mat.param;

        vec3 unitDir = normalize(rIn.direction);
        float cosTheta = min(dot(-unitDir, rec.normal), 1.0);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

        bool cannotRefract = refractionRatio * sinTheta > 1.0;
        vec3 direction;

        if (cannotRefract || schlick(cosTheta, refractionRatio) > rand())
            direction = reflect(unitDir, rec.normal);
        else
            direction = refractRay(unitDir, rec.normal, refractionRatio);

        scattered = Ray(rec.point, direction);
        return true;
    }
    else if (mat.type == 3) {
        // Emissive - no scattering
        return false;
    }
    else if (mat.type == 4) {
        // Rough Dielectric (frosted glass)
        attenuation = vec3(1.0);
        float refractionRatio = rec.frontFace ? (1.0 / mat.param) : mat.param;

        vec3 unitDir = normalize(rIn.direction);
        float cosTheta = min(dot(-unitDir, rec.normal), 1.0);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

        bool cannotRefract = refractionRatio * sinTheta > 1.0;
        vec3 direction;

        if (cannotRefract || schlick(cosTheta, refractionRatio) > rand())
            direction = reflect(unitDir, rec.normal);
        else
            direction = refractRay(unitDir, rec.normal, refractionRatio);

        // Add roughness perturbation (frosted effect)
        direction = normalize(direction + mat.param2 * randomInUnitSphere());

        scattered = Ray(rec.point, direction);
        return true;
    }
    else if (mat.type == 5) {
        // Anisotropic Metal (brushed steel)
        vec3 reflected = reflect(normalize(rIn.direction), rec.normal);

        // Create tangent frame (brushed direction along surface)
        vec3 tangent = normalize(cross(rec.normal, vec3(0.0, 1.0, 0.0)));
        if (length(tangent) < 0.001)
            tangent = normalize(cross(rec.normal, vec3(1.0, 0.0, 0.0)));
        vec3 bitangent = cross(rec.normal, tangent);

        // Anisotropic roughness
        float roughT = mat.param;
        float roughB = mat.param * mat.param2;

        vec2 rnd = rand2();
        vec3 perturbation = tangent * (rnd.x * 2.0 - 1.0) * roughT +
                           bitangent * (rnd.y * 2.0 - 1.0) * roughB;

        reflected = normalize(reflected + perturbation);
        scattered = Ray(rec.point, reflected);
        attenuation = mat.albedo;
        return dot(scattered.direction, rec.normal) > 0.0;
    }
    else if (mat.type == 6) {
        // Dichroic coated glass - thin film interference
        float glassIOR = 1.5;
        float filmIOR = 1.4;

        vec3 unitDir = normalize(rIn.direction);
        float cosTheta = min(dot(-unitDir, rec.normal), 1.0);
        float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

        // Calculate thin-film interference color
        float sinTheta2 = 1.0 - cosTheta * cosTheta;
        float cosRefracted = sqrt(max(0.0, 1.0 - sinTheta2 / (filmIOR * filmIOR)));

        float peakWavelength = mix(400.0, 700.0, mat.param);
        float thickness = peakWavelength / (2.0 * filmIOR * cosRefracted);
        float pathDiff = 2.0 * filmIOR * thickness * cosRefracted;

        float wavelengths[7] = float[](650.0, 600.0, 570.0, 540.0, 490.0, 450.0, 420.0);
        vec3 spectralRGB[7] = vec3[](
            vec3(1.0, 0.0, 0.0),
            vec3(1.0, 0.5, 0.0),
            vec3(1.0, 1.0, 0.0),
            vec3(0.0, 1.0, 0.0),
            vec3(0.0, 0.7, 1.0),
            vec3(0.0, 0.0, 1.0),
            vec3(0.4, 0.0, 1.0)
        );

        float sharpness = mix(4.0, 1.0, mat.param2);
        vec3 interferenceColor = vec3(0.0);
        for (int i = 0; i < 7; i++) {
            float phase = pathDiff / wavelengths[i];
            float interference = pow(0.5 + 0.5 * cos(phase * 2.0 * PI), sharpness);
            interferenceColor += interference * spectralRGB[i];
        }
        interferenceColor /= 3.5;

        // Glass refraction logic
        float refractionRatio = rec.frontFace ? (1.0 / glassIOR) : glassIOR;
        bool cannotRefract = refractionRatio * sinTheta > 1.0;

        float baseReflect = schlick(cosTheta, refractionRatio);
        float filmBoost = 0.15 + 0.25 * pow(1.0 - cosTheta, 2.0);
        float reflectProb = min(baseReflect + filmBoost, 0.95);

        if (cannotRefract || rand() < reflectProb) {
            vec3 reflected = reflect(unitDir, rec.normal);
            scattered = Ray(rec.point, reflected);
            attenuation = interferenceColor;
        } else {
            vec3 refracted = refractRay(unitDir, rec.normal, refractionRatio);
            scattered = Ray(rec.point, refracted);
            attenuation = vec3(1.0) - interferenceColor * 0.1;
        }
        return true;
    }
    else if (mat.type == 7) {
        // Green Marble - polished with glossy coat
        vec3 p = rec.point * mat.param;

        float greenNoise = fbm(p * 0.8, 5);
        float greenVar = smoothstep(-0.3, 0.5, greenNoise);

        vec3 darkGreen = mat.albedo * 0.3;
        vec3 midGreen = mat.albedo;
        vec3 veinColor = mat.emission;

        vec3 baseColor = mix(darkGreen, midGreen, greenVar);

        float plasma = fbm(p * 0.5, 6);
        float center = 0.1;
        float width = 0.025;
        float quartzAmt = 1.0 - smoothstep(0.0, width, abs(plasma - center));
        quartzAmt = pow(quartzAmt, 1.5);

        vec3 diffuseColor = mix(baseColor, veinColor, quartzAmt);

        float cosTheta = abs(dot(normalize(-rIn.direction), rec.normal));
        float fresnel = 0.04 + 0.96 * pow(1.0 - cosTheta, 5.0);

        if (rand() < fresnel) {
            vec3 reflected = reflect(normalize(rIn.direction), rec.normal);
            reflected += 0.03 * randomInUnitSphere();
            scattered = Ray(rec.point, normalize(reflected));
            attenuation = vec3(1.0);
        } else {
            vec3 scatterDir = rec.normal + randomUnitVector();
            if (dot(scatterDir, scatterDir) < EPSILON)
                scatterDir = rec.normal;
            scattered = Ray(rec.point, normalize(scatterDir));
            attenuation = diffuseColor;
        }
        return true;
    }
    else if (mat.type == 8) {
        // Wood - polished/lacquered
        vec3 noisePos = rec.point * mat.param;

        float dist = length(noisePos.xz);
        float noise = gradientNoise(noisePos * 0.5) * 1.5;
        noise += gradientNoise(noisePos * 1.0) * 0.5;

        float ring = fract((dist + noise) * 1.0);
        float t = pow(ring, 0.5);

        float grain = gradientNoise(vec3(noisePos.x * 0.5, noisePos.y * 8.0, noisePos.z * 0.5));
        t = clamp(t + grain * 0.15, 0.0, 1.0);

        vec3 diffuseColor = mix(mat.emission, mat.albedo, t);

        float cosTheta = abs(dot(normalize(-rIn.direction), rec.normal));
        float fresnel = 0.04 + 0.96 * pow(1.0 - cosTheta, 5.0);

        if (rand() < fresnel) {
            vec3 reflected = reflect(normalize(rIn.direction), rec.normal);
            reflected += 0.02 * randomInUnitSphere();
            scattered = Ray(rec.point, normalize(reflected));
            attenuation = vec3(1.0);
        } else {
            vec3 scatterDir = rec.normal + randomUnitVector();
            if (dot(scatterDir, scatterDir) < EPSILON)
                scatterDir = rec.normal;
            scattered = Ray(rec.point, normalize(scatterDir));
            attenuation = diffuseColor;
        }
        return true;
    }
    else if (mat.type == 9) {
        // Swirl - polished
        vec3 noisePos = rec.point * mat.param;

        vec3 warp = vec3(
            fbm(noisePos, 4),
            fbm(noisePos + vec3(5.2, 1.3, 2.8), 4),
            fbm(noisePos + vec3(1.7, 9.2, 3.1), 4)
        );
        float n = fbm(noisePos + warp * 2.0, 4);
        float t = 0.5 + 0.5 * n;

        vec3 diffuseColor = mix(mat.albedo, mat.emission, t);

        float cosTheta = abs(dot(normalize(-rIn.direction), rec.normal));
        float fresnel = 0.04 + 0.96 * pow(1.0 - cosTheta, 5.0);

        if (rand() < fresnel) {
            vec3 reflected = reflect(normalize(rIn.direction), rec.normal);
            reflected += 0.03 * randomInUnitSphere();
            scattered = Ray(rec.point, normalize(reflected));
            attenuation = vec3(1.0);
        } else {
            vec3 scatterDir = rec.normal + randomUnitVector();
            if (dot(scatterDir, scatterDir) < EPSILON)
                scatterDir = rec.normal;
            scattered = Ray(rec.point, normalize(scatterDir));
            attenuation = diffuseColor;
        }
        return true;
    }
    else if (mat.type == 10) {
        // Checker pattern - polished floor
        vec3 p = rec.point * mat.param;

        float fx = fract(p.x * 0.5) * 2.0;
        float fy = fract(p.y * 0.5) * 2.0;
        float fz = fract(p.z * 0.5) * 2.0;

        bool flipX = fx >= 1.0;
        bool flipY = fy >= 1.0;
        bool flipZ = fz >= 1.0;
        bool isEven = (flipX ^^ flipY ^^ flipZ);

        vec3 diffuseColor = isEven ? mat.albedo : mat.emission;

        float cosTheta = abs(dot(normalize(-rIn.direction), rec.normal));
        float fresnel = 0.04 + 0.96 * pow(1.0 - cosTheta, 5.0);

        if (rand() < fresnel) {
            vec3 reflected = reflect(normalize(rIn.direction), rec.normal);
            reflected += 0.02 * randomInUnitSphere();
            vec3 offsetOrigin = rec.point + rec.normal * 0.001;
            scattered = Ray(offsetOrigin, normalize(reflected));
            attenuation = vec3(0.75) * mix(vec3(1.0), diffuseColor, 0.15);
        } else {
            vec3 scatterDir = rec.normal + randomUnitVector();
            if (dot(scatterDir, scatterDir) < EPSILON)
                scatterDir = rec.normal;
            vec3 offsetOrigin = rec.point + rec.normal * 0.001;
            scattered = Ray(offsetOrigin, normalize(scatterDir));
            attenuation = diffuseColor;
        }
        return true;
    }

    return false;
}
