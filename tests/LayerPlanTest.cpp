//Copyright (c) 2019 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <gtest/gtest.h>

#include "../src/Application.h" //To provide settings for the layer plan.
#include "../src/LayerPlan.h" //The code under test.
#include "../src/RetractionConfig.h" //To provide retraction settings.
#include "../src/Slice.h" //To provide settings for the layer plan.
#include "../src/sliceDataStorage.h" //To provide slice data as input for the planning stage.

namespace cura
{

/*!
 * A fixture to test layer plans with.
 *
 * This fixture gets the previous location initialised to 0,0. You can
 * optionally fill it with some layer data.
 */
class LayerPlanTest : public testing::Test
{
public:
    /*!
     * Cooling settings, which are passed to the layer plan by reference.
     *
     * One for each extruder. There is only one extruder by default in this
     * fixture.
     *
     * \note This needs to be allocated BEFORE layer_plan since the constructor
     * of layer_plan in the initializer list needs to have a valid vector
     * reference.
     */
    std::vector<FanSpeedLayerTimeSettings> fan_speed_layer_time_settings;

    /*!
     * Sliced layers divided up into regions for each structure.
     */
    SliceDataStorage* storage;

    /*!
     * A pre-filled layer plan.
     */
    LayerPlan layer_plan;

    /*!
     * A shortcut to easily modify settings in a test.
     */
    Settings* settings;

    LayerPlanTest() :
        storage(setUpStorage()),
        layer_plan(
            *storage,
            /*layer_nr=*/100,
            /*z=*/10000,
            /*layer_thickness=*/100,
            /*extruder_nr=*/0,
            fan_speed_layer_time_settings,
            /*comb_boundary_offset=*/2000,
            /*comb_move_inside_distance=*/1000,
            /*travel_avoid_distance=*/5000
        )
    {
    }

