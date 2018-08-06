//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#ifdef ARCUS

#include <Arcus/Socket.h> //The socket to communicate to.
#include <sstream> //For ostringstream.
#include <thread> //To sleep while waiting for the connection.
#include <unordered_map> //To map settings to their extruder numbers for limit_to_extruder.

#include "ArcusCommunication.h"
#include "Listener.h" //To listen to the Arcus socket.
#include "SliceDataStruct.h" //To store sliced layer data.
#include "../Application.h" //To get and set the current slice command.
#include "../FffProcessor.h" //To start a slice.
#include "../PrintFeature.h"
#include "../Slice.h" //To process slices.
#include "../settings/types/LayerIndex.h" //To point to layers.
#include "../utils/logoutput.h"

namespace cura
{

//Forward declarations for compilation speed.
class MeshGroup;

class ArcusCommunication::Private
{
public:
    Private()
        : socket(nullptr)
        , object_count(0)
        , last_sent_progress(-1)
    { }

    /*
     * Get the unoptimised layer data for a specific layer.
     * \param layer_nr The layer number to get the layer data for.
     * \return The layer data for that layer.
     */
    std::shared_ptr<cura::proto::Layer> getLayerById(LayerIndex layer_nr);

    /*
     * Get the optimised layer data for a specific layer.
     * \param layer_nr The layer number to get the optimised layer data for.
     * \return The optimised layer data for that layer.
     */
    std::shared_ptr<cura::proto::LayerOptimized> getOptimizedLayerById(LayerIndex layer_nr);

    /*
     * Reads the global settings from a Protobuf message.
     *
     * The global settings are stored in the current scene.
     * \param global_settings_message The global settings message.
     */
    void readGlobalSettingsMessage(const cura::proto::SettingList& global_settings_message)
    {
        Slice& slice = Application::getInstance().current_slice;
        for (const cura::proto::Setting& setting_message : global_settings_message.settings())
        {
            slice.scene.settings.add(setting_message.name(), setting_message.value());
        }
    }
    
    void readExtruderSettingsMessage(const google::protobuf::RepeatedPtrField<cura::proto::Extruder>& extruder_message)
    {
        Slice& slice = Application::getInstance().current_slice;
        const size_t extruder_count = slice.scene.settings.get<size_t>("machine_extruder_count");
        for (size_t extruder_nr = 0; extruder_nr < extruder_count; extruder_nr++)
        {
            slice.scene.extruders.emplace_back(extruder_nr, &slice.scene.settings);
        }
        for (const cura::proto::Extruder& extruder_message : extruder_message)
        {
            const int32_t extruder_nr = extruder_message.id(); //Cast from proto::int to int32_t!
            if (extruder_nr < 0 || extruder_nr >= static_cast<int32_t>(extruder_count))
            {
                logWarning("Received extruder index that is out of range: %i", extruder_nr);
                continue;
            }
            ExtruderTrain& extruder = slice.scene.extruders[extruder_nr];
            for (const cura::proto::Setting& setting_message : extruder_message.settings().settings())
            {
                extruder.setSetting(setting_message.name(), setting_message.value());
            }
        }
    }

    /*
     * \brief Reads a Protobuf message describing a mesh group.
     *
     * This gets the vertex data from the message as well as the settings.
     */
    void readMeshGroupMessage(const cura::proto::ObjectList& mesh_group_message)
    {
        if (mesh_group_message.objects_size() <= 0)
        {
            return; //Don't slice empty mesh groups.
        }

        objects_to_slice.push_back(std::make_shared<MeshGroup>(FffProcessor::getInstance()));
        MeshGroup* mesh_group = objects_to_slice.back().get();
        mesh_group->settings.setParent(&Application::getInstance().current_slice.scene.settings);

        //Load the settings in the mesh group.
        for (const cura::proto::Setting& setting : mesh_group_message.settings())
        {
            mesh_group->settings.add(setting.name(), setting.value());
        }

        FMatrix3x3 matrix;
        for (const cura::proto::Object& object : mesh_group_message.objects())
        {
            unsigned int bytes_per_face = sizeof(FPoint3) * 3; //3 vectors per face.
            size_t face_count = object.vertices().size() / bytes_per_face;

            if (face_count <= 0)
            {
                logWarning("Got an empty mesh. Ignoring it!");
                continue;
            }

            mesh_group->meshes.emplace_back();
            Mesh& mesh = mesh_group->meshes.back();

            //Load the settings for the mesh.
            for (const cura::proto::Setting& setting : object.settings())
            {
                mesh.settings.add(setting.name(), setting.value());
            }
            ExtruderTrain& extruder = mesh.settings.get<ExtruderTrain&>("extruder_nr"); //Set the parent setting to the correct extruder.
            mesh.settings.setParent(&extruder.settings);

            for (size_t face = 0; face < face_count; face++)
            {
                const std::string data = object.vertices().substr(face * bytes_per_face, bytes_per_face);
                const FPoint3* float_vertices = reinterpret_cast<const FPoint3*>(data.data());

                Point3 verts[3];
                verts[0] = matrix.apply(float_vertices[0]);
                verts[1] = matrix.apply(float_vertices[1]);
                verts[2] = matrix.apply(float_vertices[2]);
                mesh.addFace(verts[0], verts[1], verts[2]);
            }

            mesh.finish();
        }
        object_count++;
        mesh_group->finalize();
    }

