//Copyright (c) 2017 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifndef TOPSURFACE_H
#define TOPSURFACE_H

#include "utils/polygon.h" //For the polygon areas.
#include "sliceDataStorage.h" //For the input mesh.

namespace cura
{

class SliceMeshStorage;

class TopSurface
{
public:
    /*!
     * Create an empty top surface area.
     */
    TopSurface();

    /*!
     * \brief Generate new top surface for a specific layer part.
     *
     * The surface will be generated by subtracting the layer above from the
     * current layer. Anything that is leftover is then part of the top surface
     * (since there is nothing above it).
     *
     * \param mesh The mesh to generate the top surface area for.
     * \param layer_number The layer to generate the top surface area for.
     * \param part_number The layer part within that layer to generate the top
     * surface area for.
     */
    TopSurface(SliceMeshStorage& mesh, size_t layer_number, size_t part_number);

    /*!
     * \brief The areas of top surface, for each layer.
     */
    Polygons areas;
};

}

#endif /* TOPSURFACE_H */

