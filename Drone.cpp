/**
 * Includes
 */
#include <csignal>
#include <errno.h>
#include <vector>

#include "Drone.h"

/**
 * Some implementation
 */
/* removed **/

/**
 * Some more define to clean out
 */
#define BD_CLIENT_STREAM_PORT 55004
#define BD_CLIENT_CONTROL_PORT 55005

/**
 * Methods
 */

/// PUBLIC
Drone::Drone(const std::string& ipAddress, unsigned int discoveryPort, unsigned int c2dPort, unsigned int d2cPort):
        _deviceController(NULL),
        _deviceState(ARCONTROLLER_DEVICE_STATE_MAX),
        _ip(ipAddress),
        _discoveryPort(discoveryPort),
        _c2dPort(c2dPort),
        _d2cPort(d2cPort)
{
    bool failed = false;
    ARDISCOVERY_Device_t *device = NULL;
    pid_t child = 0;
    eARCONTROLLER_ERROR error = ARCONTROLLER_OK;


    if (mkdtemp(_fifo_dir) == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, "ERROR", "Mkdtemp failed.");
        _isValid = false;
        return;
    }
    snprintf(_fifo_name, sizeof(_fifo_name), "%s/%s", _fifo_dir, FIFO_NAME);

    videoOut = fopen(_fifo_name, "wb+");
    if(videoOut == NULL)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, "ERROR", "Mkfifo failed: %d, %s", errno, strerror(errno));
        _isValid = false;
        return;
    }

    ARSAL_Sem_Init (&(_stateSem), 0, 0);

    /**
     * Exchanging JSON information with the drone
     */
    this->ardiscoveryConnect();


    /**
     *  Getting Network controller
     */
    if (!failed)
    {
        /* start */
        //failed = this->startNetwork();
    }
    /**
     * Creating device controller
     */
    eARDISCOVERY_ERROR errorDiscovery = ARDISCOVERY_OK;
    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- init discovey device ... ");
    device = ARDISCOVERY_Device_New (&errorDiscovery);
    if (errorDiscovery == ARDISCOVERY_OK) {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "    - ARDISCOVERY_Device_InitWifi ...");
        errorDiscovery = ARDISCOVERY_Device_InitWifi (device, ARDISCOVERY_PRODUCT_BEBOP_2, "bebop2", _ip.c_str(), discoveryPort);

        if (errorDiscovery != ARDISCOVERY_OK)
        {
            failed = 1;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Discovery error :%s", ARDISCOVERY_Error_ToString(errorDiscovery));
        }
    }

    // create a device controller
    if (!failed)
    {
        _deviceController = ARCONTROLLER_Device_New (device, &error);

        if (error != ARCONTROLLER_OK)
        {
            ARSAL_PRINT (ARSAL_PRINT_ERROR, TAG, "Creation of deviceController failed.");
            failed = 1;
        }

        if (!failed)
        {
            ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- delete discovey device ... ");
            ARDISCOVERY_Device_Delete (&device);
        }
    }

    // add the state change callback to be informed when the device controller starts, stops...
    if (!failed)
    {
        error = ARCONTROLLER_Device_AddStateChangedCallback (_deviceController, stateChanged, this);

        if (error != ARCONTROLLER_OK)
        {
            ARSAL_PRINT (ARSAL_PRINT_ERROR, TAG, "add State callback failed.");
            failed = 1;
        }
    }

    // add the command received callback to be informed when a command has been received from the device
    if (!failed)
    {
        error = ARCONTROLLER_Device_AddCommandReceivedCallback (_deviceController, commandReceived, this);

        if (error != ARCONTROLLER_OK)
        {
            ARSAL_PRINT (ARSAL_PRINT_ERROR, TAG, "add callback failed.");
            failed = 1;
        }
    }

    if(!failed){
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- set arcommands decoder ... ");
        eARCOMMANDS_DECODER_ERROR decError = ARCOMMANDS_DECODER_OK;
        _commandsDecoder = ARCOMMANDS_Decoder_NewDecoder(&decError);

        //ARCOMMANDS_Decoder_SetARDrone3PilotingStateSpeedChangedCb(_deviceController.)
    }



    // add the frame received callback to be informed when a streaming frame has been received from the device
    if (!failed)
    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- set Video callback ... ");
        error = ARCONTROLLER_Device_SetVideoStreamCallbacks (_deviceController, decoderConfigCallback, didReceiveFrameCallback, NULL , this);

        if (error != ARCONTROLLER_OK)
        {
            failed = 1;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- error: %s", ARCONTROLLER_Error_ToString(error));
        }
    }
    _isValid = !failed;
    _isConnected = false;
}


