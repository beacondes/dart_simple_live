#pragma once
// Minimal column-major mat4 math for OpenXR + OpenGL ES.
#include <openxr/openxr.h>
#include <cmath>
#include <cstring>

struct Mat4 {
    float m[16];
    Mat4() { SetIdentity(); }
    void SetIdentity() {
        std::memset(m, 0, sizeof(m));
        m[0] = m[5] = m[10] = m[15] = 1.0f;
    }
    static Mat4 Mul(const Mat4& a, const Mat4& b) {
        Mat4 r;
        for (int c = 0; c < 4; c++) {
            for (int row = 0; row < 4; row++) {
                r.m[c*4+row] = a.m[0*4+row]*b.m[c*4+0]
                             + a.m[1*4+row]*b.m[c*4+1]
                             + a.m[2*4+row]*b.m[c*4+2]
                             + a.m[3*4+row]*b.m[c*4+3];
            }
        }
        return r;
    }
};

inline Mat4 QuatToMat4(const XrQuaternionf& q) {
    float x = q.x, y = q.y, z = q.z, w = q.w;
    float xx = x*x, yy = y*y, zz = z*z;
    float xy = x*y, xz = x*z, yz = y*z;
    float wx = w*x, wy = w*y, wz = w*z;
    Mat4 r;
    r.m[0] = 1-2*(yy+zz); r.m[4] = 2*(xy-wz);   r.m[8]  = 2*(xz+wy); r.m[12] = 0;
    r.m[1] = 2*(xy+wz);   r.m[5] = 1-2*(xx+zz); r.m[9]  = 2*(yz-wx); r.m[13] = 0;
    r.m[2] = 2*(xz-wy);   r.m[6] = 2*(yz+wx);   r.m[10] = 1-2*(xx+yy); r.m[14] = 0;
    r.m[3] = 0;           r.m[7] = 0;           r.m[11] = 0;           r.m[15] = 1;
    return r;
}

inline Mat4 Translation(const XrVector3f& t) {
    Mat4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

inline Mat4 XrPoseToViewMatrix(const XrPosef& pose) {
    Mat4 rot = QuatToMat4(pose.orientation);
    Mat4 rotT;
    rotT.SetIdentity();
    for (int c = 0; c < 3; c++)
        for (int row = 0; row < 3; row++)
            rotT.m[c*4+row] = rot.m[row*4+c];
    XrVector3f neg = { -pose.position.x, -pose.position.y, -pose.position.z };
    return Mat4::Mul(rotT, Translation(neg));
}

inline Mat4 XrFovToProjectionMatrix(const XrFovf& fov, float nearZ, float farZ) {
    float tl = tanf(fov.angleLeft);
    float tr = tanf(fov.angleRight);
    float tu = tanf(fov.angleUp);
    float td = tanf(fov.angleDown);
    float w = tr - tl;
    float h = tu - td;
    Mat4 r;
    std::memset(r.m, 0, sizeof(r.m));
    r.m[0] = 2.0f / w;
    r.m[2] = (tr + tl) / w;
    r.m[5] = 2.0f / h;
    r.m[6] = (tu + td) / h;
    r.m[10] = -(farZ + nearZ) / (farZ - nearZ);
    r.m[11] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    r.m[14] = -1.0f;
    return r;
}