    Arcus::Socket* socket; //!< Socket to send data to.
    size_t object_count; //!< Number of objects that need to be sliced.
    std::string temp_gcode_file; //!< Temporary buffer for the g-code.
    std::ostringstream gcode_output_stream; //!< The stream to write g-code to.
    std::vector<std::shared_ptr<MeshGroup>> objects_to_slice; //!< Print object that holds one or more meshes that need to be sliced.

    SliceDataStruct<cura::proto::Layer> sliced_layers;
    SliceDataStruct<cura::proto::LayerOptimized> optimized_layers;

    int last_sent_progress; //!< Last sent progress promille (1/1000th). Used to not send duplicate messages with the same promille.

    /*
     * \brief How often we've sliced so far during this run of CuraEngine.
     *
     * This is currently used to limit the number of slices per run to 1,
     * because CuraEngine produced different output for each slice. The fix was
     * to restart CuraEngine every time you make a slice.
     *
     * Once this bug is resolved, we can allow multiple slices for each run. Our
     * intuition says that there might be some differences if we let stuff
     * depend on the order of iteration in unordered_map or unordered_set,
     * because those data structures will give a different order if more memory
     * has already been reserved for them.
     */
    size_t slice_count; //!< How often we've sliced so far during this run of CuraEngine.
};

/*
 * \brief A computation class that formats layer view data in a way that the
 * front-end can understand it.
 *
 * This converts data from CuraEngine's internal data structures to Protobuf
 * messages that can be sent to the front-end.
 */
class ArcusCommunication::PathCompiler
{
    typedef cura::proto::PathSegment::PointType PointType;
    static_assert(sizeof(PrintFeatureType) == 1, "To be compatible with the Cura frontend code PrintFeatureType needs to be of size 1");
    //! Reference to the private data of the CommandSocket used to send the data to the front end.
    ArcusCommunication::Private& _cs_private_data;
    //! Keeps track of the current layer number being processed. If layer number is set to a different value, the current data is flushed to CommandSocket.
    int _layer_nr;
    int extruder;
    PointType data_point_type;

    std::vector<PrintFeatureType> line_types; //!< Line types for the line segments stored, the size of this vector is N.
    std::vector<float> line_widths; //!< Line widths for the line segments stored, the size of this vector is N.
    std::vector<float> line_thicknesses; //!< Line thicknesses for the line segments stored, the size of this vector is N.
    std::vector<float> line_feedrates; //!< Line feedrates for the line segments stored, the size of this vector is N.
    std::vector<float> points; //!< The points used to define the line segments, the size of this vector is D*(N+1) as each line segment is defined from one point to the next. D is the dimensionality of the point.

    Point last_point;

    PathCompiler(const PathCompiler&) = delete;
    PathCompiler& operator=(const PathCompiler&) = delete;
public:
    /*
     * Create a new path compiler.
     */
    PathCompiler(ArcusCommunication::Private& cs_private_data):
        _cs_private_data(cs_private_data),
        _layer_nr(0),
        extruder(0),
        data_point_type(cura::proto::PathSegment::Point2D),
        line_types(),
        line_widths(),
        line_thicknesses(),
        line_feedrates(),
        points(),
        last_point{0,0}
    {}

    /*
     * Flush the remaining unflushed paths when destroying this compiler.
     */
    ~PathCompiler()
    {
        if (line_types.size())
        {
            flushPathSegments();
        }
    }

    /*!
     * \brief Used to select which layer the following layer data is intended
     * for.
     */
    void setLayer(int new_layer_nr)
    {
        if (_layer_nr != new_layer_nr)
        {
            flushPathSegments();
            _layer_nr = new_layer_nr;
        }
    }
    /*!
     * \brief Returns the current layer which data is written to.
     */
    int getLayer() const
    {
        return _layer_nr;
    }
    /*!
     * \brief Used to set which extruder will be used for printing the following
     * layer data.
     */
    void setExtruder(int new_extruder)
    {
        if (extruder != new_extruder)
        {
            flushPathSegments();
            extruder = new_extruder;
        }
    }