Drone::~Drone() {
    if(_deviceController != NULL and !isStopped()){
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "Disconnecting ...");

        eARCONTROLLER_ERROR error = ARCONTROLLER_Device_Stop (_deviceController);

        if (error == ARCONTROLLER_OK)
        {
            // wait state update update
            ARSAL_Sem_Wait (&(_stateSem));
        }

        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "ARCONTROLLER_Device_Delete ...");
        ARCONTROLLER_Device_Delete (&_deviceController);


        ARSAL_Sem_Destroy (&(_stateSem));

        unlink(_fifo_name);
        rmdir(_fifo_dir);

        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "Drone successfully destroyed");
    }
}

bool Drone::connect()
{
    eARCONTROLLER_ERROR error;
    if (_isValid and !_isConnected)
    {
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "Connecting ...");
        error = ARCONTROLLER_Device_Start (_deviceController);

        if (error != ARCONTROLLER_OK)
        {
            _isConnected = false;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- error :%s", ARCONTROLLER_Error_ToString(error));
        }else {
            _isConnected = true;
        }
    }

    if(_isConnected){
        ARSAL_Sem_Wait (&(_stateSem));

        _deviceState = ARCONTROLLER_Device_GetState (_deviceController, &error);

        if ((error != ARCONTROLLER_OK) || (_deviceState != ARCONTROLLER_DEVICE_STATE_RUNNING))
        {
            _isConnected = false;
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- deviceState :%d", _deviceState);
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "- error :%s", ARCONTROLLER_Error_ToString(error));
        }
    }

    bool res = _isValid and _isConnected and !isStopped();
    return res;
}
/// GETTERS

int Drone::getBatteryLvl() {
    return _batteryLvl;
}


/// COMMANDS
bool Drone::takeOff() {
    eARCONTROLLER_ERROR error = _deviceController->aRDrone3->sendPilotingTakeOff(_deviceController->aRDrone3);

    if(error != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending takeoff command");
    }
    return error == ARCONTROLLER_OK;
}
bool Drone::land(){
    eARCONTROLLER_ERROR error = _deviceController->aRDrone3->sendPilotingLanding(_deviceController->aRDrone3);
    if(error != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending land command");
    }
    return error == ARCONTROLLER_OK;
}
bool Drone::emergency(){
    eARCONTROLLER_ERROR error = _deviceController->aRDrone3->sendPilotingEmergency(_deviceController->aRDrone3);
    if(error != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending emergency command");
    }
    return error == ARCONTROLLER_OK;
}
bool Drone::modifyAltitude(int8_t value){
    eARCONTROLLER_ERROR error = _deviceController->aRDrone3->setPilotingPCMDGaz(_deviceController->aRDrone3, value);
    if(error != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending PCMDGaz command with %d", value);
    }
    return error == ARCONTROLLER_OK;
}
bool Drone::modifyYaw(int8_t value){
    eARCONTROLLER_ERROR error = _deviceController->aRDrone3->setPilotingPCMDYaw(_deviceController->aRDrone3, value);
    if(error != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending PCMDYaw command with %d", value);
    }
    return error == ARCONTROLLER_OK;
}
bool Drone::modifyPitch(int8_t value){
    eARCONTROLLER_ERROR error1 = _deviceController->aRDrone3->setPilotingPCMDPitch(_deviceController->aRDrone3, value);
    if(error1 != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending PCMDPitch command with %d", value);
    }


    eARCONTROLLER_ERROR error2 = _deviceController->aRDrone3->setPilotingPCMDFlag(_deviceController->aRDrone3, 1);
    if(error2 != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending PCMDFlag command with 1");
    }
    return error1 == ARCONTROLLER_OK and error2 == ARCONTROLLER_OK;
}
bool Drone::modifyRoll(int8_t value){
    eARCONTROLLER_ERROR error1 = _deviceController->aRDrone3->setPilotingPCMDRoll(_deviceController->aRDrone3, value);
    if(error1 != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending PCMDRoll command with %d", value);
    }


    eARCONTROLLER_ERROR error2 = _deviceController->aRDrone3->setPilotingPCMDFlag(_deviceController->aRDrone3, 1);
    if(error2 != ARCONTROLLER_OK){
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "-error sending PCMDFlag command with 1");
    }
    return error1 == ARCONTROLLER_OK and error2 == ARCONTROLLER_OK;
}


