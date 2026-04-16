#include "CollisionUtil.h"

AABB ConvertOBBToAABB(const OBB &obb) {
    AABB box;

    box.min = {obb.center.x - obb.size.x, obb.center.y - obb.size.y,
               obb.center.z - obb.size.z};

    box.max = {obb.center.x + obb.size.x, obb.center.y + obb.size.y,
               obb.center.z + obb.size.z};

    return box;
}

bool CollisionUtil::CheckOBB(const OBB &a, const OBB &b) {
    AABB aa = ConvertOBBToAABB(a);
    AABB bb = ConvertOBBToAABB(b);

    return CheckAABB(aa, bb);
}

bool CollisionUtil::CheckAABB(const AABB &a, const AABB &b) {
    if (a.max.x < b.min.x || a.min.x > b.max.x)
        return false;
    if (a.max.y < b.min.y || a.min.y > b.max.y)
        return false;
    if (a.max.z < b.min.z || a.min.z > b.max.z)
        return false;

    return true;
}
