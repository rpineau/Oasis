//
//  Oasis.cpp
//  Takahashi Oasis X2 plugin
//
//  Created by Rodolphe Pineau on 2012/01/16.


#include "Oasis.h"

void threaded_sender(std::future<void> futureObj, COasisController *OasisControllerObj, hid_device *hidDevice)
{
    const byte cmdData[REPORT_SIZE] = {0x00, CODE_GET_STATUS, 0x00};
    int nByteWriten = 0;
    while (futureObj.wait_for(std::chrono::milliseconds(1000)) == std::future_status::timeout) {
        if(hidDevice && OasisControllerObj && OasisControllerObj->m_DevAccessMutex.try_lock()) {
            nByteWriten = hid_write(hidDevice, cmdData, sizeof(cmdData));
            OasisControllerObj->m_DevAccessMutex.unlock();
            if(nByteWriten == -1)  { // error
                std::this_thread::yield();
                continue;
            }
        }
        else {
            std::this_thread::yield();
        }
    }
}

void threaded_poller(std::future<void> futureObj, COasisController *OasisControllerObj, hid_device *hidDevice)
{
    int nbRead;
    byte cHIDBuffer[REPORT_SIZE];

    while (futureObj.wait_for(std::chrono::milliseconds(1)) == std::future_status::timeout) {
        if(hidDevice && OasisControllerObj && OasisControllerObj->m_DevAccessMutex.try_lock()) {
            nbRead = hid_read(hidDevice, cHIDBuffer, sizeof(cHIDBuffer));
            OasisControllerObj->m_DevAccessMutex.unlock();
            if(nbRead>0){
                OasisControllerObj->parseResponse(cHIDBuffer, nbRead);
            }
            else {
                std::this_thread::yield();
            }
        }
        else {
            std::this_thread::yield();
        }
    }
}

COasisController::COasisController()
{
    m_bDebugLog = false;
    m_bIsConnected = false;
    m_Oasis_Settings.nCurPos = 0;
    m_nTargetPos = 0;
    m_nTempSource = INTERNAL;
    m_DevHandle = nullptr;
    m_Oasis_Settings.fInternal = -100.0;
    m_Oasis_Settings.fAmbient = -100.0;
    m_bSetUserConf = false;
    
    m_ThreadsAreRunning = false;
    m_nGotoTries = 0;

    m_sSerialNumber.clear();
    m_DevHandle = nullptr;

    m_Oasis_Settings.nCurPos = 0;
    m_Oasis_Settings.nMaxPos = 0;
    m_Oasis_Settings.bIsMoving = false;
    m_Oasis_Settings.bIsReversed = false;
    m_Oasis_Settings.sVersion.clear();
    m_Oasis_Settings.sModel.clear();
    m_Oasis_Settings.sSerial.clear();
    m_Oasis_Settings.nBackstep = 0;
    m_Oasis_Settings.nBacklash = 0;
    m_Oasis_Settings.fInternal = 0.0f;
    m_Oasis_Settings.fAmbient = 0.0f;
    m_Oasis_Settings.bExternalSensorPresent = false;
    m_Oasis_Settings.backlash = 0;
    m_Oasis_Settings.backlashDirection = 0;
    m_Oasis_Settings.speed = 0;
    m_Oasis_Settings.beepOnMove = false;
    m_Oasis_Settings.beepOnStartup = false;
    m_Oasis_Settings.bluetoothOn = false;
    m_Oasis_Settings.sBluetoothName.clear();
    m_Oasis_Settings.sFriendlyName.clear();


    m_bGotconfig = false;
    m_bGotBluetoothName = false;
    m_bGotFriendlyName = false;
    m_bGotModel = false;
    m_bGotVersion = false;
    
#ifdef PLUGIN_DEBUG
#if defined(SB_WIN_BUILD)
    m_sLogfilePath = getenv("HOMEDRIVE");
    m_sLogfilePath += getenv("HOMEPATH");
    m_sLogfilePath += "\\Oasis-Log.txt";
    m_sPlatform = "Windows";
#elif defined(SB_LINUX_BUILD)
    m_sLogfilePath = getenv("HOME");
    m_sLogfilePath += "/Oasis-Log.txt";
    m_sPlatform = "Linux";
#elif defined(SB_MAC_BUILD)
    m_sLogfilePath = getenv("HOME");
    m_sLogfilePath += "/Oasis-Log.txt";
    m_sPlatform = "macOS";
#endif
    m_sLogFile.open(m_sLogfilePath, std::ios::out |std::ios::trunc);
#endif

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 2
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [COasisController] Version " << std::fixed << std::setprecision(2) << PLUGIN_VERSION << " build " << __DATE__ << " " << __TIME__ << " on "<< m_sPlatform << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [COasisController] Constructor Called." << std::endl;
    m_sLogFile.flush();
#endif
}

COasisController::~COasisController()
{

    if(m_bIsConnected)
        Disconnect();

#ifdef	PLUGIN_DEBUG
    // Close LogFile
    if(m_sLogFile.is_open())
        m_sLogFile.close();
#endif

}