bool Drone::startStreamingME() {
/*
    cv::VideoCapture cv(_fifo_name);

    videoOut = fopen(_fifo_name, "w");
    if(videoOut == NULL)
        return false;
*/
    _deviceController->aRDrone3->sendMediaStreamingVideoEnable(_deviceController->aRDrone3, 1);

    while(!isStreaming());
    return !errorStream();
}

/*
bool Drone::startStreamingEXPL()
{
    bool sentStatus = true;
    uint8_t cmdBuffer[128];
    int32_t cmdSize = 0;
    eARCOMMANDS_GENERATOR_ERROR cmdError;
    eARNETWORK_ERROR netError = ARNETWORK_ERROR;

    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- Send Streaming Begin");

    // Send Streaming begin command
    cmdError = ARCOMMANDS_Generator_GenerateARDrone3MediaStreamingVideoEnable(cmdBuffer, sizeof(cmdBuffer), &cmdSize, 1);
    if (cmdError == ARCOMMANDS_GENERATOR_OK)
    {
        netError = ARNETWORK_Manager_SendData(_netManager, BD_NET_CD_ACK_ID, cmdBuffer, cmdSize, this, arnetworkCmdCallback, 1);
    }

    if ((cmdError != ARCOMMANDS_GENERATOR_OK) || (netError != ARNETWORK_OK))
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "Failed to send Streaming command. cmdError:%d netError:%s", cmdError, ARNETWORK_Error_ToString(netError));
        sentStatus = false;
    }

    return sentStatus;
}
*/

/// GETTERS
bool Drone::isConnected() {
    return _isConnected;
}

bool Drone::isValid() {
    return _isValid;
}

bool Drone::isStopped(){
    return _deviceState == ARCONTROLLER_DEVICE_STATE_STOPPED;
}
bool Drone::isStarting(){
    return _deviceState == ARCONTROLLER_DEVICE_STATE_STARTING;
}
bool Drone::isRunning() {
    return _deviceState == ARCONTROLLER_DEVICE_STATE_RUNNING;
}
bool Drone::isPaused(){
    return _deviceState == ARCONTROLLER_DEVICE_STATE_PAUSED;
}
bool Drone::isStopping(){
    return _deviceState == ARCONTROLLER_DEVICE_STATE_STOPPING;
}
bool Drone::errorStream() {
    return _streamingState == ARCOMMANDS_ARDRONE3_MEDIASTREAMINGSTATE_VIDEOENABLECHANGED_ENABLED_ERROR;
}
bool Drone::isStreaming() {
    return  _streamingState == ARCOMMANDS_ARDRONE3_MEDIASTREAMINGSTATE_VIDEOENABLECHANGED_ENABLED_ENABLED;
}

/// PROTECTED
/**
 * STATIC
 * @param commandKey
 * @param elementDictionary
 * @param drone (void*)(Drone d)
 */
