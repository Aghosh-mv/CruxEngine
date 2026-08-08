#include "Scene/Scene.h"

namespace Crux {

Mat4 quatToMat4(const Quat& q) {
    Mat4 r = Mat4::identity();
    Mat3 m = Mat3::fromQuat(q);
    r.m[0] = m.m[0]; r.m[4] = m.m[1]; r.m[8] = m.m[2];
    r.m[1] = m.m[3]; r.m[5] = m.m[4]; r.m[9] = m.m[5];
    r.m[2] = m.m[6]; r.m[6] = m.m[7]; r.m[10] = m.m[8];
    return r;
}

}
