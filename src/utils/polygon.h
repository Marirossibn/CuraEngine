#ifndef UTILS_POLYGON_H
#define UTILS_POLYGON_H

#include <vector>
#include <assert.h>
#include <float.h>
#include <clipper/clipper.hpp>

#include "intpoint.h"

//#define CHECK_POLY_ACCESS
#ifdef CHECK_POLY_ACCESS
#define POLY_ASSERT(e) assert(e)
#else
#define POLY_ASSERT(e) do {} while(0)
#endif

namespace cura {

enum PolygonType
{
    NoneType,
    Inset0Type,
    InsetXType,
    SkinType,
    SupportType,
    SkirtType
};

const static int clipper_init = (0);
#define NO_INDEX (std::numeric_limits<unsigned int>::max())

class PolygonRef
{
    ClipperLib::Path* polygon;
    PolygonRef()
    : polygon(nullptr)
    {}
public:
    PolygonRef(ClipperLib::Path& polygon)
    : polygon(&polygon)
    {}

    unsigned int size() const
    {
        return polygon->size();
    }

    Point& operator[] (unsigned int index) const
    {
        POLY_ASSERT(index < size());
        return (*polygon)[index];
    }

    void* data()
    {
        return polygon->data();
    }

    void add(const Point p)
    {
        polygon->push_back(p);
    }

    void remove(unsigned int index)
    {
        POLY_ASSERT(index < size());
        polygon->erase(polygon->begin() + index);
    }

    void clear()
    {
        polygon->clear();
    }

    bool orientation() const
    {
        return ClipperLib::Orientation(*polygon);
    }

    void reverse()
    {
        ClipperLib::ReversePath(*polygon);
    }

    int64_t polygonLength() const
    {
        int64_t length = 0;
        Point p0 = (*polygon)[polygon->size()-1];
        for(unsigned int n=0; n<polygon->size(); n++)
        {
            Point p1 = (*polygon)[n];
            length += vSize(p0 - p1);
            p0 = p1;
        }
        return length;
    }
    
    Point min() const
    {
        Point ret = Point(POINT_MAX, POINT_MAX);
        for(Point p : *polygon)
        {
            ret.X = std::min(ret.X, p.X);
            ret.Y = std::min(ret.Y, p.Y);
        }
        return ret;
    }
    
    Point max() const
    {
        Point ret = Point(POINT_MIN, POINT_MIN);
        for(Point p : *polygon)
        {
            ret.X = std::max(ret.X, p.X);
            ret.Y = std::max(ret.Y, p.Y);
        }
        return ret;
    }


    double area() const
    {
        return ClipperLib::Area(*polygon);
    }

    Point centerOfMass() const
    {
        double x = 0, y = 0;
        Point p0 = (*polygon)[polygon->size()-1];
        for(unsigned int n=0; n<polygon->size(); n++)
        {
            Point p1 = (*polygon)[n];
            double second_factor = (p0.X * p1.Y) - (p1.X * p0.Y);

            x += double(p0.X + p1.X) * second_factor;
            y += double(p0.Y + p1.Y) * second_factor;
            p0 = p1;
        }

        double area = Area(*polygon);
        x = x / 6 / area;
        y = y / 6 / area;

        if (x < 0)
        {
            x = -x;
            y = -y;
        }
        return Point(x, y);
    }

    Point closestPointTo(Point p)
    {
        Point ret = p;
        float bestDist = FLT_MAX;
        for(unsigned int n=0; n<polygon->size(); n++)
        {
            float dist = vSize2f(p - (*polygon)[n]);
            if (dist < bestDist)
            {
                ret = (*polygon)[n];
                bestDist = dist;
            }
        }
        return ret;
    }
    
    //Check if we are inside the polygon. We do this by tracing from the point towards the negative X direction,
    //  every line we cross increments the crossings counter. If we have an even number of crossings then we are not inside the polygon.
    bool inside(Point p)
    {
        if (polygon->size() < 1)
            return false;
        
        int crossings = 0;
        Point p0 = (*polygon)[polygon->size()-1];
        for(unsigned int n=0; n<polygon->size(); n++)
        {
            Point p1 = (*polygon)[n];
            
            if ((p0.Y >= p.Y && p1.Y < p.Y) || (p1.Y > p.Y && p0.Y <= p.Y))
            {
                int64_t x = p0.X + (p1.X - p0.X) * (p.Y - p0.Y) / (p1.Y - p0.Y);
                if (x >= p.X)
                    crossings ++;
            }
            p0 = p1;
        }
        return (crossings % 2) == 1;
    }

    PolygonRef smooth(int remove_length)
    {
        PolygonRef thiss = *this;
        ClipperLib::Path* poly = new ClipperLib::Path();
        PolygonRef new_poly(*poly);
        if (size() > 0)
            poly->push_back(thiss[0]);
        for (int l = 1; l < size(); l++)
        {
            if (shorterThen(thiss[l-1]-thiss[l], remove_length))
            {
                l++; // skip the next line piece (dont escalate the removal of edges)
                if (l < size())
                    poly->push_back(thiss[l]);
            } else poly->push_back(thiss[l]);
        }
        return new_poly;
    }