void Drone::commandReceived (eARCONTROLLER_DICTIONARY_KEY commandKey,
                             ARCONTROLLER_DICTIONARY_ELEMENT_t *elementDictionary,
                             void *drone)
{
    Drone* d = (Drone*)drone;
    if (d->_deviceController == NULL)
        return;

    // if the command received is a battery state changed
    switch(commandKey) {
        case ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_BATTERYSTATECHANGED:
            d->cmdBatteryStateChangedRcv(elementDictionary);
            break;
        case ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED:
            d->cmdSensorStateListChangedRcv(elementDictionary);
            break;
        case ARCONTROLLER_DICTIONARY_KEY_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED:
            d->cmdFlyingStateChangedRcv(elementDictionary);
            break;
        case ARCONTROLLER_DICTIONARY_KEY_ARDRONE3_MEDIASTREAMINGSTATE_VIDEOENABLECHANGED:
            d->cmdStreamingStateChangedRcv(elementDictionary);
            break;
        default:
            break;
    }
}

/**
 * STATIC
 * @param newState
 * @param error
 * @param drone (void*)(Drone d)
 */
void Drone::stateChanged (eARCONTROLLER_DEVICE_STATE newState,
                          eARCONTROLLER_ERROR error,
                          void *drone)
{
    Drone* d = (Drone*) drone;
    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "    - stateChanged newState: %d .....", newState);

    switch (newState)
    {
        case ARCONTROLLER_DEVICE_STATE_STOPPED:
            ARSAL_Sem_Post (&(d->_stateSem));
            //stop
            std::cout << "I STOPPED... DUNNO WHY." << std::endl;

            break;

        case ARCONTROLLER_DEVICE_STATE_RUNNING:
            ARSAL_Sem_Post (&(d->_stateSem));
            break;

        default:
            break;
    }

    d->_deviceState = newState;
}

void Drone::cmdSensorStateListChangedRcv(ARCONTROLLER_DICTIONARY_ELEMENT_t *elementDictionary)
{
    ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;
    ARCONTROLLER_DICTIONARY_ELEMENT_t *dictElement = NULL;
    ARCONTROLLER_DICTIONARY_ELEMENT_t *dictTmp = NULL;

    eARCOMMANDS_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORNAME sensorName = ARCOMMANDS_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORNAME_MAX;
    int sensorState = 0;

    if (elementDictionary == NULL) {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "elements is NULL");
        return;
    }

    // get the command received in the device controller
    HASH_ITER(hh, elementDictionary, dictElement, dictTmp) {
        // get the Name
        HASH_FIND_STR (dictElement->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORNAME, arg);
        if (arg != NULL) {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "arg sensorName is NULL");
            continue;
        }

        sensorName = (eARCOMMANDS_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORNAME) arg->value.I32;

        // get the state
        HASH_FIND_STR (dictElement->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_SENSORSSTATESLISTCHANGED_SENSORSTATE, arg);
        if (arg == NULL) {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "arg sensorState is NULL");
            continue;
        }

        sensorState = arg->value.U8;
        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "sensorName %d ; sensorState: %d", sensorName, sensorState);
    }
}

void Drone::cmdStreamingStateChangedRcv(ARCONTROLLER_DICTIONARY_ELEMENT_t *elementDictionary)
{
    ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;
    ARCONTROLLER_DICTIONARY_ELEMENT_t *element = NULL;

    // get the command received in the device controller
    HASH_FIND_STR (elementDictionary, ARCONTROLLER_DICTIONARY_SINGLE_KEY, element);
    if (element != NULL)
    {
        // get the value
        HASH_FIND_STR (element->arguments, ARCONTROLLER_DICTIONARY_KEY_ARDRONE3_MEDIASTREAMINGSTATE_VIDEOENABLECHANGED_ENABLED, arg);

        if (arg != NULL)
        {
            _streamingState = (eARCOMMANDS_ARDRONE3_MEDIASTREAMINGSTATE_VIDEOENABLECHANGED_ENABLED) arg->value.I32;
        }
    }
}


