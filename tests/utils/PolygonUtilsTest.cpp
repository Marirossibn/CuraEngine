//Copyright (c) 2019 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <gtest/gtest.h>

#include <../src/utils/IntPoint.h> //Creating and testing with points.
#include <../src/utils/polygon.h> //Creating polygons to test with.
#include <../src/utils/polygonUtils.h> //The class under test.

namespace cura
{

struct MoveInsideParameters
{
    Point close_to;
    coord_t distance;
    Point supposed;

    MoveInsideParameters(Point close_to, const coord_t distance, Point supposed)
    : close_to(close_to)
    , distance(distance)
    , supposed(supposed)
    {
    }
};

class MoveInsideTest : public testing::TestWithParam<MoveInsideParameters>
{
public:
    Polygon test_square;

    void SetUp()
    {
        test_square.emplace_back(0, 0);
        test_square.emplace_back(100, 0);
        test_square.emplace_back(100, 100);
        test_square.emplace_back(0, 100);
    }
};

TEST_P(MoveInsideTest, MoveInside)
{
    const MoveInsideParameters parameters = GetParam();
    const ClosestPolygonPoint cpp = PolygonUtils::findClosest(parameters.close_to, test_square);
    Point result = PolygonUtils::moveInside(cpp, parameters.distance);
    ASSERT_LE(vSize(result - parameters.supposed), 10)
        << parameters.close_to << " moved with " << parameters.distance << " micron inside to " << result << " rather than " << parameters.supposed << ".\n"
        << "\tPS: dist to boundary computed = " << vSize(cpp.location - result) << "; vs supposed = " << vSize(cpp.location - parameters.supposed) << ".\n"
        << "\tclosest_point = " << cpp.location << " at index " << cpp.point_idx << ".";
}

TEST_P(MoveInsideTest, MoveInside2)
{
    const MoveInsideParameters parameters = GetParam();
    Polygons polys;
    polys.add(test_square);
    Point result = parameters.close_to;
    PolygonUtils::moveInside2(polys, result, parameters.distance);
    ASSERT_LE(vSize(result - parameters.supposed), 10) << parameters.close_to << " moved with " << parameters.distance << " micron inside to " << result << "rather than " << parameters.supposed << ".";
}

INSTANTIATE_TEST_SUITE_P(MoveInsideInstantiation, MoveInsideTest, testing::Values(
    MoveInsideParameters(Point(110, 110), 28, Point(80, 80)), //Near a corner, moving inside.
    MoveInsideParameters(Point(50, 110), 20, Point(50, 80)), //Near an edge, moving inside.
    MoveInsideParameters(Point(110, 110), -28, Point(120, 120)), //Near a corner, moving outside.
    MoveInsideParameters(Point(50, 110), -20, Point(50, 120)), //Near an edge, moving outside.
    MoveInsideParameters(Point(110, 105), 28, Point(80, 80)), //Near a corner but not exactly diagonal.
    MoveInsideParameters(Point(100, 50), 20, Point(80, 50)), //Starting on the border.
    MoveInsideParameters(Point(80, 50), 20, Point(80, 50)), //Already inside.
    MoveInsideParameters(Point(110, 50), 0, Point(100, 50)), //Not keeping any distance from the border.
    MoveInsideParameters(Point(110, 50), 100000, Point(-99900, 50)) //A very far move.
));

TEST_F(MoveInsideTest, cornerEdgeTest)
{
    const Point close_to(110, 100);
    const Point supposed1(80, 80); //Allow two possible values here, since the behaviour for this edge case is not specified.
    const Point supposed2(72, 100);
    constexpr coord_t distance = 28;
    const ClosestPolygonPoint cpp = PolygonUtils::findClosest(close_to, test_square);
    const Point result = PolygonUtils::moveInside(cpp, distance);

    constexpr coord_t maximum_error = 10;
    ASSERT_TRUE(vSize(result - supposed1) <= maximum_error || vSize(result - supposed2) <= maximum_error)
        << close_to << " moved with " << distance << " micron inside to " << result << " rather than " << supposed1 << " or " << supposed2 << ".\n"
        << "\tPS: dist to boundary computed = " << vSize(cpp.location - result) << "; vs supposed = " << vSize(cpp.location - supposed1) << " or " << vSize(cpp.location - supposed2) << ".\n"
        << "\tclosest point = " << cpp.location << " at index " << cpp.point_idx << ".";
}

TEST_F(MoveInsideTest, middleTest)
{
    const Point close_to(50, 50);
    const Point supposed1(80, 50); //Allow four possible values here, since the behaviour for this edge case is not specified.
    const Point supposed2(50, 80);
    const Point supposed3(20, 50);
    const Point supposed4(50, 20);
    constexpr coord_t distance = 20;
    const ClosestPolygonPoint cpp = PolygonUtils::findClosest(close_to, test_square);
    const Point result = PolygonUtils::moveInside(cpp, distance);

    constexpr coord_t maximum_error = 10;
    ASSERT_TRUE(vSize(result - supposed1) <= maximum_error || vSize(result - supposed2) <= maximum_error || vSize(result - supposed3) <= maximum_error || vSize(result - supposed4) <= maximum_error)
        << close_to << " moved with " << distance << " micron inside to " << result << " rather than " << supposed1 << ", " << supposed2 << ", " << supposed3 << " or " << supposed4 << ".\n"
        << "\tPS: dist to boundary computed = " << vSize(cpp.location - result) << "; vs supposed = " << vSize(cpp.location - supposed1) << ", " << vSize(cpp.location - supposed2) << ", " << vSize(cpp.location - supposed3) << " or " << vSize(cpp.location - supposed4) << ".\n"
        << "\tclosest point = " << cpp.location << " at index " << cpp.point_idx << ".";
}

TEST_F(MoveInsideTest, middleTestPenalty)
{
    const Point close_to(50, 50);
    const Point supposed(80, 50); 
    const Point preferred_dir(120, 60);
    constexpr coord_t distance = 20;
    const ClosestPolygonPoint cpp = PolygonUtils::findClosest(close_to, test_square, [preferred_dir](Point candidate) { return vSize2(candidate - preferred_dir); } );
    const Point result = PolygonUtils::moveInside(cpp, distance);

    ASSERT_LE(vSize(result - supposed), 10)
        << close_to << " moved with " << distance << " micron inside to " << result << " rather than " << supposed << ".\n"
        << "\tPS: dist to boundary computed = " << vSize(cpp.location - result) << "; vs supposed = " << vSize(cpp.location - supposed) << ".\n"
        << "\tclosest point = " << cpp.location << " at index " << cpp.point_idx << ".";
}

TEST_F(MoveInsideTest, cornerEdgeTest2)
{
    const Point close_to(110, 100);
    const Point supposed1(80, 80); //Allow two possible values here, since the behaviour for this edge case is not specified.
    const Point supposed2(72, 100);
    constexpr coord_t distance = 28;
    Polygons polys;
    polys.add(test_square);
    Point result = close_to;
    PolygonUtils::moveInside2(polys, result, distance);

    constexpr coord_t maximum_error = 10;
    ASSERT_TRUE(vSize(result - supposed1) <= maximum_error || vSize(result - supposed2) <= maximum_error)
        << close_to << " moved with " << distance << " micron inside to " << result << " rather than " << supposed1 << " or " << supposed2 << ".";
}

struct FindCloseParameters
{
    Point close_to;
    Point supposed;
    coord_t cell_size;
    std::function<int(Point)>* penalty_function;

