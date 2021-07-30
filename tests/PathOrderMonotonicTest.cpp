//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <string>

#include <gtest/gtest.h>
#include <clipper.hpp>

#include "../src/infill.h"
#include "../src/utils/linearAlg2D.h"
#include "../src/utils/math.h"
#include "../src/PathOrderMonotonic.h"
#include "../src/utils/polygon.h"
#include "ReadTestPolygons.h"

//#define TEST_PATHS_SVG_OUTPUT
#ifdef TEST_PATHS_SVG_OUTPUT
#include <cstdlib>
#include "../src/utils/SVG.h"
#endif //TEST_PATHS_SVG_OUTPUT

namespace cura
{
    /* Fixture to allow parameterized tests.
     */
    class PathOrderMonotonicTest : public testing::TestWithParam<std::tuple<std::string, AngleRadians>>
    {};

    inline Point startVertex(const PathOrderMonotonic<ConstPolygonRef>::Path& path)
    {
        return path.vertices[path.start_vertex];
    }

    inline Point endVertex(const PathOrderMonotonic<ConstPolygonRef>::Path& path)
    {
        return path.vertices[path.vertices.size() - (1 + path.start_vertex)];
    }

    coord_t projectPathAlongAxis(const PathOrderMonotonic<ConstPolygonRef>::Path& path, const Point& vector)
    {
        return dot(startVertex(path), vector);
    }

    coord_t projectEndAlongAxis(const PathOrderMonotonic<ConstPolygonRef>::Path& path, const Point& vector)
    {
        return dot(endVertex(path), vector);
    }

    bool rangeOverlaps(const std::pair<coord_t, coord_t>& range_b, const std::pair<coord_t, coord_t>& range_a)
    {
        const coord_t len_b = std::abs(range_b.first - range_b.second);
        const coord_t len_a = std::abs(range_a.first - range_a.second);
        const coord_t len_total = std::max({ range_b.first, range_b.second, range_a.first, range_a.second })
                                - std::min({ range_b.first, range_b.second, range_a.first, range_a.second });
        return len_total < (len_b + len_a);
    }

    coord_t shortestDistance
    (
        const PathOrderMonotonic<ConstPolygonRef>::Path& path_a,
        const PathOrderMonotonic<ConstPolygonRef>::Path& path_b
    )
    {
        // NOTE: Assume these are more or less lines.
        const auto point_pair =
            LinearAlg2D::getClosestConnection(startVertex(path_a), endVertex(path_a), startVertex(path_b), endVertex(path_b));
        return vSize(point_pair.second - point_pair.first);
    }

    coord_t pathLength(const PathOrderMonotonic<ConstPolygonRef>::Path& path)
    {
        // NOTE: Assume these are more or less lines.
        return vSize(endVertex(path) - startVertex(path));
    }

