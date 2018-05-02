/** Copyright (C) 2017 Tim Kuipers - Released under terms of the AGPLv3 License */
#ifndef INFILL_DENSITY_PROVIDER_H
#define INFILL_DENSITY_PROVIDER_H


namespace cura
{

/*!
 * Parent class of function objects which return the density required for a given region.
 * 
 * This density requirement can be based on user input, distance to the 3d model shell, Z distance to top skin, etc.
 */
class DensityProvider
{
public:
    /*!
     * \return the approximate required density of a quadrilateral
     */
    virtual float operator()(const Point& a, const Point& b, const Point& c, const Point& d) const = 0;
    virtual ~DensityProvider()
    {
    };
};

} // namespace cura


#endif // INFILL_DENSITY_PROVIDER_H
