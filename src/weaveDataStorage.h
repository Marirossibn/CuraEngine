/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#ifndef WEAVE_DATA_STORAGE_H
#define WEAVE_DATA_STORAGE_H

#include "utils/intpoint.h"
#include "utils/polygon.h"
#include "mesh.h"
#include "gcodePlanner.h"

#include "MACROS.h"

namespace cura {

    
ENUM( WeaveSegmentType, UP, DOWN, FLAT, MOVE);


struct WeaveConnectionSegment
{
    Point3 to;
    WeaveSegmentType segmentType;
    WeaveConnectionSegment(Point3 to, WeaveSegmentType dir) : to(to), segmentType(dir) {};
};

struct PolyLine3
{
    Point3 from;
    std::vector<WeaveConnectionSegment> segments;
};

struct WeaveConnectionPart
{
    PolyLine3 connection;
    int supported_index;//!< index of corresponding supported polygon in WeaveConnection.supported (! last point in polygon is first point to start printing it!)
    WeaveConnectionPart(int top_idx) : supported_index(top_idx) {};
};

struct WeaveConnection
{
    int z0;//!< height of the supporting polygons (of the prev layer, roof inset, etc.)
    int z1;//!< height of the [supported] polygons
    std::vector<WeaveConnectionPart> connections; //!< for each polygon in [supported] the connection // \\ // \\ // \\ // \\.
    Polygons supported; //!< polygons to be supported by connections (from other polygons)
};
typedef std::vector<WeaveConnectionSegment> WeaveInsetPart; //!< Polygon with extra information on each point
struct WeaveRoofPart : WeaveConnection
{
    // [supported] is an insets of the roof polygons (or of previous insets of it)
    // [connections] are the connections between two consecutive roof polygon insets
    std::vector<WeaveInsetPart> supported_withMoves; //!< optimized inset polygons, with some parts of the polygons replaced by moves
};

struct WeaveLayer : WeaveConnection
{
    // [supported] are the outline polygons on the next layer which are (to be) connected,
    //             as well as the polygons supported by roofs (holes and boundaries of roofs)
    // [connections] are the vertical connections
    std::vector<WeaveRoofPart> roof_insets; //!< connections between consecutive insets of the roof polygons
};
struct WireFrame
{
    Polygons bottom;
    int z_bottom;
    std::vector<WeaveRoofPart> bottom_insets; //!< connections between consecutive insets of the bottom polygons
    std::vector<WeaveLayer> layers;
};
    
}//namespace cura

#endif//WEAVE_DATA_STORAGE_H
