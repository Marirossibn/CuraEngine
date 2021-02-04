//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <gtest/gtest.h>

#include "../src/settings/Settings.h" //Settings to generate walls with.
#include "../src/utils/polygon.h" //To create example polygons.
#include "../src/sliceDataStorage.h" //Sl
#include "../src/WallsComputation.h" //Unit under test.

namespace cura
{

/*!
 * Fixture that provides a basis for testing wall computation.
 */
class WallsComputationTest : public testing::Test
{
public:
    /*!
     * Settings to slice with. This is linked in the walls_computation fixture.
     */
    Settings settings;

    /*!
     * WallsComputation instance to test with. The layer index will be 100.
     */
    WallsComputation walls_computation;

    /*!
     * Basic 10x10mm square shape to work with.
     */
    Polygons square_shape;

    WallsComputationTest()
    : walls_computation(settings, LayerIndex(100))
    {
        square_shape.emplace_back();
        square_shape.back().emplace_back(0, 0);
        square_shape.back().emplace_back(MM2INT(10), 0);
        square_shape.back().emplace_back(MM2INT(10), MM2INT(10));
        square_shape.back().emplace_back(0, MM2INT(10));

        //Settings for a simple 2 walls, about as basic as possible.
        settings.add("alternate_extra_perimeter", "false");
        settings.add("beading_strategy_type", "inward_distributed");
        settings.add("fill_outline_gaps", "false");
        settings.add("initial_layer_line_width_factor", "100");
        settings.add("magic_spiralize", "false");
        settings.add("meshfix_maximum_deviation", "0.1");
        settings.add("meshfix_maximum_extrusion_area_deviation", "0.01");
        settings.add("meshfix_maximum_resolution", "0.01");
        settings.add("min_bead_width", "0");
        settings.add("min_feature_size", "0");
        settings.add("wall_0_extruder_nr", "0");
        settings.add("wall_0_inset", "0");
        settings.add("wall_line_count", "2");
        settings.add("wall_line_width_0", "0.4");
        settings.add("wall_line_width_x", "0.4");
        settings.add("wall_transition_angle", "30");
        settings.add("wall_transition_filter_distance", "1");
        settings.add("wall_transition_length", "1");
        settings.add("wall_transition_threshold", "50");
        settings.add("wall_x_extruder_nr", "0");
    }

};

/*!
 * Tests if something is generated in the basic happy case.
 */
TEST_F(WallsComputationTest, GenerateWallsForLayerSinglePart)
{
    SliceLayer layer;
    layer.parts.emplace_back();
    SliceLayerPart& part = layer.parts.back();
    part.outline.add(square_shape);

    //Run the test.
    walls_computation.generateWalls(&layer);

    //Verify that something was generated.
    EXPECT_FALSE(part.wall_toolpaths.empty()) << "There must be some walls.";
    EXPECT_GT(part.print_outline.area(), 0) << "The print outline must encompass the outer wall, so it must be more than 0.";
    EXPECT_LE(part.print_outline.area(), square_shape.area()) << "The print outline must stay within the bounds of the original part.";
    EXPECT_GT(part.inner_area.area(), 0) << "The inner area must be within the innermost wall. There are not enough walls to fill the entire part, so there is a positive inner area.";
}

}