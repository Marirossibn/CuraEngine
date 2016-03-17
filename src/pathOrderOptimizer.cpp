/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#include "pathOrderOptimizer.h"
#include "utils/logoutput.h"
#include "utils/BucketGrid2D.h"

#define INLINE static inline

namespace cura {

/**
*
*/
void PathOrderOptimizer::optimize()
{
    bool picked[polygons.size()];
    memset(picked, false, sizeof(bool) * polygons.size());/// initialized as falses
    
    for (PolygonRef poly : polygons) /// find closest point to initial starting point within each polygon +initialize picked
    {
        int best = -1;
        float bestDist = std::numeric_limits<float>::infinity();
        for (unsigned int point_idx = 0; point_idx < poly.size(); point_idx++) /// get closest point in polygon
        {
            float dist = vSize2f(poly[point_idx] - startPoint);
            if (dist < bestDist)
            {
                best = point_idx;
                bestDist = dist;
            }
        }
        polyStart.push_back(best);
        //picked.push_back(false); /// initialize all picked values as false

        assert(poly.size() != 2);
    }


    Point prev_point = startPoint;
    for (unsigned int poly_order_idx = 0; poly_order_idx < polygons.size(); poly_order_idx++) /// actual path order optimizer
    {
        int best_poly_idx = -1;
        float bestDist = std::numeric_limits<float>::infinity();


        for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
        {
            if (picked[poly_idx] || polygons[poly_idx].size() < 1) /// skip single-point-polygons
            {
                continue;
            }

            assert (polygons[poly_idx].size() != 2);

            float dist = vSize2f(polygons[poly_idx][polyStart[poly_idx]] - prev_point);
            if (dist < bestDist)
            {
                best_poly_idx = poly_idx;
                bestDist = dist;
            }

        }


        if (best_poly_idx > -1) /// should always be true; we should have been able to identify the best next polygon
        {
            assert(polygons[best_poly_idx].size() != 2);

            prev_point = polygons[best_poly_idx][polyStart[best_poly_idx]];

            picked[best_poly_idx] = true;
            polyOrder.push_back(best_poly_idx);
        }
        else
        {
            logError("Failed to find next closest polygon.\n");
        }
    }

    prev_point = startPoint;
    for (unsigned int order_idx = 0; order_idx < polyOrder.size(); order_idx++) /// decide final starting points in each polygon
    {
        int poly_idx = polyOrder[order_idx];
        int point_idx = getPolyStart(prev_point, poly_idx);
        polyStart[poly_idx] = point_idx;
        prev_point = polygons[poly_idx][point_idx];

    }
}

int PathOrderOptimizer::getPolyStart(Point prev_point, int poly_idx)
{
    switch (type)
    {
        case EZSeamType::BACK:      return getFarthestPointInPolygon(poly_idx); 
        case EZSeamType::RANDOM:    return getRandomPointInPolygon(poly_idx); 
        case EZSeamType::SHORTEST:  return getClosestPointInPolygon(prev_point, poly_idx);
        default:                    return getClosestPointInPolygon(prev_point, poly_idx);
    }
}


int PathOrderOptimizer::getClosestPointInPolygon(Point prev_point, int poly_idx)
{
    PolygonRef poly = polygons[poly_idx];

    const int64_t dot_score_scale = 20000;

    int best_point_idx = -1;
    float best_point_score = std::numeric_limits<float>::infinity();
    bool orientation = poly.orientation();
    Point p0 = poly.back();
    for (unsigned int point_idx = 0; point_idx < poly.size(); point_idx++)
    {
        Point& p1 = poly[point_idx];
        Point& p2 = poly[(point_idx + 1) % poly.size()];
        float dist = vSize2f(p1 - prev_point);
        Point n0 = normal(p0 - p1, dot_score_scale);
        Point n1 = normal(p1 - p2, dot_score_scale);
        float dot_score = dot(n0, n1) - dot(turn90CCW(n0), n1); /// prefer binnenbocht
        if (orientation)
        {
            dot_score = -dot_score;
        }
        if (dist + dot_score < best_point_score)
        {
            best_point_idx = point_idx;
            best_point_score = dist + dot_score;
        }
        p0 = p1;
    }
    return best_point_idx;
}

int PathOrderOptimizer::getRandomPointInPolygon(int poly_idx)
{
    return rand() % polygons[poly_idx].size();
}


int PathOrderOptimizer::getFarthestPointInPolygon(int poly_idx)
{
    PolygonRef poly = polygons[poly_idx];
    int best_point_idx = -1;
    float best_y = std::numeric_limits<float>::min();
    for(unsigned int point_idx=0 ; point_idx<poly.size() ; point_idx++)
    {
        if (poly[point_idx].Y > best_y)
        {
            best_point_idx = point_idx;
            best_y = poly[point_idx].Y;
        }
    }
    return best_point_idx;
}


/**
*
*/
void LineOrderOptimizer::optimize()
{
    int gridSize = 5000; // the size of the cells in the hash grid.
    BucketGrid2D<unsigned int> line_bucket_grid(gridSize);
    bool picked[polygons.size()];
    memset(picked, false, sizeof(bool) * polygons.size());/// initialized as falses
    
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++) /// find closest point to initial starting point within each polygon +initialize picked
    {
        int best_point_idx = -1;
        float best_point_dist = std::numeric_limits<float>::infinity();
        PolygonRef poly = polygons[poly_idx];
        for (unsigned int point_idx = 0; point_idx < poly.size(); point_idx++) /// get closest point from polygon
        {
            float dist = vSize2f(poly[point_idx] - startPoint);
            if (dist < best_point_dist)
            {
                best_point_idx = point_idx;
                best_point_dist = dist;
            }
        }
        polyStart.push_back(best_point_idx);

        assert(poly.size() == 2);

        line_bucket_grid.insert(poly[0], poly_idx);
        line_bucket_grid.insert(poly[1], poly_idx);

    }


