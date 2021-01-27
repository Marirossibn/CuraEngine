//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef LIGHTNING_GENERATOR_H
#define LIGHTNING_GENERATOR_H

#include "../utils/polygonUtils.h"

#include <functional>
#include <memory>
#include <vector>
#include <unordered_set>

namespace cura 
{
class LightningTreeNode;
class SliceMeshStorage;

// NOTE: Currently, the following class is just scaffolding so the entirety can be run during development, while other parts are made in sync.
//       No particular attention is paid to efficiency & the like. Might be _very_ slow!
class LightningDistanceField
{
public:
    /*!
     * constructor
     */
    LightningDistanceField
    (
        const coord_t& radius,
        const Polygons& current_outline,
        const Polygons& current_overhang,
        const std::unordered_set<std::shared_ptr<LightningTreeNode>>& initial_trees
    );

    /*!
     * Gets the next unsupported location to be supported by a new branch.
     * 
     * Returns false if \ref LightningDistanceField::unsupported is empty
     */
    bool tryGetNextPoint(Point* p, coord_t supporting_radius) const;

    /*! update the distance field with a newly added branch
     * TODO: check whether this explanation is correct
     */
    void update(const Point& to_node, const Point& added_leaf);

protected:
    coord_t supporting_radius; //!< The radius of the area of the layer above supported by a point on a branch of a tree
    Polygons unsupported;
    Polygons supported;
};

struct GroundingLocation
{
    std::shared_ptr<LightningTreeNode> tree_node; //!< not null if the gounding location is on a tree
    std::optional<ClosestPolygonPoint> boundary_location; //!< in case the gounding location is on the boundary
    Point p() const;
};

/*!
 * A layer of the lightning fill.
 * 
 * Contains the trees to be printed and propagated to the next layer below.
 */
class LightningLayer
{
public:
    std::unordered_set<std::shared_ptr<LightningTreeNode>> tree_roots;

    void generateNewTrees(const Polygons& current_overhang, Polygons& current_outlines, coord_t supporting_radius);

    //! Determine & connect to connection point in tree/outline.
    GroundingLocation getBestGroundingLocation(const Point& unsupported_location, const Polygons& current_outlines, const coord_t supporting_radius, const SparsePointGridInclusive<std::weak_ptr<LightningTreeNode>>& tree_node_locator, const std::shared_ptr<LightningTreeNode>& exclude_tree = nullptr);

    void attach(const Point& unsupported_loc, const GroundingLocation& ground);

    void reconnectRoots(std::unordered_set<std::shared_ptr<LightningTreeNode>>& to_be_reconnected_tree_roots, const Polygons& current_outlines, const coord_t supporting_radius);

    Polygons convertToLines() const;

    coord_t getWeightedDistance(const Point& boundary_loc, const Point& unsupported_loc);

    void fillLocator(SparsePointGridInclusive<std::weak_ptr<LightningTreeNode>>& tree_node_locator, const std::unordered_set<std::shared_ptr<LightningTreeNode>>& excluded_trees_by_root = {});
};

/*
 *            .---------.
 *     . :+*#%%@*=\\\*@@@#:
 *  .+*=+@%#%@@@@#%@@@@@@@@*
 * :@@%+=@##==%@@@@@@@@@@@@@
 * %@@%=-'     '"-+*#%@@@@@@@.
 * %@#'            ...*=@@@@@-
 * .-             ....*=@@@@@*
 *  .        ..:......:#@@@@@=
 *  : :-- .-*%@%%**=-:.-%@@#=.=.
 *   =##%: :==-.:::..:::=@+++. )
 *   :     ..       .::--:-#% /
 *    \    ...     ..:---==:_;
 *     :  :=w=:   ..:----+=
 *      :-#@@@%#*-.:::---==
 *      :*=--==--:.:----=-=.
 *       . .-=-...:--=+*+-+=:.
 *        \     .-=+:'       .:
 *         .':-==-"  .:-=+#%@@@*
 *       .'      :+#@@@@@@@@@@@@+
 *    .=#%#. :-+#%@@@@@@@@@@@@@@@:
 *  -+%##*+#***%@%####%@@@@@@@@@@@*.
 * 
 *                           <3 Nikolai
 */
class LightningGenerator
{
public:
    /*!
     * TODO: instead of radius we should pass around the overhang_angle
     * and compute the radius from the tangent of the angle and the local (adaptive) layer thickness
     */
    LightningGenerator(const coord_t& radius, const SliceMeshStorage& mesh);

    const LightningLayer& getTreesForLayer(const size_t& layer_id);

protected:
    // Necesary, since normally overhangs are only generated for the outside of the model, and only when support is generated.
    void generateInitialInternalOverhangs(const SliceMeshStorage& mesh, coord_t supporting_radius);

    void generateTrees(const SliceMeshStorage& mesh);

    coord_t supporting_radius;
    std::vector<Polygons> overhang_per_layer;
    std::vector<LightningLayer> lightning_layers;
};

} // namespace cura

#endif // LIGHTNING_GENERATOR_H