    constexpr EFillMethod pattern = EFillMethod::LINES;
    constexpr bool zig_zagify = false;
    constexpr bool connect_polygons = false;
    constexpr coord_t line_distance = 350;
    constexpr coord_t outline_offset = 0;
    constexpr coord_t infill_line_width = 350;
    constexpr coord_t infill_overlap = 0;
    constexpr size_t infill_multiplier = 1;
    constexpr coord_t z = 2;
    constexpr coord_t shift = 0;
    constexpr coord_t max_resolution = 10;
    constexpr coord_t max_deviation = 5;
    bool getInfillLines(const std::string& filename, const AngleRadians& angle, Polygons& output)
    {
        std::vector<Polygons> shapes;
        if (!readTestPolygons(filename, shapes))
        {
            return false;
        }

        Polygons dummy_polys;
        for (const auto& shape : shapes)
        {
            Infill infill_comp
            (
                pattern,
                zig_zagify,
                connect_polygons,
                shape,
                outline_offset,
                infill_line_width,
                line_distance,
                infill_overlap,
                infill_multiplier,
                AngleDegrees(angle),
                z,
                shift,
                max_resolution,
                max_deviation
            );
            infill_comp.generate(dummy_polys, output);
        }
        return true;
    }

#ifdef TEST_PATHS_SVG_OUTPUT
    void writeDebugSVG
    (
        const std::string& original_filename,
        const AngleRadians& angle,
        const Point& monotonic_vec,
        const std::vector<std::vector<PathOrderMonotonic<ConstPolygonRef>::Path>>& sections
    )
    {
        constexpr int buff_size = 1024;
        char buff[buff_size];
        const size_t xx = original_filename.find_first_of('_');
        std::string basename = original_filename.substr(xx, original_filename.find_last_of('.') - xx);
        std::snprintf(buff, buff_size, "/tmp/%s_%d.svg", basename.c_str(), (int) AngleDegrees(angle));
        const std::string filename(buff);

        AABB aabb;
        for (const auto& section : sections)
        {
            for (const auto& path : section)
            {
                aabb.include(startVertex(path.vertices));
                aabb.include(endVertex(path.vertices));
            }
        }
        aabb.include(Point{0, 0});
        aabb.include(monotonic_vec);

        SVG svgFile(filename.c_str(), aabb);

        int color_id = -1;
        for (const auto& section : sections)
        {
            ++color_id;
            SVG::Color section_color{ (SVG::Color) (((int) SVG::Color::GRAY) + (color_id % 7)) };
            for (const auto& path : section)
            {
                svgFile.writePolyline(path.vertices, section_color);
            }
        }
        svgFile.writeArrow(Point{ 0, 0 }, monotonic_vec, SVG::Color::BLACK);
        // Note: SVG writes 'itself' when the object is destroyed.
    }
#endif //TEST_PATHS_SVG_OUTPUT

