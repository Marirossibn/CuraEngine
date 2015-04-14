#include "Wireframe2gcode.h"

#include <cmath> // sqrt
#include <fstream> // debug IO

#include "weaveDataStorage.h"

using namespace cura;





void Wireframe2gcode::writeGCode(CommandSocket* commandSocket, int& maxObjectHeight)
{

    if (commandSocket)
        commandSocket->beginGCode();
    
    maxObjectHeight = wireFrame.layers.back().z1;
    
    { // starting Gcode
        if (hasSetting("bedTemperature") && getSettingInt("bedTemperature") > 0)
            gcode.writeBedTemperatureCommand(getSettingInt("bedTemperature"), true);
        if (hasSetting("printTemperature") && getSettingInt("printTemperature") > 0)
            gcode.writeTemperatureCommand(getSettingInt("extruderNr"), getSettingInt("printTemperature"));
        
        gcode.writeCode(getSetting("startCode").c_str());
        if (gcode.getFlavor() == GCODE_FLAVOR_BFB)
        {
            gcode.writeComment("enable auto-retraction");
            std::ostringstream tmp;
            tmp << "M227 S" << (getSettingInt("retractionAmount") * 2560 / 1000) << " P" << (getSettingInt("retractionAmount") * 2560 / 1000);  // TODO: put hard coded value in a variable with an explanatory name (and make var a parameter, and perhaps even a setting?)
            gcode.writeLine(tmp.str().c_str());
        }
    }
    
    
            
    unsigned int totalLayers = wireFrame.layers.size();
    gcode.writeLayerComment(0);
    gcode.writeTypeComment("SKIRT");

    gcode.setZ(initial_layer_thickness);
    
    for (PolygonRef bottom_part : wireFrame.bottom_infill.roof_outlines)
    {
        if (bottom_part.size() == 0) continue;
        writeMoveWithRetract(bottom_part[bottom_part.size()-1]);
        for (Point& segment_to : bottom_part)
        {
            gcode.writeMove(segment_to, speedBottom, extrusion_per_mm_flat);
        }
    }
    
    
    
    // bottom:
    Polygons empty_outlines;
    writeFill(wireFrame.bottom_infill.roof_insets, empty_outlines, 
              [this](Wireframe2gcode& thiss, WeaveRoofPart& inset, WeaveConnectionPart& part, unsigned int segment_idx) { 
                    WeaveConnectionSegment& segment = part.connection.segments[segment_idx]; 
                    if (segment.segmentType == WeaveSegmentType::MOVE || segment.segmentType == WeaveSegmentType::DOWN_AND_FLAT) // this is the case when an inset overlaps with a hole 
                    {
                        writeMoveWithRetract(segment.to); 
                    } else 
                    {
                        gcode.writeMove(segment.to, speedBottom, extrusion_per_mm_connection); 
                    }
                }   
            , 
              [this](Wireframe2gcode& thiss, WeaveConnectionSegment& segment) { 
                    if (segment.segmentType == WeaveSegmentType::MOVE)
                        writeMoveWithRetract(segment.to);
                    else if (segment.segmentType == WeaveSegmentType::DOWN_AND_FLAT)
                        return; // do nothing
                    else 
                        gcode.writeMove(segment.to, speedBottom, extrusion_per_mm_flat); 
                }
            );
    
    for (unsigned int layer_nr = 0; layer_nr < wireFrame.layers.size(); layer_nr++)
    {
        
        logProgress("export", layer_nr+1, totalLayers); // abuse the progress system of the normal mode of CuraEngine
        if (commandSocket) commandSocket->sendProgress(2.0/3.0 + 1.0/3.0 * float(layer_nr) / float(totalLayers));
        
        WeaveLayer& layer = wireFrame.layers[layer_nr];
        
        gcode.writeLayerComment(layer_nr+1);
        
        int fanSpeed = getSettingInt("fanSpeedMax");
        if (layer_nr == 0)
            fanSpeed = getSettingInt("fanSpeedMin");
        gcode.writeFanCommand(fanSpeed);
        
        for (unsigned int part_nr = 0; part_nr < layer.connections.size(); part_nr++)
        {
            WeaveConnectionPart& part = layer.connections[part_nr];
       
            if (part.connection.segments.size() == 0) continue;
            
            gcode.writeTypeComment("SUPPORT"); // connection
            {
                if (vSize2(gcode.getPositionXY() - part.connection.from) > connectionHeight)
                {
                    Point3 point_same_height(part.connection.from.x, part.connection.from.y, layer.z1+100);
                    writeMoveWithRetract(point_same_height);
                }
                writeMoveWithRetract(part.connection.from);
                for (unsigned int segment_idx = 0; segment_idx < part.connection.segments.size(); segment_idx++)
                {
                    handle_segment(layer, part, segment_idx);
                }
            }
            
            
            
            gcode.writeTypeComment("WALL-OUTER"); // top
            {
                for (unsigned int segment_idx = 0; segment_idx < part.connection.segments.size(); segment_idx++)
                {
                    WeaveConnectionSegment& segment = part.connection.segments[segment_idx];
                    if (segment.segmentType == WeaveSegmentType::DOWN) continue;
                    if (segment.segmentType == WeaveSegmentType::MOVE) 
                    {
                        writeMoveWithRetract(segment.to);
                    } else 
                    {
                        gcode.writeMove(segment.to, speedFlat, extrusion_per_mm_flat);
                        gcode.writeDelay(flat_delay);
                    }
                }
            }
        }
        
        // roofs:
        gcode.setZ(layer.z1);
        std::function<void (Wireframe2gcode& thiss, WeaveRoofPart& inset, WeaveConnectionPart& part, unsigned int segment_idx)>
            handle_roof = &Wireframe2gcode::handle_roof_segment;
        writeFill(layer.roofs.roof_insets, layer.roofs.roof_outlines,
                  handle_roof,
                [this](Wireframe2gcode& thiss, WeaveConnectionSegment& segment) { // handle flat segments
                    if (segment.segmentType == WeaveSegmentType::MOVE)
                    {
                        writeMoveWithRetract(segment.to);
                    } else if (segment.segmentType == WeaveSegmentType::DOWN_AND_FLAT)
                    {
                        // do nothing
                    } else 
                    {   
                        gcode.writeMove(segment.to, speedFlat, extrusion_per_mm_flat);
                        gcode.writeDelay(flat_delay);
                    }
                });
        
     
        
    }
    
    gcode.setZ(maxObjectHeight);
    
    gcode.writeRetraction(&standard_retraction_config);
    
    
    gcode.updateTotalPrintTime();
    
    gcode.writeDelay(0.3);
    
    gcode.writeFanCommand(0);

    if (commandSocket)
    {
        gcode.finalize(maxObjectHeight, getSettingInt("moveSpeed"), getSetting("endCode").c_str());
        for(int e=0; e<MAX_EXTRUDERS; e++)
            gcode.writeTemperatureCommand(e, 0, false);

        commandSocket->sendGCodeLayer();
        commandSocket->endSendSlicedObject();
    }
}

    
void Wireframe2gcode::go_down(WeaveLayer& layer, WeaveConnectionPart& part, unsigned int segment_idx) 
{ 
    WeaveConnectionSegment& segment = part.connection.segments[segment_idx];
    Point3 from = (segment_idx == 0)? part.connection.from : part.connection.segments[segment_idx - 1].to;
    if (go_back_to_last_top)
        gcode.writeMove(from, speedDown, 0);
    if (straight_first_when_going_down <= 0)
    {
        gcode.writeMove(segment.to, speedDown, extrusion_per_mm_connection);
    } else 
    {
        Point3& to = segment.to;
        Point3 from = gcode.getPosition();// segment.from;
        Point3 vec = to - from;
        Point3 in_between = from + vec * straight_first_when_going_down / 100;
        
        Point3 up(in_between.x, in_between.y, from.z);
        int64_t new_length = (up - from).vSize() + (to - up).vSize() + 5;
        int64_t orr_length = vec.vSize();
        double enlargement = new_length / orr_length;
        gcode.writeMove(up, speedDown*enlargement, extrusion_per_mm_connection / enlargement);
        gcode.writeMove(to, speedDown*enlargement, extrusion_per_mm_connection / enlargement);
    }
    gcode.writeDelay(bottom_delay);
    if (up_dist_half_speed > 0)
    {
        
        gcode.writeMove(Point3(0,0,up_dist_half_speed) + gcode.getPosition(), speedUp / 2, extrusion_per_mm_connection * 2);
    }
};


    
void Wireframe2gcode::strategy_knot(WeaveLayer& layer, WeaveConnectionPart& part, unsigned int segment_idx)
{
    WeaveConnectionSegment& segment = part.connection.segments[segment_idx];
    gcode.writeMove(segment.to, speedUp, extrusion_per_mm_connection);
    Point3 next_vector;
    if (segment_idx + 1 < part.connection.segments.size())
    {
        WeaveConnectionSegment& next_segment = part.connection.segments[segment_idx+1];
        next_vector = next_segment.to - segment.to;
    } else
    {
        next_vector = part.connection.segments[0].to - segment.to;
    }
    Point next_dir_2D(next_vector.x, next_vector.y);
    next_dir_2D = next_dir_2D * top_jump_dist / vSize(next_dir_2D);
    Point3 next_dir (next_dir_2D.X / 2, next_dir_2D.Y / 2, -top_jump_dist);
    
    Point3 current_pos = gcode.getPosition();
    
    gcode.writeMove(current_pos - next_dir, speedUp, 0);
    gcode.writeDelay(top_delay);
    gcode.writeMove(current_pos + next_dir_2D, speedUp, 0);
};
void Wireframe2gcode::strategy_retract(WeaveLayer& layer, WeaveConnectionPart& part, unsigned int segment_idx)
{
    WeaveConnectionSegment& segment = part.connection.segments[segment_idx];
    Point3 from = (segment_idx == 0)? part.connection.from : part.connection.segments[segment_idx - 1].to;
    
    RetractionConfig retraction_config;
    // TODO: get these from the settings!
    retraction_config.amount = 500; //INT2MM(getSettingInt("retractionAmount"))
    retraction_config.primeAmount = 0;//INT2MM(getSettingInt("retractionPrime
    retraction_config.speed = 20; // 40;
    retraction_config.primeSpeed = 15; // 30;
    retraction_config.zHop = 0; //getSettingInt("retractionZHop");

    double top_retract_pause = 2.0;
    int retract_hop_dist = 1000;
    bool after_retract_hop = false;
    //bool go_horizontal_first = true;
    bool lower_retract_start = true;
    
    
    Point3& to = segment.to;
    if (lower_retract_start)
    {
        Point3 vec = to - from;
        Point3 lowering = vec * retract_hop_dist / 2 / vec.vSize();
        Point3 lower = to - lowering;
        gcode.writeMove(lower, speedUp, extrusion_per_mm_connection);
        gcode.writeRetraction(&retraction_config);
        gcode.writeMove(to + lowering, speedUp, 0);
        gcode.writeDelay(top_retract_pause);
        if (after_retract_hop)
            gcode.writeMove(to + Point3(0, 0, retract_hop_dist), speedFlat, 0);
        
    } else 
    {
        gcode.writeMove(to, speedUp, extrusion_per_mm_connection);
        gcode.writeRetraction(&retraction_config);
        gcode.writeMove(to + Point3(0, 0, retract_hop_dist), speedFlat, 0);
        gcode.writeDelay(top_retract_pause);
        if (after_retract_hop)    
            gcode.writeMove(to + Point3(0, 0, retract_hop_dist*3), speedFlat, 0);
        }
};

