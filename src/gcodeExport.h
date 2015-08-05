/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#ifndef GCODEEXPORT_H
#define GCODEEXPORT_H

#include <stdio.h>
#include <deque> // for extrusionAmountAtPreviousRetractions
#include <sstream> // for stream.str()

#include "settings.h"
#include "utils/intpoint.h"
#include "timeEstimate.h"
#include "MeshGroup.h"

namespace cura {

struct CoastingConfig
{
    bool coasting_enable; 
    double coasting_volume_move; 
    double coasting_speed_move; 
    double coasting_min_volume_move; 

    double coasting_volume_retract;
    double coasting_speed_retract;
    double coasting_min_volume_retract;
};
    
class RetractionConfig
{
public:
    double amount; //!< The amount retracted
    double speed; //!< The speed with which to retract
    double primeSpeed; //!< the speed with which to unretract
    double primeAmount; //!< the amount of material primed after unretracting
    int zHop; //!< the amount with which to lift the head during a retraction-travel
};

//The GCodePathConfig is the configuration for moves/extrusion actions. This defines at which width the line is printed and at which speed.
class GCodePathConfig
{
private:
    double speed; //!< movement speed
    int line_width; //!< width of the line extruded
    double flow; //!< extrusion flow in %
    int layer_thickness; //!< layer height
    double extrusion_mm3_per_mm;//!< mm^3 filament moved per mm line extruded
public:
    const char* name;
    bool spiralize;
    RetractionConfig* retraction_config;
    
    GCodePathConfig() : speed(0), line_width(0), extrusion_mm3_per_mm(0.0), name(nullptr), spiralize(false), retraction_config(nullptr) {}
    GCodePathConfig(RetractionConfig* retraction_config, const char* name) : speed(0), line_width(0), extrusion_mm3_per_mm(0.0), name(name), spiralize(false), retraction_config(retraction_config) {}
    
    void setSpeed(double speed)
    {
        this->speed = speed;
    }
    
    void setLineWidth(int line_width)
    {
        this->line_width = line_width;
        calculateExtrusion();
    }
    
    void setLayerHeight(int layer_height)
    {
        this->layer_thickness = layer_height;
        calculateExtrusion();
    }

    void setFlow(double flow)
    {
        this->flow = flow;
        calculateExtrusion();
    }
    
    void smoothSpeed(double min_speed, int layer_nr, double max_speed_layer) 
    {
        speed = (speed*layer_nr)/max_speed_layer + (min_speed*(max_speed_layer-layer_nr)/max_speed_layer);
    }
    
    double getExtrusionMM3perMM()
    {
        return extrusion_mm3_per_mm;
    }
    
    double getSpeed()
    {
        return speed;
    }
    
    int getLineWidth()
    {
        return line_width;
    }

private:
    void calculateExtrusion()
    {
        extrusion_mm3_per_mm = INT2MM(line_width) * INT2MM(layer_thickness) * double(flow) / 100.0;
    }
};

//The GCodeExport class writes the actual GCode. This is the only class that knows how GCode looks and feels.
//  Any customizations on GCodes flavors are done in this class.
class GCodeExport
{
private:
    struct ExtruderTrainAttributes
    {
        Point extruderOffset;
        char extruderCharacter;
        std::string extruder_start_code;
        std::string extruder_end_code;
        double filament_area; //!< in mm^2 for non-volumetric, cylindrical filament

        double extruderSwitchRetraction;
        int extruderSwitchRetractionSpeed;
        int extruderSwitchPrimeSpeed;
        double retraction_extrusion_window;
        int retraction_count_max;
        
        double totalFilament; //!< total filament used per extruder in mm^3
        int currentTemperature;
        
        ExtruderTrainAttributes()
        : extruderOffset(0,0)
        , extruderCharacter(0)
        , extruder_start_code("")
        , extruder_end_code("")
        , filament_area(0)
        , extruderSwitchRetraction(0.0)
        , extruderSwitchRetractionSpeed(0)
        , extruderSwitchPrimeSpeed(0)
        , retraction_extrusion_window(0.0)
        , retraction_count_max(1)
        , totalFilament(0)
        , currentTemperature(0)
        { }
    };
    ExtruderTrainAttributes extruder_attr[MAX_EXTRUDERS];
    
