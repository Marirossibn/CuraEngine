#include "wallOverlap.h"

#include <stdio.h> // for output to HTML
#include <cmath> // isfinite

#include "debug.h"

namespace cura 
{
    
void WallOverlapComputation::findOverlapPoints()
{
    for (unsigned int poly_idx = 0; poly_idx < list_polygons.size(); poly_idx++)
    {
        ListPolygon& poly = list_polygons[poly_idx];
        for (unsigned int poly2_idx = 0; poly2_idx <= poly_idx; poly2_idx++)
        {
            for (ListPolygon::iterator it = poly.begin(); it != poly.end(); ++it)
            {
                ListPolyIt lpi(poly, it);
                if (poly_idx == poly2_idx)
                {
//                     ListPolygon::iterator it2(it);
//                     ++it2;
//                     if (it2 != poly.end())
                    {
                        findOverlapPoints(lpi, poly2_idx, it);
                    }
                }
                else 
                {
                    findOverlapPoints(lpi, poly2_idx);
                }
            }
        }
    }
}


    
void WallOverlapComputation::convertPolygonsToLists(Polygons& polys, ListPolygons& result)
{
    for (PolygonRef poly : polys)
    {
        result.emplace_back();
        for (Point& p : poly) 
        {
            result.back().push_back(p);
        }
    }
}    

void WallOverlapComputation::convertListPolygonsToPolygons(ListPolygons& list_polygons, Polygons& polygons)
{
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
    {
        polygons[poly_idx].clear();
        for (Point& p : list_polygons[poly_idx])
        {
            polygons[poly_idx].add(p);
        }
    }
}
    
void WallOverlapComputation::findOverlapPoints(ListPolyIt from, unsigned int to_list_poly_idx)
{
    findOverlapPoints(from, to_list_poly_idx, list_polygons[to_list_poly_idx].begin());
}

void WallOverlapComputation::findOverlapPoints(ListPolyIt from_it, unsigned int to_list_poly_idx, const ListPolygon::iterator start)
{
    ListPolygon& to_list_poly = list_polygons[to_list_poly_idx];
    Point& from = from_it.p();
    ListPolygon::iterator last_it = to_list_poly.end();
    last_it--;
    for (ListPolygon::iterator it = start; it != to_list_poly.end(); ++it)
    {
        Point& last_point = *last_it;
        Point& point = *it;
        
        if ( from_it.poly == to_list_poly 
            && (
                (from_it.it == last_it || from_it.it == it) // we currently consider a linesegment directly connected to [from]
                || (from_it.prev().it == it || from_it.next().it == last_it) // line segment from [last_point] to [point] is connected to line segment of which [from] is the other end
                ) 
           )
        { 
            last_it = it;
            continue;
        }
        Point closest = getClosestOnLine(from, last_point, point);
        
        int64_t dist2 = vSize2(closest - from);
        
        if (dist2 > line_width * line_width
            || ( from_it.poly == to_list_poly 
                && dot(from_it.next().p() - from, point - last_point) > 0 
                && dot(from - from_it.prev().p(), point - last_point) > 0  ) // line segments are likely connected, because the winding order is in the same general direction
        )
        { // line segment too far away to have overlap
            last_it = it;
            continue;
        }
        
        int64_t dist = sqrt(dist2);
        
        if (closest == last_point)
        {
            addOverlapPoint(from_it, ListPolyIt(to_list_poly, last_it), dist);
        }
        else if (closest == point)
        {
            addOverlapPoint(from_it, ListPolyIt(to_list_poly, it), dist);
        }
        else 
        {
            ListPolygon::iterator new_it = to_list_poly.insert(it, closest);
            addOverlapPoint(from_it, ListPolyIt(to_list_poly, new_it), dist);
        }
        
        last_it = it;
    }
    
}


bool WallOverlapComputation::addOverlapPoint(ListPolyIt from, ListPolyIt to, int64_t dist)
{
    WallOverlapPointLink link(from, to);
    WallOverlapPointLinkAttributes attr(dist, false);
    std::pair<WallOverlapPointLinks::iterator, bool> result =
        overlap_point_links.emplace(link, attr);
        
    if (! result.second)
    {
//         DEBUG_PRINTLN("couldn't emplace in overlap_point_links! : ");
        result.first->second = attr;
    }
    
    WallOverlapPointLinks::iterator it = result.first;
    addToPoint2LinkMap(*it->first.a.it, it);
    addToPoint2LinkMap(*it->first.b.it, it);
    
    
    return result.second;
}

bool WallOverlapComputation::addOverlapPoint_endings(ListPolyIt from, ListPolyIt to, int64_t dist)
{
    WallOverlapPointLink link(from, to);
    WallOverlapPointLinkAttributes attr(dist, false);
    std::pair<WallOverlapPointLinks::iterator, bool> result =
        overlap_point_links_endings.emplace(link, attr);
        
    if (! result.second)
    {
//         DEBUG_PRINTLN("couldn't emplace in overlap_point_links! : ");
        result.first->second = attr;
    }
    
    WallOverlapPointLinks::iterator it = result.first;
    addToPoint2LinkMap(*it->first.a.it, it);
    addToPoint2LinkMap(*it->first.b.it, it);
    
    
    return result.second;
}

void WallOverlapComputation::addOverlapEndings()
{
    for (std::pair<WallOverlapPointLink, WallOverlapPointLinkAttributes> link_pair : overlap_point_links)
    {
            bool debug_now = (link_pair.first.b.p() == Point(144601, 92500) && link_pair.first.a.p() == Point(144800, 92512)) 
                          || (link_pair.first.a.p() == Point(144601, 92500) && link_pair.first.b.p() == Point(144800, 92512));
        if (debug_now)
        {
            std::cerr << " now" << std::endl;
            DEBUG_PRINTLN(" debuggin !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1");
            DEBUG_SHOW(link_pair.second.dist);
        }
        if (link_pair.second.dist == line_width)
        { // its ending itself
            DEBUG_PRINTLN("is an ending itself! skipping...");
            continue;
        }
        WallOverlapPointLink& link = link_pair.first;
        const ListPolyIt& a_1 = link.a;
        const ListPolyIt& b_1 = link.b;
        // an overlap segment can be an ending in two directions
        { 
            DEBUG_PRINTLN("first dir end");
            ListPolyIt a_2 = a_1.next();
            ListPolyIt b_2 = b_1.prev();
            addOverlapEnding(link_pair, a_2, b_2, a_2, b_1);
        }
        { 
            DEBUG_PRINTLN("second dir end");
            ListPolyIt a_2 = a_1.prev();
            ListPolyIt b_2 = b_1.next();
            addOverlapEnding(link_pair, a_2, b_2, a_1, b_2);
        }
    }
}

void WallOverlapComputation::addOverlapEnding(std::pair<WallOverlapPointLink, WallOverlapPointLinkAttributes> link_pair, const ListPolyIt& a2_it, const ListPolyIt& b2_it, const ListPolyIt& a_after_middle, const ListPolyIt& b_after_middle)
{
    WallOverlapPointLink& link = link_pair.first;
    Point& a1 = link.a.p();
    Point& a2 = a2_it.p();
    Point& b1 = link.b.p();
    Point& b2 = b2_it.p();
    Point a = a2-a1;
    Point b = b2-b1;

    bool debug_now = (b1 == Point(144601, 92500) && a1 == Point(144800, 92512));
    if (debug_now)
    {
            std::cerr << " now" << std::endl;
        DEBUG_PRINTLN("debugging now <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<");
        DEBUG_SHOW(a1);
        DEBUG_SHOW(a2);
        DEBUG_SHOW(b1);
        DEBUG_SHOW(b2);
    }
    if (point_to_link.find(a2_it.p()) == point_to_link.end() 
        || point_to_link.find(b2_it.p()) == point_to_link.end())
    {
        endings.push_back(a1);
        endings.push_back(b1);
        int64_t dist = overlapEndingDistance(a1, a2, b1, b2, link_pair.second.dist);
        if (debug_now)
        {
            DEBUG_SHOW(dist);
        }
        if (dist < 0) { return; }
        int64_t a_length2 = vSize2(a);
        int64_t b_length2 = vSize2(b);
        if (dist*dist > std::min(a_length2, b_length2) )
        { // TODO remove this /\ case if error below is never shown
            DEBUG_PRINTLN("Next point should have been linked already!!");
//             DEBUG_SHOW(dist);
//             DEBUG_SHOW(std::sqrt(a_length2));
//             DEBUG_SHOW(std::sqrt(b_length2));
//             DEBUG_SHOW(b2);
//             DEBUG_SHOW(b1);
//             DEBUG_SHOW(link.b.next().p());
//             DEBUG_SHOW(link.b.prev().p());
//             DEBUG_SHOW( (link.b.it == b2_it.it) );
//             DEBUG_SHOW( (link.b.poly == b2_it.poly) );
            dist = std::sqrt(std::min(a_length2, b_length2));
            if (a_length2 < b_length2)
            {
                Point b_p = b1 + normal(b, dist);
                ListPolygon::iterator new_b = link.b.poly.insert(b_after_middle.it, b_p);
                endings_linked.emplace_back((a2_it.p()+ *new_b)/2, (a1+b1)/2);
                addOverlapPoint_endings(a2_it, ListPolyIt(link.b.poly, new_b), line_width);
            }
            else if (b_length2 < a_length2)
            {
                Point a_p = a1 + normal(a, dist);
                ListPolygon::iterator new_a = link.a.poly.insert(a_after_middle.it, a_p);
                endings_linked.emplace_back((b2_it.p()+ *new_a)/2, (a1+b1)/2);
                addOverlapPoint_endings(ListPolyIt(link.a.poly, new_a), b2_it, line_width);
            }
            else // equal
            {
                endings_linked.emplace_back((b2_it.p() + a2_it.p())/2, (a1+b1)/2);
                addOverlapPoint_endings(a2_it, b2_it, line_width);
            }
        }
        if (dist > 0)
        {
            Point a_p = a1 + normal(a, dist);
            ListPolygon::iterator new_a = link.a.poly.insert(a_after_middle.it, a_p);
            Point b_p = b1 + normal(b, dist);
            ListPolygon::iterator new_b = link.b.poly.insert(b_after_middle.it, b_p);
            endings_linked.emplace_back((*new_b + *new_a)/2, (a1+b1)/2);
            addOverlapPoint_endings(ListPolyIt(link.a.poly, new_a), ListPolyIt(link.b.poly, new_b), line_width);
        }
        else if (dist == 0)
        {
            addOverlapPoint_endings(link.a, link.b, line_width);
            DEBUG_PRINTLN("dist == 0! !"); // TODO remove line 
            endings_special.push_back(a1); // TODO remove line 
            endings_special.push_back(b1); // TODO remove line 
        }
        else 
        { // TODO: remove case
            DEBUG_PRINTLN("WTF!" );
            DEBUG_SHOW(dist);
        }
        if (debug_now)
        {
            std::cerr << " now" << std::endl;
            DEBUG_PRINTLN(">>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>");
        }
    }
}

int64_t WallOverlapComputation::overlapEndingDistance(Point& a1, Point& a2, Point& b1, Point& b2, int a1b1_dist)
{
    int overlap = line_width - a1b1_dist;
    Point a = a2-a1;
    Point b = b2-b1;
    double cos_angle = INT2MM2(dot(a, b)) / vSizeMM(a) / vSizeMM(b);
    // result == .5*overlap / tan(.5*angle) == .5*overlap / tan(.5*acos(cos_angle)) 
    // [wolfram alpha] == 0.5*overlap * sqrt(cos_angle+1)/sqrt(1-cos_angle)
    // [assuming positive x] == 0.5*overlap / sqrt( 2 / (cos_angle + 1) - 1 ) 
    if (cos_angle <= 0
        || ! std::isfinite(cos_angle) )
    {
        return -1; // line_width / 2;
    }
    else if (cos_angle > .9999) // values near 1 can lead too large numbers  for 1/x
    {
        return std::min(vSize(b), vSize(a));
    }
    else 
    {
        int64_t dist = overlap * double ( 1.0 / (2.0 * sqrt(2.0 / (cos_angle+1.0) - 1.0)) );
        if (dist < 0)
        {
            DEBUG_SHOW(overlap);
            DEBUG_SHOW(2.0 / (cos_angle+1.0) );
            DEBUG_SHOW( sqrt(2.0 / (cos_angle+1.0) - 1.0) );
            DEBUG_SHOW(1.0 / (2.0 * sqrt(2.0 / (cos_angle+1.0) - 1.0)));
        }
        return dist;
    }
    
}

void WallOverlapComputation::addSharpCorners()
{
    
}
    
void WallOverlapComputation::addToPoint2LinkMap(Point p, WallOverlapPointLinks::iterator it)
{
    point_to_link.emplace(p, it);
    // TODO: what to do if the map already contained a link? > three-way overlap
}

float WallOverlapComputation::getFlow(Point& from, Point& to)
{
    Point2Link::iterator from_link_pair = point_to_link.find(from);
    if (from_link_pair == point_to_link.end()) { return 1; }
    WallOverlapPointLinks::iterator from_link = from_link_pair->second;
    if (!from_link->second.passed)// || !to_link->second.passed)
    {
        from_link->second.passed = true;
//         to_link->second.passed = true;
        return 1;
    }
    from_link->second.passed = true;
    
    Point2Link::iterator to_link_pair = point_to_link.find(to);
    if (to_link_pair == point_to_link.end()) { return 1; }
    WallOverlapPointLinks::iterator to_link = to_link_pair->second;
//     to_link->second = true;
    
    
    // both points have already been passed
    
    float avg_link_dist = 0.5 * ( INT2MM(from_link->second.dist) + INT2MM(to_link->second.dist) );
   
    float ratio = avg_link_dist / INT2MM(line_width);

    if (ratio > 1.0) { return 1.0; }
    
    return ratio;
}



void WallOverlapComputation::debugCheck()
{
    for (std::pair<WallOverlapPointLink, WallOverlapPointLinkAttributes> pair : overlap_point_links)
    {
        if (std::abs(vSize( pair.first.a.p() - pair.first.b.p()) - pair.second.dist) > 10)
            DEBUG_PRINTLN(vSize( pair.first.a.p() - pair.first.b.p())<<" != " << pair.second.dist);
        
    }
}


void WallOverlapComputation::wallOverlaps2HTML(const char* filename)
{
    
    DEBUG_PRINTLN(" >>>>>>>>>>>>>>>>> wallOverlaps2HTML <<<<<<<<<<<<<<<<<< ");
    FILE* out = fopen(filename, "w");
    fprintf(out, "<!DOCTYPE html><html><body>");
    
    int canvasSize = 5000;
    
    
    Point min = polygons.min();
    Point size = polygons.max() - min;
    
    auto transform = [&](Point& p) { return (p-min)*canvasSize/size + Point(10,10); };
    
    fprintf(out, "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" style=\"width: %dpx; height:%dpx\">\n", canvasSize+20, canvasSize+20);
    
    for(PolygonsPart part : polygons.splitIntoParts())
    {
        for (unsigned int j = 0; j < part.size(); j++)
        {
            fprintf(out, "<polygon points=\"");
            for(Point& p : part[j])
            {
                Point pf = transform(p);
                fprintf(out, "%lli,%lli ", pf.Y, pf.X);
            }
            if (j == 0)
                fprintf(out, "\" style=\"fill:gray; stroke:black;stroke-width:1\" />\n");
            else
                fprintf(out, "\" style=\"fill:white; stroke:black;stroke-width:1\" />\n");
        }
    }
    
    for (ListPolygon poly : list_polygons)
    {
        int pp = 0;
        for (Point& p : poly)
        {
            pp++;
            Point pf = transform(p);
            fprintf(out, "<circle cx=\"%lli\" cy=\"%lli\" r=\"%d\" stroke=\"black\" stroke-width=\"1\" fill=\"black\" />",pf.Y, pf.X, 1);
            
            // number:
//             fprintf(out, "<text x=\"%lli\" y=\"%lli\" style=\"font-size: 10;\" fill=\"black\">%lli</text>",pf.Y, pf.X, pp);
            // coords:
            fprintf(out, "<text x=\"%lli\" y=\"%lli\" style=\"font-size: 10;\" fill=\"black\">%lli,%lli</text>",pf.Y, pf.X, p.X, p.Y, pp);
            
        }
    }
        
    for (Point& p : endings)
    {
        Point pf = transform(p);
        fprintf(out, "<circle cx=\"%lli\" cy=\"%lli\" r=\"%d\" stroke=\"blue\" stroke-width=\"1\" fill=\"black\" />",pf.Y, pf.X, 1);
    }
        
    for (Point& p : endings_special)
    {
        Point pf = transform(p);
        fprintf(out, "<circle cx=\"%lli\" cy=\"%lli\" r=\"%d\" stroke=\"blue\" stroke-width=\"1\" fill=\"yellow\" />",pf.Y, pf.X, 1);
    }
    
    for (std::pair<Point , Point> pair : endings_linked)
    {
        Point a = transform(pair.first);
        Point b = transform(pair.second);
        fprintf(out, "<line x1=\"%lli\" y1=\"%lli\" x2=\"%lli\" y2=\"%lli\" style=\"stroke:rgb(0,0,255);stroke-width:1\" />", a.Y, a.X, b.Y, b.X);
    }
    
    for (std::pair<WallOverlapPointLink , WallOverlapPointLinkAttributes> link_pair : overlap_point_links)
    {
        WallOverlapPointLink& link = link_pair.first;
            bool debug_now = (link_pair.first.b.p() == Point(144601, 92500) && link_pair.first.a.p() == Point(144800, 92512)) 
                          || (link_pair.first.a.p() == Point(144601, 92500) && link_pair.first.b.p() == Point(144800, 92512));
        if (debug_now)
        {
            std::cerr << " now" << std::endl;
            DEBUG_PRINTLN(" debuggin !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1");
            DEBUG_SHOW(link_pair.second.dist);
        }
        Point a = transform(link.a.p());
        Point b = transform(link.b.p());
        fprintf(out, "<line x1=\"%lli\" y1=\"%lli\" x2=\"%lli\" y2=\"%lli\" style=\"stroke:rgb(%d,%d,0);stroke-width:1\" />", a.Y, a.X, b.Y, b.X, link_pair.second.dist == line_width? 0 : 255, link_pair.second.dist==line_width? 255 : 0);
    }
    
    for (std::pair<WallOverlapPointLink , WallOverlapPointLinkAttributes> link_pair : overlap_point_links_endings)
    {
        WallOverlapPointLink& link = link_pair.first;
            bool debug_now = (link_pair.first.b.p() == Point(144601, 92500) && link_pair.first.a.p() == Point(144800, 92512)) 
                          || (link_pair.first.a.p() == Point(144601, 92500) && link_pair.first.b.p() == Point(144800, 92512));
        if (debug_now)
        {
            std::cerr << " now" << std::endl;
            DEBUG_PRINTLN(" debuggin !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!1");
            DEBUG_SHOW(link_pair.second.dist);
        }
        Point a = transform(link.a.p());
        Point b = transform(link.b.p());
        fprintf(out, "<line x1=\"%lli\" y1=\"%lli\" x2=\"%lli\" y2=\"%lli\" style=\"stroke:rgb(%d,%d,0);stroke-width:1\" />", a.Y, a.X, b.Y, b.X, link_pair.second.dist == line_width? 0 : 255, link_pair.second.dist==line_width? 255 : 0);
    }
    
    fprintf(out, "</svg>\n");
    
    
    fprintf(out, "</body></html>");
    fclose(out);
    
    DEBUG_PRINTLN(" \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\ wallOverlaps2HTML ////////////////// ");
}
    
} // namespace cura 
