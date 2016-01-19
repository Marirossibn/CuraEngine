/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#include "infill.h"
#include "functional"
#include "utils/polygonUtils.h"
#include "utils/logoutput.h"

namespace cura {

void Infill::generate(Polygons& result_polygons, Polygons& result_lines, Polygons* in_between)
{
    if (in_outline.size() == 0) return;
    if (line_distance == 0) return;
    const Polygons* outline = &in_outline;
    Polygons outline_offsetted;
    switch(pattern)
    {
    case EFillMethod::GRID:
        generateGridInfill(result_lines, line_distance * 2, fill_angle);
        break;
    case EFillMethod::LINES:
        generateLineInfill(result_lines, line_distance, fill_angle);
        break;
    case EFillMethod::TRIANGLES:
        generateTriangleInfill(result_lines, line_distance * 3, fill_angle);
        break;
    case EFillMethod::CONCENTRIC:
        PolygonUtils::offsetSafe(in_outline, outlineOffset - infill_line_width / 2, infill_line_width, outline_offsetted, false); // - infill_line_width / 2 cause generateConcentricInfill expects [outline] to be the outer most polygon instead of the outer outline 
        outline = &outline_offsetted;
        if (abs(infill_line_width - line_distance) < 10)
        {
            generateConcentricInfillDense(*outline, result_polygons, in_between, avoidOverlappingPerimeters);
        }
        else 
        {
            generateConcentricInfill(*outline, result_polygons, line_distance);
        }
        break;
    case EFillMethod::ZIG_ZAG:
        if (outlineOffset != 0)
        {
            PolygonUtils::offsetSafe(in_outline, outlineOffset, infill_line_width, outline_offsetted, avoidOverlappingPerimeters);
            outline = &outline_offsetted;
        }
        generateZigZagInfill(*outline, result_lines, line_distance, fill_angle, connected_zigzags, use_endPieces);
        break;
    default:
        logError("Fill pattern has unknown value.\n");
        break;
    }
}

    
      
void Infill::generateConcentricInfillDense(Polygons outline, Polygons& result, Polygons* in_between, bool avoidOverlappingPerimeters)
{
    while(outline.size() > 0)
    {
        for (unsigned int polyNr = 0; polyNr < outline.size(); polyNr++)
        {
            PolygonRef r = outline[polyNr];
            result.add(r);
        }
        Polygons next_outline;
        PolygonUtils::offsetExtrusionWidth(outline, true, infill_line_width, next_outline, in_between, avoidOverlappingPerimeters);
        outline = next_outline;
    } 

}

void Infill::generateConcentricInfill(Polygons outline, Polygons& result, int inset_value)
{
    while(outline.size() > 0)
    {
        for (unsigned int polyNr = 0; polyNr < outline.size(); polyNr++)
        {
            PolygonRef r = outline[polyNr];
            result.add(r);
        }
        outline = outline.offset(-inset_value);
    } 
}


void Infill::generateGridInfill(Polygons& result, int lineSpacing, double rotation)
{
    generateLineInfill(result, lineSpacing, rotation);
    generateLineInfill(result, lineSpacing, rotation + 90);
}

void Infill::generateTriangleInfill(Polygons& result, int lineSpacing, double rotation)
{
    generateLineInfill(result, lineSpacing, rotation);
    generateLineInfill(result, lineSpacing, rotation + 60);
    generateLineInfill(result, lineSpacing, rotation + 120);
}

void Infill::addLineInfill(Polygons& result, const PointMatrix& matrix, const int scanline_min_idx, const int lineSpacing, const AABB boundary, std::vector<std::vector<int64_t>>& cutList)
{
    auto addLine = [&](Point from, Point to)
    {            
        PolygonRef p = result.newPoly();
        p.add(matrix.unapply(from));
        p.add(matrix.unapply(to));
    };
    
    auto compare_int64_t = [](const void* a, const void* b)
    {
        int64_t n = (*(int64_t*)a) - (*(int64_t*)b);
        if (n < 0)
        {
            return -1;
        }
        if (n > 0)
        {
            return 1;
        }
        return 0;
    };
    
    int scanline_idx = 0;
    for(int64_t x = scanline_min_idx * lineSpacing; x < boundary.max.X; x += lineSpacing)
    {
        std::vector<int64_t>& crossings = cutList[scanline_idx];
        qsort(crossings.data(), crossings.size(), sizeof(int64_t), compare_int64_t);
        for(unsigned int crossing_idx = 0; crossing_idx + 1 < crossings.size(); crossing_idx += 2)
        {
            if (crossings[crossing_idx + 1] - crossings[crossing_idx] < infill_line_width / 5)
            { // segment is too short to create infill
                continue;
            }
            addLine(Point(x, crossings[crossing_idx]), Point(x, crossings[crossing_idx + 1]));
        }
        scanline_idx += 1;
    }
}

void Infill::generateLineInfill(Polygons& result, int lineSpacing, const double& fill_angle)
{
    PointMatrix rotation_matrix(fill_angle);
    NoZigZagConnectorProcessor lines_processor(rotation_matrix, result);
    bool connected_zigzags = false;
    generateLinearBasedInfill(outlineOffset, result, lineSpacing, rotation_matrix, lines_processor, connected_zigzags);
}


void Infill::generateZigZagInfill(const Polygons& in_outline, Polygons& result, const int lineSpacing, const double& fill_angle, const bool connected_zigzags, const bool use_endPieces)
{
    PointMatrix rotation_matrix(fill_angle);
    if (use_endPieces)
    {
        if (connected_zigzags)
        {
            ZigzagConnectorProcessorConnectedEndPieces zigzag_processor(rotation_matrix, result);
            generateLinearBasedInfill(0, result, lineSpacing, rotation_matrix, zigzag_processor, connected_zigzags);
        }
        else
        {
            ZigzagConnectorProcessorDisconnectedEndPieces zigzag_processor(rotation_matrix, result);
            generateLinearBasedInfill(0, result, lineSpacing, rotation_matrix, zigzag_processor, connected_zigzags);
        }
    }
    else 
    {
        ZigzagConnectorProcessorNoEndPieces zigzag_processor(rotation_matrix, result);
        generateLinearBasedInfill(0, result, lineSpacing, rotation_matrix, zigzag_processor, connected_zigzags);
    }
}


void Infill::generateLinearBasedInfill(const int outlineOffset, Polygons& result, const int lineSpacing, const PointMatrix& rotation_matrix, ZigzagConnectorProcessor& zigzag_connector_processor, const bool connected_zigzags)
{
    if (lineSpacing == 0)
    {
        return;
    }
    if (in_outline.size() == 0)
    {
        return;
    }
    
    Polygons outline = ((outlineOffset)? in_outline.offset(outlineOffset) : in_outline).offset(infill_line_width * infill_overlap / 100);
    if (outline.size() == 0)
    {
        return;
    }
    
    outline.applyMatrix(rotation_matrix);

    
    AABB boundary(outline);
    
    int scanline_min_idx = boundary.min.X / lineSpacing;
    int lineCount = (boundary.max.X + (lineSpacing - 1)) / lineSpacing - scanline_min_idx;
  
    std::vector<std::vector<int64_t> > cutList; // mapping from scanline to all intersections with polygon segments
    
    for(int scanline_idx = 0; scanline_idx < lineCount; scanline_idx++)
    {
        cutList.push_back(std::vector<int64_t>());
    }
    
    for(unsigned int poly_idx = 0; poly_idx < outline.size(); poly_idx++)
    {
        PolygonRef poly = outline[poly_idx];
        Point p0 = poly.back();
        zigzag_connector_processor.registerVertex(p0); // always adds the first point to ZigzagConnectorProcessorEndPieces::first_zigzag_connector when using a zigzag infill type
        for(unsigned int point_idx = 0; point_idx < poly.size(); point_idx++)
        {
            Point p1 = poly[point_idx];
            if (p1.X == p0.X)
            {
                zigzag_connector_processor.registerVertex(p1); 
                // TODO: how to make sure it always adds the shortest line? (in order to prevent overlap with the zigzag connectors)
                // note: this is already a problem for normal infill, but hasn't really cothered anyone so far.
                p0 = p1;
                continue; 
            }
            
            int scanline_idx0 = (p0.X + ((p0.X > 0)? -1 : -lineSpacing)) / lineSpacing; // -1 cause a linesegment on scanline x counts as belonging to scansegment x-1   ...
            int scanline_idx1 = (p1.X + ((p1.X > 0)? -1 : -lineSpacing)) / lineSpacing; // -linespacing because a line between scanline -n and -n-1 belongs to scansegment -n-1 (for n=positive natural number)
            // this way of handling the indices takes care of the case where a boundary line segment ends exactly on a scanline:
            // in case the next segment moves back from that scanline either 2 or 0 scanline-boundary intersections are created
            // otherwise only 1 will be created, counting as an actual intersection
            int direction = 1;
            if (p0.X > p1.X) 
            { 
                direction = -1; 
                scanline_idx1 += 1; // only consider the scanlines in between the scansegments
            }
            else
            {
                scanline_idx0 += 1; // only consider the scanlines in between the scansegments
            }
            
            for(int scanline_idx = scanline_idx0; scanline_idx != scanline_idx1 + direction; scanline_idx += direction)
            {
                int x = scanline_idx * lineSpacing;
                int y = p1.Y + (p0.Y - p1.Y) * (x - p1.X) / (p0.X - p1.X);
                cutList[scanline_idx - scanline_min_idx].push_back(y);
                Point scanline_linesegment_intersection(x, y);
                zigzag_connector_processor.registerScanlineSegmentIntersection(scanline_linesegment_intersection, scanline_idx % 2 == 0);
            }
            zigzag_connector_processor.registerVertex(p1);
            p0 = p1;
        }
        zigzag_connector_processor.registerPolyFinished();
    }

    if (cutList.size() == 0)
    {
        return;
    }
    if (connected_zigzags && cutList.size() == 1 && cutList[0].size() <= 2)
    {
        return;  // don't add connection if boundary already contains whole outline!
    }

    addLineInfill(result, rotation_matrix, scanline_min_idx, lineSpacing, boundary, cutList);
}

}//namespace cura