void Wireframe2gcode::strategy_compensate(WeaveLayer& layer, WeaveConnectionPart& part, unsigned int segment_idx)
{
    WeaveConnectionSegment& segment = part.connection.segments[segment_idx];
    Point3 from = (segment_idx == 0)? part.connection.from : part.connection.segments[segment_idx - 1].to;
    Point3 to = segment.to + Point3(0, 0, fall_down*(segment.to - from).vSize() / connectionHeight);
    Point3 vector = segment.to - from;
    Point3 dir = vector * drag_along / vector.vSize();
    
    Point3 next_point;
    if (segment_idx + 1 < part.connection.segments.size())
    {
        WeaveConnectionSegment& next_segment = part.connection.segments[segment_idx+1];
        next_point = next_segment.to;
    } else
    {
        next_point = part.connection.segments[0].to;
    }
    Point3 next_vector = next_point - segment.to;
    Point next_dir_2D(next_vector.x, next_vector.y);
    int64_t next_dir_2D_size = vSize(next_dir_2D);
    if (next_dir_2D_size > 0)
        next_dir_2D = next_dir_2D * drag_along / next_dir_2D_size;
    Point3 next_dir (next_dir_2D.X, next_dir_2D.Y, 0);
    
    Point3 newTop = to - next_dir + dir;
    
    int64_t orrLength = (segment.to - from).vSize() + next_vector.vSize() + 1; // + 1 in order to avoid division by zero
    int64_t newLength = (newTop - from).vSize() + (next_point - newTop).vSize() + 1; // + 1 in order to avoid division by zero
    
    gcode.writeMove(newTop, speedUp * newLength / orrLength, extrusion_per_mm_connection * orrLength / newLength);
};
void Wireframe2gcode::handle_segment(WeaveLayer& layer, WeaveConnectionPart& part, unsigned int segment_idx) 
{
    WeaveConnectionSegment& segment = part.connection.segments[segment_idx];
    
    switch(segment.segmentType)
    {
        case WeaveSegmentType::MOVE:
            writeMoveWithRetract(segment.to);
            break;
        case WeaveSegmentType::DOWN:
            go_down(layer, part, segment_idx);
            break;
        case WeaveSegmentType::FLAT:
            DEBUG_SHOW("flat piece in connection?!!?!");
            break;
        case WeaveSegmentType::UP:
            if (strategy == STRATEGY_KNOT)
            {
                strategy_knot(layer, part, segment_idx);
            } else if (strategy == STRATEGY_RETRACT)
            { 
                strategy_retract(layer, part, segment_idx);
            } else if (strategy == STRATEGY_COMPENSATE)
            {
                strategy_compensate(layer, part, segment_idx);
            }
            break;
        case WeaveSegmentType::DOWN_AND_FLAT:
            logError("Down and flat move in non-horizontal connection!");
            break;
    }
};