    FindCloseParameters(const Point close_to, const Point supposed, const coord_t cell_size, std::function<int(Point)>* penalty_function = nullptr)
    : close_to(close_to)
    , supposed(supposed)
    , cell_size(cell_size)
    , penalty_function(penalty_function)
    {
    }
};

class FindCloseTest : public testing::TestWithParam<FindCloseParameters>
{
public:
    Polygon test_square;

    void SetUp()
    {
        test_square.emplace_back(0, 0);
        test_square.emplace_back(100, 0);
        test_square.emplace_back(100, 100);
        test_square.emplace_back(0, 100);
    }
};

TEST_P(FindCloseTest, FindClose)
{
    const FindCloseParameters parameters = GetParam();
    Polygons polygons;
    polygons.add(test_square);
    SparseLineGrid<PolygonsPointIndex, PolygonsPointIndexSegmentLocator>* loc_to_line = PolygonUtils::createLocToLineGrid(polygons, parameters.cell_size);

    std::optional<ClosestPolygonPoint> cpp;
    if (parameters.penalty_function)
    {
        cpp = PolygonUtils::findClose(parameters.close_to, polygons, *loc_to_line, *parameters.penalty_function);
    }
    else
    {
        cpp = PolygonUtils::findClose(parameters.close_to, polygons, *loc_to_line);
    }

    if (cpp)
    {
        const Point result = cpp->location;
        ASSERT_LE(vSize(result - parameters.supposed), 10) << "Close to " << parameters.close_to << " we found " << result << " rather than " << parameters.supposed << ".\n";
    }
    else
    {
        FAIL() << "Couldn't find anything close to " << parameters.close_to << " (should have been " << parameters.supposed << ").\n";
    }
}

/*
 * Test penalty function to use with findClose.
 */
std::function<int(Point)> testPenalty([](Point candidate)
{
   return -vSize2(candidate - Point(50, 100)); //The further from 50, 100, the lower the penalty.
});

INSTANTIATE_TEST_SUITE_P(FindCloseInstantiation, FindCloseTest, testing::Values(
    FindCloseParameters(Point(110, 110), Point(100, 100), 15), //Near a corner.
    FindCloseParameters(Point(50, 110), Point(50, 100), 15), //Near a side.
    FindCloseParameters(Point(50, 50), Point(50, 0), 60, &testPenalty) //Using a penalty function.
));

void PolygonUtilsTest::moveInsidePointyCornerTest()
{
    Point from(55, 170); // above pointy bit
    Point result(from);
    Polygons inside;
    inside.add(pointy_square);
    ClosestPolygonPoint cpp = PolygonUtils::ensureInsideOrOutside(inside, result, 10);
    if (cpp.point_idx == NO_INDEX || cpp.poly_idx == NO_INDEX)
    {
        std::stringstream ss;
        ss << "Couldn't ensure point inside close to " << from << ".\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), false);
    }
    else
    {
        std::stringstream ss;
        ss << from << " couldn't be moved inside.\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), inside.inside(result));
    }
}

void PolygonUtilsTest::moveInsidePointyCornerTestFail()
{ // should fail with normal moveInside2 (and the like)
    Point from(55, 170); // above pointy bit
    Point result(from);
    Polygons inside;
    inside.add(pointy_square);
    ClosestPolygonPoint cpp = PolygonUtils::moveInside2(inside, result, 10);
    if (cpp.point_idx == NO_INDEX || cpp.poly_idx == NO_INDEX)
    {
        std::stringstream ss;
        ss << "Couldn't ensure point inside close to " << from << ".\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), false);
    }
    else
    {
        std::stringstream ss;
        ss << from << " could be moved inside, while it was designed to fail.\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), !inside.inside(result));
    }
}

void PolygonUtilsTest::moveOutsidePointyCornerTest()
{
    Point from(60, 70); // above pointy bit
    Point result(from);
    Point supposed(50, 70); // 10 below pointy bit
    Polygons inside;
    inside.add(pointy_square);
//     ClosestPolygonPoint cpp = PolygonUtils::moveInside2(inside, result, -10);
    ClosestPolygonPoint cpp = PolygonUtils::ensureInsideOrOutside(inside, result, -10);
    if (cpp.point_idx == NO_INDEX || cpp.poly_idx == NO_INDEX)
    {
        std::stringstream ss;
        ss << "Couldn't ensure point inside close to " << from << ".\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), false);
    }
    else
    {
        std::stringstream ss;
        ss << from << " couldn't be moved inside.\n";
//         CPPUNIT_ASSERT_MESSAGE(ss.str(), vSize(result - supposed) < 5 + maximum_error && !inside.inside(result)); // +5 because ensureInside might do half the preferred distance moved inside
        CPPUNIT_ASSERT_MESSAGE(ss.str(), !inside.inside(result)); // +5 because ensureInside might do half the preferred distance moved inside
    }
}

void PolygonUtilsTest::moveOutsidePointyCornerTestFail()
{ // should fail with normal moveInside2 (and the like)
    Point from(60, 70); // above pointy bit
    Point result(from);
    Point supposed(50, 70); // 10 below pointy bit
    Polygons inside;
    inside.add(pointy_square);
    ClosestPolygonPoint cpp = PolygonUtils::moveInside2(inside, result, -10);
//     ClosestPolygonPoint cpp = PolygonUtils::ensureInsideOrOutside(inside, result, -10);
    if (cpp.point_idx == NO_INDEX || cpp.poly_idx == NO_INDEX)
    {
        std::stringstream ss;
        ss << "Couldn't ensure point inside close to " << from << ".\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), false);
    }
    else
    {
        std::stringstream ss;
        ss << from << " could be moved inside to " << result << ", while it was designed to fail.\n";
//         CPPUNIT_ASSERT_MESSAGE(ss.str(), vSize(result - supposed) < 5 + maximum_error && !inside.inside(result)); // +5 because ensureInside might do half the preferred distance moved inside
        CPPUNIT_ASSERT_MESSAGE(ss.str(), inside.inside(result)); // +5 because ensureInside might do half the preferred distance moved inside
    }
}










void PolygonUtilsTest::spreadDotsTestSegment()
{
    std::vector<ClosestPolygonPoint> supposed;
    supposed.emplace_back(Point(50, 0), 0, test_squares[0], 0);
    supposed.emplace_back(Point(100, 0), 1, test_squares[0], 0);
    supposed.emplace_back(Point(100, 50), 1, test_squares[0], 0);

    spreadDotsAssert(PolygonsPointIndex(&test_squares, 0, 0), PolygonsPointIndex(&test_squares, 0, 2), 3, supposed);
}


void PolygonUtilsTest::spreadDotsTestFull()
{
    std::vector<ClosestPolygonPoint> supposed;
    supposed.emplace_back(Point(0, 0), 0, test_squares[0], 0);
    supposed.emplace_back(Point(50, 0), 0, test_squares[0], 0);
    supposed.emplace_back(Point(100, 0), 1, test_squares[0], 0);
    supposed.emplace_back(Point(100, 50), 1, test_squares[0], 0);
    supposed.emplace_back(Point(100, 100), 2, test_squares[0], 0);
    supposed.emplace_back(Point(50, 100), 2, test_squares[0], 0);
    supposed.emplace_back(Point(0, 100), 3, test_squares[0], 0);
    supposed.emplace_back(Point(0, 50), 3, test_squares[0], 0);

    spreadDotsAssert(PolygonsPointIndex(&test_squares, 0, 0), PolygonsPointIndex(&test_squares, 0, 0), 8, supposed);

}



void PolygonUtilsTest::spreadDotsAssert(PolygonsPointIndex start, PolygonsPointIndex end, unsigned int n_dots, const std::vector<ClosestPolygonPoint>& supposed)
{
    std::vector<ClosestPolygonPoint> result;
    PolygonUtils::spreadDots(start, end, n_dots, result);

    std::stringstream ss;
    ss << "PolygonUtils::spreadDots(" << start.point_idx << ", " << end.point_idx << ", " << n_dots << ") generated " << result.size() << " points, rather than " << supposed.size() << "!\n";
    CPPUNIT_ASSERT_MESSAGE(ss.str(), result.size() == supposed.size());

    for (unsigned int point_idx = 0 ; point_idx < result.size(); point_idx++)
    {
        std::stringstream ss;
        ss << point_idx << "nd point of PolygonUtils::spreadDots(" << start.point_idx << ", " << end.point_idx << ", " << n_dots << ") was " << result[point_idx].p() << ", rather than " << supposed[point_idx].p() << "!\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), result[point_idx].p() == supposed[point_idx].p());
    }
}

void PolygonUtilsTest::getNextParallelIntersectionTest1()
{
    Point start_point(20, 100);
    Point line_to = Point(150, 200);
    bool forward = true;
    coord_t dist = 35;
    getNextParallelIntersectionAssert(Point(0, 40), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest2()
{
    Point start_point(80, 100);
    Point line_to = Point(150, 200);
    bool forward = true;
    coord_t dist = 35;
    getNextParallelIntersectionAssert(Point(37, 100), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest3()
{
    Point start_point(20, 100);
    Point line_to = Point(120, 200);
    bool forward = false;
    coord_t dist = 35;
    getNextParallelIntersectionAssert(Point(70, 100), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest4()
{
    Point start_point(50, 100);
    Point line_to = Point(150, 200);
    bool forward = true;
    coord_t dist = 35;
    getNextParallelIntersectionAssert(Point(0, 0), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest5()
{
    Point start_point(10, 0);
    Point line_to = Point(-90, -100);
    bool forward = true;
    coord_t dist = 35;
    getNextParallelIntersectionAssert(Point(60, 0), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest6()
{
    Point start_point(10, 0);
    Point line_to = Point(-90, -100);
    bool forward = false;
    coord_t dist = 35;
    getNextParallelIntersectionAssert(Point(0, 40), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest7()
{
    Point start_point(50, 100);
    Point line_to = Point(150, 100);
    bool forward = true;
    coord_t dist = 25;
    getNextParallelIntersectionAssert(Point(0, 75), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest8()
{
    Point start_point(50, 100);
    Point line_to = Point(50, 200);
    bool forward = true;
    coord_t dist = 25;
    getNextParallelIntersectionAssert(Point(25, 100), start_point, line_to, forward, dist);
}
void PolygonUtilsTest::getNextParallelIntersectionTest9()
{
    Point start_point(100, 100);
    Point line_to = Point(200, 200);
    bool forward = true;
    coord_t dist = 80;
    getNextParallelIntersectionAssert(std::optional<Point>(), start_point, line_to, forward, dist);
}

void PolygonUtilsTest::getNextParallelIntersectionTest10()
{
    Point start_point(5, 100);
    Point line_to = Point(105, 200);
    bool forward = true;
    coord_t dist = 35;
    getNextParallelIntersectionAssert(Point(0, 45), start_point, line_to, forward, dist);
}

void PolygonUtilsTest::getNextParallelIntersectionAssert(std::optional<Point> predicted, Point start_point, Point line_to, bool forward, coord_t dist)
{
    ClosestPolygonPoint start = PolygonUtils::findClosest(start_point, test_squares);
    std::optional<ClosestPolygonPoint> computed = PolygonUtils::getNextParallelIntersection(start, line_to, dist, forward);

    std::stringstream ss;
    ss << "PolygonUtils::getNextParallelIntersection(" << start_point << ", " << line_to << ", " << dist << ", " << forward << ") ";

    constexpr bool draw_problem_scenario = false; // make this true if you are debugging the function getNextParallelIntersection(.)

    auto draw = [this, predicted, start, line_to, dist, computed]()
        {
            if (!draw_problem_scenario)
            {
                return;
            }
            SVG svg("output/bs.svg", AABB(test_squares), Point(500,500));
            svg.writePolygons(test_squares);
            svg.writeLine(start.p(), line_to, SVG::Color::BLUE);
            svg.writePoint(start.p(), true);
            Point vec = line_to - start.p();
            Point shift = normal(turn90CCW(vec), dist);
            svg.writeLine(start.p() - vec + shift, line_to + vec + shift, SVG::Color::GREEN);
            svg.writeLine(start.p() - vec - shift, line_to + vec - shift, SVG::Color::GREEN);
            if (computed)
            {
                svg.writePoint(computed->p(), true, 5, SVG::Color::RED);
            }
            if (predicted)
            {
                svg.writePoint(*predicted, true, 5, SVG::Color::GREEN);
            }
        };

    if (!predicted && computed)
    {
        draw();
        ss << "gave a result (" << computed->p() << ") rather than the predicted no result!\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), false);
    }
    if (predicted && !computed)
    {
        draw();
        ss << "gave no result rather than the predicted " << *predicted << "!\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), false);
    }
    if (predicted && computed)
    {
        draw();
        ss << "gave " << computed->p() << " while it was predicted to be " << *predicted << "!\n";
        CPPUNIT_ASSERT_MESSAGE(ss.str(), vSize(*predicted - computed->p()) < maximum_error);
    }
}*/

}
