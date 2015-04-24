/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#include "skin.h"
#include "polygonOptimizer.h"
#include "utils/polygonUtils.h"

namespace cura 
{

void generateSkins(int layerNr, SliceMeshStorage& storage, int extrusionWidth, int downSkinCount, int upSkinCount, int insetCount, int avoidOverlappingPerimeters)
{
    generateSkinAreas(layerNr, storage, extrusionWidth, downSkinCount, upSkinCount);

    SliceLayer* layer = &storage.layers[layerNr];
    for(unsigned int partNr=0; partNr<layer->parts.size(); partNr++)
    {
        SliceLayerPart* part = &layer->parts[partNr];
        generateSkinInsets(part, extrusionWidth, insetCount, avoidOverlappingPerimeters);
    }
}
void generateSkinAreas(int layerNr, SliceMeshStorage& storage, int extrusionWidth, int downSkinCount, int upSkinCount)
{
    SliceLayer* layer = &storage.layers[layerNr];

    for(unsigned int partNr=0; partNr<layer->parts.size(); partNr++)
    {
        SliceLayerPart* part = &layer->parts[partNr];
        
        Polygons upskin = part->insets.back().offset(-extrusionWidth/2);
        Polygons downskin = upskin;

        
        if (static_cast<int>(layerNr - downSkinCount) >= 0)
        {
            SliceLayer* layer2 = &storage.layers[layerNr - downSkinCount];
            for(unsigned int partNr2=0; partNr2<layer2->parts.size(); partNr2++)
            {
                if (part->boundaryBox.hit(layer2->parts[partNr2].boundaryBox))
                    downskin = downskin.difference(layer2->parts[partNr2].insets[layer2->parts[partNr2].insets.size() - 1]);
            }
        }
        if (static_cast<int>(layerNr + upSkinCount) < static_cast<int>(storage.layers.size()))
        {
            SliceLayer* layer2 = &storage.layers[layerNr + upSkinCount];
            for(unsigned int partNr2=0; partNr2<layer2->parts.size(); partNr2++)
            {
                if (part->boundaryBox.hit(layer2->parts[partNr2].boundaryBox))
                    upskin = upskin.difference(layer2->parts[partNr2].insets[layer2->parts[partNr2].insets.size() - 1]);
            }
        }
        
        Polygons skin = upskin.unionPolygons(downskin);
          
        double minAreaSize = (2 * M_PI * INT2MM(extrusionWidth) * INT2MM(extrusionWidth)) * 0.3; // TODO: hardcoded value!
        skin.removeSmallAreas(minAreaSize);
        
        for (Polygons& skin_area_part : skin.splitIntoParts())
        {
            part->skin_parts.emplace_back();
            part->skin_parts.back().outline = skin_area_part;
        }
    }
}


void generateSkinInsets(SliceLayerPart* part, int extrusionWidth, int insetCount, bool avoidOverlappingPerimeters)
{
    if (insetCount == 0)
    {
        return;
    }
    
    for (SkinPart& skin_part : part->skin_parts)
    {
        for(int i=0; i<insetCount; i++)
        {
            skin_part.insets.push_back(Polygons());
            if (i == 0)
            {
                offsetSafe(skin_part.outline, - extrusionWidth/2, extrusionWidth, skin_part.insets[0], avoidOverlappingPerimeters);
                Polygons in_between = skin_part.outline.difference(skin_part.insets[0].offset(extrusionWidth/2)); 
                part->perimeterGaps.add(in_between);
            } else
            {
                offsetExtrusionWidth(skin_part.insets[i-1], true, extrusionWidth, skin_part.insets[i], &part->perimeterGaps, avoidOverlappingPerimeters);
            }
                
            optimizePolygons(skin_part.insets[i]);
            if (skin_part.insets[i].size() < 1)
            {
                skin_part.insets.pop_back();
                break;
            }
        }
    }
}

void generateSparse(int layerNr, SliceMeshStorage& storage, int extrusionWidth, int downSkinCount, int upSkinCount, int avoidOverlappingPerimeters)
{
    SliceLayer* layer = &storage.layers[layerNr];

    for(unsigned int partNr=0; partNr<layer->parts.size(); partNr++)
    {
        SliceLayerPart* part = &layer->parts[partNr];
        
        Polygons sparse = part->insets.back().offset(-extrusionWidth/2);

        
        Polygons downskin = sparse;
        Polygons upskin = sparse;
        
        if (static_cast<int>(layerNr - downSkinCount) >= 0)
        {
            SliceLayer* layer2 = &storage.layers[layerNr - downSkinCount];
            for(unsigned int partNr2=0; partNr2<layer2->parts.size(); partNr2++)
            {
                if (part->boundaryBox.hit(layer2->parts[partNr2].boundaryBox))
                {
                    if (layer2->parts[partNr2].insets.size() > 1)
                    {
                        downskin = downskin.difference(layer2->parts[partNr2].insets[layer2->parts[partNr2].insets.size() - 2]);
                    }else{
                        downskin = downskin.difference(layer2->parts[partNr2].insets[layer2->parts[partNr2].insets.size() - 1]);
                    }
                }
            }
        }
        if (static_cast<int>(layerNr + upSkinCount) < static_cast<int>(storage.layers.size()))
        {
            SliceLayer* layer2 = &storage.layers[layerNr + upSkinCount];
            for(unsigned int partNr2=0; partNr2<layer2->parts.size(); partNr2++)
            {
                if (part->boundaryBox.hit(layer2->parts[partNr2].boundaryBox))
                {
                    if (layer2->parts[partNr2].insets.size() > 1)
                    {
                        upskin = upskin.difference(layer2->parts[partNr2].insets[layer2->parts[partNr2].insets.size() - 2]);
                    }else{
                        upskin = upskin.difference(layer2->parts[partNr2].insets[layer2->parts[partNr2].insets.size() - 1]);
                    }
                }
            }
        }
        
        Polygons result = upskin.unionPolygons(downskin);
        // TODO: why code duplication? Why not just use part->skinOutline (with some offset) ?!

        double minAreaSize = 3.0;//(2 * M_PI * INT2MM(config.extrusionWidth) * INT2MM(config.extrusionWidth)) * 3;
        for(unsigned int i=0; i<result.size(); i++)
        {
            double area = INT2MM(INT2MM(fabs(result[i].area())));
            if (area < minAreaSize) /* Only create an up/down skin if the area is large enough. So you do not create tiny blobs of "trying to fill" */
            {
                result.remove(i);
                i -= 1;
            }
        }
        
        part->sparse_outline.push_back(sparse.difference(result));
    }
}

void combineSparseLayers(int layerNr, SliceMeshStorage& storage, int amount)
{
    SliceLayer* layer = &storage.layers[layerNr];

    for(int n=1; n<amount; n++)
    {
        if (layerNr < n)
            break;
        
        SliceLayer* layer2 = &storage.layers[layerNr - n];
        for(SliceLayerPart& part : layer->parts)
        {
            Polygons result;
            for(SliceLayerPart& part2 : layer2->parts)
            {
                if (part.boundaryBox.hit(part2.boundaryBox))
                {
                    Polygons intersection = part.sparse_outline[n - 1].intersection(part2.sparse_outline[0]).offset(-200).offset(200);
                    result.add(intersection);
                    part.sparse_outline[n - 1] = part.sparse_outline[n - 1].difference(intersection);
                    part2.sparse_outline[0] = part2.sparse_outline[0].difference(intersection);
                }
            }
            
            part.sparse_outline.push_back(result);
        }
    }
}


void generatePerimeterGaps(int layer_nr, SliceMeshStorage& storage, int extrusionWidth, int downSkinCount, int upSkinCount)
{
    SliceLayer& layer = storage.layers[layer_nr];
    
    for (SliceLayerPart& part : layer.parts) 
    { // handle gaps between perimeters etc.
        if (downSkinCount > 0 && upSkinCount > 0 && // note: if both are zero or less, then all gaps will be used
            layer_nr >= downSkinCount && layer_nr < static_cast<int>(storage.layers.size() - upSkinCount)) // remove gaps which appear within print, i.e. not on the bottom most or top most skin
        {
            Polygons outlines_above;
            for (SliceLayerPart& part_above : storage.layers[layer_nr + upSkinCount].parts)
            {
                outlines_above.add(part_above.outline);
            }
            Polygons outlines_below;
            for (SliceLayerPart& part_below : storage.layers[layer_nr - downSkinCount].parts)
            {
                outlines_below.add(part_below.outline);
            }
            part.perimeterGaps = part.perimeterGaps.intersection(outlines_above.xorPolygons(outlines_below));
        }
        double minAreaSize = (2 * M_PI * INT2MM(extrusionWidth) * INT2MM(extrusionWidth)) * 0.3; // TODO: hardcoded value!
        part.perimeterGaps.removeSmallAreas(minAreaSize);
    }
}

}//namespace cura
