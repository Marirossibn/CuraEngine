//Copyright (c) 2021 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef RIBBED_SUPPORT_VAULT_GENERATOR_H
#define RIBBED_SUPPORT_VAULT_GENERATOR_H

#include "../utils/polygonUtils.h"

#include <functional>
#include <memory>
#include <vector>
#include <map>

namespace cura 
{
    class SliceMeshStorage;

    // NOTE: As written, this struct will only be valid for a single layer, will have to be updated for the next.
    // NOTE: Reasons for implementing this with some separate closures:
    //       - keep clear deliniation during development
    //       - possibility of multiple distance field strategies
    class RibbedVaultTreeNode : public std::enable_shared_from_this<RibbedVaultTreeNode>
    {
    public:
        friend class std::shared_ptr<RibbedVaultTreeNode>;

        // For use with the 'visit___' function(s).
        // Input: Uptree junction point (closer to root), downtree branch point (closer to leaves).
        typedef std::function<void(const Point&, const Point&)> visitor_func_t;

        // Constructs a node, for insertion into a tree:
        RibbedVaultTreeNode(const Point& p);

        // Constructs a root (and initial trunk):
        RibbedVaultTreeNode(const Point& a, const Point& b);

        const Point& getLocation() const;
        void setLocation(Point p);

        void addChild(const Point& p);

        // TODO: should be moved outside of this class, because we want to efficiently find pairs of close nodes
        std::shared_ptr<RibbedVaultTreeNode> findClosestNode(const Point& x, const coord_t supporting_radius);

        /*!
         * Propagate this tree to the next layer.
         * 
         * Create a copy of this tree,
         * realign it to the new layer boundaries \p next_outlines
         * and reduce (i.e. prune and straighten) it.
         */
        void propagateToNextLayer
        (
            std::vector<std::shared_ptr<RibbedVaultTreeNode>>& next_trees,
            const Polygons& next_outlines,
            const coord_t& prune_distance,
            const coord_t& smooth_magnitude
        ) const;

        // NOTE: Depth-first, as currently implemented.
        //       Skips the root (because that has no root itself), but all initial nodes will have the root point anyway.
        void visitBranches(const visitor_func_t& visitor) const;

        coord_t getWeightedDistance(const Point unsupported_loc, const coord_t supporting_radius);
    protected:
        RibbedVaultTreeNode() = delete; // Don't allow empty contruction

        /*!
         * What does this function do?!
         */
        void findClosestNodeHelper(const Point& x, const coord_t supporting_radius, coord_t& closest_distance, std::shared_ptr<RibbedVaultTreeNode>& closest_node);

        std::shared_ptr<RibbedVaultTreeNode> deepCopy() const; //!< Copy this node and all its children

        /*! Reconnect trees from the layer above to the new outlines of the lower layer.
         * \return Wether or not the root is kept (false is no, true is yes).
         */
        bool realign(const Polygons& outlines, std::vector<std::shared_ptr<RibbedVaultTreeNode>>& rerooted_parts);

        struct RectilinearJunction
        {
            coord_t total_recti_dist; //!< rectilinear distance along the tree from the last junction above to the junction below
            Point junction_loc; //!< junction location below
        };
        
        /*! Smoothen the tree to make it a bit more printable, while still supporting the trees above.
         */
        void straighten(const coord_t& magnitude);

        /*! Recursive part of \ref straighten(.)
         * \param junction_above The last seen junction with multiple children above
         * \param accumulated_dist The distance along the tree from the last seen junction to this node
         * \return the total distance along the tree from the last junction above to the first next junction below and the location of the next junction below
         */
        RectilinearJunction straighten(const coord_t& magnitude, Point junction_above, coord_t accumulated_dist);

        /*! Prune the tree from the extremeties (leaf-nodes) until the pruning distance is reached.
         * \return The distance that has been pruned. If less than \p distance, then the whole tree was puned away.
         */
        coord_t prune(const coord_t& distance);

        bool is_root = false;
        Point p;
        
        std::vector<std::shared_ptr<RibbedVaultTreeNode>> children;
    };

    // NOTE: Currently, the following class is just scaffolding so the entirety can be run during development, while other parts are made in sync.
    //       No particular attention is paid to efficiency & the like. Might be _very_ slow!
    class RibbedVaultDistanceField
    {
    public:
        /*!
         * constructor
         */
        RibbedVaultDistanceField
        (
            const coord_t& radius,
            const Polygons& current_outline,
            const Polygons& current_overhang,
            const std::vector<std::shared_ptr<RibbedVaultTreeNode>>& initial_trees
        );

        /*!
         * Gets the next unsupported location to be supported by a new branch.
         * 
         * Returns false if \ref RibbedVaultDistanceField::unsupported is empty
         */
        bool tryGetNextPoint(Point* p) const;

        /*! update the distance field with a newly added branch
         * TODO: check whether this explanation is correct
         */
        void update(const Point& to_node, const Point& added_leaf);

    protected:
        coord_t supporting_radius; //!< The radius of the area of the layer above supported by a point on a branch of a tree
        Polygons unsupported;
        Polygons supported;
    };

    //
    // TODO: sugggestion:
    //  Introduce a new class RibbedVaultLayer,
    //  which contains both:
    //    Polygons overhang;
    //    std::vector<RibbedVaultTreeNode> tree_roots;
    //  and maybe
    //    RibbedVaultDistanceField distance_field;
    //
    // That way we can extend the amount of data we pass around through FffGcodeWriter more easily
    class RibbedVaultLayer
    {
    public:
        std::vector<std::shared_ptr<RibbedVaultTreeNode>> tree_roots;

        Polygons convertToLines() const;

        coord_t getWeightedDistance(const Point boundary_loc, const Point unsupported_loc);
    };

    class RibbedSupportVaultGenerator
    {
    public:
        /*!
         * TODO: instead of radius we should pass around the overhang_angle
         * and compute the radius from the tangent of the angle and the local (adaptive) layer thickness
         */
        RibbedSupportVaultGenerator(const coord_t& radius, const SliceMeshStorage& mesh);

        const RibbedVaultLayer& getTreesForLayer(const size_t& layer_id);

    protected:
        // Necesary, since normally overhangs are only generated for the outside of the model, and only when support is generated.
        void generateInitialInternalOverhangs(const SliceMeshStorage& mesh, coord_t supporting_radius);

        void generateTrees(const SliceMeshStorage& mesh);

        coord_t supporting_radius;
        std::vector<Polygons> overhang_per_layer;
        std::vector<RibbedVaultLayer> tree_roots_per_layer;
    };

} // namespace cura

#endif // RIBBED_SUPPORT_VAULT_GENERATOR_H