int COasisController::Connect()
{
    int nErr = PLUGIN_OK;
    int nTimeout;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] Called." << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] m_sSerialNumber : " << m_sSerialNumber << std::endl;
    m_sLogFile.flush();
#endif
    // vendor id is : 0x20E1 and the product id is : 0x0002.
    if(!m_sSerialNumber.size()) {
        std::vector<std::string> focuserSNList;
        listFocusers(focuserSNList);
        if(focuserSNList.size()) {
            m_sSerialNumber.assign(focuserSNList.at(0));
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] opening device with serial " <<  m_sSerialNumber << " for vendor id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex <<  VENDOR_ID << " product id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << PRODUCT_ID << std::dec << std::endl;
            m_sLogFile.flush();
#endif
            std::wstring widestr = std::wstring(m_sSerialNumber.begin(), m_sSerialNumber.end());
            const wchar_t* widecstr = widestr.c_str();
            m_DevHandle = hid_open(VENDOR_ID, PRODUCT_ID, widecstr);
        }
        else {
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] opening first available device for vendor id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex <<  VENDOR_ID << " product id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << PRODUCT_ID << std::dec << std::endl;
            m_sLogFile.flush();
#endif
            m_DevHandle = hid_open(VENDOR_ID, PRODUCT_ID, NULL);
        }

    }
    else {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] opening device with serial " <<  m_sSerialNumber << " for vendor id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex <<  VENDOR_ID << " product id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << PRODUCT_ID << std::dec << std::endl;
        m_sLogFile.flush();
#endif
        std::wstring widestr = std::wstring(m_sSerialNumber.begin(), m_sSerialNumber.end());
        const wchar_t* widecstr = widestr.c_str();
        m_DevHandle = hid_open(VENDOR_ID, PRODUCT_ID, widecstr);
    }
    if (!m_DevHandle) {
        m_bIsConnected = false;
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] hid_open failed for vendor id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex <<  VENDOR_ID << " product id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << PRODUCT_ID << std::dec << std::endl;
        m_sLogFile.flush();
#endif
        m_DevHandle = nullptr;
        return Oasis_CANT_CONNECT;
    }
    m_bIsConnected = true;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] Connected to vendor id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex <<  VENDOR_ID << " product id " << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << PRODUCT_ID << std::dec << std::endl;
    m_sLogFile.flush();
#endif

    // Set the hid_read() function to be non-blocking.
    hid_set_nonblocking(m_DevHandle, 1);
    startThreads();
    nTimeout = 0;
    while(!m_bGotconfig) {
        getConfig();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::yield();
        nTimeout++;
        if(nTimeout>MAX_TIMEOUT) {
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] timeout getting config" << std::endl;
            m_sLogFile.flush();
#endif
            break;
        }
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] waiting for config" << std::endl;
        m_sLogFile.flush();
#endif
    }

    nTimeout = 0;
    while(!m_bGotVersion){
        getVersions();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::yield();
        nTimeout++;
        if(nTimeout>MAX_TIMEOUT) {
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] timeout getting version" << std::endl;
            m_sLogFile.flush();
#endif
            break;
        }
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] waiting for version" << std::endl;
        m_sLogFile.flush();
#endif
    }

    nTimeout = 0;
    while(!m_bGotModel){
        getModel();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::yield();
        nTimeout++;
        if(nTimeout>10) {
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] timeout getting model" << std::endl;
            m_sLogFile.flush();
#endif
            break;
        }
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] waiting for model" << std::endl;
        m_sLogFile.flush();
#endif
    }

    nTimeout = 0;
    while(!m_bGotBluetoothName){
        getBluetoothName();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::yield();
        nTimeout++;
        if(nTimeout>MAX_TIMEOUT) {
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] timeout getting Bluetooth name" << std::endl;
            m_sLogFile.flush();
#endif
            break;
        }
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] waiting for Bluetooth name" << std::endl;
        m_sLogFile.flush();
#endif
    }

    nTimeout = 0;
    while(!m_bGotFriendlyName){
        getFriendlyName();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        std::this_thread::yield();
        nTimeout++;
        if(nTimeout>MAX_TIMEOUT) {
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] timeout getting Friendly name" << std::endl;
            m_sLogFile.flush();
#endif
            break;
        }
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Connect] waiting for Friendly name" << std::endl;
        m_sLogFile.flush();
#endif
    }

    return nErr;
}

void COasisController::Disconnect()
{
    const std::lock_guard<std::mutex> lock(m_DevAccessMutex);

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Disconnect] Disconnecting from device." << std::endl;
    m_sLogFile.flush();
#endif
    stopThreads();
    if(m_bIsConnected)
            hid_close(m_DevHandle);

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Disconnect] Calling hid_exit." << std::endl;
    m_sLogFile.flush();
#endif

    hid_exit();
    m_DevHandle = nullptr;
	m_bIsConnected = false;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [Disconnect] Disconnected from device." << std::endl;
    m_sLogFile.flush();