    PolygonRef simplify(int allowed_error_distance_squared) //!< removes consecutive line segments with same orientation
    {
        PolygonRef thiss = *this;
        ClipperLib::Path* poly = new ClipperLib::Path();
        PolygonRef new_poly(*poly);
        Point& last = thiss[size()-1];
        for (int l = 0; l < size(); l++)
        {
            /*
             *    /|
             * c / | a
             *  /__|
             *  \ b|
             * e \ | d
             *    \|
             * 
             * b^2 = c^2 - a^2
             * b^2 = e^2 - d^2
             * 
             * approximately: 
             * a/d = c/e
             * a/(a+d) = c/(c+e)
             * a^2 / (a+d)^2 = c^2 / (c+e)^2
             * a^2 = c^2 * (a+d)^2/ (c+e)^2
             * 
             */
            if ( vSize2(thiss[l]-last) < allowed_error_distance_squared )
                continue;
            
            Point& next = thiss[(l+1) % size()];
            auto square = [](double in) { return in*in; };
            int64_t a2 = vSize2(next-thiss[l]) * vSize2(next-last) /  static_cast<int64_t>(square(vSizeMM(next-last) + vSizeMM(thiss[l]-last))*1000*1000);
            
            int64_t error2 = vSize2(next-thiss[l]) - a2;
            if (error2 < allowed_error_distance_squared)
            {
                // don't add the point to the result
            } else 
            {
                poly->push_back(thiss[l]);
                last = thiss[l];
            }
        }
        return new_poly;
    }
        
    ClipperLib::Path::iterator begin()
    {
        return polygon->begin();
    }

    ClipperLib::Path::iterator end()
    {
        return polygon->end();
    }

    ClipperLib::Path::const_iterator begin() const
    {
        return polygon->begin();
    }

    ClipperLib::Path::const_iterator end() const
    {
        return polygon->end();
    }


    friend class Polygons;
    friend class Polygon;
};

class Polygon : public PolygonRef
{
    ClipperLib::Path poly;
public:
    Polygon()
    : PolygonRef(poly)
    {
    }

    Polygon(const PolygonRef& other)
    : PolygonRef(poly)
    {
        poly = *other.polygon;
    }
};

class Polygons
{
private:
    ClipperLib::Paths polygons;
public:
    unsigned int size() const
    {
        return polygons.size();
    }

    PolygonRef operator[] (unsigned int index)
    {
        POLY_ASSERT(index < size());
        return PolygonRef(polygons[index]);
    }
    void remove(unsigned int index)
    {
        POLY_ASSERT(index < size());
        polygons.erase(polygons.begin() + index);
    }
    void clear()
    {
        polygons.clear();
    }
    void add(const PolygonRef& poly)
    {
        polygons.push_back(*poly.polygon);
    }
    void add(const Polygons& other)
    {
        for(unsigned int n=0; n<other.polygons.size(); n++)
            polygons.push_back(other.polygons[n]);
    }
    PolygonRef newPoly()
    {
        polygons.push_back(ClipperLib::Path());
        return PolygonRef(polygons[polygons.size()-1]);
    }

    Polygons() {}
    Polygons(const Polygons& other) { polygons = other.polygons; }
    Polygons& operator=(const Polygons& other) { polygons = other.polygons; return *this; }
    Polygons difference(const Polygons& other) const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.AddPaths(other.polygons, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctDifference, ret.polygons);
        return ret;
    }
    Polygons unionPolygons(const Polygons& other) const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.AddPaths(other.polygons, ClipperLib::ptSubject, true);
        clipper.Execute(ClipperLib::ctUnion, ret.polygons, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
        return ret;
    }
    Polygons intersection(const Polygons& other) const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.AddPaths(other.polygons, ClipperLib::ptClip, true);
        clipper.Execute(ClipperLib::ctIntersection, ret.polygons);
        return ret;
    }
    Polygons offset(int distance, ClipperLib::JoinType joinType = ClipperLib::jtMiter) const
    {
        Polygons ret;
        ClipperLib::ClipperOffset clipper(2.0, 10.0);
        clipper.AddPaths(polygons, joinType, ClipperLib::etClosedPolygon);
        clipper.MiterLimit = 2.0;
        clipper.Execute(ret.polygons, distance);
        return ret;
    }
    Polygons smooth(int remove_length, int min_area) //!< removes points connected to small lines
    {
        Polygons ret;
        for (unsigned int p = 0; p < size(); p++)
        {
            PolygonRef poly(polygons[p]);
            if (poly.area() < min_area || poly.size() <= 5) // when optimally removing, a poly with 5 pieces results in a triangle. Smaller polys dont have area!
            {
                ret.add(poly);
                continue;
            }
            
            if (poly.size() == 0)
                continue;
            if (poly.size() == 2)
                ret.add(poly);
            else 
                ret.add(poly.smooth(remove_length));

        }
        return ret;
    }
    Polygons simplify(int allowed_error_distance_squared) //!< removes points connected to small lines
    {
        Polygons ret;
        for (unsigned int p = 0; p < size(); p++)
        {
            ret.add((*this)[p].simplify(allowed_error_distance_squared));
        }
        return ret;
    }
    std::vector<Polygons> splitIntoParts(bool unionAll = false) const
    {
        std::vector<Polygons> ret;
        ClipperLib::Clipper clipper(clipper_init);
        ClipperLib::PolyTree resultPolyTree;
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        if (unionAll)
            clipper.Execute(ClipperLib::ctUnion, resultPolyTree, ClipperLib::pftNonZero, ClipperLib::pftNonZero);
        else
            clipper.Execute(ClipperLib::ctUnion, resultPolyTree);

        _processPolyTreeNode(&resultPolyTree, ret);
        return ret;
    }
