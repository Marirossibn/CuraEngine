/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#include <stdio.h>

#include "utils/gettime.h"
#include "utils/logoutput.h"

#include "slicer.h"
#include "debug.h" // TODO remove

namespace cura {
    
int largest_neglected_gap_first_phase = MM2INT(0.01); //!< distance between two line segments regarded as connected
int largest_neglected_gap_second_phase = MM2INT(0.02); //!< distance between two line segments regarded as connected
int max_stitch1 = MM2INT(10.0); //!< maximal distance stitched between open polylines to form polygons

void SlicerLayer::makeBasicPolygonLoops(Mesh* mesh, Polygons& open_polylines)
{
    for(unsigned int start_segment_idx = 0; start_segment_idx < segments.size(); start_segment_idx++)
    {
        if (!segments[start_segment_idx].addedToPolygon)
        {
            makeBasicPolygonLoop(mesh, open_polylines, start_segment_idx);
        }
    }
    //Clear the segmentList to save memory, it is no longer needed after this point.
    segments.clear();
}

void SlicerLayer::makeBasicPolygonLoop(Mesh* mesh, Polygons& open_polylines, unsigned int start_segment_idx)
{
    
    Polygon poly;
    poly.add(segments[start_segment_idx].start);
    
    for (int segment_idx = start_segment_idx; segment_idx != -1; )
    {
        SlicerSegment& segment = segments[segment_idx];
        Point p0 = segment.end;
        poly.add(p0);
        segment.addedToPolygon = true;
        segment_idx = getNextSegmentIdx(mesh, segment, start_segment_idx);
        if (segment_idx == static_cast<int>(start_segment_idx)) 
        { // polyon is closed
            polygons.add(poly);
            return;
        }
    }
    // polygon couldn't be closed
    open_polylines.add(poly);
}

int SlicerLayer::getNextSegmentIdx(Mesh* mesh, SlicerSegment& segment, unsigned int start_segment_idx)
{
    int next_segment_idx = -1;
    const MeshFace& face = mesh->faces[segment.faceIndex];
    for(unsigned int face_edge_idx = 0; face_edge_idx < 3; face_edge_idx++)
    { // check segments in connected faces
        decltype(face_idx_to_segment_idx.begin()) it;
        if (face.connected_face_index[face_edge_idx] > -1 && (it = face_idx_to_segment_idx.find(face.connected_face_index[face_edge_idx])) != face_idx_to_segment_idx.end())
        {
            int segment_idx = (*it).second;
            Point p1 = segments[segment_idx].start;
            Point diff = segment.end - p1;
            if (shorterThen(diff, largest_neglected_gap_first_phase))
            {
                if (segment_idx == static_cast<int>(start_segment_idx))
                    return start_segment_idx;
                if (segments[segment_idx].addedToPolygon)
                    continue;
                next_segment_idx = segment_idx; // not immediately returned since we might still encounter the start_segment_idx
            }
        }
    }
    return next_segment_idx;
}

void SlicerLayer::connectOpenPolylines(Polygons& open_polylines)
{
    // TODO use some space partitioning data structure to make this run faster than O(n^2)
    for(unsigned int open_polyline_idx = 0; open_polyline_idx < open_polylines.size(); open_polyline_idx++)
    {
        PolygonRef open_polyline = open_polylines[open_polyline_idx];
        
        if (open_polyline.size() < 1) continue;
        for(unsigned int open_polyline_other_idx = 0; open_polyline_other_idx < open_polylines.size(); open_polyline_other_idx++)
        {
            PolygonRef open_polyline_other = open_polylines[open_polyline_other_idx];
            
            if (open_polyline_other.size() < 1) continue;
            
            Point diff = open_polyline.back() - open_polyline_other[0];

            if (shorterThen(diff, largest_neglected_gap_second_phase))
            {
                if (open_polyline_idx == open_polyline_other_idx)
                {
                    polygons.add(open_polyline);
                    open_polyline.clear();
                    break;
                }
                else
                {
                    for(unsigned int line_idx = 0; line_idx<open_polyline_other.size(); line_idx++)
                    {
                        open_polyline.add(open_polyline_other[line_idx]);
                    }
                    open_polyline_other.clear();
                }
            }
        }
    }
}

void SlicerLayer::stitch(Polygons open_polylines)
{ // TODO This is an inefficient implementation which can run in O(n^3) time.
    // below code closes smallest gaps first
    while(1)
    {
        int64_t best_dist2 = max_stitch1 * max_stitch1;
        unsigned int best_polyline_1_idx = -1;
        unsigned int best_polyline_2_idx = -1;
        bool reversed = false;
        for(unsigned int polyline_1_idx = 0; polyline_1_idx < open_polylines.size(); polyline_1_idx++)
        {
            PolygonRef polyline_1 = open_polylines[polyline_1_idx];
            
            if (polyline_1.size() < 1) continue;
            for(unsigned int polyline_2_idx = 0; polyline_2_idx < open_polylines.size(); polyline_2_idx++)
            {
                PolygonRef polyline_2 = open_polylines[polyline_2_idx];
                
                if (polyline_2.size() < 1) continue;
                
                Point diff = polyline_1.back() - polyline_2[0];
                int64_t dist2 = vSize2(diff);
                if (dist2 < best_dist2)
                {
                    best_dist2 = dist2;
                    best_polyline_1_idx = polyline_1_idx;
                    best_polyline_2_idx = polyline_2_idx;
                    reversed = false;
                }

                if (polyline_1_idx != polyline_2_idx)
                {
                    Point diff = polyline_1.back() - polyline_2.back();
                    int64_t dist2 = vSize2(diff);
                    if (dist2 < best_dist2)
                    {
                        best_dist2 = dist2;
                        best_polyline_1_idx = polyline_1_idx;
                        best_polyline_2_idx = polyline_2_idx;
                        reversed = true;
                    }
                }
            }
        }
        
        if (best_dist2 >= max_stitch1 * max_stitch1)
            break; // this code is reached if there was nothing to stitch within the distance limits
        
        PolygonRef polyline_1 = open_polylines[best_polyline_1_idx];
        PolygonRef polyline_2 = open_polylines[best_polyline_2_idx];
        
        if (best_polyline_1_idx == best_polyline_2_idx)
        { // connect last piece of 'circle'
            polygons.add(polyline_1);
            polyline_1.clear();
        }
        else
        { // connect two polylines
            if (reversed)
            {
                if (polyline_1.polygonLength() > polyline_2.polygonLength()) // old code loked at actual length?!
//                 if (polyline_1.size() > polyline_2.size())
                {
                    for(unsigned int poly_idx = polyline_2.size()-1; int(poly_idx)>=0; poly_idx--)
                        polyline_1.add(polyline_2[poly_idx]);
                    polyline_2.clear();
                } 
                else
                {
                    for(unsigned int poly_idx = polyline_1.size()-1; int(poly_idx)>=0; poly_idx--)
                        polyline_2.add(polyline_1[poly_idx]);
                    polyline_1.clear();
                }
            }
            else
            {
                for(Point& p : polyline_2)
                    polyline_1.add(p);
                polyline_2.clear();
            }
        }
    }
}

void SlicerLayer::makePolygons(Mesh* mesh, bool keep_none_closed, bool extensive_stitching)
{
    Polygons open_polylines;
    
    makeBasicPolygonLoops(mesh, open_polylines);
    
    
    // TODO: (?) for mesh surface mode: connect open polygons. Maybe the above algorithm can create two open polygons which are actually connected when the starting segment is in the middle between the two open polygons.

    connectOpenPolylines(mesh, open_polylines);

    if (mesh->getSettingAsSurfaceMode("magic_mesh_surface_mode") == ESurfaceMode::NORMAL)
    {
        stitch(mesh, open_polylines);
    }
    
    if (extensive_stitching)
    {
        //For extensive stitching find 2 open polygons that are touching 2 closed polygons.
        // Then find the sortest path over this polygon that can be used to connect the open polygons,
        // And generate a path over this shortest bit to link up the 2 open polygons.
        // (If these 2 open polygons are the same polygon, then the final result is a closed polyon)
        
        while(1)
        {
            unsigned int bestA = -1;
            unsigned int bestB = -1;
            GapCloserResult bestResult;
            bestResult.len = POINT_MAX;
            bestResult.polygonIdx = -1;
            bestResult.pointIdxA = -1;
            bestResult.pointIdxB = -1;
            
            for(unsigned int i=0; i<open_polylines.size(); i++)
            {
                if (open_polylines[i].size() < 1) continue;
                
                {
                    GapCloserResult res = findPolygonGapCloser(open_polylines[i][0], open_polylines[i][open_polylines[i].size()-1]);
                    if (res.len > 0 && res.len < bestResult.len)
                    {
                        bestA = i;
                        bestB = i;
                        bestResult = res;
                    }
                }

                for(unsigned int j=0; j<open_polylines.size(); j++)
                {
                    if (open_polylines[j].size() < 1 || i == j) continue;
                    
                    GapCloserResult res = findPolygonGapCloser(open_polylines[i][0], open_polylines[j][open_polylines[j].size()-1]);
                    if (res.len > 0 && res.len < bestResult.len)
                    {
                        bestA = i;
                        bestB = j;
                        bestResult = res;
                    }
                }
            }
            
            if (bestResult.len < POINT_MAX)
            {
                if (bestA == bestB)
                {
                    if (bestResult.pointIdxA == bestResult.pointIdxB)
                    {
                        polygons.add(open_polylines[bestA]);
                        open_polylines[bestA].clear();
                    }
                    else if (bestResult.AtoB)
                    {
                        PolygonRef poly = polygons.newPoly();
                        for(unsigned int j = bestResult.pointIdxA; j != bestResult.pointIdxB; j = (j + 1) % polygons[bestResult.polygonIdx].size())
                            poly.add(polygons[bestResult.polygonIdx][j]);
                        for(unsigned int j = open_polylines[bestA].size() - 1; int(j) >= 0; j--)
                            poly.add(open_polylines[bestA][j]);
                        open_polylines[bestA].clear();
                    }
                    else
                    {
                        unsigned int n = polygons.size();
                        polygons.add(open_polylines[bestA]);
                        for(unsigned int j = bestResult.pointIdxB; j != bestResult.pointIdxA; j = (j + 1) % polygons[bestResult.polygonIdx].size())
                            polygons[n].add(polygons[bestResult.polygonIdx][j]);
                        open_polylines[bestA].clear();
                    }
                }
                else
                {
                    if (bestResult.pointIdxA == bestResult.pointIdxB)
                    {
                        for(unsigned int n=0; n<open_polylines[bestA].size(); n++)
                            open_polylines[bestB].add(open_polylines[bestA][n]);
                        open_polylines[bestA].clear();
                    }
                    else if (bestResult.AtoB)
                    {
                        Polygon poly;
                        for(unsigned int n = bestResult.pointIdxA; n != bestResult.pointIdxB; n = (n + 1) % polygons[bestResult.polygonIdx].size())
                            poly.add(polygons[bestResult.polygonIdx][n]);
                        for(unsigned int n=poly.size()-1;int(n) >= 0; n--)
                            open_polylines[bestB].add(poly[n]);
                        for(unsigned int n=0; n<open_polylines[bestA].size(); n++)
                            open_polylines[bestB].add(open_polylines[bestA][n]);
                        open_polylines[bestA].clear();
                    }
                    else
                    {
                        for(unsigned int n = bestResult.pointIdxB; n != bestResult.pointIdxA; n = (n + 1) % polygons[bestResult.polygonIdx].size())
                            open_polylines[bestB].add(polygons[bestResult.polygonIdx][n]);
                        for(unsigned int n = open_polylines[bestA].size() - 1; int(n) >= 0; n--)
                            open_polylines[bestB].add(open_polylines[bestA][n]);
                        open_polylines[bestA].clear();
                    }
                }
            }
            else
            {
                break;
            }
        }
    }

    if (keep_none_closed)
    {
        for(unsigned int n=0; n<open_polylines.size(); n++)
        {
            if (open_polylines[n].size() > 0)
                polygons.add(open_polylines[n]);
        }
    }
    
    for(unsigned int i=0;i<open_polylines.size();i++)
    {
        if (open_polylines[i].size() > 0)
            openPolylines.add(open_polylines[i]);
    }

    //Remove all the tiny polygons, or polygons that are not closed. As they do not contribute to the actual print.
    int snapDistance = MM2INT(1.0);
    for(unsigned int i=0;i<polygons.size();i++)
    {
        int length = 0;

        for(unsigned int n=1; n<polygons[i].size(); n++)
        {
            length += vSize(polygons[i][n] - polygons[i][n-1]);
            if (length > snapDistance)
                break;
        }
        if (length < snapDistance)
        {
            polygons.remove(i);
            i--;
        }
    }

    //Finally optimize all the polygons. Every point removed saves time in the long run.
    polygons.simplify();
    
    polygons.removeDegenerateVerts(); // remove verts connected to overlapping line segments
    
    int xy_offset = mesh->getSettingInMicrons("xy_offset");
    if (xy_offset != 0)
    {
        polygons = polygons.offset(xy_offset);
    }
}


Slicer::Slicer(Mesh* mesh, int initial, int thickness, int layer_count, bool keep_none_closed, bool extensive_stitching)
{
    assert(layer_count > 0);

    layers.resize(layer_count);
    
    for(int32_t layer_nr = 0; layer_nr < layer_count; layer_nr++)
    {
        layers[layer_nr].z = initial + thickness * layer_nr;
    }
    
    for(unsigned int mesh_idx = 0; mesh_idx < mesh->faces.size(); mesh_idx++)
    {
        MeshFace& face = mesh->faces[mesh_idx];
        Point3 p0 = mesh->vertices[face.vertex_index[0]].p;
        Point3 p1 = mesh->vertices[face.vertex_index[1]].p;
        Point3 p2 = mesh->vertices[face.vertex_index[2]].p;
        int32_t minZ = p0.z;
        int32_t maxZ = p0.z;
        if (p1.z < minZ) minZ = p1.z;
        if (p2.z < minZ) minZ = p2.z;
        if (p1.z > maxZ) maxZ = p1.z;
        if (p2.z > maxZ) maxZ = p2.z;
        int32_t layer_max = (maxZ - initial) / thickness;
        for(int32_t layer_nr = (minZ - initial) / thickness; layer_nr <= layer_max; layer_nr++)
        {
            int32_t z = layer_nr * thickness + initial;
            if (z < minZ) continue;
            if (layer_nr < 0) continue;
            
            SlicerSegment s;
            if (p0.z < z && p1.z >= z && p2.z >= z)
                s = project2D(p0, p2, p1, z);
            else if (p0.z > z && p1.z < z && p2.z < z)
                s = project2D(p0, p1, p2, z);

            else if (p1.z < z && p0.z >= z && p2.z >= z)
                s = project2D(p1, p0, p2, z);
            else if (p1.z > z && p0.z < z && p2.z < z)
                s = project2D(p1, p2, p0, z);

            else if (p2.z < z && p1.z >= z && p0.z >= z)
                s = project2D(p2, p1, p0, z);
            else if (p2.z > z && p1.z < z && p0.z < z)
                s = project2D(p2, p0, p1, z);
            else
            {
                //Not all cases create a segment, because a point of a face could create just a dot, and two touching faces
                //  on the slice would create two segments
                continue;
            }
            layers[layer_nr].face_idx_to_segment_idx.insert(std::make_pair(mesh_idx, layers[layer_nr].segments.size()));
            s.faceIndex = mesh_idx;
            s.addedToPolygon = false;
            layers[layer_nr].segments.push_back(s);
        }
    }
    for(unsigned int layer_nr=0; layer_nr<layers.size(); layer_nr++)
    {
        layers[layer_nr].makePolygons(mesh, keep_none_closed, extensive_stitching);
    }
}

}//namespace cura