#endif
}

int COasisController::sendCommand(byte *cHIDBuffer)
{
    int nErr = PLUGIN_OK;
    int nByteWriten = 0;
    int nNbTimeOut = 0;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(cHIDBuffer,  REPORT_SIZE, m_hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [sendCommand] sending : " << std::endl << m_hexOut << std::endl;
    m_sLogFile.flush();
#endif
    nNbTimeOut = 0;
    while(nNbTimeOut < MAX_TIMEOUT) {
        if(m_DevAccessMutex.try_lock()) {
            nByteWriten = hid_write(m_DevHandle, cHIDBuffer, REPORT_SIZE);
            m_DevAccessMutex.unlock();
            if(nByteWriten<0) {
                nNbTimeOut++;
                std::this_thread::yield();
            }
            else {
                break; // all good, no need to retry
            }
        }
        else {
            nNbTimeOut++;
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread in case we got an error
    }

    if(nNbTimeOut>=MAX_TIMEOUT) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [sendCommand] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
    return nErr;
}

int COasisController::listFocusers(std::vector<std::string> &focuserSNList)
{
    int nErr = PLUGIN_OK;
    hid_device_info *deviceInfoList, *curDev;;
    std::wstring ws;
    std::string sSerial;
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [listFocusers] called." << std::endl;
    m_sLogFile.flush();
#endif

    deviceInfoList = hid_enumerate(VENDOR_ID, PRODUCT_ID);
    curDev = deviceInfoList;

    for (curDev = deviceInfoList; curDev != nullptr; curDev = curDev->next) {
        ws.assign(curDev->serial_number);
        sSerial.assign(ws.begin(), ws.end());

#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [listFocusers] SN : " << sSerial << std::endl;
        m_sLogFile.flush();
#endif
        focuserSNList.push_back(sSerial);
    }
    return nErr;
}

bool COasisController::isFocuserPresent(std::string sSerial)
{
    std::vector<std::string> focuserSNList;
    listFocusers(focuserSNList);
    for (std::string serial : focuserSNList) {
        if(sSerial == serial)
            return true;
    }
    return false;
}

void COasisController::setFocuserSerial(std::string sSerial)
{
#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setFocuserSerial] Setting focuser serial to " << sSerial << std::endl;
    m_sLogFile.flush();
#endif

    m_sSerialNumber.assign(sSerial);
}

void COasisController::setUserConf(bool bUserConf)
{
    m_bSetUserConf = bUserConf;
}

void COasisController::startThreads()
{
    if(!m_ThreadsAreRunning) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [startThreads] Starting HID threads." << std::endl;
        m_sLogFile.flush();
#endif
        m_exitSignal = new std::promise<void>();
        m_futureObj = m_exitSignal->get_future();
        m_exitSignalSender = new std::promise<void>();
        m_futureObjSender = m_exitSignalSender->get_future();

        m_th = std::thread(&threaded_poller, std::move(m_futureObj), this, m_DevHandle);
        m_thSender = std::thread(&threaded_sender, std::move(m_futureObjSender), this,  m_DevHandle);
        m_ThreadsAreRunning = true;
    }
}

void COasisController::stopThreads()
{
    if(m_ThreadsAreRunning) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [stopThreads] Waiting for threads to exit." << std::endl;
        m_sLogFile.flush();
#endif
        m_exitSignal->set_value();
        m_exitSignalSender->set_value();
        m_th.join();
        m_thSender.join();
        delete m_exitSignal;
        delete m_exitSignalSender;
        m_exitSignal = nullptr;
        m_exitSignalSender = nullptr;
        m_ThreadsAreRunning = false;
    }
}


int COasisController::getConfig()
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    cHIDBuffer[0] = 0; // report ID
    cHIDBuffer[1] = CODE_GET_CONFIG; // command
    cHIDBuffer[2] = 0; // command length

    nErr = sendCommand(cHIDBuffer);
    return nErr;
}

#pragma mark move commands
int COasisController::haltFocuser()
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    if(m_Oasis_Settings.bIsMoving) {
        cHIDBuffer[0] = 0; // report ID
        cHIDBuffer[1] = CODE_CMD_STOP_MOVE; // command
        cHIDBuffer[2] = 0; // command length

        nErr = sendCommand(cHIDBuffer);
    }
    m_nGotoTries = MAX_GOTO_RETRY+1; // prevent goto retries

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
    return nErr;
}

int COasisController::gotoPosition(long nPos)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    DeclareFrame(FrameMoveTo, frameMove, CODE_CMD_MOVE_TO);

    if(!m_bIsConnected || !m_DevHandle)
		return ERR_COMMNOLINK;

    if(m_Oasis_Settings.bIsMoving)
        return ERR_CMD_IN_PROGRESS_FOC;

    if (nPos>m_Oasis_Settings.nMaxPos)
        return ERR_LIMITSEXCEEDED;

    frameMove.position = htonl((unsigned int)nPos);
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameMove, sizeof(FrameMoveTo));

    m_nTargetPos = nPos;

    #ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [gotoPosition] goto :  " << std::dec << nPos << " (0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPos <<")" << std::dec << std::endl;
        m_sLogFile.flush();
    #endif


    nErr = sendCommand(cHIDBuffer);
    m_gotoTimer.Reset();
    return nErr;
}

