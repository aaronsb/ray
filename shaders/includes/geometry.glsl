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
// Requires: spheres[], boxes[] buffers and pc.sphereCount, pc.boxCount
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

    return hitAnything;
}