void Wireframe2gcode::handle_roof_segment(WeaveRoofPart& inset, WeaveConnectionPart& part, unsigned int segment_idx)
{
    WeaveConnectionSegment& segment = part.connection.segments[segment_idx];
    Point3 from = (segment_idx == 0)? part.connection.from : part.connection.segments[segment_idx - 1].to;
    WeaveConnectionSegment* next_segment = nullptr;
    if (segment_idx + 1 < part.connection.segments.size())
        next_segment = &part.connection.segments[segment_idx+1];
    switch(segment.segmentType)
    {
        case WeaveSegmentType::MOVE:
        case WeaveSegmentType::DOWN_AND_FLAT:
            if (next_segment && next_segment->segmentType != WeaveSegmentType::DOWN_AND_FLAT)
            {
                writeMoveWithRetract(segment.to);
            }
            break;
        case WeaveSegmentType::UP:
        {
            Point3 to = segment.to + Point3(0, 0, roof_fall_down);
            
            Point3 vector = segment.to - from;
            if (vector.vSize2() == 0) return;
            Point3 dir = vector * roof_drag_along / vector.vSize();
            
            Point3 next_vector;
            if (next_segment)
            {
                next_vector = next_segment->to - segment.to;
            } else
            {
                next_vector = part.connection.segments[0].to - segment.to;
            }
            Point next_dir_2D(next_vector.x, next_vector.y);
            Point3 detoured = to + dir;
            if (vSize2(next_dir_2D) > 0) 
            {
                next_dir_2D = next_dir_2D * roof_drag_along / vSize(next_dir_2D);
                Point3 next_dir (next_dir_2D.X, next_dir_2D.Y, 0);
                detoured -= next_dir;
            }
            
            gcode.writeMove(detoured, speedUp, extrusion_per_mm_connection);

        }
        break;
        case WeaveSegmentType::DOWN:
            gcode.writeMove(segment.to, speedDown, extrusion_per_mm_connection);
            gcode.writeDelay(roof_outer_delay);
            break;
        case WeaveSegmentType::FLAT:
            logError("Flat move in connection!");
            break;
    }

};