int COasisController::moveRelativeToPosision(long nSteps)
{
    int nErr;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    if(m_Oasis_Settings.bIsMoving)
        return ERR_CMD_IN_PROGRESS_FOC;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [moveRelativeToPosision] goto relative position : " << nSteps << std::endl;
    m_sLogFile.flush();
#endif

    nErr = gotoPosition(m_Oasis_Settings.nCurPos + nSteps);
    return nErr;
}

#pragma mark command complete functions

int COasisController::isGoToComplete(bool &bComplete)
{
    int nErr = PLUGIN_OK;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    bComplete = false;

    if(m_gotoTimer.GetElapsedSeconds()<0.5) { // focuser take a bit of time to start moving and reporting it's moving.
        return nErr;
    }

    if(m_Oasis_Settings.bIsMoving) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] Complete : " << (bComplete?"Yes":"No") << std::endl;
        m_sLogFile.flush();
#endif
        return nErr;
    }
    bComplete = true;

    if(m_Oasis_Settings.nCurPos != m_nTargetPos) {
        if(m_nGotoTries == 0) {
            bComplete = false;
            m_nGotoTries++;
            gotoPosition(m_nTargetPos);
        }
        else if (m_nGotoTries > MAX_GOTO_RETRY){
            m_nGotoTries = 0;
            // we have an error as we're not moving but not at the target position
#ifdef PLUGIN_DEBUG
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] **** ERROR **** Not moving and not at the target position affter " << MAX_GOTO_RETRY << "tries." << std::endl;
            m_sLogFile.flush();
#endif
            m_nTargetPos = m_Oasis_Settings.nCurPos;
            nErr = ERR_CMDFAILED;
        }

    } else {

        m_nGotoTries = 0;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [isGoToComplete] Complete : " << (bComplete?"Yes":"No") << std::endl;
    m_sLogFile.flush();
#endif
    return nErr;
}

#pragma mark getters and setters
int COasisController::getVersions()
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    DeclareFrameHead(frame, CODE_GET_VERSION);
    
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frame, sizeof(FrameHead));

    nErr = sendCommand(cHIDBuffer);
    return nErr;
}

void COasisController::getVersions(std::string &sVersion)
{
    sVersion.assign(m_Oasis_Settings.sVersion);
}

int COasisController::getModel()
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    DeclareFrameHead(frame, CODE_GET_PRODUCT_MODEL);

    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frame, sizeof(FrameHead));

    nErr = sendCommand(cHIDBuffer);
    return nErr;

}

void COasisController::getModel(std::string &sModel)
{
    sModel.assign(m_Oasis_Settings.sModel);
}

int COasisController::getSerial()
{
    int nErr = PLUGIN_OK;
    wchar_t TmpStr[128];
    int nNbTimeOut = 0;
    std::wstring ws;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSerial]" << std::endl;
    m_sLogFile.flush();
#endif
    nNbTimeOut = 0;
    while(nNbTimeOut < MAX_TIMEOUT) {
        if(m_DevAccessMutex.try_lock()) {
            nErr = hid_get_serial_number_string(m_DevHandle, TmpStr, sizeof(TmpStr)/sizeof(wchar_t) );
            m_DevAccessMutex.unlock();
            // if(nByteWriten<0) {
            if(nErr<0) {
                nNbTimeOut++;
                std::this_thread::yield();
            }
            else {
                break; // all good, no need to retry
            }
        }
        else {
            nNbTimeOut++;
            std::this_thread::yield();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread in case we got an error
    }

    ws.assign(TmpStr);
    m_Oasis_Settings.sSerial.assign(ws.begin(), ws.end());

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSerial] serial : " << m_Oasis_Settings.sSerial << std::endl;
    m_sLogFile.flush();
#endif

    return nErr;
}


int COasisController::getBluetoothName()
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    DeclareFrameHead(frame, CODE_GET_BLUETOOTH_NAME);

    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frame, sizeof(FrameHead));

    nErr = sendCommand(cHIDBuffer);
    return nErr;

}

int COasisController::getFriendlyName()
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    DeclareFrameHead(frame, CODE_GET_FRIENDLY_NAME);

    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frame, sizeof(FrameHead));
    
    nErr = sendCommand(cHIDBuffer);
    return nErr;

}


int COasisController::setMaxStep(unsigned int nMaxStep)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setMaxStep] setting max steps to :  " << std::dec << nMaxStep << " (0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nMaxStep <<")" << std::dec << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = htonl(MASK_MAX_STEP);
    frameConfig.maxStep = htonl(nMaxStep);

    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);
    return nErr;
}

uint32_t COasisController::getBacklash()
{
    return m_Oasis_Settings.backlash;
}

