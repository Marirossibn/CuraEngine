/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#ifndef MODELFILE_H
#define MODELFILE_H
/**
modelFile contains the model loaders for the slicer. The model loader turns any format that it can read into a list of triangles with 3 X/Y/Z points.

The format returned is a Model class with an array of faces, which have integer points with a resolution of 1 micron. Giving a maximum object size of 4 meters.
**/

#include "../mesh.h"

//A PrintObject is a 3D model with 1 or more 3D meshes.
class PrintObject : public SettingsBase
{
public:
    std::vector<Mesh> meshes;
    
    PrintObject(SettingsBase* settings_base)
    : SettingsBase(settings_base)
    {
    }

    Point3 min()
    {
        if (meshes.size() < 1)
            return Point3(0, 0, 0);
        Point3 ret = meshes[0].min();
        for(unsigned int i=1; i<meshes.size(); i++)
        {
            Point3 v = meshes[i].min();
            ret.x = std::min(ret.x, v.x);
            ret.y = std::min(ret.y, v.y);
            ret.z = std::min(ret.z, v.z);
        }
        return ret;
    }
    Point3 max()
    {
        if (meshes.size() < 1)
            return Point3(0, 0, 0);
        Point3 ret = meshes[0].max();
        for(unsigned int i=1; i<meshes.size(); i++)
        {
            Point3 v = meshes[i].max();
            ret.x = std::max(ret.x, v.x);
            ret.y = std::max(ret.y, v.y);
            ret.z = std::max(ret.z, v.z);
        }
        return ret;
    }
    
    void clear()
    {
        for(Mesh& m : meshes)
            m.clear();
    }
    
    void offset(Point3 offset)
    {
        for(Mesh& m : meshes)
            for(MeshVertex& v : m.vertices)
                v.p += offset;
    }
};

bool loadMeshFromFile(PrintObject* object, const char* filename, FMatrix3x3& matrix);

#endif//MODELFILE_H