    /*!
     * \brief Special handling of the first point in an added line sequence.
     *
     * If the new sequence of lines does not start at the current end point
     * of the path this jump is marked as `PrintFeatureType::NoneType`.
     */
    void handleInitialPoint(Point from)
    {
        if (points.size() == 0)
        {
            addPoint2D(from);
        }
        else if (from != last_point)
        {
            addLineSegment(PrintFeatureType::NoneType, from, 1.0, 0.0, 0.0);
        }
    }

    /*!
     * \brief Transfers the currently buffered line segments to the layer
     * message storage.
     */
    void flushPathSegments();

    /*!
     * \brief Move the current point of this path to \p position.
     */
    void setCurrentPosition(Point position)
    {
        handleInitialPoint(position);
    }

    /*!
     * \brief Adds a single line segment to the current path.
     *
     * The line segment added is from the current last point to point \p to.
     */
    void sendLineTo(PrintFeatureType print_feature_type, Point to, int width, int thickness, int feedrate);

    /*!
     * \brief Adds closed polygon to the current path.
     */
    void sendPolygon(PrintFeatureType print_feature_type, ConstPolygonRef poly, int width, int thickness, int feedrate);

private:
    /*!
     * Convert and add a point to the points buffer, each point being represented as two consecutive floats. All members adding a 2D point to the data should use this function.
     */
    void addPoint2D(Point point)
    {
        points.push_back(INT2MM(point.X));
        points.push_back(INT2MM(point.Y));
        last_point = point;
    }

    /*!
     * Implements the functionality of adding a single 2D line segment to the path data. All member functions adding a 2D line segment should use this functions.
     */
    void addLineSegment(PrintFeatureType print_feature_type, Point point, int line_width, int line_thickness, int line_feedrate)
    {
        addPoint2D(point);
        line_types.push_back(print_feature_type);
        line_widths.push_back(INT2MM(line_width));
        line_thicknesses.push_back(INT2MM(line_thickness));
        line_feedrates.push_back(line_feedrate);
    }
};

ArcusCommunication::ArcusCommunication(const std::string& ip, const uint16_t port)
    : private_data(new Private)
    , path_compiler(new PathCompiler(*private_data))
{
    private_data->socket = new Arcus::Socket();
    private_data->socket->addListener(new Listener);

    private_data->socket->registerMessageType(&cura::proto::Slice::default_instance());
    private_data->socket->registerMessageType(&cura::proto::Layer::default_instance());
    private_data->socket->registerMessageType(&cura::proto::LayerOptimized::default_instance());
    private_data->socket->registerMessageType(&cura::proto::Progress::default_instance());
    private_data->socket->registerMessageType(&cura::proto::GCodeLayer::default_instance());
    private_data->socket->registerMessageType(&cura::proto::PrintTimeMaterialEstimates::default_instance());
    private_data->socket->registerMessageType(&cura::proto::SettingList::default_instance());
    private_data->socket->registerMessageType(&cura::proto::GCodePrefix::default_instance());
    private_data->socket->registerMessageType(&cura::proto::SlicingFinished::default_instance());
    private_data->socket->registerMessageType(&cura::proto::SettingExtruder::default_instance());

    log("Connecting to %s:%i\n", ip.c_str(), port);
    private_data->socket->connect(ip, port);
    while(private_data->socket->getState() != Arcus::SocketState::Connected && private_data->socket->getState() != Arcus::SocketState::Error)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); //Wait until we're connected. Check every 100ms.
    }
    log("Connected to %s:%i\n", ip.c_str(), port);
}

ArcusCommunication::~ArcusCommunication()
{
    log("Closing connection.\n");
    private_data->socket->close();
}

const bool ArcusCommunication::hasSlice() const
{
    return private_data->socket->getState() != Arcus::SocketState::Closed
        && private_data->socket->getState() != Arcus::SocketState::Error
        && private_data->slice_count < 1; //Only slice once per run of CuraEngine. See documentation of slice_count.
}

void ArcusCommunication::sendLayerComplete(const LayerIndex layer_nr, const coord_t z, const coord_t thickness)
{
    std::shared_ptr<proto::LayerOptimized> layer = private_data->getOptimizedLayerById(layer_nr);
    layer->set_height(z);
    layer->set_thickness(thickness);
}