int COasisController::setBacklash(unsigned int nBacklash)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBacklash] setting Backlash to :  " << std::dec << nBacklash << " (0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nBacklash <<")" << std::dec << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = htonl(MASK_BACKLASH);
    frameConfig.backlash = htonl(nBacklash);
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

uint8_t COasisController::getBacklashDirection()
{
    return m_Oasis_Settings.backlashDirection;
}

int COasisController::setBacklashDirection(byte nBacklashDir)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBacklashDirection] setting backlashDirection to :  " << std::dec << nBacklashDir << " (0x" << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << nBacklashDir <<")" << std::dec << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = htonl(MASK_BACKLASH_DIRECTION);
    frameConfig.backlashDirection = nBacklashDir;
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

bool  COasisController::getReverse()
{
    return m_Oasis_Settings.bIsReversed;
}

int COasisController::setReverse(bool setReverse)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setReverse] setting setReverse to :  " << (setReverse?"Reverse":"Normal") << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = htonl(MASK_REVERSE_DIRECTION);
    frameConfig.reverseDirection = setReverse?1:0;
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

uint32_t COasisController::getSpeed()
{
    return m_Oasis_Settings.speed;
}

int COasisController::setSpeed(unsigned int nSpeed)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setSpeed] setting speed to :  " << std::dec << nSpeed << " (0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nSpeed <<")" << std::dec << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = htonl(MASK_SPEED);
    frameConfig.speed = nSpeed;

    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);

    return nErr;

}

bool COasisController::getBeepOnMove()
{
    return m_Oasis_Settings.beepOnMove==1;
}

int COasisController::setBeepOnMove(bool bEnabled)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBeepOnMove] setting beep on move to :  " << (bEnabled?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = ntohl(MASK_BEEP_ON_MOVE);
    frameConfig.beepOnMove = bEnabled?1:0;
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

bool COasisController::getBeepOnStartup()
{
    return m_Oasis_Settings.beepOnStartup==1;
}

int COasisController::setBeepOnStartup(bool bEnabled)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBeepOnStartup] setting beep on statrup to :  " << (bEnabled?"True":"False") << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = htonl(MASK_BEEP_ON_STARTUP);
    frameConfig.beepOnStartup = bEnabled?1:0;
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

bool COasisController::getBluetoothEnabled()
{
    return m_Oasis_Settings.bluetoothOn==1;
}

int COasisController::setBluetoothEnabled(bool bEnabled)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBluetoothEnabled] setting bluetooth to :  " << (bEnabled?"Enable":"Disable") << std::endl;
    m_sLogFile.flush();
#endif

    DeclareFrame(FrameConfig, frameConfig  , CODE_SET_CONFIG);
    frameConfig.mask = htonl(MASK_BLUETOOTH);
    frameConfig.bluetoothOn = bEnabled?1:0;
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameConfig, sizeof(FrameConfig));

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

void COasisController::getBluetoothName(std::string &sName)
{
    sName.assign(m_Oasis_Settings.sBluetoothName);
}

int COasisController::setBluetoothName(std::string sName)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];
    std::string sNewName;
    int nameMaxLen;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setBluetoothName] setting bluetooth name to :  " << sName << std::endl;
    m_sLogFile.flush();
#endif

    sNewName = trim(sName,"\n\r ");

    DeclareFrame(FrameBluetoothName, command, CODE_SET_BLUETOOTH_NAME);
    nameMaxLen = sizeof(command.data);
    memcpy(command.data, sNewName.c_str(),nameMaxLen<sNewName.size()?nameMaxLen:sNewName.size());
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&command, sizeof(FrameBluetoothName));

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

void COasisController::getFriendlyName(std::string &sName)
{
    sName.assign(m_Oasis_Settings.sFriendlyName);
}

int COasisController::setFriendlyName(std::string sName)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];
    std::string sNewName;
    int nameMaxLen;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setFriendlyName] setting friendly name to :  " << sName << std::endl;
    m_sLogFile.flush();
#endif

    sNewName = trim(sName,"\n\r ");

    DeclareFrame(FrameBluetoothName, command, CODE_SET_FRIENDLY_NAME);
    nameMaxLen = sizeof(command.data);
    memcpy(command.data, sNewName.c_str(),nameMaxLen<sNewName.size()?nameMaxLen:sNewName.size());
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&command, sizeof(FrameBluetoothName));

    nErr = sendCommand(cHIDBuffer);

    return nErr;

}



void COasisController::getSerial(std::string &sSerial)
{
    sSerial.assign(m_Oasis_Settings.sSerial);
}

void COasisController::getFirmwareVersion(std::string &sFirmware)
{
    sFirmware.clear();
    if(m_GlobalMutex.try_lock()) {
        if(m_Oasis_Settings.sVersion.size()) {
            sFirmware.assign(m_Oasis_Settings.sVersion);
        }
        else {
            sFirmware = "NA";
        }
        m_GlobalMutex.unlock();
    }
    else
        sFirmware = "NA";

}

