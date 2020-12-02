//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "OuterWallInsetBeadingStrategy.h"

#include <algorithm>

namespace cura
{
    BeadingStrategy::Beading OuterWallInsetBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
    {
        Beading ret = parent->compute(thickness, bead_count);

        // Actual count and thickness as represented by extant walls. Don't count any potential zero-width 'signalling' walls.
        bead_count = std::count_if(ret.bead_widths.begin(), ret.bead_widths.end(), [](const coord_t width) { return width > 0; });

        // Early out when the only walls are outer. 
        if (bead_count < 3)
        {
            return ret;
        }
        
        // Actually move the outer wall inside. 
        ret.toolpath_locations[0] = ret.toolpath_locations[0] + outer_wall_offset;
        
        return ret;
    }

} // namespace cura