    /*!
     * Prepares the slice data storage before passing it to the layer plan.
     *
     * In order to prepare the slice data storage, the Application class is also
     * initialized with a proper current slice and all of the settings it needs.
     *
     * This needs to be done in a separate function so that it can be executed
     * in the initializer list.
     * \return That same SliceDataStorage.
     */
    SliceDataStorage* setUpStorage()
    {
        constexpr size_t num_mesh_groups = 1;
        Application::getInstance().current_slice = new Slice(num_mesh_groups);

        //Define all settings in the mesh group. The extruder train and model settings will fall back on that then.
        settings = &Application::getInstance().current_slice->scene.current_mesh_group->settings;
        //Default settings. These are not (always) the FDM printer defaults, but sometimes just setting values that can be recognised uniquely as much as possible.
        settings->add("acceleration_prime_tower", "5008");
        settings->add("acceleration_skirt_brim", "5007");
        settings->add("acceleration_support_bottom", "5005");
        settings->add("acceleration_support_infill", "5009");
        settings->add("acceleration_support_roof", "5004");
        settings->add("acceleration_travel", "5006");
        settings->add("adhesion_extruder_nr", "0");
        settings->add("adhesion_type", "brim");
        settings->add("cool_fan_full_layer", "3");
        settings->add("cool_fan_speed_0", "0");
        settings->add("cool_fan_speed_min", "75");
        settings->add("cool_fan_speed_max", "100");
        settings->add("cool_min_speed", "10");
        settings->add("cool_min_layer_time", "5");
        settings->add("cool_min_layer_time_fan_speed_max", "10");
        settings->add("initial_layer_line_width_factor", "1.0");
        settings->add("jerk_prime_tower", "5.8");
        settings->add("jerk_skirt_brim", "5.7");
        settings->add("jerk_support_bottom", "5.5");
        settings->add("jerk_support_infill", "5.9");
        settings->add("jerk_support_roof", "5.4");
        settings->add("jerk_travel", "5.6");
        settings->add("layer_height", "0.1");
        settings->add("layer_start_x", "0");
        settings->add("layer_start_y", "0");
        settings->add("machine_center_is_zero", "false");
        settings->add("machine_depth", "1000");
        settings->add("machine_height", "1000");
        settings->add("machine_width", "1000");
        settings->add("material_flow_layer_0", "100");
        settings->add("meshfix_maximum_travel_resolution", "0");
        settings->add("prime_tower_enable", "true");
        settings->add("prime_tower_flow", "108");
        settings->add("prime_tower_line_width", "0.48");
        settings->add("prime_tower_min_volume", "10");
        settings->add("prime_tower_size", "40");
        settings->add("raft_base_line_width", "0.401");
        settings->add("raft_base_acceleration", "5001");
        settings->add("raft_base_jerk", "5.1");
        settings->add("raft_base_speed", "51");
        settings->add("raft_base_thickness", "0.101");
        settings->add("raft_interface_acceleration", "5002");
        settings->add("raft_interface_jerk", "5.2");
        settings->add("raft_interface_line_width", "0.402");
        settings->add("raft_interface_speed", "52");
        settings->add("raft_interface_thickness", "0.102");
        settings->add("raft_surface_acceleration", "5003");
        settings->add("raft_surface_jerk", "5.3");
        settings->add("raft_surface_line_width", "0.403");
        settings->add("raft_surface_speed", "53");
        settings->add("raft_surface_thickness", "0.103");
        settings->add("retraction_amount", "8");
        settings->add("retraction_combing", "off");
        settings->add("retraction_count_max", "30");
        settings->add("retraction_enable", "false");
        settings->add("retraction_extra_prime_amount", "1");
        settings->add("retraction_extrusion_window", "10");
        settings->add("retraction_hop", "1.5");
        settings->add("retraction_hop_enabled", "false");
        settings->add("retraction_min_travel", "0");
        settings->add("retraction_prime_speed", "12");
        settings->add("retraction_retract_speed", "11");
        settings->add("skirt_brim_line_width", "0.47");
        settings->add("skirt_brim_material_flow", "107");
        settings->add("skirt_brim_speed", "57");
        settings->add("speed_prime_tower", "58");
        settings->add("speed_slowdown_layers", "1");
        settings->add("speed_support_bottom", "55");
        settings->add("speed_support_infill", "59");
        settings->add("speed_support_roof", "54");
        settings->add("speed_travel", "56");
        settings->add("support_bottom_extruder_nr", "0");
        settings->add("support_bottom_line_width", "0.405");
        settings->add("support_bottom_material_flow", "105");
        settings->add("support_infill_extruder_nr", "0");
        settings->add("support_line_width", "0.49");
        settings->add("support_material_flow", "109");
        settings->add("support_roof_extruder_nr", "0");
        settings->add("support_roof_line_width", "0.404");
        settings->add("support_roof_material_flow", "104");
        settings->add("wall_line_count", "3");
        settings->add("wall_line_width_x", "0.3");
        settings->add("wall_line_width_0", "0.301");

        Application::getInstance().current_slice->scene.extruders.emplace_back(0, settings); //Add an extruder train.

        //Set the fan speed layer time settings (since the LayerPlan constructor copies these).
        FanSpeedLayerTimeSettings fan_settings;
        fan_settings.cool_min_layer_time = settings->get<Duration>("cool_min_layer_time");
        fan_settings.cool_min_layer_time_fan_speed_max = settings->get<Duration>("cool_min_layer_time_fan_speed_max");
        fan_settings.cool_fan_speed_0 = settings->get<Ratio>("cool_fan_speed_0");
        fan_settings.cool_fan_speed_min = settings->get<Ratio>("cool_fan_speed_min");
        fan_settings.cool_fan_speed_max = settings->get<Ratio>("cool_fan_speed_max");
        fan_settings.cool_min_speed = settings->get<Velocity>("cool_min_speed");
        fan_settings.cool_fan_full_layer = settings->get<LayerIndex>("cool_fan_full_layer");
        fan_speed_layer_time_settings.push_back(fan_settings);

        //Set the retraction settings (also copied by LayerPlan).
        RetractionConfig retraction_config;
        retraction_config.distance = settings->get<double>("retraction_amount");
        retraction_config.prime_volume = settings->get<double>("retraction_extra_prime_amount");
        retraction_config.speed = settings->get<Velocity>("retraction_retract_speed");
        retraction_config.primeSpeed = settings->get<Velocity>("retraction_prime_speed");
        retraction_config.zHop = settings->get<coord_t>("retraction_hop");
        retraction_config.retraction_min_travel_distance = settings->get<coord_t>("retraction_min_travel");
        retraction_config.retraction_extrusion_window = settings->get<double>("retraction_extrusion_window");
        retraction_config.retraction_count_max = settings->get<size_t>("retraction_count_max");

        SliceDataStorage* result = new SliceDataStorage();
        result->retraction_config_per_extruder[0] = retraction_config;
        return result;
    }