void Wireframe2gcode::writeFill(std::vector<WeaveRoofPart>& fill_insets, Polygons& roof_outlines
    , std::function<void (Wireframe2gcode& thiss, WeaveRoofPart& inset, WeaveConnectionPart& part, unsigned int segment_idx)> connectionHandler
    , std::function<void (Wireframe2gcode& thiss, WeaveConnectionSegment& p)> flatHandler)
{
        
    // bottom:
    gcode.writeTypeComment("FILL");
    for (unsigned int inset_idx = 0; inset_idx < fill_insets.size(); inset_idx++)
    {
        WeaveRoofPart& inset = fill_insets[inset_idx];
        
        
        for (unsigned int inset_part_nr = 0; inset_part_nr < inset.connections.size(); inset_part_nr++)
        {
            WeaveConnectionPart& inset_part = inset.connections[inset_part_nr];
            std::vector<WeaveConnectionSegment>& segments = inset_part.connection.segments;
            
            gcode.writeTypeComment("SUPPORT"); // connection
            if (segments.size() == 0) continue;
            Point3 first_extrusion_from = inset_part.connection.from;
            unsigned int first_segment_idx;
            for (first_segment_idx = 0; first_segment_idx < segments.size() && segments[first_segment_idx].segmentType == WeaveSegmentType::MOVE; first_segment_idx++)
            { // finds the first segment which is not a move
                first_extrusion_from = segments[first_segment_idx].to;
            }
            if (first_segment_idx == segments.size())
                continue;
            writeMoveWithRetract(first_extrusion_from);
            for (unsigned int segment_idx = first_segment_idx; segment_idx < segments.size(); segment_idx++)
            {
                connectionHandler(*this, inset, inset_part, segment_idx);
            }
            
            gcode.writeTypeComment("WALL-INNER"); // top
            for (unsigned int segment_idx = 0; segment_idx < segments.size(); segment_idx++)
            {
                WeaveConnectionSegment& segment = segments[segment_idx];

                if (segment.segmentType == WeaveSegmentType::DOWN) continue;

                flatHandler(*this, segment); 
            }
        }
        
        
    }
    
    gcode.writeTypeComment("WALL-OUTER"); // outer perimeter of the flat parts
    for (PolygonRef poly : roof_outlines)
    {
        writeMoveWithRetract(poly[poly.size() - 1]);
        for (Point& p : poly)
        {
            Point3 to(p.X, p.Y, gcode.getPositionZ());
            WeaveConnectionSegment segment(to, WeaveSegmentType::FLAT);
            flatHandler(*this, segment); 
        }
    }
}




