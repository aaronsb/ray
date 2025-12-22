// Ray tracing geometry primitives and intersection tests

// Ray structure
struct Ray {
    vec3 origin;
    vec3 direction;
};

vec3 rayAt(Ray r, float t) {
    return r.origin + t * r.direction;
}

// Hit record
struct HitRecord {
    vec3 point;
    vec3 normal;
    float t;
    uint materialId;
    bool frontFace;
};

void setFaceNormal(inout HitRecord rec, Ray r, vec3 outwardNormal) {
    rec.frontFace = dot(r.direction, outwardNormal) < 0.0;
    rec.normal = rec.frontFace ? outwardNormal : -outwardNormal;
}

// Sphere intersection
// Requires: Sphere struct defined
bool hitSphere(Sphere sphere, Ray r, float tMin, float tMax, inout HitRecord rec) {
    vec3 oc = r.origin - sphere.center;
    float a = dot(r.direction, r.direction);
    float halfB = dot(oc, r.direction);
    float c = dot(oc, oc) - sphere.radius * sphere.radius;

    float discriminant = halfB * halfB - a * c;
    if (discriminant < 0.0) return false;

    float sqrtD = sqrt(discriminant);
    float root = (-halfB - sqrtD) / a;
    if (root < tMin || root > tMax) {
        root = (-halfB + sqrtD) / a;
        if (root < tMin || root > tMax)
            return false;
    }

    rec.t = root;
    rec.point = rayAt(r, root);
    vec3 outwardNormal = (rec.point - sphere.center) / sphere.radius;
    setFaceNormal(rec, r, outwardNormal);
    rec.materialId = sphere.materialId;

    return true;
}

// Axis-aligned box intersection (slab method)
// Requires: Box struct defined
bool hitBox(Box box, Ray r, float tMin, float tMax, inout HitRecord rec) {
    vec3 bmin = box.center - box.halfExtents;
    vec3 bmax = box.center + box.halfExtents;

    vec3 invDir = 1.0 / r.direction;

    vec3 t0 = (bmin - r.origin) * invDir;
    vec3 t1 = (bmax - r.origin) * invDir;

    vec3 tNear = min(t0, t1);
    vec3 tFar = max(t0, t1);

    float tEnter = max(max(tNear.x, tNear.y), tNear.z);
    float tExit = min(min(tFar.x, tFar.y), tFar.z);

    // No intersection if exit before enter, or outside range
    if (tExit < tEnter || tExit < tMin || tEnter > tMax) return false;

    // Determine which t to use (prefer entry point if valid)
    float t = tEnter;
    if (t < tMin) t = tExit;
    if (t < tMin || t > tMax) return false;

    rec.t = t;
    rec.point = rayAt(r, t);
    rec.materialId = box.materialId;

    // Compute normal from which face was hit
    vec3 localPoint = rec.point - box.center;
    vec3 absLocal = abs(localPoint);
    vec3 halfExt = box.halfExtents;

    // Find which face we hit (largest component relative to half-extent)
    vec3 d = absLocal / halfExt;
    vec3 normal;
    if (d.x > d.y && d.x > d.z) {
        normal = vec3(sign(localPoint.x), 0.0, 0.0);
    } else if (d.y > d.z) {
        normal = vec3(0.0, sign(localPoint.y), 0.0);
    } else {
        normal = vec3(0.0, 0.0, sign(localPoint.z));
    }

    setFaceNormal(rec, r, normal);
    return true;
}

