//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef INSET_ORDER_OPTIMIZER_H
#define INSET_ORDER_OPTIMIZER_H

#include <unordered_set>

#include "PathOrderOptimizer.h"
#include "sliceDataStorage.h" //For SliceMeshStorage, which is used here at implementation in the header.

namespace cura
{

class FffGcodeWriter;
class LayerPlan;

class InsetOrderOptimizer
{
public:

    /*!
     * Constructor for inset ordering optimizer.
     *
     * This constructor gets basically all of the locals passed when it needs to
     * optimise the order of insets.
     * \param gcode_writer The FffGcodeWriter on whose behalf the inset order is
     * being optimized.
     * \param storage Read slice data from this storage.
     * \param gcode_layer The layer where the resulting insets must be planned.
     * \param mesh The mesh that these insets are part of.
     * \param extruder_nr Which extruder to process. If an inset is not printed
     * with this extruder, it will not be added to the plan.
     * \param mesh_config The path configs for a single mesh, indicating the
     * line widths, flows, speeds, etc to print this mesh with.
     * \param part The part from which to read the previously generated insets.
     * \param layer_nr The current layer number.
     */
    InsetOrderOptimizer(const FffGcodeWriter& gcode_writer,
                        const SliceDataStorage& storage,
                        LayerPlan& gcode_layer,
                        const Settings& settings,
                        const int extruder_nr,
                        const GCodePathConfig& inset_0_non_bridge_config,
                        const GCodePathConfig& inset_X_non_bridge_config,
                        const GCodePathConfig& inset_0_bridge_config,
                        const GCodePathConfig& inset_X_bridge_config,
                        const bool retract_before_outer_wall,
                        const coord_t wall_0_wipe_dist,
                        const coord_t wall_x_wipe_dist,
                        const size_t wall_0_extruder_nr,
                        const size_t wall_x_extruder_nr,
                        const ZSeamConfig& z_seam_config,
                        const VariableWidthPaths& paths);

    /*!
     * Adds the insets to the given layer plan.
     *
     * The insets and the layer plan are passed to the constructor of this
     * class, so this optimize function needs no additional information.
     * \return Whether anything was added to the layer plan.
     */
    bool addToLayer();

    /*!
     * Get the order constraints of the insets assuming the Wall Ordering is outer to inner.
     * Each returned pair consists of adjacent wall lines where the left has an inset_idx one lower than the right.
     * 
     * Odd walls should always go after their enclosing wall polygons.
     * 
     * \param outer_to_inner Whether the wall polygons with a lower inset_idx should go before those with a higher one.
     */
    static std::unordered_set<std::pair<const ExtrusionLine*, const ExtrusionLine*>> getWeakOrder(const std::vector<const ExtrusionLine*>& input, const bool outer_to_inner, const bool include_transitive = true);
private:

    /*!
     * Recursive part of \ref WallToolpPaths::getWeakOrder.
     * For each node at \p node_idx we recurse on all its children at nesting[node_idx]
     */
    static void getWeakOrder(size_t node_idx, const std::unordered_map<size_t, const ExtrusionLine*>& poly_idx_to_extrusionline, const std::vector<std::vector<size_t>>& nesting, size_t max_inset_idx, const bool outer_to_inner, std::unordered_set<std::pair<const ExtrusionLine*, const ExtrusionLine*>>& result);

    const FffGcodeWriter& gcode_writer;
    const SliceDataStorage& storage;
    LayerPlan& gcode_layer;
    const Settings& settings;
    const size_t extruder_nr;
    const GCodePathConfig& inset_0_non_bridge_config;
    const GCodePathConfig& inset_X_non_bridge_config;
    const GCodePathConfig& inset_0_bridge_config;
    const GCodePathConfig& inset_X_bridge_config;
    const bool retract_before_outer_wall;
    const coord_t wall_0_wipe_dist;
    const coord_t wall_x_wipe_dist;
    const size_t wall_0_extruder_nr;
    const size_t wall_x_extruder_nr;
    const ZSeamConfig& z_seam_config;
    const VariableWidthPaths& paths;
    const unsigned int layer_nr;
    
    bool added_something;
    bool retraction_region_calculated; //Whether the retraction_region field has been calculated or not.
    std::vector<std::vector<ConstPolygonPointer>> inset_polys; // vector of vectors holding the inset polygons
    Polygons retraction_region; //After printing an outer wall, move into this region so that retractions do not leave visible blobs. Calculated lazily if needed (see retraction_region_calculated).

    /*!
     * Endpoints of polylines that are closer together than this distance
     * will be considered to be coincident,
     * closing that polyline into a polygon.
     */
    constexpr static coord_t coincident_point_distance = 10;
};

} //namespace cura

#endif // INSET_ORDER_OPTIMIZER_H
