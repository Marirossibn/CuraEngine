#include "subDivCube.h"

#include <functional>

#include "../utils/polygonUtils.h"
#include "../sliceDataStorage.h"

namespace cura
{

std::vector<int64_t> SubDivCube::side_length;
std::vector<int64_t> SubDivCube::height;
std::vector<int64_t> SubDivCube::square_height;
std::vector<int64_t> SubDivCube::max_draw_z_diff;
std::vector<int64_t> SubDivCube::max_line_offset;
double SubDivCube::radius_multiplier = 1;
int32_t SubDivCube::radius_addition = 0;
Point3Matrix SubDivCube::rotation_matrix;
PointMatrix SubDivCube::infill_rotation_matrix;

void SubDivCube::precomputeOctree(SliceMeshStorage& mesh)
{
    radius_multiplier = mesh.getSettingAsRatio("sub_div_rad_mult");
    radius_addition = mesh.getSettingInMicrons("sub_div_rad_add");
    double infill_angle = M_PI / 4.0;
    int curr_recursion_depth = 0;
    for (int64_t curr_side_length = mesh.getSettingInMicrons("infill_line_distance") * 2; curr_side_length < 25600000; curr_side_length *= 2) //!< 25600000 is an arbitrarily large number. It is imperative that any infill areas are inside of the cube defined by this number.
    {
        side_length.push_back(curr_side_length);
        height.push_back(sqrt(3) * curr_side_length);
        square_height.push_back(sqrt(2) * curr_side_length);
        max_draw_z_diff.push_back((1.0 / sqrt(3.0)) * curr_side_length);
        max_line_offset.push_back((sqrt(1.0 / 6.0) * curr_side_length));
        curr_recursion_depth++;
    }
    Point3 center(0, 0, 0);

    Point3Matrix tilt; // rotation matrix to get from axis aligned cubes to cubes standing on their tip
    // The Z axis is transformed to go in positive Y direction
    //
    //  cross section in a horizontal plane      horizontal plane showing
    //  looking down at the origin O             positive X and positive Y
    //                 Z                                                        .
    //                /:\                              Y                        .
    //               / : \                             ^                        .
    //              /  :  \                            |                        .
    //             /  .O.  \                           |                        .
    //            /.~'   '~.\                          O---->X                  .
    //          X """"""""""" Y                                                 .
    double one_over_sqrt_3 = 1.0 / sqrt(3.0);
    double one_over_sqrt_6 = 1.0 / sqrt(6.0);
    double sqrt_two_third = sqrt(2.0 / 3.0);
    tilt.matrix[0] = -one_over_sqrt_2;  tilt.matrix[1] = one_over_sqrt_2; tilt.matrix[2] = 0;
    tilt.matrix[3] = -one_over_sqrt_6; tilt.matrix[4] = -one_over_sqrt_6; tilt.matrix[5] = sqrt_two_third ;
    tilt.matrix[6] = one_over_sqrt_3;  tilt.matrix[7] = one_over_sqrt_3;  tilt.matrix[8] = one_over_sqrt_3;

    infill_rotation_matrix = PointMatrix(infill_angle);
    Point3Matrix infill_angle_mat(infill_rotation_matrix);

    rotation_matrix = infill_angle_mat.compose(tilt);

    mesh.base_subdiv_cube = new SubDivCube(mesh, center, curr_recursion_depth - 1);
}

void SubDivCube::generateSubdivisionLines(int64_t z, Polygons& result, Polygons** directional_line_groups)
{
    auto addLine = [&](Point from, Point to)
    {
        PolygonRef p = result.newPoly();
        p.add(from);
        p.add(to);
    };
    /*!
     * Adds the defined line to the specified polygons. It assumes that the specified polygons are all parallel lines. Combines line segments with touching ends closer than epsilon.
     * \param group the polygons to add the line to
     * \param from the first endpoint of the line
     * \param to the second endpoint of the line
     */
    auto addLineAndCombine = [&](Polygons& group, Point from, Point to)
    {
        int epsilon = 10;
        for (unsigned int idx = 0; idx < group.size(); idx++)
        {
            if (abs(from.X - group[idx][1].X) < epsilon && abs(from.Y - group[idx][1].Y) < epsilon)
            {
                from = group[idx][0];
                group.remove(idx);
                idx--;
                continue;
            }
            if (abs(to.X - group[idx][0].X) < epsilon && abs(to.Y - group[idx][0].Y) < epsilon)
            {
                to = group[idx][1];
                group.remove(idx);
                idx--;
                continue;
            }
        }
        PolygonRef p = group.newPoly();
        p.add(from);
        p.add(to);
    };
    bool top_level = false; //!< if this cube is the top level of the recursive call
    if (directional_line_groups == nullptr) //!< if directional_line_groups is null then set the top level flag and create directional line groups
    {
        top_level = true;
        directional_line_groups = (Polygons**)calloc(3, sizeof(Polygons*));
        for (int idx = 0; idx < 3; idx++)
        {
            directional_line_groups[idx] = new Polygons();
        }
    }
    int32_t z_diff = abs(z - center.z); //!< the difference between the cube center and the target layer.
    if (z_diff > height[depth] / 2) //!< this cube does not touch the target layer. Early exit.
    {
        return;
    }
    if (z_diff < max_draw_z_diff[depth]) //!< this cube has lines that need to be drawn.
    {
        Point relative_a, relative_b; //!< relative coordinates of line endpoints around cube center
        Point a, b; //!< absolute coordinates of line endpoints
        relative_a.X = (square_height[depth] / 2) * ((double)(max_draw_z_diff[depth] - z_diff) / (double)max_draw_z_diff[depth]);
        relative_b.X = -relative_a.X;
        relative_a.Y = max_line_offset[depth] - ((z - (center.z - max_draw_z_diff[depth])) * one_over_sqrt_2);
        relative_b.Y = relative_a.Y;
        rotatePointInitial(relative_a);
        rotatePointInitial(relative_b);
        for (int idx = 0; idx < 3; idx++)//!< draw the line, then rotate 120 degrees.
        {
            a.X = center.x + relative_a.X;
            a.Y = center.y + relative_a.Y;
            b.X = center.x + relative_b.X;
            b.Y = center.y + relative_b.Y;
            addLineAndCombine(*(directional_line_groups[idx]), a, b);
            if (idx < 2)
            {
                rotatePoint120(relative_a);
                rotatePoint120(relative_b);
            }
        }
    }
    for (int idx = 0; idx < 8; idx++) //!< draws the eight children
    {
        if (children[idx] != nullptr)
        {
            children[idx]->generateSubdivisionLines(z, result, directional_line_groups);
        }
    }
    if (top_level) //!< copy directional groups into result, then free the directional groups
    {
        for (int temp = 0; temp < 3; temp++)
        {
            for (unsigned int idx = 0; idx < directional_line_groups[temp]->size(); idx++)
            {
                addLine((*directional_line_groups[temp])[idx][0], (*directional_line_groups[temp])[idx][1]);
            }
            delete directional_line_groups[temp];
        }
        free(directional_line_groups);
    }
}

SubDivCube::SubDivCube(SliceMeshStorage& mesh, Point3& center, int depth)
{
    this->depth = depth;
    this->center = center;

    if (depth == 0) // lowest layer, no need for subdivision, exit.
    {
        return;
    }
    Point3 child_center;
    coord_t radius = double(radius_multiplier * double(height[depth])) / 4.0 + radius_addition;

    int child_nr = 0;
    std::vector<Point3> rel_child_centers;
    rel_child_centers.emplace_back(1, 1, 1); // top
    rel_child_centers.emplace_back(-1, 1, 1); // top three
    rel_child_centers.emplace_back(1, -1, 1);
    rel_child_centers.emplace_back(1, 1, -1);
    rel_child_centers.emplace_back(-1, -1, -1); // bottom
    rel_child_centers.emplace_back(1, -1, -1); // bottom three
    rel_child_centers.emplace_back(-1, 1, -1);
    rel_child_centers.emplace_back(-1, -1, 1);
    for (Point3 rel_child_center : rel_child_centers)
    {
        child_center = center + rotation_matrix.apply(rel_child_center * int32_t(side_length[depth] / 4));
        if (isValidSubdivision(mesh, child_center, radius))
        {
            children[child_nr] = new SubDivCube(mesh, child_center, depth - 1);
            child_nr++;
        }
    }
}

bool SubDivCube::isValidSubdivision(SliceMeshStorage& mesh, Point3& center, int64_t radius)
{
    int64_t distance;
    long int sphere_slice_radius;//!< radius of bounding sphere slice on target layer
    bool inside_somewhere = false;
    bool outside_somewhere = false;
    int inside;
    double part_dist;//what percentage of the radius the target layer is away from the center along the z axis. 0 - 1
    const long int layer_height = mesh.getSettingInMicrons("layer_height");
    long int bottom_layer = (center.z - radius) / layer_height;
    long int top_layer = (center.z + radius) / layer_height;
    for (long int test_layer = bottom_layer; test_layer <= top_layer; test_layer += 3) // steps of three. Low-hanging speed gain.
    {
        part_dist = (double)(test_layer * layer_height - center.z) / radius;
        sphere_slice_radius = radius * (sqrt(1 - (part_dist * part_dist)));
        Point loc(center.x, center.y);

        inside = distanceFromPointToMesh(mesh, test_layer, loc, &distance);
        if (inside == 1)
        {
            inside_somewhere = true;
        }
        else
        {
            outside_somewhere = true;
        }
        if (outside_somewhere && inside_somewhere)
        {
            return true;
        }
        if ((inside != 2) && abs(distance) < sphere_slice_radius)
        {
            return true;
        }
    }
    return false;
}

int SubDivCube::distanceFromPointToMesh(SliceMeshStorage& mesh, long int layer_nr, Point& location, int64_t* distance)
{
    if (layer_nr < 0 || (unsigned long int)layer_nr >= mesh.layers.size()) //!< this layer is outside of valid range
    {
        return 2;
    }
    Polygons collide;
    mesh.layers[layer_nr].getSecondOrInnermostWalls(collide);
    Point centerpoint = location;
    bool inside = collide.inside(centerpoint);
    ClosestPolygonPoint border_point = PolygonUtils::moveInside2(collide, centerpoint);
    *distance = sqrt((border_point.location.X - location.X) * (border_point.location.X - location.X) + (border_point.location.Y - location.Y) * (border_point.location.Y - location.Y));
    if (inside)
    {
        return 1;
    }
    return 0;
}


void SubDivCube::rotatePointInitial(Point& target)
{
    target = infill_rotation_matrix.apply(target);
}

void SubDivCube::rotatePoint120(Point& target)
{
    int64_t x;
    x = (-0.5) * target.X - sqrt_three_fourths * target.Y;
    target.Y = (-0.5)*target.Y + sqrt_three_fourths * target.X;
    target.X = x;
}

}//namespace cura
