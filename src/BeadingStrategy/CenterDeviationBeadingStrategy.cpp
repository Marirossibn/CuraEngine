//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "CenterDeviationBeadingStrategy.h"

namespace cura
{

    CenterDeviationBeadingStrategy::Beading CenterDeviationBeadingStrategy::compute(coord_t thickness, coord_t bead_count) const
    {
        Beading ret;

        ret.total_thickness = thickness;
        if (bead_count > 0)
        {
            for (coord_t bead_idx = 0; bead_idx < bead_count / 2; bead_idx++)
            {
                ret.bead_widths.emplace_back(optimal_width);
                ret.toolpath_locations.emplace_back(optimal_width * (bead_idx * 2 + 1) / 2);
            }
            if (bead_count % 2 == 1)
            {
                ret.bead_widths.emplace_back(thickness - (bead_count - 1) * optimal_width);
                ret.toolpath_locations.emplace_back(thickness / 2);
                ret.left_over = 0;
            }
            else
            {
                ret.left_over = thickness - bead_count * optimal_width;
            }
            for (coord_t bead_idx = (bead_count + 1) / 2; bead_idx < bead_count; bead_idx++)
            {
                ret.bead_widths.emplace_back(optimal_width);
                ret.toolpath_locations.emplace_back(thickness - (bead_count - bead_idx) * optimal_width + optimal_width / 2);
            }
        }
        else
        {
            ret.left_over = thickness;
        }
        return ret;
    }

    coord_t CenterDeviationBeadingStrategy::getOptimalThickness(coord_t bead_count) const
    {
        return bead_count * optimal_width;
    }

    coord_t CenterDeviationBeadingStrategy::getTransitionThickness(coord_t lower_bead_count) const
    {
        return lower_bead_count * optimal_width + minimum_line_width;
    }

    coord_t CenterDeviationBeadingStrategy::getOptimalBeadCount(coord_t thickness) const
    {
        const coord_t naive_count = thickness / optimal_width; //How many lines we can fit in for sure.
        const coord_t remainder = thickness - naive_count * optimal_width; //Space left after fitting that many lines.
        return naive_count + (remainder > minimum_line_width); //If there's enough space, fit an extra one.
    }

} // namespace cura