// Cylinder intersection
// Cylinder defined by base center, axis direction, radius, height
bool hitCylinder(Cylinder cyl, Ray r, float tMin, float tMax, inout HitRecord rec) {
    // Vector from ray origin to cylinder base
    vec3 oc = r.origin - cyl.base;

    // Project ray direction and oc onto plane perpendicular to axis
    float dDotA = dot(r.direction, cyl.axis);
    float ocDotA = dot(oc, cyl.axis);

    vec3 dPerp = r.direction - dDotA * cyl.axis;
    vec3 ocPerp = oc - ocDotA * cyl.axis;

    // Quadratic coefficients for infinite cylinder
    float a = dot(dPerp, dPerp);
    float b = 2.0 * dot(dPerp, ocPerp);
    float c = dot(ocPerp, ocPerp) - cyl.radius * cyl.radius;

    float discriminant = b * b - 4.0 * a * c;

    float bestT = tMax + 1.0;
    vec3 bestNormal;
    bool hit = false;

    // Check cylinder body
    if (discriminant >= 0.0 && a > EPSILON) {
        float sqrtD = sqrt(discriminant);
        float t1 = (-b - sqrtD) / (2.0 * a);
        float t2 = (-b + sqrtD) / (2.0 * a);

        for (int i = 0; i < 2; i++) {
            float t = (i == 0) ? t1 : t2;
            if (t >= tMin && t < bestT) {
                vec3 p = rayAt(r, t);
                float h = dot(p - cyl.base, cyl.axis);
                if (h >= 0.0 && h <= cyl.height) {
                    bestT = t;
                    vec3 centerOnAxis = cyl.base + h * cyl.axis;
                    bestNormal = normalize(p - centerOnAxis);
                    hit = true;
                }
            }
        }
    }

    // Check caps if enabled
    if (cyl.caps != 0u) {
        // Bottom cap
        float denom = dot(r.direction, cyl.axis);
        if (abs(denom) > EPSILON) {
            float t = dot(cyl.base - r.origin, cyl.axis) / denom;
            if (t >= tMin && t < bestT) {
                vec3 p = rayAt(r, t);
                if (length(p - cyl.base) <= cyl.radius) {
                    bestT = t;
                    bestNormal = -cyl.axis;
                    hit = true;
                }
            }

            // Top cap
            vec3 top = cyl.base + cyl.height * cyl.axis;
            t = dot(top - r.origin, cyl.axis) / denom;
            if (t >= tMin && t < bestT) {
                vec3 p = rayAt(r, t);
                if (length(p - top) <= cyl.radius) {
                    bestT = t;
                    bestNormal = cyl.axis;
                    hit = true;
                }
            }
        }
    }

    if (hit) {
        rec.t = bestT;
        rec.point = rayAt(r, bestT);
        setFaceNormal(rec, r, bestNormal);
        rec.materialId = cyl.materialId;
    }

    return hit;
}

// Cone intersection
// Cone defined by base center, axis direction (pointing to tip), base radius, height
bool hitCone(Cone cone, Ray r, float tMin, float tMax, inout HitRecord rec) {
    vec3 tip = cone.base + cone.height * cone.axis;
    vec3 oc = r.origin - tip;

    // Cone half-angle
    float tanTheta = cone.radius / cone.height;
    float cosTheta2 = 1.0 / (1.0 + tanTheta * tanTheta);
    float sinTheta2 = 1.0 - cosTheta2;

    // Quadratic coefficients
    float dDotA = dot(r.direction, -cone.axis);
    float ocDotA = dot(oc, -cone.axis);

    float a = dDotA * dDotA - cosTheta2;
    float b = 2.0 * (dDotA * ocDotA - cosTheta2 * dot(r.direction, oc));
    float c = ocDotA * ocDotA - cosTheta2 * dot(oc, oc);

    float discriminant = b * b - 4.0 * a * c;

    float bestT = tMax + 1.0;
    vec3 bestNormal;
    bool hit = false;

    // Check cone surface
    if (discriminant >= 0.0) {
        float sqrtD = sqrt(discriminant);
        float t1 = (-b - sqrtD) / (2.0 * a);
        float t2 = (-b + sqrtD) / (2.0 * a);

        for (int i = 0; i < 2; i++) {
            float t = (i == 0) ? t1 : t2;
            if (t >= tMin && t < bestT) {
                vec3 p = rayAt(r, t);
                float h = dot(tip - p, cone.axis);
                if (h >= 0.0 && h <= cone.height) {
                    bestT = t;
                    // Normal: perpendicular to surface, pointing outward
                    vec3 toTip = tip - p;
                    vec3 axisComponent = dot(toTip, cone.axis) * cone.axis;
                    vec3 radial = normalize(toTip - axisComponent);
                    // Normal tilts outward from axis
                    bestNormal = normalize(radial * cone.height + cone.axis * cone.radius);
                    hit = true;
                }
            }
        }
    }

    // Check base cap if enabled
    if (cone.cap != 0u) {
        float denom = dot(r.direction, cone.axis);
        if (abs(denom) > EPSILON) {
            float t = dot(cone.base - r.origin, cone.axis) / denom;
            if (t >= tMin && t < bestT) {
                vec3 p = rayAt(r, t);
                if (length(p - cone.base) <= cone.radius) {
                    bestT = t;
                    bestNormal = -cone.axis;
                    hit = true;
                }
            }
        }
    }

    if (hit) {
        rec.t = bestT;
        rec.point = rayAt(r, bestT);
        setFaceNormal(rec, r, bestNormal);
        rec.materialId = cone.materialId;
    }

    return hit;
}