double COasisController::getTemperature()
{
    // need to allow user to select the focuser temp source
    switch(m_nTempSource) {
        case INTERNAL:
            return m_Oasis_Settings.fInternal;
            break;
        case EXTERNAL:
            return m_Oasis_Settings.fAmbient;
            break;
        default:
            return m_Oasis_Settings.fInternal;
            break;
    }
}

double COasisController::getTemperature(int nSource)
{
    switch(nSource) {
        case INTERNAL:
            return m_Oasis_Settings.fInternal;
            break;
        case EXTERNAL:
            return m_Oasis_Settings.fAmbient;
            break;
        default:
            return m_Oasis_Settings.fInternal;
            break;
    }
}

bool COasisController::isExternalSensorPresent()
{
    return  m_Oasis_Settings.bExternalSensorPresent;

}

uint32_t COasisController::getPosition()
{

    if(m_Oasis_Settings.nCurPos < 0)
       return 0;
    return m_Oasis_Settings.nCurPos;
}

int COasisController::setPosition(unsigned int nPos)
{
    int nErr = PLUGIN_OK;
    byte cHIDBuffer[REPORT_SIZE];

    DeclareFrame(FrameSyncPosition, frameSync, CODE_CMD_SYNC_POSITION);

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    if(m_Oasis_Settings.bIsMoving)
        return ERR_CMD_IN_PROGRESS_FOC;

    frameSync.position = htonl((unsigned int)nPos);
    // clear buffer and set cHIDBuffer[0] to report ID 0
    memset(cHIDBuffer, 0, REPORT_SIZE);
    memcpy(cHIDBuffer+1, (byte*)&frameSync, sizeof(FrameMoveTo));


#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [setPosition] set to :  " << std::dec << nPos << " (0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPos <<")" << std::dec << std::endl;
    m_sLogFile.flush();
#endif

    nErr = sendCommand(cHIDBuffer);

    return nErr;
}

uint32_t COasisController::getPosLimit()
{
    return m_Oasis_Settings.nMaxPos;
}


void COasisController::setTemperatureSource(int nSource)
{
    m_nTempSource = nSource;
}

int COasisController::getTemperatureSource()
{
    return m_nTempSource;

}

#pragma mark command and response functions