    void SetUp()
    {
        layer_plan.addTravel_simple(Point(0, 0)); //Make sure that it appears as if we have already done things in this layer plan. Just the standard case.
    }

    /*!
     * Cleaning up after a test is hardly necessary but just for neatness.
     */
    void TearDown()
    {
        delete storage;
        delete Application::getInstance().current_slice;
    }
};

//Test all combinations of these settings in parameterised tests.
std::vector<std::string> retraction_enable = {"false", "true"};
std::vector<std::string> hop_enable = {"false", "true"};
std::vector<std::string> combing = {"off", "all"};
std::vector<bool> is_long = {false, true}; //Whether or not the travel move is longer than retraction_min_travel.
std::vector<bool> is_long_combing = {false, true}; //Whether or not the total travel distance is longer than retraction_combing_max_distance.
std::vector<std::string> scene = {
    "open", //The travel move goes through open air. There's nothing in the entire layer.
    "inside", //The travel move goes through a part on the inside.
    "obstruction", //The travel move goes through open air, but there's something in the way that needs to be avoided.
    "inside_obstruction", //The travel move goes through the inside of a part, but there's a hole in the way that needs to be avoided.
    "other_part" //The travel move goes from one part to another.
};

/*!
 * Parameterised testing class that combines many combinations of cases to test
 * travel moves in. The parameters are in the same order as above:
 * 1. retraction_enable
 * 2. hop_enable
 * 3. combing mode
 * 4. Long travel move.
 * 5. Long travel move (combing).
 * 6. Scene.
 */
class AddTravelTest : public LayerPlanTest, public testing::WithParamInterface<std::tuple<std::string, std::string, std::string, bool, bool, std::string>>
{
public:
    /*!
     * Runs the actual test, adding a travel move to the layer plan with the
     * specified parameters.
     * \param parameters The parameter object provided to the test.
     * \return The resulting g-code path.
     */
    GCodePath run(const std::tuple<std::string, std::string, std::string, bool, bool, std::string>& parameters)
    {
        settings->add("retraction_enable", std::get<0>(parameters));
        settings->add("retraction_hop_enable", std::get<1>(parameters));
        settings->add("retraction_combing", std::get<2>(parameters));
        settings->add("retraction_min_travel", std::get<3>(parameters) ? "1" : "10000"); //If disabled, give it a high minimum travel so we're sure that our travel move is shorter.
        storage->retraction_config_per_extruder[0].retraction_min_travel_distance = settings->get<coord_t>("retraction_min_travel"); //Update the copy that the storage has of this.
        settings->add("retraction_combing_max_distance", std::get<4>(parameters) ? "1" : "10000");
        //TODO: Set up a scene depending on std::get<5>.

        const Point destination(500000, 500000);
        return layer_plan.addTravel(destination);
    }
};

INSTANTIATE_TEST_CASE_P(AllCombinations, AddTravelTest, testing::Combine(
    testing::ValuesIn(retraction_enable),
    testing::ValuesIn(hop_enable),
    testing::ValuesIn(combing),
    testing::ValuesIn(is_long),
    testing::ValuesIn(is_long_combing),
    testing::ValuesIn(scene)
));

/*!
 * Test if there are indeed no retractions if retractions are disabled.
 */
TEST_P(AddTravelTest, NoRetractionIfDisabled)
{
    GCodePath result = run(GetParam());

    if(std::get<0>(GetParam()) == "false") //Retraction is disabled.
    {
        EXPECT_FALSE(result.retract) << "If retraction is disabled it should not retract.";
    }
}

/*!
 * Test if there are indeed no Z hops if they are disabled.
 */
TEST_P(AddTravelTest, NoHopIfDisabled)
{
    GCodePath result = run(GetParam());

    if(std::get<1>(GetParam()) == "false") //Z hop is disabled.
    {
        EXPECT_FALSE(result.perform_z_hop) << "If Z hop is disabled it should not hop.";
    }
}

/*!
 * Test if there are no retractions if the travel move is short, regardless of
 * whether retractions are enabled or not.
 */
TEST_P(AddTravelTest, NoRetractionIfShort)
{
    GCodePath result = run(GetParam());

    if(!std::get<3>(GetParam())) //Short travel move.
    {
        EXPECT_FALSE(result.retract) << "If the travel move is shorter than retraction_min_travel, it should not retract.";
    }
}

/*!
 * Tests planning a travel move:
 *  - Through open space, no polygons in the way.
 *  - Combing is disabled.
 *  - Retraction is disabled.
 *  - Z hop is disabled.
 */
TEST_F(LayerPlanTest, AddTravelOpenNoCombingNoRetractNoHop)
{
    Point destination(500000, 500000);
    GCodePath result = layer_plan.addTravel(destination);

    EXPECT_FALSE(result.retract);
    EXPECT_FALSE(result.perform_z_hop);
    EXPECT_FALSE(result.perform_prime);
    ASSERT_EQ(result.points.size(), 2);
    EXPECT_EQ(result.points[0], Point(0, 0));
    EXPECT_EQ(result.points[1], destination);
}

/*!
 * Tests planning a travel move:
 *  - Through open space, no polygons in the way.
 *  - Combing is disabled.
 *  - Retraction is enabled.
 *  - Z hop is disabled.
 */
TEST_F(LayerPlanTest, AddTravelOpenNoCombingRetractNoHop)
{
    settings->add("retraction_enable", "true");

    Point destination(500000, 500000);
    GCodePath result = layer_plan.addTravel(destination);

    EXPECT_TRUE(result.retract) << "It must retract since it's going through air.";
    EXPECT_FALSE(result.perform_z_hop);
    EXPECT_FALSE(result.perform_prime);
    ASSERT_EQ(result.points.size(), 2);
    EXPECT_EQ(result.points[0], Point(0, 0));
    EXPECT_EQ(result.points[1], destination);
}

/*!
 * Tests planning a travel move:
 *  - Through open space, no polygons in the way.
 *  - Combing is disabled.
 *  - Retraction is enabled.
 *  - Z hop is enabled.
 */
TEST_F(LayerPlanTest, AddTravelOpenNoCombingRetractHop)
{
    settings->add("retraction_enable", "true");
    settings->add("retraction_hop_enabled", "true");

    Point destination(500000, 500000);
    GCodePath result = layer_plan.addTravel(destination);

    EXPECT_TRUE(result.retract) << "It must retract since it's going through air.";
    EXPECT_TRUE(result.perform_z_hop) << "It must do a Z hop since it's retracting.";
    EXPECT_FALSE(result.perform_prime);
    ASSERT_EQ(result.points.size(), 2);
    EXPECT_EQ(result.points[0], Point(0, 0));
    EXPECT_EQ(result.points[1], destination);
}

/*!
 * Tests planning a travel move:
 *  - Through open space, no polygons in the way.
 *  - Combing is disabled.
 *  - Retraction is enabled.
 *  - Z hop is disabled.
 *  - The distance of the move is shorter than the maximum distance without
 *    retraction.
 */
TEST_F(LayerPlanTest, AddTravelOpenNoCombingRetractNoHopShort)
{
    settings->add("retraction_enable", "true");
    settings->add("retraction_min_travel", "1"); //Travels shorter than 1mm should not retract.
    storage->retraction_config_per_extruder[0].retraction_min_travel_distance = settings->get<coord_t>("retraction_min_travel"); //Update the copy that the storage has of this.

    Point destination(500, 500); //Move from 0,0 to 500,500, so travel move is 0.7mm long.
    GCodePath result = layer_plan.addTravel(destination);

    EXPECT_FALSE(result.retract) << "It must not retract since the travel move is shorter than retraction_min_travel.";
    EXPECT_FALSE(result.perform_z_hop);
    EXPECT_FALSE(result.perform_prime);
    ASSERT_EQ(result.points.size(), 2);
    EXPECT_EQ(result.points[0], Point(0, 0));
    EXPECT_EQ(result.points[1], destination);
}

}