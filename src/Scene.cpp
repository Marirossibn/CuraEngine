//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "FffProcessor.h" //To start a slice.
#include "Scene.h"
#include "Application.h"
#include "communication/Communication.h" //To flush g-code and layer view when we're done.

namespace cura
{

Scene::Scene(const size_t num_mesh_groups)
: mesh_groups(num_mesh_groups)
, current_mesh_group(mesh_groups.begin())
{
    //Do nothing.
}

void Scene::compute()
{
    logWarning("%s", getAllSettingsString());
    for (std::vector<MeshGroup>::iterator mesh_group = mesh_groups.begin(); mesh_group != mesh_groups.end(); mesh_group++)
    {
        current_mesh_group = mesh_group;
        processMeshGroup(*mesh_group);
    }
}

const std::string Scene::getAllSettingsString() const
{
    std::stringstream output;
    output << settings.getAllSettingsString(); //Global settings.

    //Per-extruder settings.
    for (size_t extruder_nr = 0; extruder_nr < extruders.size(); extruder_nr++)
    {
        output << " -e" << extruder_nr << extruders[extruder_nr].settings.getAllSettingsString();
    }

    for (size_t mesh_group_index = 0; mesh_group_index < mesh_groups.size(); mesh_group_index++)
    {
        if (mesh_group_index == 0)
        {
            output << " -g";
        }
        else
        {
            output << " --next";
        }

        //Per-mesh-group settings.
        const MeshGroup& mesh_group = mesh_groups[mesh_group_index];
        output << mesh_group.settings.getAllSettingsString();

        //Per-object settings.
        for (size_t mesh_index = 0; mesh_index < mesh_group.meshes.size(); mesh_index++)
        {
            const Mesh& mesh = mesh_group.meshes[mesh_index];
            output << " -e" << mesh.settings.get<size_t>("extruder_nr") << " -l \"" << mesh_index << "\"" << mesh.settings.getAllSettingsString();
        }
    }
    output << "\n";

    return output.str();
}

void Scene::processMeshGroup(MeshGroup& mesh_group)
{
    FffProcessor* fff_processor = FffProcessor::getInstance();
    fff_processor->time_keeper.restart();

    TimeKeeper time_keeper_total;

    bool empty = true;
    for (Mesh& mesh : mesh_group.meshes)
    {
        if (!mesh.settings.get<bool>("infill_mesh") && !mesh.settings.get<bool>("anti_overhang_mesh"))
        {
            empty = false;
            break;
        }
    }
    if (empty)
    {
        Progress::messageProgress(Progress::Stage::FINISH, 1, 1); // 100% on this meshgroup
        log("Total time elapsed %5.2fs.\n", time_keeper_total.restart());
        return;
    }

    if (mesh_group.settings.get<bool>("wireframe_enabled"))
    {
        log("Starting Neith Weaver...\n");

        Weaver weaver(fff_processor);
        weaver.weave(&mesh_group);
        
        log("Starting Neith Gcode generation...\n");
        Wireframe2gcode gcoder(weaver, fff_processor->gcode_writer.gcode, fff_processor);
        gcoder.writeGCode();
        log("Finished Neith Gcode generation...\n");
    }
    else //Normal operation (not wireframe).
    {
        SliceDataStorage storage;

        if (!fff_processor->polygon_generator.generateAreas(storage, &mesh_group, fff_processor->time_keeper))
        {
            return;
        }
        
        Progress::messageProgressStage(Progress::Stage::EXPORT, &fff_processor->time_keeper);
        fff_processor->gcode_writer.writeGCode(storage, fff_processor->time_keeper);
    }

    Progress::messageProgress(Progress::Stage::FINISH, 1, 1); // 100% on this meshgroup
    Application::getInstance().communication->flushGCode();
    Application::getInstance().communication->sendOptimizedLayerData();
    log("Total time elapsed %5.2fs.\n", time_keeper_total.restart());

    fff_processor->polygon_generator.setParent(fff_processor); // otherwise consequent getSetting calls (e.g. for finalize) will refer to non-existent meshgroup
    fff_processor->gcode_writer.setParent(fff_processor); // otherwise consequent getSetting calls (e.g. for finalize) will refer to non-existent meshgroup
}

} //namespace cura