void COasisController::parseResponse(byte *Buffer, int nLength)
{

    // locking the mutex to prevent access while we're accessing to the data.
    const std::lock_guard<std::mutex> lock(m_GlobalMutex);
    int temperatureExt;
    byte nCode;
    FrameConfig *fConfig;
    FrameStatusAck *fStatus;
    FrameVersionAck *fVersions;
    FrameSerialNumber *fSerial;
    FrameProductModelAck *fModel;
    FrameFriendlyName *fFriendlyName;
    FrameBluetoothName *fBluetoothName;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(Buffer,  nLength, m_hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] Buffer size " << std::dec << nLength <<", content : " << std::endl << m_hexOut << std::endl;
    m_sLogFile.flush();
#endif
    nCode = Buffer[0];
    switch (nCode) {
        case  CODE_GET_PRODUCT_MODEL :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_PRODUCT_MODEL" << std::endl;
            m_sLogFile.flush();
#endif
            m_bGotModel = true;
            fModel = (FrameProductModelAck*)Buffer;
            m_Oasis_Settings.sModel.assign((char*)fModel->data);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_PRODUCT_MODEL fModel->data  '" << fModel->data <<"'"<< std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_PRODUCT_MODEL m_Oasis_Settings.sModel '" << m_Oasis_Settings.sModel <<"'"<< std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_GET_VERSION :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_VERSION" << std::endl;
            m_sLogFile.flush();
#endif
            m_bGotVersion = true;
            fVersions = (FrameVersionAck *)Buffer;
            m_Oasis_Settings.sVersion.assign(std::to_string((ntohl(fVersions->firmware & 0xFF000000))>>24) + "." +
            std::to_string((fVersions->firmware & 0x00FF0000)>>16) + "." +
            std::to_string((fVersions->firmware & 0x0000FF00)>>8) + "." +
            std::to_string((fVersions->firmware & 0x000000FF))+ " " + fVersions->built) ;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_VERSION FrameVersionAck fVersions->protocal         = " << std::setfill('0') << std::setw(8) << std::hex << (fVersions->protocal) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_VERSION FrameVersionAck fVersions->hardware         = " << std::setfill('0') << std::setw(8) << std::hex << (fVersions->hardware) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_VERSION FrameVersionAck fVersions->firmware         = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fVersions->firmware) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_VERSION FrameVersionAck fVersions->built            = " << fVersions->built << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_VERSION FrameVersionAck m_Oasis_Settings.sVersion   = " << m_Oasis_Settings.sVersion << std::endl;
            m_sLogFile.flush();
#endif

            break;

        case  CODE_GET_SERIAL_NUMBER :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_SERIAL_NUMBER" << std::endl;
            m_sLogFile.flush();
#endif
            fSerial = (FrameSerialNumber*)Buffer;
            // m_Oasis_Settings.sSerial.assign((char*)(fSerial->data), FRAME_NAME_LEN);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_SERIAL_NUMBER FrameSerialNumberAck fSerial->data = " << fSerial->data << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_SERIAL_NUMBER We're not using this." << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_GET_FRIENDLY_NAME :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_FRIENDLY_NAME" << std::endl;
            m_sLogFile.flush();
#endif
            m_bGotFriendlyName= true;
            fFriendlyName = (FrameFriendlyName*)Buffer;
            m_Oasis_Settings.sFriendlyName.assign((char*)(fFriendlyName->data));
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_FRIENDLY_NAME fFriendlyName->data            '" << fFriendlyName->data <<"'"<< std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_FRIENDLY_NAME m_Oasis_Settings.sFriendlyName '" << m_Oasis_Settings.sFriendlyName <<"'"<< std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_SET_FRIENDLY_NAME :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_SET_FRIENDLY_NAME" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_GET_BLUETOOTH_NAME :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_BLUETOOTH_NAME" << std::endl;
            m_sLogFile.flush();
#endif
            m_bGotBluetoothName = true;
            fBluetoothName = (FrameBluetoothName*)Buffer;
            m_Oasis_Settings.sBluetoothName.assign((char*)(fBluetoothName->data));
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_BLUETOOTH_NAMEfBluetoothName->data             '" << fBluetoothName->data << "'" << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_BLUETOOTH_NAME m_Oasis_Settings.sBluetoothName '" << m_Oasis_Settings.sBluetoothName << "'" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_SET_BLUETOOTH_NAME :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_SET_BLUETOOTH_NAME" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_GET_USER_ID :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_USER_ID" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_SET_USER_ID :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_SET_USER_ID" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_CMD_UPGRADE :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_CMD_UPGRADE" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_CMD_UPGRADE_BOOTLOADER :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_CMD_UPGRADE_BOOTLOADER" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_GET_CONFIG :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG" << std::endl;
            m_sLogFile.flush();
#endif
            m_bGotconfig = true;
            fConfig = (FrameConfig*)Buffer;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->mask              = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fConfig->mask) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->maxStep           = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fConfig->maxStep) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->backlash          = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fConfig->backlash) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->backlashDirection = " << std::dec << (fConfig->backlashDirection==0?0:1) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->reverseDirection  = " << std::dec << (fConfig->reverseDirection==0?0:1) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->speed             = " << std::dec << std::to_string(fConfig->speed) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->beepOnMove        = " << std::dec << (fConfig->beepOnMove==0?0:1) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->beepOnStartup     = " << std::hex << (fConfig->beepOnStartup==0?0:1) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_CONFIG FrameConfig fConfig->bluetoothOn       = " << std::hex << (fConfig->bluetoothOn==0?0:1) << std::endl;
            m_sLogFile.flush();
#endif

            m_Oasis_Settings.nMaxPos = ntohl(fConfig->maxStep);
            m_Oasis_Settings.bIsReversed = fConfig->reverseDirection;
            m_Oasis_Settings.backlash = ntohl(fConfig->backlash);
            m_Oasis_Settings.backlashDirection = fConfig->backlashDirection;
            m_Oasis_Settings.speed = fConfig->speed;
            m_Oasis_Settings.beepOnMove = fConfig->beepOnMove;
            m_Oasis_Settings.beepOnStartup = fConfig->beepOnStartup;
            m_Oasis_Settings.bluetoothOn = fConfig->bluetoothOn;
            break;

        case  CODE_SET_CONFIG :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_SET_CONFIG received" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_GET_STATUS :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS" << std::endl;
            m_sLogFile.flush();
#endif
            fStatus = (FrameStatusAck *)Buffer;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fStatus->position             = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fStatus->position) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fStatus->temperatureInt       = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fStatus->temperatureInt) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fStatus->temperatureExt       = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fStatus->temperatureExt) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fStatus->temperatureDetection = " << std::dec << std::to_string(fStatus->temperatureDetection) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fStatus->moving               = " << std::dec << std::to_string(fStatus->moving) << std::endl;
            m_sLogFile.flush();
#endif

            m_Oasis_Settings.bIsMoving = (fStatus->moving==0?false:true);
            m_Oasis_Settings.nCurPos = ntohl(fStatus->position);
            m_Oasis_Settings.fInternal = GetNTCTemperature(ntohl(fStatus->temperatureInt)) * 0.01;
            if(fStatus->temperatureDetection == 1) {//external probe present
                m_Oasis_Settings.bExternalSensorPresent = true;
                temperatureExt = ntohl(fStatus->temperatureExt);
                temperatureExt = (int)(short)(temperatureExt & 0xFFFF);
                m_Oasis_Settings.fAmbient =  (temperatureExt * 0.0625f * 100 + 0.5) * 0.01;
            }
            else
                m_Oasis_Settings.bExternalSensorPresent = false;

            break;

        case  CODE_CMD_FACTORY_RESET :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_CMD_FACTORY_RESET" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_SET_ZERO_POSITION :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_SET_ZERO_POSITION" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_CMD_MOVE_STEP :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_CMD_MOVE_STEP" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_CMD_MOVE_TO :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_CMD_MOVE_TO" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_CMD_STOP_MOVE :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_PRODUCT_MODEL" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_CMD_SYNC_POSITION :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_CMD_SYNC_POSITION" << std::endl;
            m_sLogFile.flush();
