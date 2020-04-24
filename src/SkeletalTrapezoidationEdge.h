//Copyright (c) 2019 Ultimaker B.V.


#ifndef VORONOI_QUADRILATERALIZATION_EDGE_H
#define VORONOI_QUADRILATERALIZATION_EDGE_H


namespace arachne
{
    using namespace cura;

class SkeletalTrapezoidationEdge
{
    using type_t = int_least16_t;
public:
    type_t type;
    static constexpr type_t NORMAL = 0; // from voronoi diagram
    static constexpr type_t EXTRA_VD = 1; // introduced to voronoi diagram in order to make the gMAT
    static constexpr type_t TRANSITION_END = 2; // introduced to voronoi diagram in order to make the gMAT

    SkeletalTrapezoidationEdge()
    : SkeletalTrapezoidationEdge(NORMAL)
    {}
    SkeletalTrapezoidationEdge(type_t type)
    : type(type)
    , is_marked(-1)
    {}

    bool isMarked() const
    {
        assert(is_marked != -1);
        return is_marked;
    }
    void setMarked(bool b)
    {
        is_marked = b;
    }
    bool markingIsSet() const
    {
        return is_marked >= 0;
    }
private:
    int_least8_t is_marked; //! whether the edge is significant; whether the source segments have a sharp angle; -1 is unknown
};




} // namespace arachne
#endif // VORONOI_QUADRILATERALIZATION_EDGE_H