    Point incommingPerpundicularNormal(0, 0);
    Point prev_point = startPoint;
    for (unsigned int order_idx = 0; order_idx < polygons.size(); order_idx++) /// actual path order optimizer
    {
        int best_line_idx = -1;
        float bestDist = std::numeric_limits<float>::infinity();

        for(unsigned int close_line_poly_idx :  line_bucket_grid.findNearbyObjects(prev_point)) /// check if single-line-polygon is close to last point
        {
            if (picked[close_line_poly_idx] || polygons[close_line_poly_idx].size() < 1)
            {
                continue;
            }

            checkIfLineIsBest(close_line_poly_idx, best_line_idx, bestDist, prev_point, incommingPerpundicularNormal);
        }

        if (best_line_idx == -1) /// if single-line-polygon hasn't been found yet
        {
            for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
            {
                if (picked[poly_idx] || polygons[poly_idx].size() < 1) /// skip single-point-polygons
                {
                    continue;
                }
                assert(polygons[poly_idx].size() == 2);

                checkIfLineIsBest(poly_idx, best_line_idx, bestDist, prev_point, incommingPerpundicularNormal);

            }
        }

        if (best_line_idx > -1) /// should always be true; we should have been able to identify the best next polygon
        {
            assert(polygons[best_line_idx].size() == 2);

            int endIdx = polyStart[best_line_idx] * -1 + 1; /// 1 -> 0 , 0 -> 1
            prev_point = polygons[best_line_idx][endIdx];
            incommingPerpundicularNormal = turn90CCW(normal(polygons[best_line_idx][endIdx] - polygons[best_line_idx][polyStart[best_line_idx]], 1000));

            picked[best_line_idx] = true;
            polyOrder.push_back(best_line_idx);
        }
        else
        {
            logError("Failed to find next closest line.\n");
        }
    }

    prev_point = startPoint;
    for (int poly_idx : polyOrder)
    {
        PolygonRef poly = polygons[poly_idx];
        int best = -1;
        float bestDist = std::numeric_limits<float>::infinity();
        bool orientation = poly.orientation();
        for (unsigned int point_idx = 0; point_idx < poly.size(); point_idx++)
        {
            float dist = vSize2f(polygons[poly_idx][point_idx] - prev_point);
            Point n0 = normal(poly[(point_idx+poly.size()-1)%poly.size()] - poly[point_idx], 2000);
            Point n1 = normal(poly[point_idx] - poly[(point_idx + 1) % poly.size()], 2000);
            float dot_score = dot(n0, n1) - dot(turn90CCW(n0), n1);
            if (orientation)
                dot_score = -dot_score;
            if (dist + dot_score < bestDist)
            {
                best = point_idx;
                bestDist = dist + dot_score;
            }
        }

        polyStart[poly_idx] = best;
        assert(poly.size() == 2);
        prev_point = poly[best *-1 + 1]; /// 1 -> 0 , 0 -> 1

    }
}

inline void LineOrderOptimizer::checkIfLineIsBest(unsigned int poly_idx, int& best, float& best_dist, Point& prev_point, Point& incomming_perpundicular_normal)
{
    { /// check distance to first point on line (0)
        float dist = vSize2f(polygons[poly_idx][0] - prev_point);
        dist += std::abs(dot(incomming_perpundicular_normal, normal(polygons[poly_idx][1] - polygons[poly_idx][0], 1000))) * 0.0001f; /// penalize sharp corners
        if (dist < best_dist)
        {
            best = poly_idx;
            best_dist = dist;
            polyStart[poly_idx] = 0;
        }
    }
    { /// check distance to second point on line (1)
        float dist = vSize2f(polygons[poly_idx][1] - prev_point);
        dist += std::abs(dot(incomming_perpundicular_normal, normal(polygons[poly_idx][0] - polygons[poly_idx][1], 1000))) * 0.0001f; /// penalize sharp corners
        if (dist < best_dist)
        {
            best = poly_idx;
            best_dist = dist;
            polyStart[poly_idx] = 1;
        }
    }
}

}//namespace cura