// Ground plane intersection (y = 0, bounded)
bool hitGroundPlane(Ray r, float tMin, float tMax, inout HitRecord rec, uint materialId) {
    // Plane at y = 0 with normal pointing up
    if (abs(r.direction.y) < 0.0001) return false;  // ray parallel to plane

    float t = -r.origin.y / r.direction.y;
    if (t < tMin || t > tMax) return false;

    // Compute hit point with better precision for y=0 plane
    // Instead of origin + t * direction (which loses precision for large t),
    // compute x,z directly using similar triangles
    float invDirY = 1.0 / r.direction.y;
    vec3 hitPoint;
    hitPoint.x = r.origin.x - r.origin.y * r.direction.x * invDirY;
    hitPoint.y = 0.0;  // Exactly on the plane
    hitPoint.z = r.origin.z - r.origin.y * r.direction.z * invDirY;

    // Bound the plane to a reasonable size (50 unit radius)
    float distFromOrigin = length(hitPoint.xz);
    if (distFromOrigin > 50.0) return false;

    rec.t = t;
    rec.point = hitPoint;
    rec.normal = vec3(0.0, 1.0, 0.0);  // always up
    rec.frontFace = r.direction.y < 0.0;
    if (!rec.frontFace) rec.normal = -rec.normal;
    rec.materialId = materialId;
    return true;
}

// Scene intersection
// Requires: spheres[], boxes[], cylinders[], cones[] buffers and counts
bool hitScene(Ray r, float tMin, float tMax, inout HitRecord rec) {
    HitRecord tempRec;
    bool hitAnything = false;
    float closestSoFar = tMax;

    // Check ground plane first (material 13 = checker)
    if (hitGroundPlane(r, tMin, closestSoFar, tempRec, 13)) {
        hitAnything = true;
        closestSoFar = tempRec.t;
        rec = tempRec;
    }

    // Check spheres (skip index 0 which was the floor sphere)
    for (uint i = 1; i < pc.sphereCount; i++) {
        if (hitSphere(spheres[i], r, tMin, closestSoFar, tempRec)) {
            hitAnything = true;
            closestSoFar = tempRec.t;
            rec = tempRec;
        }
    }

    // Check boxes
    for (uint i = 0; i < pc.boxCount; i++) {
        if (hitBox(boxes[i], r, tMin, closestSoFar, tempRec)) {
            hitAnything = true;
            closestSoFar = tempRec.t;
            rec = tempRec;
        }
    }

    // Check cylinders
    for (uint i = 0; i < pc.cylinderCount; i++) {
        if (hitCylinder(cylinders[i], r, tMin, closestSoFar, tempRec)) {
            hitAnything = true;
            closestSoFar = tempRec.t;
            rec = tempRec;
        }
    }

    // Check cones
    for (uint i = 0; i < pc.coneCount; i++) {
        if (hitCone(cones[i], r, tMin, closestSoFar, tempRec)) {
            hitAnything = true;
            closestSoFar = tempRec.t;
            rec = tempRec;
        }
    }

    return hitAnything;
}