void Drone::cmdBatteryStateChangedRcv(ARCONTROLLER_DICTIONARY_ELEMENT_t *elementDictionary)
{

    ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;
    ARCONTROLLER_DICTIONARY_ELEMENT_t *singleElement = NULL;

    if (elementDictionary == NULL) {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "elements is NULL");
        return;
    }

    // get the command received in the device controller
    HASH_FIND_STR (elementDictionary, ARCONTROLLER_DICTIONARY_SINGLE_KEY, singleElement);

    if (singleElement == NULL) {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "singleElement is NULL");
        return;
    }

    // get the value
    HASH_FIND_STR (singleElement->arguments, ARCONTROLLER_DICTIONARY_KEY_COMMON_COMMONSTATE_BATTERYSTATECHANGED_PERCENT, arg);

    if (arg == NULL) {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "arg is NULL");
        return;
    }
    _batteryLvl.store((int)(arg->value.U8));
}

void Drone::cmdFlyingStateChangedRcv(ARCONTROLLER_DICTIONARY_ELEMENT_t * elementDictionary)
{
    ARCONTROLLER_DICTIONARY_ARG_t *arg = NULL;
    ARCONTROLLER_DICTIONARY_ELEMENT_t *element = NULL;

    // get the command received in the device controller
    HASH_FIND_STR (elementDictionary, ARCONTROLLER_DICTIONARY_SINGLE_KEY, element);
    if (element != NULL)
    {
        // get the value
        HASH_FIND_STR (element->arguments, ARCONTROLLER_DICTIONARY_KEY_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE, arg);

        if (arg != NULL)
        {
            _flyingState = (eARCOMMANDS_ARDRONE3_PILOTINGSTATE_FLYINGSTATECHANGED_STATE) arg->value.I32;
        }
    }
}
/**
 * STATIC
 * @param codec
 * @param customData
 * @return
 */
char* CODECBUFFER;
int CODEBUFFERLEN;

eARCONTROLLER_ERROR Drone::decoderConfigCallback (ARCONTROLLER_Stream_Codec_t codec, void *drone)
{
    ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "DECODE CONFIG");
    Drone* d = (Drone*)drone;
    if (d->videoOut != NULL)
    {
        if (codec.type == ARCONTROLLER_STREAM_CODEC_TYPE_H264)
        {
            //TODO FIND OUT IF DECODING IS NEEDED
            if (true)
            {
                CODEBUFFERLEN = codec.parameters.h264parameters.spsSize+codec.parameters.h264parameters.ppsSize;
                CODECBUFFER = (char*) malloc(sizeof(char) * CODEBUFFERLEN);

                memcpy(CODECBUFFER, codec.parameters.h264parameters.spsBuffer, codec.parameters.h264parameters.spsSize);
                memcpy(CODECBUFFER + codec.parameters.h264parameters.spsSize, codec.parameters.h264parameters.ppsBuffer, codec.parameters.h264parameters.ppsSize);

                fwrite(codec.parameters.h264parameters.spsBuffer, codec.parameters.h264parameters.spsSize, 1, d->videoOut);
                fwrite(codec.parameters.h264parameters.ppsBuffer, codec.parameters.h264parameters.ppsSize, 1, d->videoOut);

                fflush (d->videoOut);
            }
        }

    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "videoOut is NULL !!!!!! 605.");
    }

    return ARCONTROLLER_OK;
}

/// PRIVATE

