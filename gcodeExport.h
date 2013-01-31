#ifndef GCODEEXPORT_H
#define GCODEEXPORT_H

class GCodeExport
{
    FILE* f;
    double extrusionAmount;
    double extrusionPerMM;
    Point3 currentPosition;
    int moveSpeed, extrudeSpeed, currentSpeed;
public:
    GCodeExport(const char* filename)
    : currentPosition(0,0,0)
    {
        f = fopen(filename, "w");
        extrusionAmount = 0;
        extrusionPerMM = 0;
        
        moveSpeed = 150;
        extrudeSpeed = 50;
        currentSpeed = 0;
    }
    
    ~GCodeExport()
    {
        fclose(f);
    }
    
    void setExtrusion(int layerThickness, int lineWidth, int filamentDiameter)
    {
        double filamentArea = M_PI * (double(filamentDiameter) / 1000.0 / 2.0) * (double(filamentDiameter) / 1000.0 / 2.0);
        extrusionPerMM = double(layerThickness) / 1000.0 * double(lineWidth) / 1000.0 / filamentArea;
    }
    
    void setSpeeds(int moveSpeed, int extrudeSpeed)
    {
        this->moveSpeed = moveSpeed;
        this->extrudeSpeed = extrudeSpeed;
    }
    
    void addComment(const char* comment, ...)
    {
        va_list args;
        va_start(args, comment);
        fprintf(f, ";");
        vfprintf(f, comment, args);
        fprintf(f, "\n");
        va_end(args);
    }
    
    void addMove(Point3 p, double extrusion)
    {
        int speed;
        extrusionAmount += extrusion;
        if ((p - currentPosition).testLength(200))
            return;
        if (extrusion != 0)
        {
            fprintf(f, "G1");
            speed = extrudeSpeed;
        }else{
            fprintf(f, "G0");
            speed = moveSpeed;
        }
        
        if (currentSpeed != speed)
        {
            fprintf(f, " F%i", speed * 60);
            currentSpeed = speed;
        }
        fprintf(f, " X%0.2f Y%0.2f", float(p.x)/1000, float(p.y)/1000);
        if (p.z != currentPosition.z)
            fprintf(f, " Z%0.2f", float(p.z)/1000);
        if (extrusion != 0)
            fprintf(f, " E%0.4lf", extrusionAmount);
        fprintf(f, "\n");
        
        currentPosition = p;
    }
    
    void addPolygon(ClipperLib::Polygon& polygon, int startIdx, int z)
    {
        ClipperLib::IntPoint p0 = polygon[startIdx];
        addMove(Point3(p0.X, p0.Y, z), 0.0f);
        for(unsigned int i=1; i<polygon.size(); i++)
        {
            ClipperLib::IntPoint p1 = polygon[(startIdx + i) % polygon.size()];
            addMove(Point3(p1.X, p1.Y, z), (Point(p1) - Point(p0)).vSizeMM() * extrusionPerMM);
            p0 = p1;
        }
        addMove(Point3(polygon[startIdx].X, polygon[startIdx].Y, z), (Point(polygon[startIdx]) - Point(p0)).vSizeMM() * extrusionPerMM);
    }
    
    void addStartCode()
    {
        fprintf(f, "G21           ;metric values\n");
        fprintf(f, "G90           ;absolute positioning\n");
        fprintf(f, "M109 S210     ;Heatup to 210C\n");
        fprintf(f, "G28           ;Home\n");
        fprintf(f, "G1 Z15.0 F300 ;move the platform down 15mm\n");
        fprintf(f, "G92 E0        ;zero the extruded length\n");
        fprintf(f, "G1 F200 E3    ;extrude 3mm of feed stock\n");
        fprintf(f, "G92 E0        ;zero the extruded length again\n");
    }
    void addEndCode()
    {
        fprintf(f, "M104 S0                     ;extruder heater off\n");
        fprintf(f, "M140 S0                     ;heated bed heater off (if you have it)\n");
        fprintf(f, "G91                            ;relative positioning\n");
        fprintf(f, "G1 E-1 F300                    ;retract the filament a bit before lifting the nozzle, to release some of the pressure\n");
        fprintf(f, "G1 Z+0.5 E-5 X-20 Y-20 F9000   ;move Z up a bit and retract filament even more\n");
        fprintf(f, "G28 X0 Y0                      ;move X/Y to min endstops, so the head is out of the way\n");
        fprintf(f, "M84                         ;steppers off\n");
        fprintf(f, "G90                         ;absolute positioning\n");
    }

    int getFileSize(){
        return ftell(f);
    }
    void tellFileSize() {
        float fsize = (float) ftell(f);
        char fmagnitude = ' ';
        if(fsize > 1024*1024) {
            fmagnitude = 'M';
            fsize /= 1024.0*1024.0;
            fprintf(stderr, "Wrote %5.1f MB.\n",fsize);
        }
        if(fsize > 1024) {
            fmagnitude = 'k';
            fsize /= 1024.0;
            fprintf(stderr, "Wrote %5.1f kilobytes.\n",fsize);
        }
    }
};

#endif//GCODEEXPORT_H
