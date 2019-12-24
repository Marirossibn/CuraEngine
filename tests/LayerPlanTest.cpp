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
     * A pre-filled layer plan.
     */
    LayerPlan layer_plan;

    /*!
     * Sliced layers divided up into regions for each structure.
     */
    SliceDataStorage storage;

    /*!
     * Cooling settings, which are passed to the layer plan by reference.
     *
     * One for each extruder. There is only one extruder by default in this
     * fixture.
     */
    std::vector<FanSpeedLayerTimeSettings> fan_speed_layer_time_settings;

    //Some generic settings.
    LayerIndex layer_nr = 100;
    coord_t layer_thickness = 100; //0.1mm.
    coord_t z = layer_thickness * layer_nr;
    size_t extruder_nr = 0;
    size_t comb_boundary_offset = 2000; //2mm.
    size_t comb_move_inside_distance = 1000; //1mm.
    size_t travel_avoid_distance = 5000; //5mm.

    LayerPlanTest() : layer_plan(setUp(storage), layer_nr, z, layer_thickness, extruder_nr, fan_speed_layer_time_settings, comb_boundary_offset, comb_move_inside_distance, travel_avoid_distance)
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
     * \param storage The SliceDataStorage to fill.
     * \return That same SliceDataStorage.
     */
    SliceDataStorage& setUp(SliceDataStorage& storage)
    {
        constexpr size_t num_mesh_groups = 1;
        Application::getInstance().current_slice = new Slice(num_mesh_groups);

        //Define all settings in the mesh group. The extruder train and model settings will fall back on that then.
        Settings& settings = Application::getInstance().current_slice->scene.current_mesh_group->settings;
        //Path config storage settings.
        settings.add("acceleration_prime_tower", "5008");
        settings.add("acceleration_skirt_brim", "5007");
        settings.add("acceleration_support_bottom", "5005");
        settings.add("acceleration_support_infill", "5009");
        settings.add("acceleration_support_roof", "5004");
        settings.add("acceleration_travel", "5006");
        settings.add("adhesion_extruder_nr", "0");
        settings.add("adhesion_type", "brim");
        settings.add("initial_layer_line_width_factor", "1.0");
        settings.add("jerk_prime_tower", "5.8");
        settings.add("jerk_skirt_brim", "5.7");
        settings.add("jerk_support_bottom", "5.5");
        settings.add("jerk_support_infill", "5.9");
        settings.add("jerk_support_roof", "5.4");
        settings.add("jerk_travel", "5.6");
        settings.add("material_flow_layer_0", "100");
        settings.add("prime_tower_flow", "108");
        settings.add("prime_tower_line_width", "0.48");
        settings.add("raft_base_line_width", "0.401");
        settings.add("raft_base_acceleration", "5001");
        settings.add("raft_base_jerk", "5.1");
        settings.add("raft_base_speed", "51");
        settings.add("raft_base_thickness", "0.101");
        settings.add("raft_interface_acceleration", "5002");
        settings.add("raft_interface_jerk", "5.2");
        settings.add("raft_interface_line_width", "0.402");
        settings.add("raft_interface_speed", "52");
        settings.add("raft_interface_thickness", "0.102");
        settings.add("raft_surface_acceleration", "5003");
        settings.add("raft_surface_jerk", "5.3");
        settings.add("raft_surface_line_width", "0.403");
        settings.add("raft_surface_speed", "53");
        settings.add("raft_surface_thickness", "0.103");
        settings.add("skirt_brim_line_width", "0.47");
        settings.add("skirt_brim_material_flow", "107");
        settings.add("skirt_brim_speed", "57");
        settings.add("speed_prime_tower", "58");
        settings.add("speed_slowdown_layers", "1");
        settings.add("speed_support_bottom", "55");
        settings.add("speed_support_infill", "59");
        settings.add("speed_support_roof", "54");
        settings.add("speed_travel", "56");
        settings.add("support_bottom_extruder_nr", "0");
        settings.add("support_bottom_line_width", "0.405");
        settings.add("support_bottom_material_flow", "105");
        settings.add("support_infill_extruder_nr", "0");
        settings.add("support_line_width", "0.49");
        settings.add("support_material_flow", "109");
        settings.add("support_roof_extruder_nr", "0");
        settings.add("support_roof_line_width", "0.404");
        settings.add("support_roof_material_flow", "104");

        Application::getInstance().current_slice->scene.extruders.emplace_back(0, &settings); //Add an extruder train.
        return storage;
    }

    void SetUp()
    {
        FanSpeedLayerTimeSettings fan_settings;
        fan_settings.cool_min_layer_time = 5;
        fan_settings.cool_min_layer_time_fan_speed_max = 10;
        fan_settings.cool_fan_speed_0 = 0.0;
        fan_settings.cool_fan_speed_min = 75.0;
        fan_settings.cool_fan_speed_max = 100.0;
        fan_settings.cool_min_speed = 10;
        fan_settings.cool_fan_full_layer = 3;
        fan_speed_layer_time_settings.push_back(fan_settings);
    }
};

/*!
 * Tests planning a travel move through open space.
 */
TEST_F(LayerPlanTest, AddTravelOpen)
{
    
}

}