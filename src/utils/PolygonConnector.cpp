//Copyright (c) 2022 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "PolygonConnector.h"

#include "linearAlg2D.h"
#include "AABB.h"

namespace cura 
{

PolygonConnector::PolygonConnector(const coord_t line_width, const coord_t max_dist)
: line_width(line_width - 5) // a bit less so that consecutive lines which have become connected can still connect to other lines
//                |                     |                      |
// ----------o    |      ----------o    |       ----------o,,,,o
//           |    |  ==>           |    |  ==>
// -----o    |    |      -----o----o    |       -----o----o----o
//      |    |    |                     |                      |
//      |    |    |           o''''o    |            o''''o    |
//      |    |    |           |    |    |            |    |    |
, max_dist(max_dist)
{}

void PolygonConnector::add(const Polygons& input)
{
    for (ConstPolygonRef poly : input)
    {
        input_polygons.push_back(poly);
    }
}

void PolygonConnector::add(const VariableWidthPaths& input)
{
    for(const VariableWidthLines& lines : input)
    {
        for(const ExtrusionLine& line : lines)
        {
            input_paths.push_back(line);
        }
    }
}

void PolygonConnector::connect(Polygons& output_polygons, VariableWidthPaths& output_paths)
{
    std::vector<Polygon> result_polygons = connectGroup(input_polygons);
    for(Polygon& polygon : result_polygons)
    {
        output_polygons.add(polygon);
    }

    std::vector<ExtrusionLine> result_paths = connectGroup(input_paths);
    output_paths.push_back(result_paths);
}

Point PolygonConnector::getPosition(const Point& vertex) const
{
    return vertex;
}

Point PolygonConnector::getPosition(const ExtrusionJunction& junction) const
{
    return junction.p;
}

coord_t PolygonConnector::getWidth(const Point&) const
{
    return line_width;
}

coord_t PolygonConnector::getWidth(const ExtrusionJunction& junction) const
{
    return junction.w;
}

void PolygonConnector::addVertex(Polygon& polygonal, const Point& position, const coord_t) const
{
    polygonal.add(position);
}

void PolygonConnector::addVertex(Polygon& polygonal, const Point& vertex) const
{
    polygonal.add(vertex);
}

void PolygonConnector::addVertex(ExtrusionLine& polygonal, const Point& position, const coord_t width) const
{
    polygonal.emplace_back(position, width, 0); //Perimeter indices don't make sense any more once perimeters are merged. Use 0 as placeholder.
}

void PolygonConnector::addVertex(ExtrusionLine& polygonal, const ExtrusionJunction& vertex) const
{
    polygonal.emplace_back(vertex);
}

bool PolygonConnector::isClosed(Polygon&) const
{
    return true;
}

bool PolygonConnector::isClosed(ExtrusionLine& polygonal) const
{
    return vSize2(polygonal.front() - polygonal.back()) < 10;
}

}//namespace cura