bool Drone::ardiscoveryConnect ()
{
    bool failed = false;

    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- ARDiscovery Connection");

    eARDISCOVERY_ERROR err = ARDISCOVERY_OK;
    ARDISCOVERY_Connection_ConnectionData_t *discoveryData = ARDISCOVERY_Connection_New (ARDISCOVERY_Connection_SendJsonCallback, ARDISCOVERY_Connection_ReceiveJsonCallback, this, &err);
    if (discoveryData == NULL || err != ARDISCOVERY_OK)
    {
        ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Error while creating discoveryData : %s", ARDISCOVERY_Error_ToString(err));
        failed = true;
    }

    if (!failed)
    {
        eARDISCOVERY_ERROR err = ARDISCOVERY_Connection_ControllerConnection(discoveryData, _discoveryPort, _ip.c_str());
        if (err != ARDISCOVERY_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Error while opening discovery connection : %s", ARDISCOVERY_Error_ToString(err));
            failed = true;
        }
    }

    ARDISCOVERY_Connection_Delete(&discoveryData);

    return failed;
}
eARDISCOVERY_ERROR Drone::ARDISCOVERY_Connection_SendJsonCallback (uint8_t *dataTx, uint32_t *dataTxSize, void *drone)
{
    Drone* self = (Drone*) drone;
    eARDISCOVERY_ERROR err = ARDISCOVERY_OK;

    if ((dataTx != NULL) && (dataTxSize != NULL))
    {
        *dataTxSize = sprintf((char *)dataTx, "{ \"%s\": %d,\n \"%s\": \"%s\",\n \"%s\": \"%s\",\n \"%s\": %d,\n \"%s\": %d }",
                              ARDISCOVERY_CONNECTION_JSON_D2CPORT_KEY, self->_d2cPort,
                              ARDISCOVERY_CONNECTION_JSON_CONTROLLER_NAME_KEY, "BebopDroneStartStream",
                              ARDISCOVERY_CONNECTION_JSON_CONTROLLER_TYPE_KEY, "Unix",
                              ARDISCOVERY_CONNECTION_JSON_ARSTREAM2_CLIENT_STREAM_PORT_KEY, BD_CLIENT_STREAM_PORT,
                              ARDISCOVERY_CONNECTION_JSON_ARSTREAM2_CLIENT_CONTROL_PORT_KEY, BD_CLIENT_CONTROL_PORT) + 1;
    }
    else
    {
        err = ARDISCOVERY_ERROR;
    }

    return err;
}

eARDISCOVERY_ERROR Drone::ARDISCOVERY_Connection_ReceiveJsonCallback (uint8_t *dataRx, uint32_t dataRxSize, char *ip, void *drone)
{
    Drone* self = (Drone*) drone;
    eARDISCOVERY_ERROR err = ARDISCOVERY_OK;

    if ((dataRx != NULL) && (dataRxSize != 0))
    {
        char *json = (char*) malloc(dataRxSize + 1);
        strncpy(json, (char *)dataRx, dataRxSize);
        json[dataRxSize] = '\0';

        //read c2dPort ...

        ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "    - ReceiveJson:%s ", json);

        free(json);
    }
    else
    {
        err = ARDISCOVERY_ERROR;
    }

    return err;
}
/*
bool Drone::startNetwork ()
{
    bool failed = false;
    eARNETWORK_ERROR netError = ARNETWORK_OK;
    eARNETWORKAL_ERROR netAlError = ARNETWORKAL_OK;
    int pingDelay = 0; // 0 means default, -1 means no ping

    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- Start ARNetwork");

    // Create the ARNetworkALManager
    _alManager = ARNETWORKAL_Manager_New(&netAlError);
    if (netAlError != ARNETWORKAL_OK)
    {
        failed = true;
    }

    if (!failed)
    {
        // Initilize the ARNetworkALManager
        netAlError = ARNETWORKAL_Manager_InitWifiNetwork(_alManager, _ip.c_str(), _c2dPort, _d2cPort, 1);
        if (netAlError != ARNETWORKAL_OK)
        {
            failed = true;
        }
    }

    if (!failed)
    {
        // Create the ARNetworkManager.
        _netManager = ARNETWORK_Manager_New(_alManager, numC2dParams, c2dParams, numD2cParams, d2cParams, pingDelay, onDisconnectNetwork, NULL, &netError);
        if (netError != ARNETWORK_OK)
        {
            failed = true;
        }
    }

    if (!failed)
    {
        // Create and start Tx and Rx threads.
        if (ARSAL_Thread_Create(&(_rxThread), ARNETWORK_Manager_ReceivingThreadRun, _netManager) != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Creation of Rx thread failed.");
            failed = true;
        }

        if (ARSAL_Thread_Create(&(_txThread), ARNETWORK_Manager_SendingThreadRun, _netManager) != 0)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "Creation of Tx thread failed.");
            failed = true;
        }
    }

    // Print net error
    if (failed)
    {
        if (netAlError != ARNETWORKAL_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "ARNetWorkAL Error : %s", ARNETWORKAL_Error_ToString(netAlError));
        }

        if (netError != ARNETWORK_OK)
        {
            ARSAL_PRINT(ARSAL_PRINT_ERROR, TAG, "ARNetWork Error : %s", ARNETWORK_Error_ToString(netError));
        }
    }

    return failed;
}
void Drone::stopNetwork()
{
    ARSAL_PRINT(ARSAL_PRINT_INFO, TAG, "- Stop ARNetwork");

    // ARNetwork cleanup
    if (_netManager != NULL)
    {
        ARNETWORK_Manager_Stop(_netManager);
        if (_rxThread != NULL)
        {
            ARSAL_Thread_Join(_rxThread, NULL);
            ARSAL_Thread_Destroy(&(_rxThread));
            _rxThread = NULL;
        }

        if (_txThread != NULL)
        {
            ARSAL_Thread_Join(_txThread, NULL);
            ARSAL_Thread_Destroy(&(_txThread));
            _txThread = NULL;
        }
    }

    if (_alManager != NULL)
    {
        ARNETWORKAL_Manager_Unlock(_alManager);

        ARNETWORKAL_Manager_CloseWifiNetwork(_alManager);
    }

    ARNETWORK_Manager_Delete(&(_netManager));
    ARNETWORKAL_Manager_Delete(&(_alManager));
}
void Drone::onDisconnectNetwork(ARNETWORK_Manager_t *manager, ARNETWORKAL_Manager_t *alManager, void *drone)
{
    Drone* self = (Drone*) drone;
    ARSAL_PRINT(ARSAL_PRINT_DEBUG, TAG, "onDisconnectNetwork ...");
}

eARNETWORK_MANAGER_CALLBACK_RETURN Drone::arnetworkCmdCallback(int buffer_id, uint8_t *data, void *drone, eARNETWORK_MANAGER_CALLBACK_STATUS cause)
{
    std::cout << "coucou" << std::endl;

    Drone* self = (Drone*) drone;

    eARNETWORK_MANAGER_CALLBACK_RETURN retval = ARNETWORK_MANAGER_CALLBACK_RETURN_DEFAULT;

    ARSAL_PRINT(ARSAL_PRINT_DEBUG, TAG, "    - arnetworkCmdCallback %d, cause:%d ", buffer_id, cause);

    if (cause == ARNETWORK_MANAGER_CALLBACK_STATUS_TIMEOUT)
    {
        retval = ARNETWORK_MANAGER_CALLBACK_RETURN_DATA_POP;
    }

    return retval;
}
*/