void ArcusCommunication::sendOptimizedLayerData()
{
    path_compiler->flushPathSegments(); //Make sure the last path segment has been flushed from the compiler.

    SliceDataStruct<cura::proto::LayerOptimized>& data = private_data->optimized_layers;
    data.sliced_objects++;
    data.current_layer_offset = data.current_layer_count;
    if (data.sliced_objects < private_data->object_count) //Nothing to send.
    {
        return;
    }
    log("Sending %d layers.", data.current_layer_count);

    for (std::pair<const int, std::shared_ptr<proto::LayerOptimized>> entry : data.slice_data) //Note: This is in no particular order!
    {
        logDebug("Sending layer data for layer %i of %i.\n", entry.first, data.slice_data.size());
        private_data->socket->sendMessage(entry.second); //Send the actual layers.
    }
    data.sliced_objects = 0;
    data.current_layer_count = 0;
    data.current_layer_offset = 0;
    data.slice_data.clear();
}

void ArcusCommunication::sendProgress(float progress) const
{
    const int rounded_amount = 1000 * progress;
    if (private_data->last_sent_progress == rounded_amount) //No need to send another tiny update step.
    {
        return;
    }

    std::shared_ptr<proto::Progress> message = std::make_shared<cura::proto::Progress>();
    progress /= private_data->object_count;
    progress += private_data->optimized_layers.sliced_objects * (1. / private_data->object_count);
    message->set_amount(progress);
    private_data->socket->sendMessage(message);

    private_data->last_sent_progress = rounded_amount;
}

void ArcusCommunication::sliceNext()
{
    const Arcus::MessagePtr message = private_data->socket->takeNextMessage();

    //Handle the main Slice message.
    const cura::proto::Slice* slice_message = dynamic_cast<cura::proto::Slice*>(message.get()); //See if the message is of the message type Slice. Returns nullptr otherwise.
    if(slice_message)
    {
        logDebug("Received a Slice message.\n");

        Application::getInstance().current_slice.reset(); //Create a new Slice.
        Slice& slice = Application::getInstance().current_slice;

        private_data->readGlobalSettingsMessage(slice_message->global_settings());
        private_data->readExtruderSettingsMessage(slice_message->extruders());
        const size_t extruder_count = slice.scene.settings.get<size_t>("machine_extruder_count");

        //For each setting, register what extruder it should be obtained from (if this is limited to an extruder).
        for (const cura::proto::SettingExtruder& setting_extruder : slice_message->limit_to_extruder())
        {
            const int32_t extruder_nr = setting_extruder.extruder(); //Cast from proto::int to int32_t!
            if (extruder_nr < 0 || extruder_nr > static_cast<int32_t>(extruder_count))
            {
                //If it's -1 it should be ignored as per the spec. Let's also ignore it if it's beyond range.
                continue;
            }
            ExtruderTrain& extruder = slice.scene.extruders[setting_extruder.extruder()];
            slice.scene.settings.setLimitToExtruder(setting_extruder.name(), &extruder);
        }

        //Load all mesh groups, meshes and their settings.
        private_data->object_count = 0;
        for (const cura::proto::ObjectList& mesh_group_message : slice_message->object_lists())
        {
            private_data->readMeshGroupMessage(mesh_group_message);
        }
        logDebug("Done reading Slice message.\n");
    }

    if (!private_data->objects_to_slice.empty())
    {
        const size_t object_count = private_data->objects_to_slice.size();
        logDebug("Slicing %i objects.\n", object_count);
        FffProcessor::getInstance()->resetMeshGroupNumber();
        for (size_t i = 0; i < object_count; i++)
        {
            logDebug("Slicing object %i of %i.\n", i + 1, object_count);
            if (!FffProcessor::getInstance()->processMeshGroup(private_data->objects_to_slice[i].get()))
            {
                logError("Slicing mesh group failed!\n");
            }
        }
        logDebug("Done slicing objects.\n");
        private_data->objects_to_slice.clear();
        FffProcessor::getInstance()->finalize();
        flushGCode();
        sendPrintTimeMaterialEstimates();
        sendFinishedSlicing();
        private_data->slice_count++;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(250)); //Pause before checking again for a slice message.
}

std::shared_ptr<proto::LayerOptimized> ArcusCommunication::Private::getOptimizedLayerById(LayerIndex layer_nr)
{
    layer_nr += optimized_layers.current_layer_offset;
    std::unordered_map<int, std::shared_ptr<proto::LayerOptimized>>::iterator find_result = optimized_layers.slice_data.find(layer_nr);

    if (find_result != optimized_layers.slice_data.end()) //Load layer from the cache.
    {
        return find_result->second;
    }
    else //Not in the cache yet. Create an empty layer.
    {
        std::shared_ptr<proto::LayerOptimized> layer = std::make_shared<proto::LayerOptimized>();
        layer->set_id(layer_nr);
        optimized_layers.current_layer_count++;
        optimized_layers.slice_data[layer_nr] = layer;
        return layer;
    }
}

} //namespace cura

#endif //ARCUS