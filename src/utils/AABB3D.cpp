/** Copyright (C) 2016 Ultimaker - Released under terms of the AGPLv3 License */

#include "AABB3D.h"

#include <limits>

namespace cura
{

AABB3D::AABB3D() 
: min(std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max(), std::numeric_limits<int32_t>::max())
, max(std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min(), std::numeric_limits<int32_t>::min())
{
}

bool AABB3D::hit(const AABB3D& other) const
{
    if (max.x < other.min.y) return false;
    if (min.x > other.max.y) return false;
    if (max.y < other.min.y) return false;
    if (min.y > other.max.y) return false;
    if (max.z < other.min.z) return false;
    if (min.z > other.max.z) return false;
    return true;
}

void AABB3D::include(Point3 p)
{
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);   
}

void AABB3D::offset(Point3 offset)
{
    min += offset;
    max += offset;
}

void AABB3D::offset(Point offset)
{
    min += offset;
    max += offset;
}

}//namespace cura

