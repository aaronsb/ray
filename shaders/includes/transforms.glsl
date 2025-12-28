// Rotation and transform utilities
// Used for instance transforms and CSG primitive rotations

#ifndef TRANSFORMS_GLSL
#define TRANSFORMS_GLSL

vec3 rotateX(vec3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
}

vec3 rotateY(vec3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
}

vec3 rotateZ(vec3 v, float angle) {
    float c = cos(angle);
    float s = sin(angle);
    return vec3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}

// Rotate by Euler angles (XYZ order)
vec3 rotateXYZ(vec3 v, vec3 angles) {
    return rotateZ(rotateY(rotateX(v, angles.x), angles.y), angles.z);
}

// Inverse rotation (ZYX order with negated angles)
vec3 rotateXYZInverse(vec3 v, vec3 angles) {
    return rotateX(rotateY(rotateZ(v, -angles.z), -angles.y), -angles.x);
}

// Build 3x3 rotation matrix from Euler angles (XYZ order)
// Rotation order: Z * Y * X (applied to column vector)
mat3 buildRotationMatrix(float rx, float ry, float rz) {
    float cx = cos(rx), sx = sin(rx);
    float cy = cos(ry), sy = sin(ry);
    float cz = cos(rz), sz = sin(rz);
    return mat3(
        cy*cz, sx*sy*cz + cx*sz, -cx*sy*cz + sx*sz,
        -cy*sz, -sx*sy*sz + cx*cz, cx*sy*sz + sx*cz,
        sy, -sx*cy, cx*cy
    );
}

#endif // TRANSFORMS_GLSL