    std::ostream* output_stream;
    double extrusion_amount; // in mm or mm^3
    std::deque<double> extrusion_amount_at_previous_n_retractions; // in mm or mm^3
    Point3 currentPosition;
    Point3 startPosition;
    double currentSpeed;
    int zPos;
    bool isRetracted;
    bool isZHopped;

    double last_coasted_amount_mm3; //!< The coasted amount of filament to be primed on the first next extrusion. (same type as GCodeExport::extrusion_amount)
    double retractionPrimeSpeed;
    int current_extruder;
    int currentFanSpeed;
    EGCodeFlavor flavor;

    double totalPrintTime;
    TimeEstimateCalculator estimateCalculator;
    
    bool is_volumatric;
public:
    
    GCodeExport();
    ~GCodeExport();
    
    void setOutputStream(std::ostream* stream);
    
    Point getExtruderOffset(int id);
    
    void setFlavor(EGCodeFlavor flavor);
    EGCodeFlavor getFlavor();
    
    void setZ(int z);
    
    void setLastCoastedAmountMM3(double last_coasted_amount) { this->last_coasted_amount_mm3 = last_coasted_amount; }
    
    Point3 getPosition();
    
    Point getPositionXY();
    
    void resetStartPosition();

    Point getStartPositionXY();

    int getPositionZ();

    int getExtruderNr();
    
    void setFilamentDiameter(unsigned int n, int diameter);
    double getFilamentArea(unsigned int extruder);
    
    double getExtrusionAmountMM3(unsigned int extruder);
    
    double getTotalFilamentUsed(int e);

    double getTotalPrintTime();
    void updateTotalPrintTime();
    void resetTotalPrintTimeAndFilament();
    
    void writeComment(std::string comment);
    void writeTypeComment(const char* type);
    void writeLayerComment(int layer_nr);
    
    void writeLine(const char* line);
    
    void resetExtrusionValue();
    
    void writeDelay(double timeAmount);
    
    void writeMove(Point p, double speed, double extrusion_per_mm);
    
    void writeMove(Point3 p, double speed, double extrusion_per_mm);
private:
    void writeMove(int x, int y, int z, double speed, double extrusion_per_mm);
public:
    void writeRetraction(RetractionConfig* config, bool force=false);
    
    void switchExtruder(int newExtruder);
    
    void writeCode(const char* str);
    
    void writeFanCommand(double speed);
    
    void writeTemperatureCommand(int extruder, double temperature, bool wait = false);
    void writeBedTemperatureCommand(double temperature, bool wait = false);
    
    void preSetup(MeshGroup* settings)
    {
        for(int n=0; n<settings->getSettingAsCount("machine_extruder_count"); n++)
        {
            ExtruderTrain* train = settings->getExtruderTrain(n);
            setFilamentDiameter(n, train->getSettingInMicrons("material_diameter")); 
            
            extruder_attr[n].extruderOffset = Point(train->getSettingInMicrons("machine_nozzle_offset_x"), train->getSettingInMicrons("machine_nozzle_offset_y"));
            extruder_attr[n].extruder_start_code = train->getSettingString("machine_extruder_start_code");
            extruder_attr[n].extruder_end_code = train->getSettingString("machine_extruder_end_code");
            extruder_attr[n].extruderSwitchRetraction = INT2MM(train->getSettingInMicrons("machine_switch_extruder_retraction_amount")); 
            extruder_attr[n].extruderSwitchRetractionSpeed = train->getSettingInMillimetersPerSecond("machine_switch_extruder_retraction_speed");
            extruder_attr[n].extruderSwitchPrimeSpeed = train->getSettingInMillimetersPerSecond("material_switch_extruder_prime_speed");
            extruder_attr[n].retraction_extrusion_window = INT2MM(train->getSettingInMicrons("retraction_extrusion_window"));
            extruder_attr[n].retraction_count_max = train->getSettingAsCount("retraction_count_max");
        }

        setFlavor(settings->getSettingAsGCodeFlavor("machine_gcode_flavor"));
    }
    void finalize(int maxObjectHeight, double moveSpeed, const char* endCode);
    
};

}

#endif//GCODEEXPORT_H