#endif
            break;

        default :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] PLUGIN_DEBUG" << std::endl;
            m_sLogFile.flush();
#endif
            break;
    }


#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nCurPos             : " << std::dec << m_Oasis_Settings.nCurPos << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nMaxPos             : " << std::dec << m_Oasis_Settings.nMaxPos << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.bIsMoving           : " << ( m_Oasis_Settings.bIsMoving ?"Yes":"No") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.bIsReversed         : " << ( m_Oasis_Settings.bIsReversed ?"Yes":"No") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sVersion            : " << m_Oasis_Settings.sVersion << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sModel              : " << m_Oasis_Settings.sModel << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sSerial             : " << m_Oasis_Settings.sSerial << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nBackstep           : " << std::dec << m_Oasis_Settings.nBackstep << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nBacklash           : " << std::dec << m_Oasis_Settings.nCurPos << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.fInternal           : " << m_Oasis_Settings.fInternal << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.fAmbient            : " << m_Oasis_Settings.fAmbient << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.backlash            : " << std::to_string(m_Oasis_Settings.backlash) << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.backlashDirection   : " << (m_Oasis_Settings.backlashDirection==0?"In":"Out") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.speed               : " << std::to_string(m_Oasis_Settings.speed) << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.beepOnStartup       : " << (m_Oasis_Settings.beepOnStartup==1?"Enable":"Disable") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.beepOnMove          : " << (m_Oasis_Settings.beepOnMove==1?"Enable":"Disable") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.bluetoothOn         : " << (m_Oasis_Settings.bluetoothOn==1?"On":"Off") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sBluetoothName      : " <<  m_Oasis_Settings.sBluetoothName << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sFriendlyName       : " <<  m_Oasis_Settings.sFriendlyName << std::endl;
    m_sLogFile.flush();
#endif


}


int COasisController::sendSettings()
{
    int nErr = PLUGIN_OK;
    int nByteWriten = 0;
    byte cHIDBuffer[REPORT_SIZE];
    int nNbTimeOut = 0;

    if(!m_bIsConnected)
        return ERR_COMMNOLINK;

    if(!m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);

    return nErr;

    // TO REWRITE

    nErr = sendCommand(cHIDBuffer);
    return nErr;
}

int COasisController::GetNTCTemperature(int ad)
{
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] ad = " << ad << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] AD_MAX = " << AD_MAX << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] B = " << B << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] T25 = " << T25 << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] K = " << K << std::endl;
    m_sLogFile.flush();
#endif

    if (ad <= 0)
        ad = 1;
    else if (ad >= AD_MAX)
        ad = AD_MAX - 1;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] after boundary check ad = " << ad << std::endl;
    m_sLogFile.flush();
#endif

    float T = (float)(B / (log((AD_MAX - ad) / (float)ad) + B / T25) - K);
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] T = " << T << std::endl;
    m_sLogFile.flush();
#endif

    T += (T >= 0) ? 0.005f : -0.005f;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature] adjusted T = " << T << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [GetNTCTemperature]  T *100 = " << T*100 << std::endl;
    m_sLogFile.flush();
#endif

    return (int)(T * 100);
}

std::string& COasisController::trim(std::string &str, const std::string& filter )
{
    return ltrim(rtrim(str, filter), filter);
}

std::string& COasisController::ltrim(std::string& str, const std::string& filter)
{
    str.erase(0, str.find_first_not_of(filter));
    return str;
}

std::string& COasisController::rtrim(std::string& str, const std::string& filter)
{
    str.erase(str.find_last_not_of(filter) + 1);
    return str;
}

#ifdef PLUGIN_DEBUG
void  COasisController::hexdump(const byte *inputData, int inputSize,  std::string &outHex)
{
    int idx=0;
    std::stringstream ssTmp;

    outHex.clear();
    for(idx=0; idx<inputSize; idx++){
        if((idx%16) == 0 && idx>0)
            ssTmp << std::endl;
        ssTmp << "0x" << std::uppercase << std::setfill('0') << std::setw(2) << std::hex << (int)inputData[idx] <<" ";
    }
    outHex.assign(ssTmp.str());
}

void COasisController::logToFile(const std::string sLogLine)
{
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [log] " << sLogLine << std::endl;
    m_sLogFile.flush();

}

const std::string COasisController::getTimeStamp()
{
    time_t     now = time(0);
    struct tm  tstruct;
    char       buf[80];
    tstruct = *localtime(&now);
    std::strftime(buf, sizeof(buf), "%Y-%m-%d.%X", &tstruct);

    return buf;
}
#endif
