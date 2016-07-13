#include "utils/logoutput.h"
#include "commandSocket.h"
#include "FffProcessor.h"
#include "progress/Progress.h"

#include <thread>
#include <cinttypes>

#ifdef ARCUS
#include <Arcus/Socket.h>
#include <Arcus/SocketListener.h>
#include <Arcus/Error.h>
#endif

#include <string> // stoi

#ifdef _WIN32
#include <windows.h>
#endif

#include "settings/SettingRegistry.h" // loadExtruderJSONsettings

#define DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR(x) 

// std::cerr << x;

namespace cura {

#define BYTES_PER_FLOAT 4
#define FLOATS_PER_VECTOR 3
#define VECTORS_PER_FACE 3

CommandSocket* CommandSocket::instance = nullptr; // instantiate instance

#ifdef ARCUS
class Listener : public Arcus::SocketListener
{
public:
    void stateChanged(Arcus::SocketState::SocketState newState) override
    {
    }

    void messageReceived() override
    {
    }

    void error(const Arcus::Error & error) override
    {
        if (error.getErrorCode() == Arcus::ErrorCode::Debug)
        {
            log("%s\n", error.toString().c_str());
        }
        else
        {
            logError("%s\n", error.toString().c_str());
        }
    }
};

/*!
 * A template structure used to store data to be sent to the front end.
 */
template <typename T>
class SliceDataStruct
{
    SliceDataStruct( const SliceDataStruct& ) = delete;
    SliceDataStruct& operator=( const SliceDataStruct& ) = delete;
public:

    SliceDataStruct()
        : sliced_objects(0)
        , current_layer_count(0)
        , current_layer_offset(0)
    { }

    //! The number of sliced objects for this sliced object list
    int sliced_objects;

    int current_layer_count;//!< Number of layers for which data has been buffered in slice_data so far.
    int current_layer_offset;//!< Offset to add to layer number for the current slice object when slicing one at a time.

    std::unordered_map<int, std::shared_ptr<T>> slice_data;
};

class CommandSocket::Private
{
public:
    Private()
        : socket(nullptr)
        , object_count(0)
    { }

    std::shared_ptr<cura::proto::Layer> getLayerById(int id);

    std::shared_ptr<cura::proto::LayerOptimized> getOptimizedLayerById(int id);

    Arcus::Socket* socket;
    
    // Number of objects that need to be sliced
    int object_count;

    std::string temp_gcode_file;
    std::ostringstream gcode_output_stream;
    
    // Print object that olds one or more meshes that need to be sliced. 
    std::vector< std::shared_ptr<MeshGroup> > objects_to_slice;

    SliceDataStruct<cura::proto::Layer> sliced_layers;
    SliceDataStruct<cura::proto::LayerOptimized> optimized_layers;
};

/*!
 * PathCompiler buffers and prepares the sliced data to be sent to the front end and saves them in
 * appropriate buffers
 */
class CommandSocket::PathCompiler
{
    static_assert(sizeof(PrintFeatureType) == 1, "To be compatible with the Cura frontend code PrintFeatureType needs to be of size 1");
    //! Reference to the private data of the CommandSocket used to send the data to the front end.
    CommandSocket::Private& _cs_private_data;
    //! Keeps track of the current layer number being processed. If layer number is set to a different value, the current data is flushed to CommandSocket.
    int _layer_nr;

    std::vector<PrintFeatureType> line_types; //!< Line types for the line segments stored, the size of this vector is N.
    std::vector<int> line_widths; //!< Line widths for the line segments stored, the size of this vector is N.
    std::vector<Point> points; //!< The points used to define the line segments, the size of this vector is N+1 as each line segment is defined from one point to the next.

    PathCompiler(const PathCompiler&) = delete;
    PathCompiler& operator=(const PathCompiler&) = delete;
public:
    PathCompiler(CommandSocket::Private& cs_private_data):
        _cs_private_data(cs_private_data),
        _layer_nr( 0 ),
        line_widths(),
        points()
    {}
    ~PathCompiler()
    {
        if( points.size() )
            flushPathSegments();
    }

    /*!
     * Used to select which layer the following layer data is intended for.
     */
    void setLayer(int new_layer_nr)
    {
        if(_layer_nr != new_layer_nr ){
            flushPathSegments();
            _layer_nr = new_layer_nr;
        }
    }

    /*!
     * Special handling of the first point in an added line sequence.
     * If the new sequence of lines does not start at the current end point
     * of the path this jump is marked as PrintFeatureType::NoneType
     */
    void handleInitialPoint( Point from )
    {
        if( points.size() == 0 ){
            points.push_back( from );
        }
        else if( from != points.back() )
        {
            addLineSegment( PrintFeatureType::NoneType, from, 1.0);
        }
    }

