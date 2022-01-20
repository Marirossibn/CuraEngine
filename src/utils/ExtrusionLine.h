//Copyright (c) 2020 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.


#ifndef UTILS_EXTRUSION_LINE_H
#define UTILS_EXTRUSION_LINE_H

#include "ExtrusionJunction.h"
#include "polygon.h"

namespace cura
{

/*!
 * Represents a polyline (not just a line) that is to be extruded with variable
 * line width.
 *
 * This polyline is a sequence of \ref ExtrusionJunction, with a bit of metadata
 * about which inset it represents.
 */
struct ExtrusionLine
{
    /*!
     * Which inset this path represents, counted from the outside inwards.
     *
     * The outer wall has index 0.
     */
    size_t inset_idx;

    /*!
     * If a thin piece needs to be printed with an odd number of walls (e.g. 5
     * walls) then there will be one wall in the middle that is not a loop. This
     * field indicates whether this path is such a line through the middle, that
     * has no companion line going back on the other side and is not a closed
     * loop.
     */
    bool is_odd;

    /*!
     * Which region this line is part of. A solid polygon without holes has only one region.
     * A polygon with holes has 2. Disconnected parts of the polygon are also separate regions.
     * Will be 0 if no region was given.
     */
    size_t region_id;

    /*!
     * Gets the number of vertices in this polygon.
     * \return The number of vertices in this polygon.
     */
    size_t size() const
    {
        return junctions.size();
    }

    /*!
     * Whether there are no junctions.
     */
    bool empty() const
    {
        return junctions.empty();
    }

    /*!
     * The list of vertices along which this path runs.
     *
     * Each junction has a width, making this path a variable-width path.
     */
    std::vector<ExtrusionJunction> junctions;

    ExtrusionLine(const size_t inset_idx, const bool is_odd, const size_t region_id = 0);
    ExtrusionLine()
    : inset_idx(-1)
    , is_odd(true)
    , region_id(-1)
    {}

    ExtrusionLine(const ExtrusionLine& other)
    : inset_idx(other.inset_idx)
    , is_odd(other.is_odd)
    , region_id(other.region_id)
    , junctions(other.junctions)
    {}
    
    ExtrusionLine& operator=(ExtrusionLine&& other)
    {
        junctions = std::move(other.junctions);
        inset_idx = other.inset_idx;
        is_odd = other.is_odd;
        region_id = other.region_id;
        return *this;
    }

    ExtrusionLine& operator=(const ExtrusionLine& other)
    {
        junctions = other.junctions;
        inset_idx = other.inset_idx;
        is_odd = other.is_odd;
        region_id = other.region_id;
        return *this;
    }

    
    std::vector<ExtrusionJunction>::const_iterator begin() const
    {
        return junctions.begin();
    }

    std::vector<ExtrusionJunction>::const_iterator end() const
    {
        return junctions.end();
    }

    std::vector<ExtrusionJunction>::const_reverse_iterator rbegin() const
    {
        return junctions.rbegin();
    }

    std::vector<ExtrusionJunction>::const_reverse_iterator rend() const
    {
        return junctions.rend();
    }

    std::vector<ExtrusionJunction>::const_reference front() const
    {
        return junctions.front();
    }

    std::vector<ExtrusionJunction>::const_reference back() const
    {
        return junctions.back();
    }

    const ExtrusionJunction& operator[] (unsigned int index) const
    {
        return junctions[index];
    }

    ExtrusionJunction& operator[] (unsigned int index)
    {
        return junctions[index];
    }

    std::vector<ExtrusionJunction>::iterator begin()
    {
        return junctions.begin();
    }

    std::vector<ExtrusionJunction>::iterator end()
    {
        return junctions.end();
    }

    std::vector<ExtrusionJunction>::reference front()
    {
        return junctions.front();
    }

    std::vector<ExtrusionJunction>::reference back()
    {
        return junctions.back();
    }

    template <typename... Args>
    void emplace_back(Args&&... args)
    {
        junctions.emplace_back(args...);
    }

    void remove(unsigned int index)
    {
        junctions.erase(junctions.begin() + index);
    }

    void insert(size_t index, const ExtrusionJunction& p)
    {
        junctions.insert(junctions.begin() + index, p);
    }

    template <class iterator>
    std::vector<ExtrusionJunction>::iterator insert(std::vector<ExtrusionJunction>::const_iterator pos, iterator first, iterator last)
    {
        return junctions.insert(pos, first, last);
    }

    void clear()
    {
        junctions.clear();
    }

    void reverse()
    {
        std::reverse(junctions.begin(), junctions.end());
    }
    
    /*!
     * Sum the total length of this path.
     */
    coord_t getLength() const;
    coord_t polylineLength() const { return getLength(); }

    Polygon toPolygon() const
    {
        Polygon ret;
        
        for (const ExtrusionJunction& j : junctions)
            ret.add(j.p);
        
        return ret;
    }

    /*!
     * Get the minimal width of this path
     */
    coord_t getMinimalWidth() const;

    /*!
     * Export the included junctions as vector.
     */
    void appendJunctionsTo(LineJunctions& result) const;