private:
    void _processPolyTreeNode(ClipperLib::PolyNode* node, std::vector<Polygons>& ret) const
    {
        for(int n=0; n<node->ChildCount(); n++)
        {
            ClipperLib::PolyNode* child = node->Childs[n];
            Polygons polygons;
            polygons.add(child->Contour);
            for(int i=0; i<child->ChildCount(); i++)
            {
                polygons.add(child->Childs[i]->Contour);
                _processPolyTreeNode(child->Childs[i], ret);
            }
            ret.push_back(polygons);
        }
    }
public:
    Polygons processEvenOdd() const
    {
        Polygons ret;
        ClipperLib::Clipper clipper(clipper_init);
        clipper.AddPaths(polygons, ClipperLib::ptSubject, true);
        clipper.Execute(ClipperLib::ctUnion, ret.polygons);
        return ret;
    }

    int64_t polygonLength() const
    {
        int64_t length = 0;
        for(unsigned int i=0; i<polygons.size(); i++)
        {
            Point p0 = polygons[i][polygons[i].size()-1];
            for(unsigned int n=0; n<polygons[i].size(); n++)
            {
                Point p1 = polygons[i][n];
                length += vSize(p0 - p1);
                p0 = p1;
            }
        }
        return length;
    }
    
    Point min() const
    {
        Point ret = Point(POINT_MAX, POINT_MAX);
        for(const ClipperLib::Path& polygon : polygons)
        {
            for(Point p : polygon)
            {
                ret.X = std::min(ret.X, p.X);
                ret.Y = std::min(ret.Y, p.Y);
            }
        }
        return ret;
    }
    
    Point max() const
    {
        Point ret = Point(POINT_MIN, POINT_MIN);
        for(const ClipperLib::Path& polygon : polygons)
        {
            for(Point p : polygon)
            {
                ret.X = std::max(ret.X, p.X);
                ret.Y = std::max(ret.Y, p.Y);
            }
        }
        return ret;
    }

    bool inside(Point p)
    {
        if (size() < 1)
            return false;
        if (!(*this)[0].inside(p))
            return false;
        for(unsigned int n=1; n<polygons.size(); n++)
        {
            if ((*this)[n].inside(p))
                return false;
        }
        return true;
    }

    void applyMatrix(const PointMatrix& matrix)
    {
        for(unsigned int i=0; i<polygons.size(); i++)
        {
            for(unsigned int j=0; j<polygons[i].size(); j++)
            {
                polygons[i][j] = matrix.apply(polygons[i][j]);
            }
        }
    }
};

/* Axis aligned boundary box */
class AABB
{
public:
    Point min, max;

    AABB()
    : min(POINT_MIN, POINT_MIN), max(POINT_MIN, POINT_MIN)
    {
    }
    AABB(Polygons polys)
    : min(POINT_MIN, POINT_MIN), max(POINT_MIN, POINT_MIN)
    {
        calculate(polys);
    }

    void calculate(Polygons polys)
    {
        min = Point(POINT_MAX, POINT_MAX);
        max = Point(POINT_MIN, POINT_MIN);
        for(unsigned int i=0; i<polys.size(); i++)
        {
            for(unsigned int j=0; j<polys[i].size(); j++)
            {
                if (min.X > polys[i][j].X) min.X = polys[i][j].X;
                if (min.Y > polys[i][j].Y) min.Y = polys[i][j].Y;
                if (max.X < polys[i][j].X) max.X = polys[i][j].X;
                if (max.Y < polys[i][j].Y) max.Y = polys[i][j].Y;
            }
        }
    }

    bool hit(const AABB& other) const
    {
        if (max.X < other.min.X) return false;
        if (min.X > other.max.X) return false;
        if (max.Y < other.min.Y) return false;
        if (min.Y > other.max.Y) return false;
        return true;
    }
};

}//namespace cura

#endif//UTILS_POLYGON_H
