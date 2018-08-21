//Copyright (c) 2018 Ultimaker B.V.
//CuraEngine is released under the terms of the AGPLv3 or higher.

#include "ArcusCommunicationTest.h"
#include "../src/settings/types/LayerIndex.h"
#include "../src/FffProcessor.h"

namespace cura
{
    CPPUNIT_TEST_SUITE_REGISTRATION(ArcusCommunicationTest);

    ArcusCommunicationTest::MockSocket::MockSocket()
    {

    }

//    ArcusCommunicationTest::MockSocket::~MockSocket()
//    {
//
//    }

    Arcus::SocketState::SocketState ArcusCommunicationTest::MockSocket::getState() const
    {
        return Arcus::SocketState::Connected;
    }

//    Arcus::Error ArcusCommunicationTest::MockSocket::getLastError()
//    {
//        std::cout << "getLastError\n";
//        const Arcus::Error error = Arcus::Error();
//        return error;
//    }


    void ArcusCommunicationTest::MockSocket::clearError()
    {
        std::cout << "clearError\n";
    }

    bool ArcusCommunicationTest::MockSocket::registerMessageType(const google::protobuf::Message* message_type)
    {
        std::cout << "registerMessageType" << message_type << "\n";
    }

    bool ArcusCommunicationTest::MockSocket::registerAllMessageTypes(const std::string& file_name)
    {
        std::cout << "registerAllMessageTypes" << file_name << "\n";
        return true;
    }

    void ArcusCommunicationTest::MockSocket::addListener(Arcus::SocketListener* listener)
    {
        std::cout << "addListener " << listener << "\n";
    }

    void ArcusCommunicationTest::MockSocket::removeListener(Arcus::SocketListener* listener)
    {
        std::cout << "removeListener" << listener << "\n";
    }

    void ArcusCommunicationTest::MockSocket::connect(const std::string& address, int port)
    {
        std::cout << "connect " << address << " - " << port << "\n";
    }

    void ArcusCommunicationTest::MockSocket::listen(const std::string& address, int port)
    {
        std::cout << "listen " << address << " - " << port << "\n";
    }

    void ArcusCommunicationTest::MockSocket::close()
    {
        std::cout << "close\n";
    }

    void ArcusCommunicationTest::MockSocket::reset()
    {
        std::cout << "reset\n";
    }

    void ArcusCommunicationTest::MockSocket::sendMessage(Arcus::MessagePtr message)
    {
        std::cout << "sendMessage: " << message << "\n";
    }

//    Arcus::MessagePtr ArcusCommunicationTest::MockSocket::takeNextMessage()
//    {}
//
//    Arcus::MessagePtr ArcusCommunicationTest::MockSocket::createMessage(const std::string& type_name)
//    {
//        std::cout << "createMessage: " << type_name << "\n";
//        return MessagePtr
//    }

    ///////////////////////

    void ArcusCommunicationTest::setUp()
    {
        ip = "0.0.0.0";
        port = 12345;
        socket = new MockSocket();
    }

    void ArcusCommunicationTest::tearDown()
    {
        //Do nothing.
    }

    void ArcusCommunicationTest::smokeTest()
    {
        Communication* ac = new ArcusCommunication(ip, port, socket);
    }

    void ArcusCommunicationTest::beginGCodeTest()
    {
        ArcusCommunication* ac = new ArcusCommunication(ip, port, socket);
        ac->beginGCode();

        // requires a lot of friendly touching privates
//        FffProcessor::getInstance()->gcode_writer.gcode.output_stream;

        //ac->private_data->socket->getState()
        //ac.flushGCode();

    }

    void ArcusCommunicationTest::flushGCodeTest()
    {

    }

    void ArcusCommunicationTest::isSequentialTest()
    {

    }

    void ArcusCommunicationTest::hasSliceTest()
    {

    }

    void ArcusCommunicationTest::sendCurrentPositionTest()
    {

    }

    void ArcusCommunicationTest::sendGCodePrefixTest()
    {

    }

    void ArcusCommunicationTest::sendFinishedSlicingTest()
    {

    }

    void ArcusCommunicationTest::sendLayerCompleteTest()
    {

    }

    void ArcusCommunicationTest::sendLineToTest()
    {

    }

    void ArcusCommunicationTest::sendOptimizedLayerDataTest()
    {

    }

    void ArcusCommunicationTest::sendPolygonTest()
    {

    }

    void ArcusCommunicationTest::sendPolygonsTest()
    {

    }

    void ArcusCommunicationTest::sendPrintTimeMaterialEstimatesTest()
    {

    }

    void ArcusCommunicationTest::sendProgressTest()
    {

    }

    void ArcusCommunicationTest::setLayerForSendTest()
    {

    }

    void ArcusCommunicationTest::setExtruderForSendTest()
    {

    }

    void ArcusCommunicationTest::sliceNextTest()
    {

    }

}

