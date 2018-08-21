//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include <cstring>

#include "Application.h" //To communicate layer view data.
#include "LayerPlan.h"
#include "MergeInfillLines.h"
#include "pathOrderOptimizer.h"
#include "raft.h" // getTotalExtraLayers
#include "sliceDataStorage.h"
#include "communication/Communication.h"
#include "settings/types/Ratio.h"
#include "utils/polygonUtils.h"

namespace cura {


ExtruderPlan::ExtruderPlan(int extruder, int layer_nr, bool is_initial_layer, bool is_raft_layer, int layer_thickness, const FanSpeedLayerTimeSettings& fan_speed_layer_time_settings, const RetractionConfig& retraction_config)
: extruder(extruder)
, heated_pre_travel_time(0)
, required_start_temperature(-1)
, layer_nr(layer_nr)
, is_initial_layer(is_initial_layer)
, is_raft_layer(is_raft_layer)
, layer_thickness(layer_thickness)
, fan_speed_layer_time_settings(fan_speed_layer_time_settings)
, retraction_config(retraction_config)
, extrudeSpeedFactor(1.0)
, travelSpeedFactor(1.0)
, extraTime(0.0)
, totalPrintTime(0)
{
}

void ExtruderPlan::setExtrudeSpeedFactor(double speedFactor)
{
    this->extrudeSpeedFactor = speedFactor;
}

double ExtruderPlan::getExtrudeSpeedFactor()
{
    return this->extrudeSpeedFactor;
}

void ExtruderPlan::setTravelSpeedFactor(double speedFactor)
{
    if (speedFactor < 1) speedFactor = 1.0;
    this->travelSpeedFactor = speedFactor;
}

double ExtruderPlan::getTravelSpeedFactor()
{
    return this->travelSpeedFactor;
}

void ExtruderPlan::setFanSpeed(double _fan_speed)
{
    fan_speed = _fan_speed;
}
double ExtruderPlan::getFanSpeed()
{
    return fan_speed;
}


GCodePath* LayerPlan::getLatestPathWithConfig(const GCodePathConfig& config, SpaceFillType space_fill_type, float flow, bool spiralize, double speed_factor)
{
    std::vector<GCodePath>& paths = extruder_plans.back().paths;
    if (paths.size() > 0 && paths.back().config == &config && !paths.back().done && paths.back().flow == flow && paths.back().speed_factor == speed_factor) // spiralize can only change when a travel path is in between
        return &paths.back();
    paths.emplace_back(config, space_fill_type, flow, spiralize, speed_factor);
    GCodePath* ret = &paths.back();
    return ret;
}

void LayerPlan::forceNewPathStart()
{
    std::vector<GCodePath>& paths = extruder_plans.back().paths;
    if (paths.size() > 0)
        paths[paths.size()-1].done = true;
}

LayerPlan::LayerPlan(const SliceDataStorage& storage, LayerIndex layer_nr, coord_t z, coord_t layer_thickness, size_t start_extruder, const std::vector<FanSpeedLayerTimeSettings>& fan_speed_layer_time_settings_per_extruder, CombingMode combing_mode, coord_t comb_boundary_offset, coord_t comb_move_inside_distance, bool travel_avoid_other_parts, bool travel_avoid_supports, coord_t travel_avoid_distance)
: storage(storage)
, configs_storage(storage, layer_nr, layer_thickness)
, z(z)
, layer_nr(layer_nr)
, is_initial_layer(layer_nr == 0 - Raft::getTotalExtraLayers(storage))
, is_raft_layer(layer_nr < 0 - Raft::getFillerLayerCount(storage))
, layer_thickness(layer_thickness)
, has_prime_tower_planned_per_extruder(Application::getInstance().current_slice->scene.extruders.size(), false)
, last_extruder_previous_layer(start_extruder)
, last_planned_extruder(&Application::getInstance().current_slice->scene.extruders[start_extruder])
, first_travel_destination_is_inside(false) // set properly when addTravel is called for the first time (otherwise not set properly)
, comb_boundary_inside1(computeCombBoundaryInside(combing_mode, 1))
, comb_boundary_inside2(computeCombBoundaryInside(combing_mode, 2))
, comb_move_inside_distance(comb_move_inside_distance)
, fan_speed_layer_time_settings_per_extruder(fan_speed_layer_time_settings_per_extruder)
{
    int current_extruder = start_extruder;
    comb = nullptr;
    was_inside = true; // not used, because the first travel move is bogus
    is_inside = false; // assumes the next move will not be to inside a layer part (overwritten just before going into a layer part)
    if (combing_mode != CombingMode::OFF)
    {
        comb = new Comb(storage, layer_nr, comb_boundary_inside1, comb_boundary_inside2, comb_boundary_offset, travel_avoid_other_parts, travel_avoid_supports, travel_avoid_distance, comb_move_inside_distance);
    }
    else
    {
        comb = nullptr;
    }
    for (const ExtruderTrain& train : Application::getInstance().current_slice->scene.extruders)
    {
        layer_start_pos_per_extruder.emplace_back(train.settings.get<coord_t>("layer_start_x"), train.settings.get<coord_t>("layer_start_y"));
    }
    extruder_plans.reserve(Application::getInstance().current_slice->scene.extruders.size());
    extruder_plans.emplace_back(current_extruder, layer_nr, is_initial_layer, is_raft_layer, layer_thickness, fan_speed_layer_time_settings_per_extruder[current_extruder], storage.retraction_config_per_extruder[current_extruder]);

    for (size_t extruder_nr = 0; extruder_nr < Application::getInstance().current_slice->scene.extruders.size(); extruder_nr++)
    { //Skirt and brim.
        skirt_brim_is_processed[extruder_nr] = false;
    }
}

LayerPlan::~LayerPlan()
{
    if (comb)
        delete comb;
}

ExtruderTrain* LayerPlan::getLastPlannedExtruderTrain()
{
    return last_planned_extruder;
}


Polygons LayerPlan::computeCombBoundaryInside(CombingMode combing_mode, int max_inset)
{
    if (combing_mode == CombingMode::OFF)
    {
        return Polygons();
    }
    if (layer_nr < 0)
    { // when a raft is present
        if (combing_mode == CombingMode::NO_SKIN)
        {
            return Polygons();
        }
        else
        {
            return storage.raftOutline.offset(MM2INT(0.1));
        }
    }
    else 
    {
        Polygons comb_boundary;
        for (const SliceMeshStorage& mesh : storage.meshes)
        {
            const SliceLayer& layer = mesh.layers[layer_nr];
            if (mesh.settings.get<bool>("infill_mesh")) {
                continue;
            }
            if (mesh.settings.get<CombingMode>("retraction_combing") == CombingMode::NO_SKIN)
            {
                for (const SliceLayerPart& part : layer.parts)
                {
                    comb_boundary.add(part.infill_area);
                }
            }
            else
            {
                layer.getInnermostWalls(comb_boundary, max_inset, mesh);
            }
        }
        return comb_boundary;
    }
}

void LayerPlan::setIsInside(bool _is_inside)
{
    is_inside = _is_inside;
}

bool LayerPlan::setExtruder(const size_t extruder_nr)
{
    if (extruder_nr == getExtruder())
    {
        return false;
    }
    setIsInside(false);
    { // handle end position of the prev extruder
        ExtruderTrain* train = getLastPlannedExtruderTrain();
        const bool end_pos_absolute = train->settings.get<bool>("machine_extruder_end_pos_abs");
        Point end_pos(train->settings.get<coord_t>("machine_extruder_end_pos_x"), train->settings.get<coord_t>("machine_extruder_end_pos_y"));
        if (!end_pos_absolute)
        {
            end_pos += getLastPlannedPositionOrStartingPosition();
        }
        else 
        {
            const Point extruder_offset(train->settings.get<coord_t>("machine_nozzle_offset_x"), train->settings.get<coord_t>("machine_nozzle_offset_y"));
            end_pos += extruder_offset; // absolute end pos is given as a head position
        }
        if (end_pos_absolute || last_planned_position)
        {
            addTravel(end_pos); //  + extruder_offset cause it
        }
    }
    if (extruder_plans.back().paths.empty() && extruder_plans.back().inserts.empty())
    { // first extruder plan in a layer might be empty, cause it is made with the last extruder planned in the previous layer
        extruder_plans.back().extruder = extruder_nr;
    }
    else 
    {
        extruder_plans.emplace_back(extruder_nr, layer_nr, is_initial_layer, is_raft_layer, layer_thickness, fan_speed_layer_time_settings_per_extruder[extruder_nr], storage.retraction_config_per_extruder[extruder_nr]);
        assert(extruder_plans.size() <= Application::getInstance().current_slice->scene.extruders.size() && "Never use the same extruder twice on one layer!");
    }
    last_planned_extruder = &Application::getInstance().current_slice->scene.extruders[extruder_nr];

    { // handle starting pos of the new extruder
        ExtruderTrain* train = getLastPlannedExtruderTrain();
        const bool start_pos_absolute = train->settings.get<bool>("machine_extruder_start_pos_abs");
        Point start_pos(train->settings.get<coord_t>("machine_extruder_start_pos_x"), train->settings.get<coord_t>("machine_extruder_start_pos_y"));
        if (!start_pos_absolute)
        {
            start_pos += getLastPlannedPositionOrStartingPosition();
        }
        else 
        {
            Point extruder_offset(train->settings.get<coord_t>("machine_nozzle_offset_x"), train->settings.get<coord_t>("machine_nozzle_offset_y"));
            start_pos += extruder_offset; // absolute start pos is given as a head position
        }
        if (start_pos_absolute || last_planned_position)
        {
            last_planned_position = start_pos;
        }
    }
    return true;
}

void LayerPlan::moveInsideCombBoundary(const coord_t distance)
{
    constexpr coord_t max_dist2 = MM2INT(2.0) * MM2INT(2.0); // if we are further than this distance, we conclude we are not inside even though we thought we were.
    // this function is to be used to move from the boudary of a part to inside the part
    Point p = getLastPlannedPositionOrStartingPosition(); // copy, since we are going to move p
    if (PolygonUtils::moveInside(comb_boundary_inside2, p, distance, max_dist2) != NO_INDEX)
    {
        //Move inside again, so we move out of tight 90deg corners
        PolygonUtils::moveInside(comb_boundary_inside2, p, distance, max_dist2);
        if (comb_boundary_inside2.inside(p))
        {
            addTravel_simple(p);
            //Make sure the that any retraction happens after this move, not before it by starting a new move path.
            forceNewPathStart();
        }
    }
}

bool LayerPlan::getPrimeTowerIsPlanned(unsigned int extruder_nr) const
{
    return has_prime_tower_planned_per_extruder[extruder_nr];
}

void LayerPlan::setPrimeTowerIsPlanned(unsigned int extruder_nr)
{
    has_prime_tower_planned_per_extruder[extruder_nr] = true;
}

std::optional<std::pair<Point, bool>> LayerPlan::getFirstTravelDestinationState() const
{
    std::optional<std::pair<Point, bool>> ret;
    if (first_travel_destination)
    {
        ret = std::make_pair(*first_travel_destination, first_travel_destination_is_inside);
    }
    return ret;
}

GCodePath& LayerPlan::addTravel(Point p, bool force_comb_retract)
{
    const GCodePathConfig& travel_config = configs_storage.travel_config_per_extruder[getExtruder()];
    const RetractionConfig& retraction_config = storage.retraction_config_per_extruder[getExtruder()];

    GCodePath* path = getLatestPathWithConfig(travel_config, SpaceFillType::None);

    bool combed = false;

    const ExtruderTrain* extr = getLastPlannedExtruderTrain();

    const bool perform_z_hops = extr->settings.get<bool>("retraction_hop_enabled");
    const coord_t maximum_travel_resolution = extr->settings.get<coord_t>("meshfix_maximum_travel_resolution");

    const bool is_first_travel_of_extruder_after_switch = extruder_plans.back().paths.size() == 1 && (extruder_plans.size() > 1 || last_extruder_previous_layer != getExtruder());
    bool bypass_combing = is_first_travel_of_extruder_after_switch && extr->settings.get<bool>("retraction_hop_after_extruder_switch");

    const bool is_first_travel_of_layer = !static_cast<bool>(last_planned_position);
    if (is_first_travel_of_layer)
    {
        bypass_combing = true; // first travel move is bogus; it is added after this and the previous layer have been planned in LayerPlanBuffer::addConnectingTravelMove
        first_travel_destination = p;
        first_travel_destination_is_inside = is_inside;
        forceNewPathStart(); // force a new travel path after this first bogus move
    }
    else if (force_comb_retract && last_planned_position && !shorterThen(*last_planned_position - p, retraction_config.retraction_min_travel_distance))
    {
        // path is not shorter than min travel distance, force a retraction
        path->retract = true;
    }

    if (comb != nullptr && !bypass_combing)
    {
        const bool perform_z_hops_only_when_collides = extr->settings.get<bool>("retraction_hop_only_when_collides");

        CombPaths combPaths;
        const bool via_outside_makes_combing_fail = perform_z_hops && !perform_z_hops_only_when_collides;
        const bool fail_on_unavoidable_obstacles = perform_z_hops && perform_z_hops_only_when_collides;
        combed = comb->calc(*last_planned_position, p, combPaths, was_inside, is_inside, retraction_config.retraction_min_travel_distance, via_outside_makes_combing_fail, fail_on_unavoidable_obstacles);
        if (combed)
        {
            bool retract = path->retract || combPaths.size() > 1;
            if (!retract)
            { // check whether we want to retract
                if (combPaths.throughAir)
                {
                    retract = true;
                }
                else
                {
                    for (CombPath& combPath : combPaths)
                    { // retract when path moves through a boundary
                        if (combPath.cross_boundary)
                        {
                            retract = true;
                            break;
                        }
                    }
                }
                if (combPaths.size() == 1)
                {
                    CombPath comb_path = combPaths[0];
                    if (extr->settings.get<bool>("limit_support_retractions") &&
                        combPaths.throughAir && !comb_path.cross_boundary && comb_path.size() == 2 && comb_path[0] == *last_planned_position && comb_path[1] == p)
                    { // limit the retractions from support to support, which didn't cross anything
                        retract = false;
                    }
                }
            }

            coord_t dist = 0;
            Point last_point((last_planned_position) ? *last_planned_position : Point(0, 0));
            for (CombPath& combPath : combPaths)
            { // add all comb paths (don't do anything special for paths which are moving through air)
                if (combPath.size() == 0)
                {
                    continue;
                }
                for (Point& comb_point : combPath)
                {
                    if (path->points.empty() || vSize2(path->points.back() - comb_point) > maximum_travel_resolution * maximum_travel_resolution)
                    {
                        path->points.push_back(comb_point);
                        dist += vSize(last_point - comb_point);
                        last_point = comb_point;
                    }
                }
                last_planned_position = combPath.back();
                dist += vSize(last_point - p);
                const coord_t retract_threshold = extr->settings.get<coord_t>("retraction_combing_max_distance");
                path->retract = retract || (retract_threshold > 0 && dist > retract_threshold);
                // don't perform a z-hop
            }
        }
    }
    
    // no combing? retract only when path is not shorter than minimum travel distance
    if (!combed && !is_first_travel_of_layer && last_planned_position && !shorterThen(*last_planned_position - p, retraction_config.retraction_min_travel_distance))
    {
        if (was_inside) // when the previous location was from printing something which is considered inside (not support or prime tower etc)
        {               // then move inside the printed part, so that we don't ooze on the outer wall while retraction, but on the inside of the print.
            assert (extr != nullptr);
            coord_t innermost_wall_line_width = extr->settings.get<coord_t>((extr->settings.get<size_t>("wall_line_count") > 1) ? "wall_line_width_x" : "wall_line_width_0");
            if (layer_nr == 0)
            {
                innermost_wall_line_width *= extr->settings.get<Ratio>("initial_layer_line_width_factor");
            }
            moveInsideCombBoundary(innermost_wall_line_width);
        }
        path->retract = true;
        path->perform_z_hop = perform_z_hops;
    }

    GCodePath& ret = addTravel_simple(p, path);
    was_inside = is_inside;
    return ret;
}

GCodePath& LayerPlan::addTravel_simple(Point p, GCodePath* path)
{
    bool is_first_travel_of_layer = !static_cast<bool>(last_planned_position);
    if (is_first_travel_of_layer)
    { // spiralize calls addTravel_simple directly as the first travel move in a layer
        first_travel_destination = p;
        first_travel_destination_is_inside = is_inside;
    }
    if (path == nullptr)
    {
        path = getLatestPathWithConfig(configs_storage.travel_config_per_extruder[getExtruder()], SpaceFillType::None);
    }
    path->points.push_back(p);
    last_planned_position = p;
    return *path;
}

void LayerPlan::planPrime()
{
    forceNewPathStart();
    constexpr float prime_blob_wipe_length = 10.0;
    GCodePath& prime_travel = addTravel_simple(getLastPlannedPositionOrStartingPosition() + Point(0, MM2INT(prime_blob_wipe_length)));
    prime_travel.retract = false;
    prime_travel.perform_prime = true;
    forceNewPathStart();
}

void LayerPlan::addExtrusionMove(Point p, const GCodePathConfig& config, SpaceFillType space_fill_type, float flow, bool spiralize, double speed_factor, double fan_speed)
{
    GCodePath* path = getLatestPathWithConfig(config, space_fill_type, flow, spiralize, speed_factor);
    path->points.push_back(p);
    path->setFanSpeed(fan_speed);
    last_planned_position = p;
}

void LayerPlan::addPolygon(ConstPolygonRef polygon, int start_idx, const GCodePathConfig& config, WallOverlapComputation* wall_overlap_computation, coord_t wall_0_wipe_dist, bool spiralize, float flow_ratio, bool always_retract)
{
    Point p0 = polygon[start_idx];
    addTravel(p0, always_retract);
    for (unsigned int point_idx = 1; point_idx < polygon.size(); point_idx++)
    {
        Point p1 = polygon[(start_idx + point_idx) % polygon.size()];
        float flow = (wall_overlap_computation)? flow_ratio * wall_overlap_computation->getFlow(p0, p1) : flow_ratio;
        addExtrusionMove(p1, config, SpaceFillType::Polygons, flow, spiralize);
        p0 = p1;
    }
    if (polygon.size() > 2)
    {
        const Point& p1 = polygon[start_idx];
        float flow = (wall_overlap_computation)? flow_ratio * wall_overlap_computation->getFlow(p0, p1) : flow_ratio;
        addExtrusionMove(p1, config, SpaceFillType::Polygons, flow, spiralize);

        if (wall_0_wipe_dist > 0)
        { // apply outer wall wipe
            p0 = polygon[start_idx];
            int distance_traversed = 0;
            for (unsigned int point_idx = 1; ; point_idx++)
            {
                Point p1 = polygon[(start_idx + point_idx) % polygon.size()];
                int p0p1_dist = vSize(p1 - p0);
                if (distance_traversed + p0p1_dist >= wall_0_wipe_dist)
                {
                    Point vector = p1 - p0;
                    Point half_way = p0 + normal(vector, wall_0_wipe_dist - distance_traversed);
                    addTravel_simple(half_way);
                    break;
                }
                else
                {
                    addTravel_simple(p1);
                    distance_traversed += p0p1_dist;
                }
                p0 = p1;
            }
            forceNewPathStart();
        }
    }
    else 
    {
        logWarning("WARNING: line added as polygon! (LayerPlan)\n");
    }
}

void LayerPlan::addPolygonsByOptimizer(const Polygons& polygons, const GCodePathConfig& config, WallOverlapComputation* wall_overlap_computation, const ZSeamConfig& z_seam_config, coord_t wall_0_wipe_dist, bool spiralize, float flow_ratio, bool always_retract, bool reverse_order)
{
    
    if (polygons.size() == 0)
    {
        return;
    }
    PathOrderOptimizer orderOptimizer(getLastPlannedPositionOrStartingPosition(), z_seam_config);
    for (unsigned int poly_idx = 0; poly_idx < polygons.size(); poly_idx++)
    {
        orderOptimizer.addPolygon(polygons[poly_idx]);
    }
    orderOptimizer.optimize();
    
    if(reverse_order == false)
    {
        for (unsigned int poly_idx : orderOptimizer.polyOrder)
        {
            addPolygon(polygons[poly_idx], orderOptimizer.polyStart[poly_idx], config, wall_overlap_computation, wall_0_wipe_dist, spiralize, flow_ratio, always_retract);
        }
    }
    else
    {
        for(int index = orderOptimizer.polyOrder.size() - 1; index >= 0; --index)
        {
            int poly_idx = orderOptimizer.polyOrder[index];
            addPolygon(polygons[poly_idx], orderOptimizer.polyStart[poly_idx], config, wall_overlap_computation, wall_0_wipe_dist, spiralize, flow_ratio, always_retract);
        }
    }
}

static const float max_non_bridge_line_volume = 100000.0f; // limit to accumulated "volume" of non-bridge lines which is proportional to distance x extrusion rate

void LayerPlan::addWallLine(const Point& p0, const Point& p1, const GCodePathConfig& non_bridge_config, const GCodePathConfig& bridge_config, float flow, float& non_bridge_line_volume, double& speed_factor, double distance_to_bridge_start)
{
    const double min_line_len = 5; // we ignore lines less than 5um long
    const double acceleration_segment_len = 1000; // accelerate using segments of this length
    const double acceleration_factor = 0.85; // must be < 1, the larger the value, the slower the acceleration
    const bool spiralize = false;

    const ExtruderTrain* extr = getLastPlannedExtruderTrain();
    const coord_t min_bridge_line_len = extr->settings.get<coord_t>("bridge_wall_min_length");
    const Ratio bridge_wall_coast = extr->settings.get<Ratio>("bridge_wall_coast");

    Point cur_point = p0;

    // helper function to add a single non-bridge line

    // If the line precedes a bridge line, it may be coasted to reduce the nozzle pressure before the bridge is reached

    // alternatively, if the line follows a bridge line, it may be segmented and the print speed gradually increased to reduce under-extrusion

    auto addNonBridgeLine = [&](const Point& line_end)
    {
        double distance_to_line_end = vSize(cur_point - line_end);

        while (distance_to_line_end > min_line_len)
        {
            // if we are accelerating after a bridge line, the segment length is less than the whole line length
            Point segment_end = (speed_factor == 1 || distance_to_line_end < acceleration_segment_len) ? line_end : cur_point + (line_end - cur_point) * acceleration_segment_len / distance_to_line_end;

            // flow required for the next line segment - when accelerating after a bridge segment, the flow is increased in inverse proportion to the speed_factor
            // so the slower the feedrate, the greater the flow - the idea is to get the extruder back to normal pressure as quickly as possible
            const float segment_flow = (speed_factor < 1) ? flow * (1 / speed_factor) : flow;

            // if a bridge is present in this wall, this particular segment may need to be partially or wholely coasted
            if (distance_to_bridge_start > 0)
            {
                // speed_flow_factor approximates how the extrusion rate alters between the non-bridge wall line and the following bridge wall line
                // if the extrusion rates are the same, its value will be 1, if the bridge config extrusion rate is < the non-bridge config extrusion rate, the value is < 1

                const double speed_flow_factor = (bridge_config.getSpeed() * bridge_config.getFlowPercentage()) / (non_bridge_config.getSpeed() * non_bridge_config.getFlowPercentage());

                // coast distance is proportional to distance, speed and flow of non-bridge segments just printed and is throttled by speed_flow_factor
                const double coast_dist = std::min(non_bridge_line_volume, max_non_bridge_line_volume) * (1 - speed_flow_factor) * bridge_wall_coast / 40;

                if ((distance_to_bridge_start - distance_to_line_end) <= coast_dist)
                {
                    // coast takes precedence over acceleration
                    segment_end = line_end;
                }

                const double len = vSize(cur_point - segment_end);
                if (coast_dist > 0 && ((distance_to_bridge_start - len) <= coast_dist))
                {
                    if ((len - coast_dist) > min_line_len)
                    {
                        // segment is longer than coast distance so extrude using non-bridge config to start of coast
                        addExtrusionMove(segment_end + coast_dist * (cur_point - segment_end) / len, non_bridge_config, SpaceFillType::Polygons, segment_flow, spiralize, speed_factor);
                    }
                    // then coast to start of bridge segment
                    addExtrusionMove(segment_end, non_bridge_config, SpaceFillType::Polygons, 0, spiralize, speed_factor);
                }
                else
                {
                    // no coasting required, just normal segment using non-bridge config
                    addExtrusionMove(segment_end, non_bridge_config, SpaceFillType::Polygons, segment_flow, spiralize, speed_factor);
                }

                distance_to_bridge_start -= len;
            }
            else
            {
                // no coasting required, just normal segment using non-bridge config
                addExtrusionMove(segment_end, non_bridge_config, SpaceFillType::Polygons, segment_flow, spiralize, speed_factor);
            }
            non_bridge_line_volume += vSize(cur_point - segment_end) * segment_flow * speed_factor * non_bridge_config.getSpeed();
            cur_point = segment_end;
            speed_factor = 1 - (1 - speed_factor) * acceleration_factor;
            distance_to_line_end = vSize(cur_point - line_end);
        }
    };

    if (bridge_wall_mask.empty())
    {
        // no bridges required
        addExtrusionMove(p1, non_bridge_config, SpaceFillType::Polygons, flow);
    }
    else
    {
        // bridges may be required
        if (PolygonUtils::polygonCollidesWithLineSegment(bridge_wall_mask, p0, p1))
        {
            // the line crosses the boundary between supported and non-supported regions so one or more bridges are required

            // determine which segments of the line are bridges

            Polygon line_poly;
            line_poly.add(p0);
            line_poly.add(p1);
            Polygons line_polys;
            line_polys.add(line_poly);
            line_polys = bridge_wall_mask.intersectionPolyLines(line_polys);

            // line_polys now contains the wall lines that need to be printed using bridge_config

            while (line_polys.size() > 0)
            {
                // find the bridge line segment that's nearest to the current point
                int nearest = 0;
                float smallest_dist2 = vSize2f(cur_point - line_polys[0][0]);
                for(unsigned i = 1; i < line_polys.size(); ++i)
                {
                    float dist2 = vSize2f(cur_point - line_polys[i][0]);
                    if (dist2 < smallest_dist2)
                    {
                        nearest = i;
                        smallest_dist2 = dist2;
                    }
                }
                ConstPolygonRef bridge = line_polys[nearest];

                // set b0 to the nearest vertex and b1 the furthest
                Point b0 = bridge[0];
                Point b1 = bridge[1];

                if (vSize2f(cur_point - b1) < vSize2f(cur_point - b0))
                {
                    // swap vertex order
                    b0 = bridge[1];
                    b1 = bridge[0];
                }

                // extrude using non_bridge_config to the start of the next bridge segment

                addNonBridgeLine(b0);

                const double bridge_line_len = vSize(b1 - cur_point);

                if (bridge_line_len >= min_bridge_line_len)
                {
                    // extrude using bridge_config to the end of the next bridge segment

                    if (bridge_line_len > min_line_len)
                    {
                        addExtrusionMove(b1, bridge_config, SpaceFillType::Polygons, flow);
                        non_bridge_line_volume = 0;
                        cur_point = b1;
                        // after a bridge segment, start slow and accelerate to avoid under-extrusion due to extruder lag
                        speed_factor = std::min(bridge_config.getSpeed() / non_bridge_config.getSpeed(), 1.0);
                    }
                }
                else
                {
                    // treat the short bridge line just like a normal line

                    addNonBridgeLine(b1);
                }

                // finished with this segment
                line_polys.remove(nearest);
            }

            // if we haven't yet reached p1, fill the gap with non_bridge_config line
            addNonBridgeLine(p1);
        }
        else if (bridge_wall_mask.inside(p0, true) && vSize(p0 - p1) >= min_bridge_line_len)
        {
            // both p0 and p1 must be above air (the result will be ugly!)
            addExtrusionMove(p1, bridge_config, SpaceFillType::Polygons, flow);
            non_bridge_line_volume = 0;
        }
        else
        {
            // no part of the line is above air or the line is too short to print as a bridge line
            addNonBridgeLine(p1);
        }
    }
}

void LayerPlan::addWall(ConstPolygonRef wall, int start_idx, const GCodePathConfig& non_bridge_config, const GCodePathConfig& bridge_config, WallOverlapComputation* wall_overlap_computation, coord_t wall_0_wipe_dist, float flow_ratio, bool always_retract)
{
    // make sure wall start point is not above air!
    if (!bridge_wall_mask.empty()) {
        int count = wall.size(); // avoid infinite loop if none of the points are above a solid region
        while (count-- > 0 && bridge_wall_mask.inside(wall[start_idx], true))
        {
            if (++start_idx >= (int)wall.size())
            {
                start_idx = 0;
            }
        }
    }

    float non_bridge_line_volume = max_non_bridge_line_volume; // assume extruder is fully pressurised before first non-bridge line is output
    double speed_factor = 1.0; // start first line at normal speed
    double distance_to_bridge_start = 0; // will be updated before each line is processed

    const ExtruderTrain* extr = getLastPlannedExtruderTrain();
    const double min_bridge_line_len = extr->settings.get<coord_t>("bridge_wall_min_length");
    const double wall_min_flow = extr->settings.get<Ratio>("wall_min_flow");
    const bool wall_min_flow_retract = extr->settings.get<bool>("wall_min_flow_retract");

    // helper function to calculate the distance from the start of the current wall line to the first bridge segment

    auto computeDistanceToBridgeStart = [&](unsigned current_index)
    {
        distance_to_bridge_start = 0;

        if (!bridge_wall_mask.empty())
        {
            // there is air below the part so iterate through the lines that have not yet been output accumulating the total distance to the first bridge segment
            for (unsigned point_idx = current_index; point_idx < wall.size(); ++point_idx)
            {
                const Point& p0 = wall[point_idx];
                const Point& p1 = wall[(point_idx + 1) % wall.size()];

                if (PolygonUtils::polygonCollidesWithLineSegment(bridge_wall_mask, p0, p1))
                {
                    // the line crosses the boundary between supported and non-supported regions so it will contain one or more bridge segments

                    // determine which segments of the line are bridges

                    Polygon line_poly;
                    line_poly.add(p0);
                    line_poly.add(p1);
                    Polygons line_polys;
                    line_polys.add(line_poly);
                    line_polys = bridge_wall_mask.intersectionPolyLines(line_polys);

                    while (line_polys.size() > 0)
                    {
                        // find the bridge line segment that's nearest to p0
                        int nearest = 0;
                        float smallest_dist2 = vSize2f(p0 - line_polys[0][0]);
                        for(unsigned i = 1; i < line_polys.size(); ++i)
                        {
                            float dist2 = vSize2f(p0 - line_polys[i][0]);
                            if (dist2 < smallest_dist2)
                            {
                                nearest = i;
                                smallest_dist2 = dist2;
                            }
                        }
                        ConstPolygonRef bridge = line_polys[nearest];

                        // set b0 to the nearest vertex and b1 the furthest
                        Point b0 = bridge[0];
                        Point b1 = bridge[1];

                        if (vSize2f(p0 - b1) < vSize2f(p0 - b0))
                        {
                            // swap vertex order
                            b0 = bridge[1];
                            b1 = bridge[0];
                        }

                        distance_to_bridge_start += vSize(b0 - p0);

                        const double bridge_line_len = vSize(b1 - b0);

                        if (bridge_line_len >= min_bridge_line_len)
                        {
                            // job done, we have found the first bridge line
                            return;
                        }

                        distance_to_bridge_start += bridge_line_len;

                        // finished with this segment
                        line_polys.remove(nearest);
                    }
                }
                else if (!bridge_wall_mask.inside(p0, true))
                {
                    // none of the line is over air
                    distance_to_bridge_start += vSize(p1 - p0);
                }
            }

            // we have got all the way to the end of the wall without finding a bridge segment so disable coasting by setting distance_to_bridge_start back to 0

            distance_to_bridge_start = 0;
        }
    };

    bool travel_required = false; // true when a wall has been omitted due to its flow being less than the minimum required

    bool first_line = true;

    Point p0 = wall[start_idx];

    for (unsigned int point_idx = 1; point_idx < wall.size(); point_idx++)
    {
        const Point& p1 = wall[(start_idx + point_idx) % wall.size()];
        const float flow = (wall_overlap_computation)? flow_ratio * wall_overlap_computation->getFlow(p0, p1) : flow_ratio;

        if (!bridge_wall_mask.empty())
        {
            computeDistanceToBridgeStart((start_idx + point_idx - 1) % wall.size());
        }

        if (flow >= wall_min_flow)
        {
            if (first_line || travel_required)
            {
                addTravel(p0, (first_line) ? always_retract : wall_min_flow_retract);
                first_line = false;
                travel_required = false;
            }
            addWallLine(p0, p1, non_bridge_config, bridge_config, flow, non_bridge_line_volume, speed_factor, distance_to_bridge_start);
        }
        else
        {
            travel_required = true;
        }

        p0 = p1;
    }

    if (wall.size() > 2)
    {
        const Point& p1 = wall[start_idx];
        const float flow = (wall_overlap_computation)? flow_ratio * wall_overlap_computation->getFlow(p0, p1) : flow_ratio;

        if (!bridge_wall_mask.empty())
        {
            computeDistanceToBridgeStart((start_idx + wall.size() - 1) % wall.size());
        }

        if (flow >= wall_min_flow)
        {
            if (travel_required)
            {
                addTravel(p0, wall_min_flow_retract);
            }
            addWallLine(p0, p1, non_bridge_config, bridge_config, flow, non_bridge_line_volume, speed_factor, distance_to_bridge_start);

            if (wall_0_wipe_dist > 0)
            { // apply outer wall wipe
                p0 = wall[start_idx];
                int distance_traversed = 0;
                for (unsigned int point_idx = 1; ; point_idx++)
                {
                    Point p1 = wall[(start_idx + point_idx) % wall.size()];
                    int p0p1_dist = vSize(p1 - p0);
                    if (distance_traversed + p0p1_dist >= wall_0_wipe_dist)
                    {
                        Point vector = p1 - p0;
                        Point half_way = p0 + normal(vector, wall_0_wipe_dist - distance_traversed);
                        addTravel_simple(half_way);
                        break;
                    }
                    else
                    {
                        addTravel_simple(p1);
                        distance_traversed += p0p1_dist;
                    }
                    p0 = p1;
                }
                forceNewPathStart();
            }
        }
    }
    else
    {
        logWarning("WARNING: line added as polygon! (LayerPlan)\n");
    }
}

void LayerPlan::addWalls(const Polygons& walls, const GCodePathConfig& non_bridge_config, const GCodePathConfig& bridge_config, WallOverlapComputation* wall_overlap_computation, const ZSeamConfig& z_seam_config, coord_t wall_0_wipe_dist, float flow_ratio, bool always_retract)
{
    PathOrderOptimizer orderOptimizer(getLastPlannedPositionOrStartingPosition(), z_seam_config);
    for (unsigned int poly_idx = 0; poly_idx < walls.size(); poly_idx++)
    {
        orderOptimizer.addPolygon(walls[poly_idx]);
    }
    orderOptimizer.optimize();
    for (unsigned int poly_idx : orderOptimizer.polyOrder)
    {
        addWall(walls[poly_idx], orderOptimizer.polyStart[poly_idx], non_bridge_config, bridge_config, wall_overlap_computation, wall_0_wipe_dist, flow_ratio, always_retract);
    }
}

void LayerPlan::addLinesByOptimizer(const Polygons& polygons, const GCodePathConfig& config, SpaceFillType space_fill_type, bool enable_travel_optimization, int wipe_dist, float flow_ratio, std::optional<Point> near_start_location, double fan_speed)
{
    Polygons boundary;
    if (enable_travel_optimization && comb_boundary_inside2.size() > 0)
    {
        // use the combing boundary inflated so that all infill lines are inside the boundary
        int dist = 0;
        if (layer_nr >= 0)
        {
            // determine how much the skin/infill lines overlap the combing boundary
            for (const SliceMeshStorage& mesh : storage.meshes)
            {
                const coord_t overlap = std::max(mesh.settings.get<coord_t>("skin_overlap_mm"), mesh.settings.get<coord_t>("infill_overlap_mm"));
                if (overlap > dist)
                {
                    dist = overlap;
                }
            }
            dist += 100; // ensure boundary is slightly outside all skin/infill lines
        }
        boundary.add(comb_boundary_inside2.offset(dist));
        // simplify boundary to cut down processing time
        boundary.simplify(100, 100);
    }
    LineOrderOptimizer orderOptimizer(near_start_location.value_or(getLastPlannedPositionOrStartingPosition()), &boundary);
    for (unsigned int line_idx = 0; line_idx < polygons.size(); line_idx++)
    {
        orderOptimizer.addPolygon(polygons[line_idx]);
    }
    orderOptimizer.optimize();

    for (unsigned int order_idx = 0; order_idx < orderOptimizer.polyOrder.size(); order_idx++)
    {
        const unsigned int poly_idx = orderOptimizer.polyOrder[order_idx];
        ConstPolygonRef polygon = polygons[poly_idx];
        const size_t start = orderOptimizer.polyStart[poly_idx];
        const size_t end = 1 - start;
        const Point& p0 = polygon[start];
        addTravel(p0);
        const Point& p1 = polygon[end];
        addExtrusionMove(p1, config, space_fill_type, flow_ratio, false, 1.0, fan_speed);

        // Wipe
        if (wipe_dist != 0)
        {
            bool wipe = true;
            int line_width = config.getLineWidth();

            // Don't wipe is current extrusion is too small
            if (vSize2(p1 - p0) <= line_width * line_width * 4)
            {
                wipe = false;
            }

            // Don't wipe if next starting point is very near
            if (wipe && (order_idx < orderOptimizer.polyOrder.size() - 1))
            {
                const unsigned int next_poly_idx = orderOptimizer.polyOrder[order_idx + 1];
                ConstPolygonRef next_polygon = polygons[next_poly_idx];
                const size_t next_start = orderOptimizer.polyStart[next_poly_idx];
                const Point& next_p0 = next_polygon[next_start];
                if (vSize2(next_p0 - p1) <= line_width * line_width * 4)
                {
                    wipe = false;
                }
            }

            if (wipe)
            {
                addExtrusionMove(p1 + normal(p1-p0, wipe_dist), config, space_fill_type, 0.0, false, 1.0, fan_speed);
            }
        }
    }
}

void LayerPlan::spiralizeWallSlice(const GCodePathConfig& config, ConstPolygonRef wall, ConstPolygonRef last_wall, const int seam_vertex_idx, const int last_seam_vertex_idx)
{
    const Point origin = (last_seam_vertex_idx >= 0) ? last_wall[last_seam_vertex_idx] : wall[seam_vertex_idx];
    addTravel_simple(origin);

    const int n_points = wall.size();
    Polygons last_wall_polygons;
    last_wall_polygons.add(last_wall);
    const int max_dist2 = config.getLineWidth() * config.getLineWidth() * 4; // (2 * lineWidth)^2;

    double total_length = 0.0; // determine the length of the complete wall
    Point p0 = origin;
    for (int wall_point_idx = 1; wall_point_idx <= n_points; ++wall_point_idx)
    {
        const Point& p1 = wall[(seam_vertex_idx + wall_point_idx) % n_points];
        total_length += vSizeMM(p1 - p0);
        p0 = p1;
    }

    if (total_length == 0.0)
    {
        // nothing to do
        return;
    }

    // extrude to the points following the seam vertex
    // the last point is the seam vertex as the polygon is a loop
    double wall_length = 0.0;
    p0 = origin;
    const bool smooth_contours = Application::getInstance().current_slice->scene.current_mesh_group->settings.get<bool>("smooth_spiralized_contours");
    for (int wall_point_idx = 1; wall_point_idx <= n_points; ++wall_point_idx)
    {
        // p is a point from the current wall polygon
        const Point& p = wall[(seam_vertex_idx + wall_point_idx) % n_points];
        if (smooth_contours)
        {
            wall_length += vSizeMM(p - p0);
            p0 = p;

            // now find the point on the last wall that is closest to p
            ClosestPolygonPoint cpp = PolygonUtils::findClosest(p, last_wall_polygons);
            // if we found a point and it's not further away than max_dist2, use it
            if (cpp.isValid() && vSize2(cpp.location - p) <= max_dist2)
            {
                // interpolate between cpp.location and p depending on how far we have progressed along wall
                addExtrusionMove(cpp.location + (p - cpp.location) * (wall_length / total_length), config, SpaceFillType::Polygons, 1.0, true);
            }
            else
            {
                // no point in the last wall was found close enough to the current wall point so don't interpolate
                addExtrusionMove(p, config, SpaceFillType::Polygons, 1.0, true);
            }
        }
        else
        {
            // no smoothing, use point verbatim
            addExtrusionMove(p, config, SpaceFillType::Polygons, 1.0, true);
        }
    }
}

void ExtruderPlan::forceMinimalLayerTime(double minTime, double minimalSpeed, double travelTime, double extrudeTime)
{
    double totalTime = travelTime + extrudeTime; 
    if (totalTime < minTime && extrudeTime > 0.0)
    {
        double minExtrudeTime = minTime - travelTime;
        if (minExtrudeTime < 1)
            minExtrudeTime = 1;
        double factor = extrudeTime / minExtrudeTime;
        for (GCodePath& path : paths)
        {
            if (path.isTravelPath())
                continue;
            double speed = path.config->getSpeed() * factor;
            if (speed < minimalSpeed)
                factor = minimalSpeed / path.config->getSpeed();
        }

        //Only slow down for the minimal time if that will be slower.
        assert(getExtrudeSpeedFactor() == 1.0); // The extrude speed factor is assumed not to be changed yet
        if (factor < 1.0)
        {
            setExtrudeSpeedFactor(factor);
        }
        else 
        {
            factor = 1.0;
        }
        
        double inv_factor = 1.0 / factor; // cause multiplication is faster than division
        
        // Adjust stored naive time estimates
        estimates.extrude_time *= inv_factor;
        for (GCodePath& path : paths)
        {
            path.estimates.extrude_time *= inv_factor;
        }

        if (minTime - (extrudeTime * inv_factor) - travelTime > 0.1)
        {
            this->extraTime = minTime - (extrudeTime * inv_factor) - travelTime;
        }
        this->totalPrintTime = (extrudeTime * inv_factor) + travelTime;
    }
}
TimeMaterialEstimates ExtruderPlan::computeNaiveTimeEstimates(Point starting_position)
{
    TimeMaterialEstimates ret;
    Point p0 = starting_position;

    bool was_retracted = false; // wrong assumption; won't matter that much. (TODO)
    for (GCodePath& path : paths)
    {
        bool is_extrusion_path = false;
        double* path_time_estimate;
        double& material_estimate = path.estimates.material;
        if (!path.isTravelPath())
        {
            is_extrusion_path = true;
            path_time_estimate = &path.estimates.extrude_time;
        }
        else 
        {
            if (path.retract)
            {
                path_time_estimate = &path.estimates.retracted_travel_time;
            }
            else 
            {
                path_time_estimate = &path.estimates.unretracted_travel_time;
            }
            if (path.retract != was_retracted)
            { // handle retraction times
                double retract_unretract_time;
                if (path.retract)
                {
                    retract_unretract_time = retraction_config.distance / retraction_config.speed;
                }
                else 
                {
                    retract_unretract_time = retraction_config.distance / retraction_config.primeSpeed;
                }
                path.estimates.retracted_travel_time += 0.5 * retract_unretract_time;
                path.estimates.unretracted_travel_time += 0.5 * retract_unretract_time;
            }
        }
        for(Point& p1 : path.points)
        {
            double length = vSizeMM(p0 - p1);
            if (is_extrusion_path)
            {
                material_estimate += length * INT2MM(layer_thickness) * INT2MM(path.config->getLineWidth());
            }
            double thisTime = length / path.config->getSpeed();
            *path_time_estimate += thisTime;
            p0 = p1;
        }
        estimates += path.estimates;
    }
    return estimates;
}

void ExtruderPlan::processFanSpeedAndMinimalLayerTime(bool force_minimal_layer_time, Point starting_position)
{
    TimeMaterialEstimates estimates = computeNaiveTimeEstimates(starting_position);
    totalPrintTime = estimates.getTotalTime();
    if (force_minimal_layer_time)
    {
        forceMinimalLayerTime(fan_speed_layer_time_settings.cool_min_layer_time, fan_speed_layer_time_settings.cool_min_speed, estimates.getTravelTime(), estimates.getExtrudeTime());
    }

    /*
                   min layer time
                   :
                   :  min layer time fan speed min
                |  :  :
      ^    max..|__:  :
                |  \  :
     fan        |   \ :
    speed  min..|... \:___________
                |________________
                  layer time >


    */
    // interpolate fan speed (for cool_fan_full_layer and for cool_min_layer_time_fan_speed_max)
    fan_speed = fan_speed_layer_time_settings.cool_fan_speed_min;
    double totalLayerTime = estimates.unretracted_travel_time + estimates.extrude_time;
    if (force_minimal_layer_time && totalLayerTime < fan_speed_layer_time_settings.cool_min_layer_time)
    {
        fan_speed = fan_speed_layer_time_settings.cool_fan_speed_max;
    }
    else if (fan_speed_layer_time_settings.cool_min_layer_time >= fan_speed_layer_time_settings.cool_min_layer_time_fan_speed_max)
    {
        fan_speed = fan_speed_layer_time_settings.cool_fan_speed_min;
    }
    else if (force_minimal_layer_time && totalLayerTime < fan_speed_layer_time_settings.cool_min_layer_time_fan_speed_max)
    {
        // when forceMinimalLayerTime didn't change the extrusionSpeedFactor, we adjust the fan speed
        double fan_speed_diff = fan_speed_layer_time_settings.cool_fan_speed_max - fan_speed_layer_time_settings.cool_fan_speed_min;
        double layer_time_diff = fan_speed_layer_time_settings.cool_min_layer_time_fan_speed_max - fan_speed_layer_time_settings.cool_min_layer_time;
        double fraction_of_slope = (totalLayerTime - fan_speed_layer_time_settings.cool_min_layer_time) / layer_time_diff;
        fan_speed = fan_speed_layer_time_settings.cool_fan_speed_max - fan_speed_diff * fraction_of_slope;
    }
    /*
    Supposing no influence of minimal layer time;
    i.e. layer time > min layer time fan speed min:

              max..   fan 'full' on layer
                   |  :
                   |  :
      ^       min..|..:________________
     fan           |  /
    speed          | /
          speed_0..|/
                   |
                   |__________________
                     layer nr >

    */
    if (layer_nr < fan_speed_layer_time_settings.cool_fan_full_layer
        && fan_speed_layer_time_settings.cool_fan_full_layer > 0 // don't apply initial layer fan speed speedup if disabled.
        && !this->is_raft_layer // don't apply initial layer fan speed speedup to raft, but to model layers
    )
    {
        //Slow down the fan on the layers below the [cool_fan_full_layer], where layer 0 is speed 0.
        fan_speed = fan_speed_layer_time_settings.cool_fan_speed_0 + (fan_speed - fan_speed_layer_time_settings.cool_fan_speed_0) * static_cast<int>(std::max(LayerIndex(0), layer_nr)) / fan_speed_layer_time_settings.cool_fan_full_layer;
    }
}

void LayerPlan::processFanSpeedAndMinimalLayerTime(Point starting_position)
{
    for (unsigned int extr_plan_idx = 0; extr_plan_idx < extruder_plans.size(); extr_plan_idx++)
    {
        ExtruderPlan& extruder_plan = extruder_plans[extr_plan_idx];
        bool force_minimal_layer_time = extr_plan_idx == extruder_plans.size() - 1;
        extruder_plan.processFanSpeedAndMinimalLayerTime(force_minimal_layer_time, starting_position);
        if (!extruder_plan.paths.empty() && !extruder_plan.paths.back().points.empty())
        {
            starting_position = extruder_plan.paths.back().points.back();
        }
    }
}



void LayerPlan::writeGCode(GCodeExport& gcode)
{
    Communication* communication = Application::getInstance().communication;
    communication->setLayerForSend(layer_nr);
    communication->sendCurrentPosition(gcode.getPositionXY());
    gcode.setLayerNr(layer_nr);
    
    gcode.writeLayerComment(layer_nr);

    // flow-rate compensation
    const Settings& mesh_group_settings = Application::getInstance().current_slice->scene.current_mesh_group->settings;
    gcode.setFlowRateExtrusionSettings(mesh_group_settings.get<double>("flow_rate_max_extrusion_offset"), mesh_group_settings.get<Ratio>("flow_rate_extrusion_offset_factor"));

    if (layer_nr == 1 - Raft::getTotalExtraLayers(storage) && mesh_group_settings.get<bool>("machine_heated_bed") && mesh_group_settings.get<Temperature>("material_bed_temperature") != 0)
    {
        bool wait = false;
        gcode.writeBedTemperatureCommand(mesh_group_settings.get<Temperature>("material_bed_temperature"), wait);
    }

    gcode.setZ(z);

    const GCodePathConfig* last_extrusion_config = nullptr; // used to check whether we need to insert a TYPE comment in the gcode.

    size_t extruder_nr = gcode.getExtruderNr();
    bool acceleration_enabled = mesh_group_settings.get<bool>("acceleration_enabled");
    bool jerk_enabled = mesh_group_settings.get<bool>("jerk_enabled");

    for(unsigned int extruder_plan_idx = 0; extruder_plan_idx < extruder_plans.size(); extruder_plan_idx++)
    {
        ExtruderPlan& extruder_plan = extruder_plans[extruder_plan_idx];
        const RetractionConfig& retraction_config = storage.retraction_config_per_extruder[extruder_plan.extruder];

        if (extruder_nr != extruder_plan.extruder)
        {
            int prev_extruder = extruder_nr;
            extruder_nr = extruder_plan.extruder;
            gcode.switchExtruder(extruder_nr, storage.extruder_switch_retraction_config_per_extruder[prev_extruder]);

            const ExtruderTrain& train = Application::getInstance().current_slice->scene.extruders[extruder_nr];
            if (train.settings.get<Velocity>("max_feedrate_z_override") > 0)
            {
                gcode.writeMaxZFeedrate(train.settings.get<Velocity>("max_feedrate_z_override"));
            }

            { // require printing temperature to be met
                constexpr bool wait = true;
                gcode.writeTemperatureCommand(extruder_nr, extruder_plan.required_start_temperature, wait);
            }

            if (extruder_plan.prev_extruder_standby_temp)
            { // turn off previous extruder
                constexpr bool wait = false;
                double prev_extruder_temp = *extruder_plan.prev_extruder_standby_temp;
                int prev_layer_nr = (extruder_plan_idx == 0)? layer_nr - 1 : layer_nr;
                if (prev_layer_nr == storage.max_print_height_per_extruder[prev_extruder])
                {
                    prev_extruder_temp = 0; // TODO ? should there be a setting for extruder_off_temperature ?
                }
                gcode.writeTemperatureCommand(prev_extruder, prev_extruder_temp, wait);
            }
        }
        else if (extruder_plan_idx == 0 && layer_nr != 0 && Application::getInstance().current_slice->scene.extruders[extruder_nr].settings.get<bool>("retract_at_layer_change"))
        {
            // only do the retract if the paths are not spiralized
            if (!mesh_group_settings.get<bool>("magic_spiralize"))
            {
                gcode.writeRetraction(retraction_config);
            }
        }
        gcode.writeFanCommand(extruder_plan.getFanSpeed());
        std::vector<GCodePath>& paths = extruder_plan.paths;

        extruder_plan.inserts.sort([](const NozzleTempInsert& a, const NozzleTempInsert& b) -> bool { 
                return  a.path_idx < b.path_idx; 
            } );

        const ExtruderTrain& train = Application::getInstance().current_slice->scene.extruders[extruder_nr];
        if (train.settings.get<Velocity>("max_feedrate_z_override") > 0)
        {
            gcode.writeMaxZFeedrate(train.settings.get<Velocity>("max_feedrate_z_override"));
        }
        const bool speed_equalize_flow_enabled = train.settings.get<bool>("speed_equalize_flow_enabled");
        const double speed_equalize_flow_max = train.settings.get<Velocity>("speed_equalize_flow_max");
        const coord_t nozzle_size = gcode.getNozzleSize(extruder_nr);

        bool update_extrusion_offset = true;

        for(unsigned int path_idx = 0; path_idx < paths.size(); path_idx++)
        {
            extruder_plan.handleInserts(path_idx, gcode);
            
            GCodePath& path = paths[path_idx];

            if (path.perform_prime)
            {
                gcode.writePrimeTrain(train.settings.get<Velocity>("speed_travel"));
                gcode.writeRetraction(retraction_config);
            }

            if (!path.retract && path.config->isTravelPath() && path.points.size() == 1 && path.points[0] == gcode.getPositionXY() && z == gcode.getPositionZ())
            {
                // ignore travel moves to the current location to avoid needless change of acceleration/jerk
                continue;
            }

            if (acceleration_enabled)
            {
                if (path.config->isTravelPath())
                {
                    gcode.writeTravelAcceleration(path.config->getAcceleration());
                }
                else
                {
                    gcode.writePrintAcceleration(path.config->getAcceleration());
                }
            }
            if (jerk_enabled)
            {
                gcode.writeJerk(path.config->getJerk());
            }

            if (path.retract)
            {
                gcode.writeRetraction(retraction_config);
                if (path.perform_z_hop)
                {
                    gcode.writeZhopStart(retraction_config.zHop);
                }
                else
                {
                    gcode.writeZhopEnd();
                }
            }
            if (!path.config->isTravelPath() && last_extrusion_config != path.config)
            {
                gcode.writeTypeComment(path.config->type);
                if (path.config->isBridgePath())
                {
                    gcode.writeComment("BRIDGE");
                }
                last_extrusion_config = path.config;
                update_extrusion_offset = true;
            }
            else
            {
                update_extrusion_offset = false;
            }

            double speed = path.config->getSpeed();

            // for some movements such as prime tower purge, the speed may get changed by this factor
            speed *= path.speed_factor;

            // Apply the relevant factor
            if (path.config->isTravelPath())
                speed *= extruder_plan.getTravelSpeedFactor();
            else
                speed *= extruder_plan.getExtrudeSpeedFactor();

            if (MergeInfillLines(gcode, paths, extruder_plan, configs_storage.travel_config_per_extruder[extruder_nr], nozzle_size, speed_equalize_flow_enabled, speed_equalize_flow_max).mergeInfillLines(path_idx)) // !! has effect on path_idx !!
            { // !! has effect on path_idx !!
                // works when path_idx is the index of the travel move BEFORE the infill lines to be merged
                continue;
            }

            if (path.config->isTravelPath())
            { // early comp for travel paths, which are handled more simply
                for(unsigned int point_idx = 0; point_idx < path.points.size(); point_idx++)
                {
                    gcode.writeTravel(path.points[point_idx], speed);
                }
                continue;
            }
            
            bool spiralize = path.spiralize;
            if (!spiralize) // normal (extrusion) move (with coasting
            {
                // if path provides a valid (in range 0-100) fan speed, use it
                const double path_fan_speed = path.getFanSpeed();
                gcode.writeFanCommand(path_fan_speed != GCodePathConfig::FAN_SPEED_DEFAULT ? path_fan_speed : extruder_plan.getFanSpeed());

                const CoastingConfig& coasting_config = storage.coasting_config[extruder_nr];
                bool coasting = coasting_config.coasting_enable; 
                if (coasting)
                {
                    coasting = writePathWithCoasting(gcode, extruder_plan_idx, path_idx, layer_thickness, coasting_config.coasting_volume, coasting_config.coasting_speed, coasting_config.coasting_min_volume);
                }
                if (! coasting) // not same as 'else', cause we might have changed [coasting] in the line above...
                { // normal path to gcode algorithm
                    if (  // change infill  ||||||   to  /\/\/\/\/ ...
                        false &&
                        path_idx + 2 < paths.size() // has a next move
                        && paths[path_idx+1].points.size() == 1 // is single extruded line
                        && !paths[path_idx+1].config->isTravelPath() // next move is extrusion
                        && paths[path_idx+2].config->isTravelPath() // next next move is travel
                        && shorterThen(path.points.back() - gcode.getPositionXY(), 2 * nozzle_size) // preceding extrusion is close by
                        && shorterThen(paths[path_idx+1].points.back() - path.points.back(), 2 * nozzle_size) // extrusion move is small
                        && shorterThen(paths[path_idx+2].points.back() - paths[path_idx+1].points.back(), 2 * nozzle_size) // consecutive extrusion is close by
                    )
                    {
                        communication->sendLineTo(paths[path_idx+2].config->type, paths[path_idx+2].points.back(), paths[path_idx+2].getLineWidthForLayerView(), paths[path_idx+2].config->getLayerThickness(), speed);
                        gcode.writeExtrusion(paths[path_idx+2].points.back(), speed, paths[path_idx+1].getExtrusionMM3perMM(), paths[path_idx+2].config->type, update_extrusion_offset);
                        path_idx += 2;
                    }
                    else 
                    {
                        for(unsigned int point_idx = 0; point_idx < path.points.size(); point_idx++)
                        {
                            communication->sendLineTo(path.config->type, path.points[point_idx], path.getLineWidthForLayerView(), path.config->getLayerThickness(), speed);
                            gcode.writeExtrusion(path.points[point_idx], speed, path.getExtrusionMM3perMM(), path.config->type, update_extrusion_offset);
                        }
                    }
                }
            }
            else
            { // SPIRALIZE
                //If we need to spiralize then raise the head slowly by 1 layer as this path progresses.
                float totalLength = 0.0;
                Point p0 = gcode.getPositionXY();
                for (unsigned int _path_idx = path_idx; _path_idx < paths.size() && !paths[_path_idx].isTravelPath(); _path_idx++)
                {
                    GCodePath& _path = paths[_path_idx];
                    for (unsigned int point_idx = 0; point_idx < _path.points.size(); point_idx++)
                    {
                        Point p1 = _path.points[point_idx];
                        totalLength += vSizeMM(p0 - p1);
                        p0 = p1;
                    }
                }

                float length = 0.0;
                p0 = gcode.getPositionXY();
                for (; path_idx < paths.size() && paths[path_idx].spiralize; path_idx++)
                { // handle all consecutive spiralized paths > CHANGES path_idx!
                    GCodePath& path = paths[path_idx];

                    for (unsigned int point_idx = 0; point_idx < path.points.size(); point_idx++)
                    {
                        Point p1 = path.points[point_idx];
                        length += vSizeMM(p0 - p1);
                        p0 = p1;
                        gcode.setZ(z + layer_thickness * length / totalLength);
                        communication->sendLineTo(path.config->type, path.points[point_idx], path.getLineWidthForLayerView(), path.config->getLayerThickness(), speed);
                        gcode.writeExtrusion(path.points[point_idx], speed, path.getExtrusionMM3perMM(), path.config->type, update_extrusion_offset);
                    }
                    // for layer display only - the loop finished at the seam vertex but as we started from
                    // the location of the previous layer's seam vertex the loop may have a gap if this layer's
                    // seam vertex is "behind" the previous layer's seam vertex. So output another line segment
                    // that joins this layer's seam vertex to the following vertex. If the layers have been blended
                    // then this can cause a visible ridge (on the screen, not on the print) because the first vertex
                    // would have been shifted in x/y to make it nearer to the previous layer outline but the seam
                    // vertex would not be shifted (as it's the last vertex in the sequence). The smoother the model,
                    // the less the vertices are shifted and the less obvious is the ridge. If the layer display
                    // really displayed a spiral rather than slices of a spiral, this would not be required.
                    communication->sendLineTo(path.config->type, path.points[0], path.getLineWidthForLayerView(), path.config->getLayerThickness(), speed);
                }
                path_idx--; // the last path_idx didnt spiralize, so it's not part of the current spiralize path
            }
        } // paths for this extruder /\  .

        if (train.settings.get<bool>("cool_lift_head") && extruder_plan.extraTime > 0.0)
        {
            gcode.writeComment("Small layer, adding delay");
            const RetractionConfig& retraction_config = storage.retraction_config_per_extruder[gcode.getExtruderNr()];
            gcode.writeRetraction(retraction_config);
            if (extruder_plan_idx == extruder_plans.size() - 1 || !train.settings.get<bool>("machine_extruder_end_pos_abs"))
            { // only move the head if it's the last extruder plan; otherwise it's already at the switching bay area 
                // or do it anyway when we switch extruder in-place
                gcode.setZ(gcode.getPositionZ() + MM2INT(3.0));
                gcode.writeTravel(gcode.getPositionXY(), configs_storage.travel_config_per_extruder[extruder_nr].getSpeed());

                const Point current_pos = gcode.getPositionXY();
                const Point machine_middle = storage.machine_size.flatten().getMiddle();
                const Point toward_middle_of_bed = current_pos - normal(current_pos - machine_middle, MM2INT(20.0));
                gcode.writeTravel(toward_middle_of_bed, configs_storage.travel_config_per_extruder[extruder_nr].getSpeed());
            }
            gcode.writeDelay(extruder_plan.extraTime);
        }

        extruder_plan.handleAllRemainingInserts(gcode);
    } // extruder plans /\  .
    
    gcode.updateTotalPrintTime();
}

void LayerPlan::overrideFanSpeeds(double speed)
{
    for (ExtruderPlan& extruder_plan : extruder_plans)
    {
        extruder_plan.setFanSpeed(speed);
    }
}


bool LayerPlan::makeRetractSwitchRetract(unsigned int extruder_plan_idx, unsigned int path_idx)
{
    std::vector<GCodePath>& paths = extruder_plans[extruder_plan_idx].paths;
    for (unsigned int path_idx2 = path_idx + 1; path_idx2 < paths.size(); path_idx2++)
    {
        if (paths[path_idx2].getExtrusionMM3perMM() > 0) 
        {
            return false; 
        }
    }
    
    if (extruder_plans.size() <= extruder_plan_idx+1)
    {
        return false; // TODO: check first extruder of the next layer! (generally only on the last layer of the second extruder)
    }
        
    if (extruder_plans[extruder_plan_idx + 1].extruder != extruder_plans[extruder_plan_idx].extruder)
    {
        return true;
    }
    else 
    {
        return false;
    }
}
    
bool LayerPlan::writePathWithCoasting(GCodeExport& gcode, unsigned int extruder_plan_idx, unsigned int path_idx, int64_t layerThickness, double coasting_volume, double coasting_speed, double coasting_min_volume)
{
    if (coasting_volume <= 0)
    { 
        return false; 
    }
    ExtruderPlan& extruder_plan = extruder_plans[extruder_plan_idx];
    std::vector<GCodePath>& paths = extruder_plan.paths;
    GCodePath& path = paths[path_idx];
    if (path_idx + 1 >= paths.size()
        ||
        ! (!path.isTravelPath() &&  paths[path_idx + 1].config->isTravelPath()) 
        ||
        path.points.size() < 2
        )
    {
        return false;
    }

    int64_t coasting_min_dist_considered = 100; // hardcoded setting for when to not perform coasting

    
    double extrude_speed = path.config->getSpeed() * extruder_plan.getExtrudeSpeedFactor(); // travel speed 
    
    int64_t coasting_dist = MM2INT(MM2_2INT(coasting_volume) / layerThickness) / path.config->getLineWidth(); // closing brackets of MM2INT at weird places for precision issues
    int64_t coasting_min_dist = MM2INT(MM2_2INT(coasting_min_volume + coasting_volume) / layerThickness) / path.config->getLineWidth(); // closing brackets of MM2INT at weird places for precision issues
    //           /\ the minimal distance when coasting will coast the full coasting volume instead of linearly less with linearly smaller paths
    
    
    std::vector<int64_t> accumulated_dist_per_point; // the first accumulated dist is that of the last point! (that of the last point is always zero...)
    accumulated_dist_per_point.push_back(0);
    
    int64_t accumulated_dist = 0;
    
    bool length_is_less_than_min_dist = true;
    
    unsigned int acc_dist_idx_gt_coast_dist = NO_INDEX; // the index of the first point with accumulated_dist more than coasting_dist (= index into accumulated_dist_per_point)
     // == the point printed BEFORE the start point for coasting
    
    
    Point* last = &path.points[path.points.size() - 1];
    for (unsigned int backward_point_idx = 1; backward_point_idx < path.points.size(); backward_point_idx++)
    {
        Point& point = path.points[path.points.size() - 1 - backward_point_idx];
        int64_t dist = vSize(point - *last);
        accumulated_dist += dist;
        accumulated_dist_per_point.push_back(accumulated_dist);
        
        if (acc_dist_idx_gt_coast_dist == NO_INDEX && accumulated_dist >= coasting_dist)
        {
            acc_dist_idx_gt_coast_dist = backward_point_idx; // the newly added point
        }
        
        if (accumulated_dist >= coasting_min_dist)
        {
            length_is_less_than_min_dist = false;
            break;
        }
        
        last = &point;
    }
    
    if (accumulated_dist < coasting_min_dist_considered)
    {
        return false;
    }
    int64_t actual_coasting_dist = coasting_dist;
    if (length_is_less_than_min_dist)
    {
        // in this case accumulated_dist is the length of the whole path
        actual_coasting_dist = accumulated_dist * coasting_dist / coasting_min_dist;
        for (acc_dist_idx_gt_coast_dist = 0 ; acc_dist_idx_gt_coast_dist < accumulated_dist_per_point.size() ; acc_dist_idx_gt_coast_dist++)
        { // search for the correct coast_dist_idx
            if (accumulated_dist_per_point[acc_dist_idx_gt_coast_dist] > actual_coasting_dist)
            {
                break;
            }
        }
    }

    assert (acc_dist_idx_gt_coast_dist < accumulated_dist_per_point.size()); // something has gone wrong; coasting_min_dist < coasting_dist ?

    unsigned int point_idx_before_start = path.points.size() - 1 - acc_dist_idx_gt_coast_dist;

    Point start;
    { // computation of begin point of coasting
        int64_t residual_dist = actual_coasting_dist - accumulated_dist_per_point[acc_dist_idx_gt_coast_dist - 1];
        Point& a = path.points[point_idx_before_start];
        Point& b = path.points[point_idx_before_start + 1];
        start = b + normal(a-b, residual_dist);
    }

    { // write normal extrude path:
        Communication* communication = Application::getInstance().communication;
        for(unsigned int point_idx = 0; point_idx <= point_idx_before_start; point_idx++)
        {
            communication->sendLineTo(path.config->type, path.points[point_idx], path.getLineWidthForLayerView(), path.config->getLayerThickness(), extrude_speed);
            gcode.writeExtrusion(path.points[point_idx], extrude_speed, path.getExtrusionMM3perMM(), path.config->type);
        }
        communication->sendLineTo(path.config->type, start, path.getLineWidthForLayerView(), path.config->getLayerThickness(), extrude_speed);
        gcode.writeExtrusion(start, extrude_speed, path.getExtrusionMM3perMM(), path.config->type);
    }

    // write coasting path
    for (unsigned int point_idx = point_idx_before_start + 1; point_idx < path.points.size(); point_idx++)
    {
        double speed = coasting_speed * path.config->getSpeed() * extruder_plan.getExtrudeSpeedFactor();
        gcode.writeTravel(path.points[point_idx], speed);
    }

    gcode.addLastCoastedVolume(path.getExtrusionMM3perMM() * INT2MM(actual_coasting_dist));
    return true;
}

}//namespace cura