    /*!
     * Chop off a segment of \p length of either end of this extrusionline
     * 
     * \warning Should only be called on non closed extrusionlines.
     * 
     * \param start_at_front Whether we chop from the beginning or from th eend of this line.
     * \return whether the line has collapsed to a single point
     */
    bool chopEnd(bool start_at_front, coord_t length)
    {
        assert(length > 10 && "Too small lengths will never be chopped due to rounding.");
        if (start_at_front)
            return chopEnd(junctions.begin(), junctions.end(), length);
        else
            return chopEnd(junctions.rbegin(), junctions.rend(), length);
    }
protected:
    /*!
     * Chop off a segment of \p length of either end of this extrusionline
     * 
     * \warning Should only be called on non closed extrusionlines.
     * 
     * \warning the \p start_pos and \p other_end should refer to iterators in this ExtrusionLine
     * 
     * \param start_pos Iterator to either begin() or rbegin()
     * \param other_end Iterator to either end() or rend()
     * \return whether the line has collapsed to a single point
     */
    template <class iterator>
    bool chopEnd(iterator start_pos, iterator other_end, coord_t length)
    {
        iterator current_it = start_pos;
        
        coord_t length_removed = 0;
        ExtrusionJunction last = *current_it;
        for (++current_it; current_it != other_end; ++current_it)
        {
            ExtrusionJunction here = *current_it;
            Point p1 = last.p;
            Point p2 = here.p;
            Point v12 = p2 - p1;
            coord_t dist = vSize(v12);
            if (length_removed + dist >= length - 10)
            {
                if (length_removed + dist <= length)
                {
                    erase(start_pos, current_it);
                    return junctions.size() <= 1;
                }
                else
                { // Cut My Line Into Pieces
                    --current_it;
                    current_it->p = p1 + (length - length_removed) * v12 / dist;
                    current_it->w = last.w + (length - length_removed) * (here.w - last.w) / dist;
                    erase(start_pos, current_it);
                    return false;
                }
            }
            length_removed += dist;
            last = here;
        }
        erase(start_pos, --other_end);
        junctions.emplace_back(junctions.front());
        junctions.back().p.X += 10;
        return true;
    }


    template <class iterator>
    void erase(iterator begin, iterator end);

public:
    /*!
     * Removes vertices of the ExtrusionLines to make sure that they are not too high
     * resolution.
     *
     * This removes junctions which are connected to line segments that are shorter
     * than the `smallest_line_segment`, unless that would introduce a deviation
     * in the contour of more than `allowed_error_distance`.
     *
     * Criteria:
     * 1. Never remove a junction if either of the connected segments is larger than \p smallest_line_segment
     * 2. Never remove a junction if the distance between that junction and the final resulting polygon would be higher
     *    than \p allowed_error_distance
     * 3. The direction of segments longer than \p smallest_line_segment always
     *    remains unaltered (but their end points may change if it is connected to
     *    a small segment)
     * 4. Never remove a junction if it has a distinctively different width than the next junction, as this can
     *    introduce unwanted irregularities on the wall widths.
     *
     * Simplify uses a heuristic and doesn't necessarily remove all removable
     * vertices under the above criteria, but simplify may never violate these
     * criteria. Unless the segments or the distance is smaller than the
     * rounding error of 5 micron.
     *
     * Vertices which introduce an error of less than 5 microns are removed
     * anyway, even if the segments are longer than the smallest line segment.
     * This makes sure that (practically) co-linear line segments are joined into
     * a single line segment.
     * \param smallest_line_segment Maximal length of removed line segments.
     * \param allowed_error_distance If removing a vertex introduces a deviation
     *         from the original path that is more than this distance, the vertex may
     *         not be removed.
     * \param maximum_extrusion_area_deviation The maximum extrusion area deviation allowed when removing intermediate
     *        junctions from a straight ExtrusionLine
     */
    void simplify(coord_t smallest_line_segment_squared, coord_t allowed_error_distance_squared, coord_t maximum_extrusion_area_deviation);

    /*!
     * Computes and returns the total area error (in μm²) of the AB and BC segments of an ABC straight ExtrusionLine
     * when the junction B with a width B.w is removed from the ExtrusionLine. The area changes due to the fact that the
     * new simplified line AC has a uniform width which equals to the weighted average of the width of the subsegments
     * (based on their length).
     *
     * \param A Start point of the 3-point-straight line
     * \param B Intermediate point of the 3-point-straight line
     * \param C End point of the 3-point-straight line
     * \param weighted_average_width The weighted average of the widths of the two colinear extrusion segments
     * */
    static coord_t calculateExtrusionAreaDeviationError(ExtrusionJunction A, ExtrusionJunction B, ExtrusionJunction C, coord_t& weighted_average_width);
};

using VariableWidthLines = std::vector<ExtrusionLine>; //<! The ExtrusionLines generated by libArachne for each Path
using VariableWidthPaths = std::vector<VariableWidthLines>; //<! The toolpaths generated by libArachne
} // namespace cura
#endif // UTILS_EXTRUSION_LINE_H
