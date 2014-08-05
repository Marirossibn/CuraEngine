/** Copyright (C) 2013 David Braam - Released under terms of the AGPLv3 License */
#ifndef GCODEEXPORT_H
#define GCODEEXPORT_H

#include <stdio.h>

#include "settings.h"
#include "utils/intpoint.h"
#include "timeEstimate.h"

namespace cura {

//The GCodeExport class writes the actual GCode. This is the only class that knows how GCode looks and feels.
//  Any customizations on GCodes flavors are done in this class.
class GCodeExport
{
private:
    FILE* f;
    double extrusionAmount;
    double extrusionPerMM;
    double retractionAmount;
    double retractionAmountPrime;
    int retractionZHop;
    double extruderSwitchRetraction;
    double minimalExtrusionBeforeRetraction;
    double extrusionAmountAtPreviousRetraction;
    Point3 currentPosition;
    Point extruderOffset[MAX_EXTRUDERS];
    char extruderCharacter[MAX_EXTRUDERS];
    int currentSpeed, retractionSpeed;
    int zPos;
    bool isRetracted;
    int extruderNr;
    int currentFanSpeed;
    GCode_Flavor flavor;
    std::string preSwitchExtruderCode;
    std::string postSwitchExtruderCode;
    
    double totalFilament[MAX_EXTRUDERS];
    double totalPrintTime;
    TimeEstimateCalculator estimateCalculator;
public:
    
    GCodeExport();
    
    ~GCodeExport();
    
    void setExtruderOffset(int id, Point p);
    Point getExtruderOffset(int id);
    void setSwitchExtruderCode(std::string preSwitchExtruderCode, std::string postSwitchExtruderCode);
    
    void setFlavor(GCode_Flavor flavor);
    int getFlavor();
    
    void setFilename(const char* filename);
    
    bool isOpened();
    
    void setExtrusion(int layerThickness, int filamentDiameter, int flow);
    
    void setRetractionSettings(int retractionAmount, int retractionSpeed, int extruderSwitchRetraction, int minimalExtrusionBeforeRetraction, int zHop, int retractionAmountPrime);
    
    void setZ(int z);
    
    Point getPositionXY();
    
    int getPositionZ();

    int getExtruderNr();
    
    double getTotalFilamentUsed(int e);

    double getTotalPrintTime();
    void updateTotalPrintTime();
    
    void writeComment(const char* comment, ...);

    void writeLine(const char* line, ...);
    
    void resetExtrusionValue();
    
    void writeDelay(double timeAmount);
    
    void writeMove(Point p, int speed, int lineWidth);
    
    void writeRetraction(bool force=false);
    
    void switchExtruder(int newExtruder);
    
    void writeCode(const char* str);
    
    void writeFanCommand(int speed);
    
    void finalize(int maxObjectHeight, int moveSpeed, const char* endCode);

    int getFileSize();
    void tellFileSize();
};

}

#endif//GCODEEXPORT_H
