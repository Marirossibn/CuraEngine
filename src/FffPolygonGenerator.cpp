#include "FffPolygonGenerator.h"

#include <algorithm>

#include "slicer.h"
#include "utils/gettime.h"
#include "utils/logoutput.h"
#include "modelFile/modelFile.h"
#include "support.h"
#include "multiVolumes.h"
#include "layerPart.h"
#include "inset.h"
#include "skirt.h"
#include "skin.h"
#include "infill.h"
#include "raft.h"
#include "debug.h"
#include "Progress.h"

namespace cura
{

    
bool FffPolygonGenerator::generateAreas(SliceDataStorage& storage, PrintObject* object, TimeKeeper& timeKeeper)
{
    if (!sliceModel(object, timeKeeper, storage)) 
    {
        return false;
    }
    
    if (getSettingBoolean("magic_polygon_mode"))
    {
        slices2polygons_magicPolygonMode(storage, timeKeeper);
    }
    else
    {
        slices2polygons(storage, timeKeeper);
    }
    
    return true;
}

bool FffPolygonGenerator::sliceModel(PrintObject* object, TimeKeeper& timeKeeper, SliceDataStorage& storage) /// slices the model
{
    storage.model_min = object->min();
    storage.model_max = object->max();
    storage.model_size = storage.model_max - storage.model_min;

    log("Slicing model...\n");
    int initial_layer_thickness = object->getSettingInMicrons("layer_height_0");
    int layer_thickness = object->getSettingInMicrons("layer_height");
    int layer_count = (storage.model_size.z - (initial_layer_thickness - layer_thickness / 2)) / layer_thickness + 1;
    std::vector<Slicer*> slicerList;
    for(Mesh& mesh : object->meshes)
    {
        Slicer* slicer = new Slicer(&mesh, initial_layer_thickness - layer_thickness / 2, layer_thickness, layer_count, mesh.getSettingBoolean("meshfix_keep_open_polygons"), mesh.getSettingBoolean("meshfix_extensive_stitching"));
        slicerList.push_back(slicer);
        /*
        for(SlicerLayer& layer : slicer->layers)
        {
            //Reporting the outline here slows down the engine quite a bit, so only do so when debugging.
            //sendPolygons("outline", layer_nr, layer.z, layer.polygonList);
            //sendPolygons("openoutline", layer_nr, layer.openPolygonList);
        }
        */
    }
    
    log("Layer count: %i\n", layer_count);
    log("Sliced model in %5.3fs\n", timeKeeper.restart());

    object->clear();///Clear the mesh data, it is no longer needed after this point, and it saves a lot of memory.

    log("Generating layer parts...\n");
    //carveMultipleVolumes(storage.meshes);
    generateMultipleVolumesOverlap(slicerList, getSettingInMicrons("multiple_mesh_overlap"));
    
    for(unsigned int meshIdx=0; meshIdx < slicerList.size(); meshIdx++)
    {
        storage.meshes.emplace_back(&object->meshes[meshIdx]);
        SliceMeshStorage& meshStorage = storage.meshes[meshIdx];
        createLayerParts(meshStorage, slicerList[meshIdx], meshStorage.settings->getSettingBoolean("meshfix_union_all"), meshStorage.settings->getSettingBoolean("meshfix_union_all_remove_holes"), commandSocket);
        delete slicerList[meshIdx];

        bool has_raft = meshStorage.settings->getSettingAsPlatformAdhesion("adhesion_type") == Adhesion_Raft;
        //Add the raft offset to each layer.
        for(unsigned int layer_nr=0; layer_nr<meshStorage.layers.size(); layer_nr++)
        {
            if (has_raft)
                meshStorage.layers[layer_nr].printZ += meshStorage.settings->getSettingInMicrons("raft_base_thickness") + meshStorage.settings->getSettingInMicrons("raft_interface_thickness") + getSettingAsCount("raft_surface_layers") * getSettingInMicrons("raft_surface_thickness");

            if (commandSocket)
                commandSocket->sendLayerInfo(layer_nr, meshStorage.layers[layer_nr].printZ, layer_nr == 0 ? meshStorage.settings->getSettingInMicrons("layer_height_0") : meshStorage.settings->getSettingInMicrons("layer_height"));
        }
    }
    log("Generated layer parts in %5.3fs\n", timeKeeper.restart());
    return true;
}



void FffPolygonGenerator::slices2polygons(SliceDataStorage& storage, TimeKeeper& timeKeeper)
{
    if (commandSocket)
        commandSocket->beginSendSlicedObject();

    // const 
    unsigned int totalLayers = storage.meshes[0].layers.size();

    //dumpLayerparts(storage, "c:/models/output.html");

    for(unsigned int layer_nr=0; layer_nr<totalLayers; layer_nr++)
    {
        processInsets(storage, layer_nr);
        
        Progress::messageProgress(Progress::Stage::INSET, layer_nr+1, totalLayers, commandSocket);
    }
    
    removeEmptyFirstLayers(storage, getSettingInMicrons("layer_height"), totalLayers);
    
    if (totalLayers < 1)
    {
        log("Stopping process because there are no layers.\n");
        return;
    }
        
    processOozeShield(storage, totalLayers);
    
    log("Generated insets in %5.3fs\n", timeKeeper.restart());  
            
    for(SliceMeshStorage& mesh : storage.meshes)
    {
        generateSupportAreas(storage, &mesh, totalLayers, commandSocket);
        for (unsigned int layer_idx = 0; layer_idx < totalLayers; layer_idx++)
        {
            Polygons& support = storage.support.supportAreasPerLayer[layer_idx];
            sendPolygons(SupportType, layer_idx, support);
        }
    }
    log("Generated support areas in %5.3fs\n", timeKeeper.restart());
    


    for(unsigned int layer_nr=0; layer_nr<totalLayers; layer_nr++)
    {
        if (!getSettingBoolean("magic_spiralize") || static_cast<int>(layer_nr) < getSettingAsCount("bottom_layers"))    //Only generate up/downskin and infill for the first X layers when spiralize is choosen.
        {
            processSkins(storage, layer_nr);
        }
        Progress::messageProgress(Progress::Stage::SKIN, layer_nr+1, totalLayers, commandSocket);
    }
    
    for(unsigned int layer_nr=totalLayers-1; layer_nr>0; layer_nr--)
    {
        for(SliceMeshStorage& mesh : storage.meshes)
            combineSparseLayers(layer_nr, mesh, mesh.settings->getSettingAsCount("fill_sparse_combine"));
    }
    
    log("Generated up/down skin in %5.3fs\n", timeKeeper.restart());


    processWipeTower(storage, totalLayers);
    
    processPlatformAdhesion(storage);

}

void FffPolygonGenerator::processInsets(SliceDataStorage& storage, unsigned int layer_nr) 
{
    for(SliceMeshStorage& mesh : storage.meshes)
    {
        int insetCount = mesh.settings->getSettingAsCount("wall_line_count");
        if (mesh.settings->getSettingBoolean("magic_spiralize") && static_cast<int>(layer_nr) < mesh.settings->getSettingAsCount("bottom_layers") && layer_nr % 2 == 1)//Add extra insets every 2 layers when spiralizing, this makes bottoms of cups watertight.
            insetCount += 5;
        SliceLayer* layer = &mesh.layers[layer_nr];
        int extrusionWidth = mesh.settings->getSettingInMicrons("wall_line_width_x");
        int inset_count = insetCount; 
        if (mesh.settings->getSettingBoolean("alternate_extra_perimeter"))
            inset_count += layer_nr % 2; 
        generateInsets(layer, extrusionWidth, inset_count, mesh.settings->getSettingBoolean("wall_overlap_avoid_enabled"));

        for(unsigned int partNr=0; partNr<layer->parts.size(); partNr++)
        {
            if (layer->parts[partNr].insets.size() > 0)
            {
                sendPolygons(Inset0Type, layer_nr, layer->parts[partNr].insets[0]);
                for(unsigned int inset=1; inset<layer->parts[partNr].insets.size(); inset++)
                    sendPolygons(InsetXType, layer_nr, layer->parts[partNr].insets[inset]);
            }
        }
    }
}

void FffPolygonGenerator::removeEmptyFirstLayers(SliceDataStorage& storage, int layer_height, unsigned int totalLayers)
{ 
    int n_empty_first_layers = 0;
    for (unsigned int layer_idx = 0; layer_idx < totalLayers; layer_idx++)
    { 
        bool layer_is_empty = true;
        for (SliceMeshStorage& mesh : storage.meshes)
        {
            if (mesh.layers[layer_idx].parts.size() > 0)
            {
                layer_is_empty = false;
                break;
            }
        }
        
        if (layer_is_empty) 
        {
            n_empty_first_layers++;
        } else
        {
            break;
        }
    }
    
    if (n_empty_first_layers > 0)
    {
        log("Removing %d layers because they are empty\n", n_empty_first_layers);
        for (SliceMeshStorage& mesh : storage.meshes)
        {
            std::vector<SliceLayer>& layers = mesh.layers;
            layers.erase(layers.begin(), layers.begin() + n_empty_first_layers);
            for (SliceLayer& layer : layers)
            {
                layer.printZ -= n_empty_first_layers * layer_height;
            }
        }
        totalLayers -= n_empty_first_layers;
    }
}

void FffPolygonGenerator::processOozeShield(SliceDataStorage& storage, unsigned int totalLayers)
{
    if (!getSettingBoolean("ooze_shield_enabled"))
    {
        return;
    }
    
    for(unsigned int layer_nr=0; layer_nr<totalLayers; layer_nr++)
    {
        Polygons oozeShield;
        for(SliceMeshStorage& mesh : storage.meshes)
        {
            for(SliceLayerPart& part : mesh.layers[layer_nr].parts)
            {
                oozeShield = oozeShield.unionPolygons(part.outline.offset(MM2INT(2.0))); // TODO: put hard coded value in a variable with an explanatory name (and make var a parameter, and perhaps even a setting?)
            }
        }
        storage.oozeShield.push_back(oozeShield);
    }

    for(unsigned int layer_nr=0; layer_nr<totalLayers; layer_nr++)
        storage.oozeShield[layer_nr] = storage.oozeShield[layer_nr].offset(-MM2INT(1.0)).offset(MM2INT(1.0)); // TODO: put hard coded value in a variable with an explanatory name (and make var a parameter, and perhaps even a setting?)
    int offsetAngle = tan(getSettingInAngleRadians("ooze_shield_angle")) * getSettingInMicrons("layer_height");//Allow for a 60deg angle in the oozeShield.
    for(unsigned int layer_nr=1; layer_nr<totalLayers; layer_nr++)
        storage.oozeShield[layer_nr] = storage.oozeShield[layer_nr].unionPolygons(storage.oozeShield[layer_nr-1].offset(-offsetAngle));
    for(unsigned int layer_nr=totalLayers-1; layer_nr>0; layer_nr--)
        storage.oozeShield[layer_nr-1] = storage.oozeShield[layer_nr-1].unionPolygons(storage.oozeShield[layer_nr].offset(-offsetAngle));
}
  
void FffPolygonGenerator::processSkins(SliceDataStorage& storage, unsigned int layer_nr) 
{
    for(SliceMeshStorage& mesh : storage.meshes)
    {
        int extrusionWidth = mesh.settings->getSettingInMicrons("wall_line_width_x");
        generateSkins(layer_nr, mesh, extrusionWidth, mesh.settings->getSettingAsCount("bottom_layers"), mesh.settings->getSettingAsCount("top_layers"), mesh.settings->getSettingAsCount("skin_outline_count"), mesh.settings->getSettingBoolean("wall_overlap_avoid_enabled"));
        if (mesh.settings->getSettingInMicrons("infill_line_distance") > 0)
        {
            generateSparse(layer_nr, mesh, extrusionWidth, mesh.settings->getSettingAsCount("bottom_layers"), mesh.settings->getSettingAsCount("top_layers"));
            if (mesh.settings->getSettingString("fill_perimeter_gaps") == "Skin")
            {
                generatePerimeterGaps(layer_nr, mesh, extrusionWidth, mesh.settings->getSettingAsCount("bottom_layers"), mesh.settings->getSettingAsCount("top_layers"));
            }
            else if (mesh.settings->getSettingString("fill_perimeter_gaps") == "Everywhere")
            {
                generatePerimeterGaps(layer_nr, mesh, extrusionWidth, 0, 0);
            }
        }

        SliceLayer& layer = mesh.layers[layer_nr];
        for(SliceLayerPart& part : layer.parts)
        {
            for (SkinPart& skin_part : part.skin_parts)
            {
                sendPolygons(SkinType, layer_nr, skin_part.outline);
            }
        }
    }
}

void FffPolygonGenerator::processWipeTower(SliceDataStorage& storage, unsigned int totalLayers)
{
    if (getSettingInMicrons("wipe_tower_distance") > 0 && getSettingInMicrons("wipe_tower_size") > 0)
    {
        PolygonRef p = storage.wipeTower.newPoly();
        int tower_size = getSettingInMicrons("wipe_tower_size");
        int tower_distance = getSettingInMicrons("wipe_tower_distance");
        p.add(Point(storage.model_min.x - tower_distance, storage.model_max.y + tower_distance));
        p.add(Point(storage.model_min.x - tower_distance, storage.model_max.y + tower_distance + tower_size));
        p.add(Point(storage.model_min.x - tower_distance - tower_size, storage.model_max.y + tower_distance + tower_size));
        p.add(Point(storage.model_min.x - tower_distance - tower_size, storage.model_max.y + tower_distance));

        storage.wipePoint = Point(storage.model_min.x - tower_distance - tower_size / 2, storage.model_max.y + tower_distance + tower_size / 2);
    }
}

void FffPolygonGenerator::processPlatformAdhesion(SliceDataStorage& storage)
{
    switch(getSettingAsPlatformAdhesion("adhesion_type"))
    {
    case Adhesion_None:
        generateSkirt(storage, getSettingInMicrons("skirt_gap"), getSettingInMicrons("skirt_line_width"), getSettingAsCount("skirt_line_count"), getSettingInMicrons("skirt_minimal_length"));
        break;
    case Adhesion_Brim:
        generateSkirt(storage, 0, getSettingInMicrons("skirt_line_width"), getSettingAsCount("brim_line_count"), getSettingInMicrons("skirt_minimal_length"));
        break;
    case Adhesion_Raft:
        generateRaft(storage, getSettingInMicrons("raft_margin"));
        break;
    }
    
    sendPolygons(SkirtType, 0, storage.skirt);
}

void FffPolygonGenerator::slices2polygons_magicPolygonMode(SliceDataStorage& storage, TimeKeeper& timeKeeper)
{
    // const 
    unsigned int totalLayers = storage.meshes[0].layers.size();
    
    for(unsigned int layer_nr=0; layer_nr<totalLayers; layer_nr++)
    {
        for(SliceMeshStorage& mesh : storage.meshes)
        {
            SliceLayer* layer = &mesh.layers[layer_nr];
            for(SliceLayerPart& part : layer->parts)
            {
                sendPolygons(Inset0Type, layer_nr, part.outline);
            }
        }
    }
}

} // namespace cura