    TEST_P(PathOrderMonotonicTest, SectionsTest)
    {
        const auto params = GetParam();
        const double angle_radians{ std::get<1>(params) };
        const auto& filename = std::get<0>(params);
        Polygons polylines;
        ASSERT_TRUE(getInfillLines(filename, angle_radians, polylines)) << "Input test-file could not be read, check setup.";

        const Point& pt_r = polylines.begin()->at(0);
        const Point& pt_s = polylines.begin()->at(1);
        const double angle_from_first_line = std::atan2(pt_s.Y - pt_r.Y, pt_s.X - pt_r.X) + 0.5 * M_PI;
        const Point monotonic_axis{ std::cos(angle_from_first_line) * 1000, std::sin(angle_from_first_line) * 1000 };
        const Point perpendicular_axis{ turn90CCW(monotonic_axis) };

        constexpr coord_t max_adjacent_distance = line_distance + 1;
        PathOrderMonotonic<ConstPolygonRef> object_under_test(angle_from_first_line, max_adjacent_distance, monotonic_axis * -1000);
        for (const auto& polyline : polylines)
        {
            object_under_test.addPolyline(polyline);
        }
        object_under_test.optimize();

        // Collect sections:
        std::vector<std::vector<PathOrderMonotonic<ConstPolygonRef>::Path>> sections;
        sections.emplace_back();
        coord_t last_path_mono_projection = projectPathAlongAxis(object_under_test.paths.front(), monotonic_axis);
        for (const auto& path : object_under_test.paths)
        {
            const coord_t path_mono_projection{ projectPathAlongAxis(path, monotonic_axis) };
            if (path_mono_projection < last_path_mono_projection && ! sections.back().empty())
            {
                sections.emplace_back();
            }
            sections.back().push_back(path);
            last_path_mono_projection = path_mono_projection;
        }

#ifdef TEST_PATHS_SVG_OUTPUT
        writeDebugSVG(filename, angle_radians, monotonic_axis, sections);
#endif //TEST_PATHS_SVG_OUTPUT

        std::unordered_map<std::pair<Point, Point>, size_t> split_section_counts_per_split_line;

        size_t section_a_id = 0;
        for (const auto& section_a : sections)
        {
            ++section_a_id;

            size_t section_b_id = 0;
            for (const auto& section_b : sections)
            {
                ++section_b_id;
                if (section_a_id >= section_b_id)
                {
                    continue; // <-- So section B will always be 'later' than section A.
                }

                // Check if the start of A is lower than the start of B, since it is ordered first.
                const coord_t mono_a{ projectPathAlongAxis(section_a.front(), monotonic_axis) };
                const coord_t mono_b{ projectPathAlongAxis(section_b.front(), monotonic_axis) };
                EXPECT_LE(mono_a, mono_b)
                    << "Section ordered before another, A's start point should be before B when ordered along the monotonic axis.";

                // Already tested for A start < B start in the monotonic direction,
                //   so assume A begins before B, so there is either no overlap, B lies 'witin' A, or B stops later than A.
                auto it_a = section_a.begin();
                for (auto it_b = section_b.begin(); it_b != section_b.end(); ++it_b)
                {
                    const coord_t mono_b = projectPathAlongAxis(*it_b, monotonic_axis);
                    for (; it_a != section_a.end() && projectPathAlongAxis(*it_a, monotonic_axis) < mono_b; ++it_a) {}
                    const std::pair<coord_t, coord_t> perp_b_range
                    {
                        projectPathAlongAxis(*it_b, perpendicular_axis),
                        projectEndAlongAxis(*it_b, perpendicular_axis)
                    };
                    if (it_a == section_a.end())
                    {
                        // A is wholly before B in the monotonic direction, test if A and B should indeed have been different sections:
                        if (it_b == section_b.begin())
                        {
                            it_a = std::prev(it_a); // end of section A
                            const std::pair<coord_t, coord_t> perp_a_range
                            {
                                projectPathAlongAxis(*it_a, perpendicular_axis),
                                projectEndAlongAxis(*it_a, perpendicular_axis)
                            };
                            if (rangeOverlaps(perp_b_range, perp_a_range) && shortestDistance(*it_a, *it_b) <= max_adjacent_distance)
                            {
                                // This is only wrong if there is no split, so no 3rd or more section that ends at the same line,
                                //   so collect those lines.

                                // Take the longer line:
                                const std::pair<Point, Point> line = pathLength(*it_a) > pathLength(*it_b) ?
                                    std::make_pair(startVertex(*it_a), endVertex(*it_a)) :
                                    std::make_pair(startVertex(*it_b), endVertex(*it_b));

                                // Collect the edges of the sections that split before that line:
                                if (split_section_counts_per_split_line.count(line) == 0)
                                {
                                    split_section_counts_per_split_line[line] = 0;
                                }
                                ++split_section_counts_per_split_line[line];
                            }
                        }
                        else
                        {
                            break;
                        }
                    }
                    else
                    {
                        // A and B intersect in monotonic direction, check if they overlap in the perpendicular direction:
                        const std::pair<coord_t, coord_t> perp_a_range
                        {
                            projectPathAlongAxis(*it_a, perpendicular_axis),
                            projectEndAlongAxis(*it_a, perpendicular_axis)
                        };
                        EXPECT_FALSE(rangeOverlaps(perp_b_range, perp_a_range))
                            << "Perpendicular range overlaps for neighbouring lines in different sections (next line of A / line in B).";
                    }
                }
            }
        }

        // If there is a line where a section ends, and only one other section begins, then they should've been 1 section to begin with:
        for (const auto& line_count_pair : split_section_counts_per_split_line)
        {
            EXPECT_GE(line_count_pair.second, 2) << "A section was split up while it could have been printed monotonically.";
        }
    }

    const std::vector<std::string> polygon_filenames =
    {
        "../tests/resources/polygon_concave.txt",
        "../tests/resources/polygon_concave_hole.txt",
        "../tests/resources/polygon_square.txt",
        "../tests/resources/polygon_square_hole.txt",
        "../tests/resources/polygon_triangle.txt",
        "../tests/resources/polygon_two_squares.txt",
        "../tests/resources/polygon_slant_gap.txt",
        "../tests/resources/polygon_sawtooth.txt",
    };
    const std::vector<AngleRadians> angle_radians =
    {
        0,
        0.1,
        0.25 * M_PI,
        1.0,
        0.5 * M_PI,
        0.75 * M_PI,
        M_PI,
        1.25 * M_PI,
        4.0,
        1.5 * M_PI,
        1.75 * M_PI,
        5.0,
        (2.0 * M_PI) - 0.1
    };

    INSTANTIATE_TEST_CASE_P(PathOrderMonotonicTestInstantiation, PathOrderMonotonicTest,
        testing::Combine(testing::ValuesIn(polygon_filenames), testing::ValuesIn(angle_radians)));

} // namespace cura