eARCONTROLLER_ERROR Drone::didReceiveFrameCallback (ARCONTROLLER_Frame_t *frame, void *drone)
{
    Drone* self = (Drone*) drone;

    //uint8_t data

    //ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "GOT A FRAME");
/*
    uint8_t* pxls = (uint8_t*) malloc(sizeof(uint8_t) * (frame->used + CODEBUFFERLEN));
    memcpy(pxls, CODECBUFFER, CODEBUFFERLEN);
    memcpy(pxls + CODEBUFFERLEN, frame->data,  frame->used);

    cv::Mat picture = cv::imdecode(std::vector<uint8_t>(pxls, pxls + frame->used + CODEBUFFERLEN), CV_LOAD_IMAGE_COLOR);
    //cv::Mat picture(856, 480, CV_8U, frame->data);

    if(picture.data != NULL) {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "YATA");
        std::cout << "OK " << picture.rows << " " << picture.cols << ":" << picture.data << std::endl;
        cv::imshow("MDR", picture);
    }else{
        std::cout << "KO " << picture.rows << " " << picture.cols << ":" << picture.data << std::endl;
    }
*/
    /**/
    if (self->videoOut != NULL)
    {
        if (frame != NULL)
        {
            //TODO detect if user requested camera ?
            if (true)
            {
                fwrite(frame->data, frame->used, 1, self->videoOut);

                fflush (self->videoOut);
            }
        }
        else
        {
            ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "frame is NULL.");
        }
    }
    else
    {
        ARSAL_PRINT(ARSAL_PRINT_WARNING, TAG, "videoOut is NULL.");
    }
/**/
    return ARCONTROLLER_OK;
}

std::string Drone::getVideoPath() {
    return _fifo_name;
}
