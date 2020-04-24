//Copyright (c) 2018 Ultimaker B.V.

#include <optional>
#include <stack>


#include "VoronoiUtils.h"

#include "linearAlg2D.h"
#include "SVG.h"

using boost::polygon::low;
using boost::polygon::high;

namespace arachne 
{

Point VoronoiUtils::p(const vd_t::vertex_type* node)
{
    return Point(node->x() + 0, node->y() + 0); // gets rid of negative zero
}

bool VoronoiUtils::isSourcePoint(Point p, const vd_t::cell_type& cell, const std::vector<Point>& points, const std::vector<Segment>& segments, coord_t snap_dist)
{
    if (cell.contains_point())
    {
        return shorterThen(p - getSourcePoint(cell, points, segments), snap_dist);
    }
    else
    {
        const Segment& segment = getSourceSegment(cell, points, segments);
        return shorterThen(p - segment.from(), snap_dist) || shorterThen(p - segment.to(), snap_dist);
    }
}

coord_t VoronoiUtils::getDistance(Point p, const vd_t::cell_type& cell, const std::vector<Point>& points, const std::vector<Segment>& segments)
{
    if (cell.contains_point())
    {
        return vSize(p - getSourcePoint(cell, points, segments));
    }
    else
    {
        const Segment& segment = getSourceSegment(cell, points, segments);
        return sqrt(LinearAlg2D::getDist2FromLineSegment(segment.from(), p, segment.to()));
    }
}

Point VoronoiUtils::getSourcePoint(const vd_t::cell_type& cell, const std::vector<Point>& points, const std::vector<Segment>& segments)
{
    assert(cell.contains_point());
    switch (cell.source_category())
    {
    case boost::polygon::SOURCE_CATEGORY_SINGLE_POINT:
        return points[cell.source_index()];
        break;
    case boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT:
        assert(cell.source_index() - points.size() < segments.size());
        return segments[cell.source_index() - points.size()].to();
        break;
    case boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT:
        assert(cell.source_index() - points.size() < segments.size());
        return segments[cell.source_index() - points.size()].from();
        break;
    default:
        assert(false && "getSourcePoint should only be called on point cells!\n");
        break;
    }
}

PolygonsPointIndex VoronoiUtils::getSourcePointIndex(const vd_t::cell_type& cell, const std::vector<Point>& points, const std::vector<Segment>& segments)
{
    assert(cell.contains_point());
    assert(cell.source_category() != boost::polygon::SOURCE_CATEGORY_SINGLE_POINT);
    switch (cell.source_category())
    {
    case boost::polygon::SOURCE_CATEGORY_SEGMENT_START_POINT:
    {
        assert(cell.source_index() - points.size() < segments.size());
        PolygonsPointIndex ret = segments[cell.source_index() - points.size()];
        ++ret;
        return ret;
        break;
    }
    case boost::polygon::SOURCE_CATEGORY_SEGMENT_END_POINT:
    {
        assert(cell.source_index() - points.size() < segments.size());
        return segments[cell.source_index() - points.size()];
        break;
    }
    default:
        assert(false && "getSourcePoint should only be called on point cells!\n");
        break;
    }
}

const VoronoiUtils::Segment& VoronoiUtils::getSourceSegment(const vd_t::cell_type& cell, const std::vector<Point>& points, const std::vector<Segment>& segments)
{
    assert(cell.contains_segment());
    return segments[cell.source_index() - points.size()];
}


std::vector<Point> VoronoiUtils::discretizeParabola(const Point& p, const Segment& segment, Point s, Point e, coord_t approximate_step_size, float transitioning_angle)
{
    std::vector<Point> discretized;
    // x is distance of point projected on the segment ab
    // xx is point projected on the segment ab
    Point a = segment.from();
    Point b = segment.to();
    Point ab = b - a;
    Point as = s - a;
    Point ae = e - a;
    coord_t ab_size = vSize(ab);
    coord_t sx = dot(as, ab) / ab_size;
    coord_t ex = dot(ae, ab) / ab_size;
    coord_t sxex = ex - sx;
    
    Point ap = p - a;
    coord_t px = dot(ap, ab) / ab_size;
    
    Point pxx = LinearAlg2D::getClosestOnLine(p, a, b);
    Point ppxx = pxx - p;
    coord_t d = vSize(ppxx);
    PointMatrix rot = PointMatrix(turn90CCW(ppxx));
    
    if (d == 0)
    {
        discretized.emplace_back(s);
        discretized.emplace_back(e);
        return discretized;
    }
    
    float marking_bound = atan(transitioning_angle * 0.5);
    coord_t msx = - marking_bound * d; // projected marking_start
    coord_t mex = marking_bound * d; // projected marking_end
    coord_t marking_start_end_h = msx * msx / (2 * d) + d / 2;
    Point marking_start = rot.unapply(Point(msx, marking_start_end_h)) + pxx;
    Point marking_end = rot.unapply(Point(mex, marking_start_end_h)) + pxx;
    coord_t dir = 1;
    if (sx > ex)
    {
        dir = -1;
        std::swap(marking_start, marking_end);
        std::swap(msx, mex);
    }
    
    bool add_marking_start = msx * dir > (sx - px) * dir && msx * dir < (ex - px) * dir;
    bool add_marking_end = mex * dir > (sx - px) * dir && mex * dir < (ex - px) * dir;

    Point apex = rot.unapply(Point(0, d / 2)) + pxx;
    bool add_apex = (sx - px) * dir < 0 && (ex - px) * dir > 0;

    assert(!(add_marking_start && add_marking_end) || add_apex);
    
    coord_t step_count = static_cast<coord_t>(static_cast<float>(std::abs(ex - sx)) / approximate_step_size + 0.5);
    
    discretized.emplace_back(s);
    for (coord_t step = 1; step < step_count; step++)
    {
        
        coord_t x = sx + sxex * step / step_count - px;
        coord_t y = x * x / (2 * d) + d / 2;
        
        if (add_marking_start && msx * dir < x * dir)
        {
            discretized.emplace_back(marking_start);
            add_marking_start = false;
        }
        if (add_apex && x * dir > 0)
        {
            discretized.emplace_back(apex);
            add_apex = false; // only add the apex just before the 
        }
        if (add_marking_end && mex * dir < x * dir)
        {
            discretized.emplace_back(marking_end);
            add_marking_end = false;
        }
        Point result = rot.unapply(Point(x, y)) + pxx;
        discretized.emplace_back(result);
    }
    if (add_apex)
    {
        discretized.emplace_back(apex);
    }
    if (add_marking_end)
    {
        discretized.emplace_back(marking_end);
    }
    discretized.emplace_back(e);
    return discretized;
}

// adapted from boost::polygon::voronoi_visual_utils.cpp
void VoronoiUtils::discretize(
        const Point& point,
        const Segment& segment,
        const coord_t max_dist,
        std::vector<Point>* discretization) {
    // Apply the linear transformation to move start point of the segment to
    // the point with coordinates (0, 0) and the direction of the segment to
    // coincide the positive direction of the x-axis.
    Point segm_vec = segment.to() - segment.from();
    coord_t sqr_segment_length = vSize2(segm_vec);

    // Compute x-coordinates of the endpoints of the edge
    // in the transformed space.
    coord_t projection_start = sqr_segment_length *
            get_point_projection((*discretization)[0], segment);
    coord_t projection_end = sqr_segment_length *
            get_point_projection((*discretization)[1], segment);

    // Compute parabola parameters in the transformed space.
    // Parabola has next representation:
    // f(x) = ((x-rot_x)^2 + rot_y^2) / (2.0*rot_y).
    Point point_vec = point - segment.from();
    coord_t rot_x = dot(segm_vec, point_vec);
    coord_t rot_y = cross(segm_vec, point_vec);

    // Save the last point.
    Point last_point = (*discretization)[1];
    discretization->pop_back();

    // Use stack to avoid recursion.
    std::stack<coord_t> point_stack;
    point_stack.push(projection_end);
    Point cur(projection_start, parabola_y(projection_start, rot_x, rot_y));

    // Adjust max_dist parameter in the transformed space.
    const coord_t max_dist_transformed = max_dist * max_dist * sqr_segment_length;
    while (!point_stack.empty()) {
        Point new_(point_stack.top(), parabola_y(point_stack.top(), rot_x, rot_y));
        Point new_vec = new_ - cur;

        // Compute coordinates of the point of the parabola that is
        // furthest from the current line segment.
        coord_t mid_x = new_vec.Y * rot_y / new_vec.X + rot_x;
        coord_t mid_y = parabola_y(mid_x, rot_x, rot_y);
        Point mid_vec = Point(mid_x, mid_y) - cur;

        // Compute maximum distance between the given parabolic arc
        // and line segment that discretize it.
        __int128 dist = cross(mid_vec, new_vec);
        dist = dist * dist / vSize2(new_vec); // TODO overflows!!!
        if (dist <= max_dist_transformed) {
            // Distance between parabola and line segment is less than max_dist.
            point_stack.pop();
            coord_t inter_x = (segm_vec.X * new_.X - segm_vec.Y * new_.Y) /
                    sqr_segment_length + segment.from().X;
            coord_t inter_y = (segm_vec.X * new_.Y + segm_vec.Y * new_.X) /
                    sqr_segment_length + segment.from().Y;
            discretization->push_back(Point(inter_x, inter_y));
            cur = new_;
        } else {
            point_stack.push(mid_x);
        }
    }

    // Update last point.
    discretization->back() = last_point;
}

// adapted from boost::polygon::voronoi_visual_utils.cpp
coord_t VoronoiUtils::parabola_y(coord_t x, coord_t a, coord_t b) {
    return ((x - a) * (x - a) + b * b) / (b + b);
}

// adapted from boost::polygon::voronoi_visual_utils.cpp
double VoronoiUtils::get_point_projection(
        const Point& point, const Segment& segment) {
    Point segment_vec = segment.to() - segment.from();
    Point point_vec = point - segment.from();
    coord_t sqr_segment_length = vSize2(segment_vec);
    coord_t vec_dot = dot(segment_vec, point_vec);
    return static_cast<double>(vec_dot) / sqr_segment_length;
}

}//namespace arachne