void Wireframe2gcode::writeMoveWithRetract(Point3 to)
{
    if ((gcode.getPosition() - to).vSize2() >= nozzle_top_diameter * nozzle_top_diameter * 2 * 2)
        gcode.writeRetraction(&standard_retraction_config);
    gcode.writeMove(to, moveSpeed, 0);
}

void Wireframe2gcode::writeMoveWithRetract(Point to)
{
    if (vSize2(gcode.getPositionXY() - to) >= nozzle_top_diameter * nozzle_top_diameter * 2 * 2)
        gcode.writeRetraction(&standard_retraction_config);
    gcode.writeMove(to, moveSpeed, 0);
}

Wireframe2gcode::Wireframe2gcode(Weaver& weaver, GCodeExport& gcode, SettingsBase* settings_base) 
: SettingsBase(settings_base) 
, gcode(gcode)
{
    wireFrame = weaver.wireFrame;
    initial_layer_thickness = getSettingInt("initialLayerThickness");
    connectionHeight = getSettingInt("wireframeConnectionHeight"); 
    roof_inset = getSettingInt("wireframeRoofInset"); 
    
    filament_diameter = getSettingInt("filamentDiameter");
    extrusionWidth = getSettingInt("extrusionWidth");
    
    flowConnection = getSettingInt("wireframeFlowConnection");
    flowFlat = getSettingInt("wireframeFlowFlat");
    
    double filament_area = /* M_PI * */ (INT2MM(filament_diameter) / 2.0) * (INT2MM(filament_diameter) / 2.0);
    double lineArea = /* M_PI * */ (INT2MM(extrusionWidth) / 2.0) * (INT2MM(extrusionWidth) / 2.0);
    extrusion_per_mm_connection = lineArea / filament_area * double(flowConnection) / 100.0;
    extrusion_per_mm_flat = lineArea / filament_area * double(flowFlat) / 100.0;
    
    nozzle_outer_diameter = getSettingInt("machineNozzleTipOuterDiameter"); // ___       ___   .
    nozzle_head_distance = getSettingInt("machineNozzleHeadDistance");      //    |     |      .
    nozzle_expansion_angle = getSettingInt("machineNozzleExpansionAngle");  //     \_U_/       .
    nozzle_clearance = getSettingInt("wireframeNozzleClearance");    // at least line width
    nozzle_top_diameter = tan(static_cast<double>(nozzle_expansion_angle)/180.0 * M_PI) * connectionHeight + nozzle_outer_diameter + nozzle_clearance;
    
    moveSpeed = 40;
    speedBottom =  getSettingInt("wireframePrintspeedBottom");
    speedUp = getSettingInt("wireframePrintspeedUp");
    speedDown = getSettingInt("wireframePrintspeedDown");
    speedFlat = getSettingInt("wireframePrintspeedFlat");

    flat_delay = getSettingInt("wireframeFlatDelay")/100.0;
    bottom_delay = getSettingInt("wireframeBottomDelay")/100.0;
    top_delay = getSettingInt("wireframeTopDelay")/100.0;
    
    up_dist_half_speed = getSettingInt("wireframeUpDistHalfSpeed");
    
    top_jump_dist = getSettingInt("wireframeTopJump");
    
    fall_down = getSettingInt("wireframeFallDown");
    drag_along = getSettingInt("wireframeDragAlong");
    
    strategy = getSettingInt("wireframeStrategy"); //  HIGHER_BEND_NO_STRAIGHTEN; // RETRACT_TO_STRAIGHTEN; // MOVE_TO_STRAIGHTEN; // 
    
    go_back_to_last_top = false;
    straight_first_when_going_down = getSettingInt("wireframeStraightBeforeDown"); // %
    
    roof_fall_down = getSettingInt("wireframeRoofFallDown");
    roof_drag_along = getSettingInt("wireframeRoofDragAlong");
    roof_outer_delay = getSettingInt("wireframeRoofOuterDelay")/100.0;
    
    
    standard_retraction_config.amount = INT2MM(getSettingInt("retractionAmount"));
    standard_retraction_config.primeAmount = INT2MM(getSettingInt("retractionPrimeAmount"));
    standard_retraction_config.speed = getSettingInt("retractionSpeed");
    standard_retraction_config.primeSpeed = getSettingInt("retractionPrimeSpeed");
    standard_retraction_config.zHop = getSettingInt("retractionZHop");


}


    
