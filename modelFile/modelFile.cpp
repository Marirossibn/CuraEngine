#include <string.h>
#include <stdio.h>

#include "modelFile/modelFile.h"

/* Custom fgets function to support Mac line-ends in Ascii STL files. OpenSCAD produces this when used on Mac */
void* fgets_(char* ptr, size_t len, FILE* f)
{
    while(len && fread(ptr, 1, 1, f) > 0)
    {
        if (*ptr == '\n' || *ptr == '\r')
        {
            *ptr = '\0';
            return ptr;
        }
        ptr++;
        len++;
    }
    return NULL;
}

SimpleModel* loadModelSTL_ascii(const char* filename, FMatrix3x3& matrix)
{
    SimpleModel* m = new SimpleModel();
    FILE* f = fopen(filename, "rt");
    char buffer[1024];
    FPoint3 vertex;
    int n = 0;
    Point3 v0(0,0,0), v1(0,0,0), v2(0,0,0);
    while(fgets_(buffer, sizeof(buffer), f))
    {
        if (sscanf(buffer, " vertex %lf %lf %lf", &vertex.x, &vertex.y, &vertex.z) == 3)
        {
            n++;
            switch(n)
            {
            case 1:
                v0 = matrix.apply(vertex);
                break;
            case 2:
                v1 = matrix.apply(vertex);
                break;
            case 3:
                v2 = matrix.apply(vertex);
                m->addFace(v0, v1, v2);
                n = 0;
                break;
            }
        }
    }
    fclose(f);
    return m;
}

SimpleModel* loadModelSTL_binary(const char* filename, FMatrix3x3& matrix)
{
    FILE* f = fopen(filename, "rb");
    char buffer[80];
    uint32_t faceCount;
    //Skip the header
    if (fread(buffer, 80, 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }
    //Read the face count
    if (fread(&faceCount, sizeof(uint32_t), 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }
    //For each face read:
    //float(x,y,z) = normal, float(X,Y,Z)*3 = vertexes, uint16_t = flags
    SimpleModel* m = new SimpleModel();
    for(unsigned int i=0;i<faceCount;i++)
    {
        if (fread(buffer, sizeof(float) * 3, 1, f) != 1)
        {
            fclose(f);
            return NULL;
        }
        float v[9];
        if (fread(v, sizeof(float) * 9, 1, f) != 1)
        {
            fclose(f);
            return NULL;
        }
        Point3 v0 = matrix.apply(FPoint3(v[0], v[1], v[2]));
        Point3 v1 = matrix.apply(FPoint3(v[3], v[4], v[5]));
        Point3 v2 = matrix.apply(FPoint3(v[6], v[7], v[8]));
        m->addFace(v0, v1, v2);
        if (fread(buffer, sizeof(uint16_t), 1, f) != 1)
        {
            fclose(f);
            return NULL;
        }
    } 
    fclose(f);
    return m;
}

SimpleModel* loadModelSTL(const char* filename, FMatrix3x3& matrix)
{
    FILE* f = fopen(filename, "r");
    char buffer[6];
    if (f == NULL)
        return NULL;
    
    if (fread(buffer, 5, 1, f) != 1)
    {
        fclose(f);
        return NULL;
    }
    fclose(f);

    buffer[5] = '\0';
    if (strcasecmp(buffer, "SOLID") == 0)
    {
        return loadModelSTL_ascii(filename, matrix);
    }
    return loadModelSTL_binary(filename, matrix);
}

SimpleModel* loadModel(const char* filename, FMatrix3x3& matrix)
{
    const char* ext = strrchr(filename, '.');
    if (strcasecmp(ext, ".stl") == 0)
    {
        return loadModelSTL(filename, matrix);
    }
    return NULL;
}