    /*!
     * Transfers the currently buffered line segments to the
     * CommandSocket layer message storage.
     */
    void flushPathSegments();
    /*!
     * Adds a single line segment to the current path
     */
    void sendLine( PrintFeatureType print_feature_type, Point from, Point to, int width);
    /*!
     * Adds closed polygon to the current path
     */
    void sendPolygon( PrintFeatureType print_feature_type, Polygon poly, int width);
private:
    void addLineSegment( PrintFeatureType print_feature_type, Point point, int line_width)
    {
        points.push_back( point );
        line_types.push_back( print_feature_type );
        line_widths.push_back( line_width );
    }
};
#endif

CommandSocket::CommandSocket()
#ifdef ARCUS
    : private_data(new Private)
    , path_comp(new PathCompiler(*private_data))
#endif
{
#ifdef ARCUS
#endif
}

CommandSocket* CommandSocket::getInstance()
{
    return instance;
}

void CommandSocket::instantiate()
{
    instance = new CommandSocket();
}

bool CommandSocket::isInstantiated()
{
    return instance != nullptr;
}


void CommandSocket::connect(const std::string& ip, int port)
{
#ifdef ARCUS
    private_data->socket = new Arcus::Socket();
    private_data->socket->addListener(new Listener());

    //private_data->socket->registerMessageType(1, &Cura::ObjectList::default_instance());
    private_data->socket->registerMessageType(&cura::proto::Slice::default_instance());
    private_data->socket->registerMessageType(&cura::proto::Layer::default_instance());
    private_data->socket->registerMessageType(&cura::proto::LayerOptimized::default_instance());
    private_data->socket->registerMessageType(&cura::proto::Progress::default_instance());
    private_data->socket->registerMessageType(&cura::proto::GCodeLayer::default_instance());
    private_data->socket->registerMessageType(&cura::proto::PrintTimeMaterialEstimates::default_instance());
    private_data->socket->registerMessageType(&cura::proto::SettingList::default_instance());
    private_data->socket->registerMessageType(&cura::proto::GCodePrefix::default_instance());
    private_data->socket->registerMessageType(&cura::proto::SlicingFinished::default_instance());

    private_data->socket->connect(ip, port);

    log("Connecting to %s:%i\n", ip.c_str(), port);

    while(private_data->socket->getState() != Arcus::SocketState::Connected && private_data->socket->getState() != Arcus::SocketState::Error)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    log("Connected to %s:%i\n", ip.c_str(), port);
    
    bool slice_another_time = true;
    
    // Start & continue listening as long as socket is not closed and there is no error.
    while(private_data->socket->getState() != Arcus::SocketState::Closed && private_data->socket->getState() != Arcus::SocketState::Error && slice_another_time)
    {
        // Actually start handling messages.
        Arcus::MessagePtr message = private_data->socket->takeNextMessage();

        /*
         * handle a message which consists purely of a SettingList
        cura::proto::SettingList* setting_list = dynamic_cast<cura::proto::SettingList*>(message.get());
        if (setting_list)
        {
            handleSettingList(setting_list);
        }
        */

        /*
         * handle a message which consists purely of an ObjectList
        cura::proto::ObjectList* object_list = dynamic_cast<cura::proto::ObjectList*>(message.get());
        if (object_list)
        {
            handleObjectList(object_list);
        }
        */

        // Handle the main Slice message
        cura::proto::Slice* slice = dynamic_cast<cura::proto::Slice*>(message.get()); // See if the message is of the message type Slice; returns nullptr otherwise
        if (slice)
        {
            const cura::proto::SettingList& global_settings = slice->global_settings();
            for (auto setting : global_settings.settings())
            {
                FffProcessor::getInstance()->setSetting(setting.name(), setting.value());
            }
            // Reset object counts
            private_data->object_count = 0;
            for (auto object : slice->object_lists())
            {
                handleObjectList(&object, slice->extruders());
            }
        }

        //If there is an object to slice, do so.
        if (private_data->objects_to_slice.size())
        {
            FffProcessor::getInstance()->resetMeshGroupNumber();
            for (auto object : private_data->objects_to_slice)
            {
                if (!FffProcessor::getInstance()->processMeshGroup(object.get()))
                {
                    logError("Slicing mesh group failed!");
                }
            }
            private_data->objects_to_slice.clear();
            FffProcessor::getInstance()->finalize();
            flushGcode();
            sendPrintTimeMaterialEstimates();
            sendFinishedSlicing();
            slice_another_time = false; // TODO: remove this when multiple slicing with CuraEngine is safe
            //TODO: Support all-at-once/one-at-a-time printing
            //private_data->processor->processModel(private_data->object_to_slice.get());
            //private_data->object_to_slice.reset();
            //private_data->processor->resetFileNumber();

            //sendPrintTimeMaterialEstimates();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }
    log("Closing connection\n");
    private_data->socket->close();
#endif
}

#ifdef ARCUS
void CommandSocket::handleObjectList(cura::proto::ObjectList* list, const google::protobuf::RepeatedPtrField<cura::proto::Extruder> settings_per_extruder_train)
{
    if (list->objects_size() <= 0)
    {
        return;
    }

    FMatrix3x3 matrix;
    //private_data->object_count = 0;
    //private_data->object_ids.clear();
    private_data->objects_to_slice.push_back(std::make_shared<MeshGroup>(FffProcessor::getInstance()));
    MeshGroup* meshgroup = private_data->objects_to_slice.back().get();

    // load meshgroup settings
    for (auto setting : list->settings())
    {
        meshgroup->setSetting(setting.name(), setting.value());
    }

    { // load extruder settings
        for (int extruder_nr = 0; extruder_nr < FffProcessor::getInstance()->getSettingAsCount("machine_extruder_count"); extruder_nr++)
        { // initialize remaining extruder trains and load the defaults
            ExtruderTrain* train = meshgroup->createExtruderTrain(extruder_nr); // create new extruder train objects or use already existing ones
            SettingRegistry::getInstance()->loadExtruderJSONsettings(extruder_nr, train);
        }

        for (auto extruder : settings_per_extruder_train)
        {
            int extruder_nr = extruder.id();
            ExtruderTrain* train = meshgroup->createExtruderTrain(extruder_nr); // create new extruder train objects or use already existing ones
            for (auto setting : extruder.settings().settings())
            {
                train->setSetting(setting.name(), setting.value());
            }
        }
    }

    for (auto object : list->objects())
    {
        int bytes_per_face = BYTES_PER_FLOAT * FLOATS_PER_VECTOR * VECTORS_PER_FACE;
        int face_count = object.vertices().size() / bytes_per_face;

        if (face_count <= 0)
        {
            logWarning("Got an empty mesh, ignoring it!");
            continue;
        }
        DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("solid Cura_out\n");

        // Check to which extruder train this object belongs
        int extruder_train_nr = 0; // assume extruder 0 if setting wasn't supplied
        for (auto setting : object.settings())
        {
            if (setting.name() == "extruder_nr")
            {
                extruder_train_nr = std::stoi(setting.value());
                break;
            }
        }
        SettingsBase* extruder_train = meshgroup->getExtruderTrain(extruder_train_nr);

        meshgroup->meshes.push_back(extruder_train); //Construct a new mesh (with the corresponding extruder train as settings parent object) and put it into MeshGroup's mesh list.
        Mesh& mesh = meshgroup->meshes.back();

        for (int i = 0; i < face_count; ++i)
        {
            //TODO: Apply matrix
            std::string data = object.vertices().substr(i * bytes_per_face, bytes_per_face);
            const FPoint3* float_vertices = reinterpret_cast<const FPoint3*>(data.data());

            Point3 verts[3];
            verts[0] = matrix.apply(float_vertices[0]);
            verts[1] = matrix.apply(float_vertices[1]);
            verts[2] = matrix.apply(float_vertices[2]);
            mesh.addFace(verts[0], verts[1], verts[2]);

            DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("  facet normal -1 0 0\n");
            DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("    outer loop\n");
            DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("      vertex "<<INT2MM(verts[0].x) <<" " << INT2MM(verts[0].y) <<" " << INT2MM(verts[0].z) << "\n");
            DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("      vertex "<<INT2MM(verts[1].x) <<" " << INT2MM(verts[1].y) <<" " << INT2MM(verts[1].z) << "\n");
            DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("      vertex "<<INT2MM(verts[2].x) <<" " << INT2MM(verts[2].y) <<" " << INT2MM(verts[2].z) << "\n");
            DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("    endloop\n");
            DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("  endfacet\n");
        }
        DEBUG_OUTPUT_OBJECT_STL_THROUGH_CERR("endsolid Cura_out\n");

        for (auto setting : object.settings())
        {
            mesh.setSetting(setting.name(), setting.value());
        }

        mesh.finish();
    }

    private_data->object_count++;
    meshgroup->finalize();
}
#endif

void CommandSocket::sendLayerInfo(int layer_nr, int32_t z, int32_t height)
{
#ifdef ARCUS
    std::shared_ptr<cura::proto::Layer> layer = private_data->getLayerById(layer_nr);
    layer->set_height(z);
    layer->set_thickness(height);
#endif
}

void CommandSocket::sendOptimizedLayerInfo(int layer_nr, int32_t z, int32_t height)
{
#ifdef ARCUS
    std::shared_ptr<cura::proto::LayerOptimized> layer = private_data->getOptimizedLayerById(layer_nr);
    layer->set_height(z);
    layer->set_thickness(height);
#endif
}

void CommandSocket::sendPolygons(PrintFeatureType type, int layer_nr, const Polygons& polygons, int line_width)
{
#ifdef ARCUS
    if (polygons.size() == 0)
        return;

    path_comp->setLayer( layer_nr );

    for (unsigned int i = 0; i < polygons.size(); ++i)
    {
   		path_comp->sendPolygon( type, polygons[i], line_width );
    }
#endif
}

void CommandSocket::sendPolygon(PrintFeatureType type, int layer_nr, Polygon& polygon, int line_width)
{
#ifdef ARCUS
    path_comp->setLayer( layer_nr );
    path_comp->sendPolygon( type, polygon, line_width );
#endif
}

void CommandSocket::sendLine(cura::PrintFeatureType type, int layer_nr, Point from, Point to, int line_width)
{
#ifdef ARCUS
    path_comp->setLayer( layer_nr );
    path_comp->sendLine( type, from, to, line_width );
#endif
}


void CommandSocket::sendProgress(float amount)
{
#ifdef ARCUS
    auto message = std::make_shared<cura::proto::Progress>();
    amount /= private_data->object_count;
    amount += private_data->optimized_layers.sliced_objects * (1. / private_data->object_count);
    message->set_amount(amount);
    private_data->socket->sendMessage(message);
#endif
}

void CommandSocket::sendProgressStage(Progress::Stage stage)
{
    // TODO
}

void CommandSocket::sendPrintTimeMaterialEstimates()
{
#ifdef ARCUS
    auto message = std::make_shared<cura::proto::PrintTimeMaterialEstimates>();

    message->set_time(FffProcessor::getInstance()->getTotalPrintTime());
    int num_extruders = FffProcessor::getInstance()->getSettingAsCount("machine_extruder_count");
    for (int extruder_nr (0); extruder_nr < num_extruders; ++extruder_nr)
    {
        cura::proto::MaterialEstimates* material_message = message->add_materialestimates();

        material_message->set_id(extruder_nr);
        material_message->set_material_amount(FffProcessor::getInstance()->getTotalFilamentUsed(extruder_nr));
    }

    private_data->socket->sendMessage(message);
#endif
}

void CommandSocket::sendPrintMaterialForObject(int index, int extruder_nr, float print_time)
{
//     socket.sendInt32(CMD_OBJECT_PRINT_MATERIAL);
//     socket.sendInt32(12);
//     socket.sendInt32(index);
//     socket.sendInt32(extruder_nr);
//     socket.sendFloat32(print_time);
}

void CommandSocket::sendLayerData()
{
#ifdef ARCUS
#endif
#ifdef ARCUS
    auto& data = private_data->sliced_layers;

    data.sliced_objects++;
    data.current_layer_offset = data.current_layer_count;
//    log("End sliced object called. Sending %d layers.", data.current_layer_count);

    // Only send the data to the front end when all mesh groups have been processed.
    if (data.sliced_objects >= private_data->object_count)
    {
        for (std::pair<const int, std::shared_ptr<cura::proto::Layer>> entry : data.slice_data) //Note: This is in no particular order!
        {
            private_data->socket->sendMessage(entry.second); //Send the actual layers.
        }
        data.sliced_objects = 0;
        data.current_layer_count = 0;
        data.current_layer_offset = 0;
        data.slice_data.clear();
    }
#endif
}

void CommandSocket::sendOptimizedLayerData()
{
#ifdef ARCUS
    path_comp->flushPathSegments(); // make sure the last path segment has been flushed from the compiler

    auto& data = private_data->optimized_layers;

    data.sliced_objects++;
    data.current_layer_offset = data.current_layer_count;
    log("End sliced object called. Sending %d layers.", data.current_layer_count);

    if (data.sliced_objects >= private_data->object_count)
    {
        for (std::pair<const int, std::shared_ptr<cura::proto::LayerOptimized>> entry : data.slice_data) //Note: This is in no particular order!
        {
            private_data->socket->sendMessage(entry.second); //Send the actual layers.
        }
        data.sliced_objects = 0;
        data.current_layer_count = 0;
        data.current_layer_offset = 0;
        data.slice_data.clear();
    }
#endif
}

void CommandSocket::sendFinishedSlicing()
{
#ifdef ARCUS
    std::shared_ptr<cura::proto::SlicingFinished> done_message = std::make_shared<cura::proto::SlicingFinished>();
    private_data->socket->sendMessage(done_message);
#endif
}

void CommandSocket::beginGCode()
{
#ifdef ARCUS
    FffProcessor::getInstance()->setTargetStream(&private_data->gcode_output_stream);
#endif
}

void CommandSocket::flushGcode()
{
#ifdef ARCUS
    auto message = std::make_shared<cura::proto::GCodeLayer>();
    message->set_data(private_data->gcode_output_stream.str());
    private_data->socket->sendMessage(message);
    
    private_data->gcode_output_stream.str("");
#endif
}

void CommandSocket::sendGCodePrefix(std::string prefix)
{
#ifdef ARCUS
    auto message = std::make_shared<cura::proto::GCodePrefix>();
    message->set_data(prefix);
    private_data->socket->sendMessage(message);
#endif
}

#ifdef ARCUS
std::shared_ptr<cura::proto::Layer> CommandSocket::Private::getLayerById(int id)
{
    id += sliced_layers.current_layer_offset;

    auto itr = sliced_layers.slice_data.find(id);

    std::shared_ptr<cura::proto::Layer> layer;
    if (itr != sliced_layers.slice_data.end())
    {
        layer = itr->second;
    }
    else
    {
        layer = std::make_shared<cura::proto::Layer>();
        layer->set_id(id);
        sliced_layers.current_layer_count++;
        sliced_layers.slice_data[id] = layer;
    }

    return layer;
}
#endif

#ifdef ARCUS
std::shared_ptr<cura::proto::LayerOptimized> CommandSocket::Private::getOptimizedLayerById(int id)
{
    id += optimized_layers.current_layer_offset;

    auto itr = optimized_layers.slice_data.find(id);

    std::shared_ptr<cura::proto::LayerOptimized> layer;
    if (itr != optimized_layers.slice_data.end())
    {
        layer = itr->second;
    }
    else
    {
        layer = std::make_shared<cura::proto::LayerOptimized>();
        layer->set_id(id);
        optimized_layers.current_layer_count++;
        optimized_layers.slice_data[id] = layer;
    }

    return layer;
}
#endif

#ifdef ARCUS
void CommandSocket::PathCompiler::flushPathSegments()
{
    if( points.size() > 0 && CommandSocket::isInstantiated() )
    {
        std::shared_ptr<cura::proto::LayerOptimized> proto_layer = _cs_private_data.getOptimizedLayerById(_layer_nr);

        cura::proto::PathSegment* p = proto_layer->add_path_segment();
        std::string line_type_data;
        line_type_data.append(reinterpret_cast<const char*>( line_types.data()), line_types.size()*sizeof(PrintFeatureType) );
        p->set_line_type(line_type_data);
        std::string polydata;
        polydata.append(reinterpret_cast<const char*>( points.data()), points.size() * sizeof(Point));
        p->set_points(polydata);
        std::string line_width_data;
        line_width_data.append(reinterpret_cast<const char*>( line_widths.data()), line_widths.size()*sizeof(int) );
        p->set_line_width(line_width_data);
    }
    points.clear();
    line_widths.clear();
    line_types.clear();
}

//TODO: Reason about these functions when points is empty or the polygon entered is empty
void CommandSocket::PathCompiler::sendLine(PrintFeatureType print_feature_type, Point from, Point to, int width)
{
    handleInitialPoint( from );

    // Ignore zero-length segments.
    if( from != to )
    {
        addLineSegment(print_feature_type, to, width );
    }
}

void CommandSocket::PathCompiler::sendPolygon(PrintFeatureType print_feature_type, Polygon polygon, int width)
{
    if( polygon.size() < 2 )
        return;
    auto it = polygon.begin();
    handleInitialPoint( *it );

    const auto it_end = polygon.end();
    while ( ++it != it_end )
    {
        // Ignore zero-length segments.
        if ( *it != points.back() )
        {
            addLineSegment( print_feature_type, *it, width );
        }
    }
    // Make sure the polygon is closed
    if ( *polygon.begin() != polygon.back() )
    {
        addLineSegment( print_feature_type, *polygon.begin(), width );
    }
}
#endif

}//namespace cura
