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
            if(nByteWriten == -1)  { // error
                std::this_thread::yield();
                continue;
            }
            OasisControllerObj->m_DevAccessMutex.unlock();
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
            if(nbRead==-1) { // error
                std::this_thread::yield();
                continue;
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
    getConfig();
    getVersions();
    getSerial();
    getModel();
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
    int nByteWriten = 0;
    byte cHIDBuffer[REPORT_SIZE];
    int nNbTimeOut = 0;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    cHIDBuffer[0] = 0; // report ID
    cHIDBuffer[1] = CODE_GET_CONFIG; // command
    cHIDBuffer[2] = 0; // command length

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getConfig] sending : " << std::endl << hexOut << std::endl;
    m_sLogFile.flush();
#endif
    nNbTimeOut = 0;
    while(nNbTimeOut < 3) {
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

    if(nNbTimeOut>=3) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getConfig] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
    return nErr;
}

#pragma mark move commands
int COasisController::haltFocuser()
{
    int nErr = PLUGIN_OK;
    int nByteWriten = 0;
    byte cHIDBuffer[REPORT_SIZE];
    int nNbTimeOut = 0;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    if(m_Oasis_Settings.bIsMoving) {
        cHIDBuffer[0] = 0; // report ID
        cHIDBuffer[1] = CODE_CMD_STOP_MOVE; // command
        cHIDBuffer[2] = 0; // command length

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
        hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [haltFocuser] sending : " << std::endl << hexOut << std::endl;
        m_sLogFile.flush();
#endif
        nNbTimeOut = 0;
        while(nNbTimeOut < 3) {
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
    }

    if(nNbTimeOut>=3) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [haltFocuser] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }
    m_nGotoTries = MAX_GOTO_RETRY+1; // prevent goto retries

    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
    return nErr;
}

int COasisController::gotoPosition(long nPos)
{
    int nErr = PLUGIN_OK;
    int nByteWriten = 0;
    byte cHIDBuffer[REPORT_SIZE];
    int nNbTimeOut = 0;

    DeclareFrame(FrameMoveTo, frameMove, CODE_CMD_MOVE_TO);

    if(!m_bIsConnected || !m_DevHandle)
		return ERR_COMMNOLINK;

    if(m_Oasis_Settings.bIsMoving)
        return ERR_CMD_IN_PROGRESS_FOC;

    if (nPos>m_Oasis_Settings.nMaxPos)
        return ERR_LIMITSEXCEEDED;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    cHIDBuffer[0] = 0; // report ID
    cHIDBuffer[1] = CODE_CMD_MOVE_TO; // command
    cHIDBuffer[2] = 4; // command length
    frameMove.position = htonl((unsigned int)nPos);

    nNbTimeOut = 0;
    m_nTargetPos = nPos;

    #ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [gotoPosition] goto :  " << std::dec << nPos << " (0x" << std::uppercase << std::setfill('0') << std::setw(4) << std::hex << nPos <<")" << std::dec << std::endl;
        m_sLogFile.flush();
    #endif

    memcpy(cHIDBuffer, (byte*)&frameMove, sizeof(FrameMoveTo));

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
        hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [gotoPosition] sending : " << std::endl << hexOut << std::endl;
        m_sLogFile.flush();
#endif

        nNbTimeOut = 0;
        while(nNbTimeOut < 3) {
            if(m_DevAccessMutex.try_lock()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // make sure nothing else is going on.
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
            std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report

    #ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [gotoPosition] nByteWriten : " << nByteWriten << std::endl;
        m_sLogFile.flush();
    #endif
    }

    if(nNbTimeOut>=3) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [gotoPosition] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
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
    int nByteWriten = 0;
    byte cHIDBuffer[REPORT_SIZE];
    int nNbTimeOut = 0;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    cHIDBuffer[0] = 0; // report ID
    cHIDBuffer[1] = CODE_GET_VERSION; // command
    cHIDBuffer[2] = 0; // command length
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getVersions] sending : " << std::endl << hexOut << std::endl;
    m_sLogFile.flush();
#endif
    nNbTimeOut = 0;
    while(nNbTimeOut < 3) {
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

    if(nNbTimeOut>=3) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getVersions] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
    return nErr;
}

void COasisController::getVersions(std::string &sVersion)
{

}

int COasisController::getModel()
{
    int nErr = PLUGIN_OK;
    int nByteWriten = 0;
    byte cHIDBuffer[REPORT_SIZE];
    int nNbTimeOut = 0;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    cHIDBuffer[0] = 0; // report ID
    cHIDBuffer[1] = CODE_GET_PRODUCT_MODEL; // command
    cHIDBuffer[2] = 0; // command length

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getModel] sending : " << std::endl << hexOut << std::endl;
    m_sLogFile.flush();
#endif
    nNbTimeOut = 0;
    while(nNbTimeOut < 3) {
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

    if(nNbTimeOut>=3) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getModel] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
    return nErr;

}

void COasisController::getModel(std::string &sModel)
{

}

int COasisController::getSerial()
{
    int nErr = PLUGIN_OK;
    int nByteWriten = 0;
    byte cHIDBuffer[REPORT_SIZE];
    wchar_t TmpStr[128];
    int nNbTimeOut = 0;
    std::wstring ws;

    if(!m_bIsConnected || !m_DevHandle)
        return ERR_COMMNOLINK;

    memset(cHIDBuffer, 0, REPORT_SIZE);
    cHIDBuffer[0] = 0; // report ID
    cHIDBuffer[1] = CODE_GET_SERIAL_NUMBER; // command
    cHIDBuffer[2] = 0; // command length

    // I might need to use hid_get_serial_number_string(handle, wstr, MAX_STR);  as this always return '0000000' for the serial number
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSerial] sending : " << std::endl << hexOut << std::endl;
    m_sLogFile.flush();
#endif
    nNbTimeOut = 0;
    while(nNbTimeOut < 3) {
        if(m_DevAccessMutex.try_lock()) {
            nErr = hid_get_serial_number_string(m_DevHandle, TmpStr, sizeof(TmpStr)/sizeof(wchar_t) );

            // nByteWriten = hid_write(m_DevHandle, cHIDBuffer, REPORT_SIZE);
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
    hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSerial] serial : " << m_Oasis_Settings.sSerial << std::endl;
    m_sLogFile.flush();
#endif

/*
    if(nNbTimeOut>=3) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [getSerial] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
*/
    return nErr;

}

void COasisController::getSerial(std::string &sSerial)
{
    // sSerial.assign(m_Oasis_)
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


long COasisController::getPosition()
{

    if(m_Oasis_Settings.nCurPos < 0)
       return 0;
    return m_Oasis_Settings.nCurPos;
}


long COasisController::getPosLimit()
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
    int nTmp;
    char cTmp[64];
    byte nCode;
    FrameConfig *fConfig;
    FrameStatusAck *fStatus;
    FrameVersionAck *fVersions;
    FrameSerialNumber *fSerial;
    FrameProductModelAck *fModel;

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(Buffer,  nLength, hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] Buffer size " << std::dec << nLength <<", content : " << std::endl << hexOut << std::endl;
    m_sLogFile.flush();
#endif
    nCode = Buffer[0];
    switch (nCode) {
        case  CODE_GET_PRODUCT_MODEL :
            fModel = (FrameProductModelAck*)Buffer;
            m_Oasis_Settings.sModel.assign((char*)(fModel->data));
            break;

        case  CODE_GET_VERSION :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_VERSION" << std::endl;
            m_sLogFile.flush();
#endif
            fVersions = (FrameVersionAck *)Buffer;
            m_Oasis_Settings.sVersion = std::to_string((ntohl(fVersions->firmware & 0xFF000000))>>24) + "." +
            std::to_string((fVersions->firmware & 0x00FF0000)>>16) + "." +
            std::to_string((fVersions->firmware & 0x0000FF00)>>8) + "." +
            std::to_string((fVersions->firmware & 0x000000FF))+ " " + fVersions->built ;

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
            // m_Oasis_Settings.sSerial.assign((char*)(fSerial->data));
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_SERIAL_NUMBER FrameSerialNumberAck m_Oasis_Settings.sSerial = " << m_Oasis_Settings.sSerial << std::endl;
            m_sLogFile.flush();
#endif
            break;

        case  CODE_GET_FRIENDLY_NAME :
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_FRIENDLY_NAME" << std::endl;
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
            fConfig = (FrameConfig*)Buffer;
#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->mask              = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fConfig->mask) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->maxStep           = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fConfig->maxStep) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->backlash          = " << std::setfill('0') << std::setw(8) << std::hex << ntohl(fConfig->backlash) << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->backlashDirection = " << std::dec << fConfig->backlashDirection << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->reverseDirection  = " << std::dec << fConfig->reverseDirection << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->speed             = " << std::hex << fConfig->speed << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->beepOnMove        = " << std::hex << fConfig->beepOnMove << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->beepOnStartup     = " << std::hex << fConfig->beepOnStartup << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fConfig->bluetoothOn       = " << std::hex << fConfig->bluetoothOn << std::endl;
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
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_SET_CONFIG" << std::endl;
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
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fStatus->temperatureDetection = " << std::dec << fStatus->temperatureDetection << std::endl;
            m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] CODE_GET_STATUS FrameStatusAck fStatus->moving               = " << std::dec << fStatus->moving << std::endl;
            m_sLogFile.flush();
#endif

            m_Oasis_Settings.bIsMoving = (fStatus->moving==0?false:true);
            m_Oasis_Settings.nCurPos = ntohl(fStatus->position);
            m_Oasis_Settings.fInternal = ntohl(fStatus->temperatureInt) * 0.01;
            if(fStatus->temperatureDetection == 1) //external probe present
                m_Oasis_Settings.fAmbient = ntohl(fStatus->temperatureExt) * 0.0625f * 100 + 0.5;
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
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nCurPos             : " << std::dec << m_Oasis_Settings.nCurPos<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nMaxPos             : " << std::dec << m_Oasis_Settings.nMaxPos<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.bIsMoving           : " << ( m_Oasis_Settings.bIsMoving ?"Yes":"No") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.bIsReversed         : " << ( m_Oasis_Settings.bIsReversed ?"Yes":"No") << std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sVersion            : " << m_Oasis_Settings.sVersion<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sModel              : " << m_Oasis_Settings.sModel<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.sSerial             : " << m_Oasis_Settings.sSerial<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nBackstep           : " << std::dec << m_Oasis_Settings.nBackstep<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.nBacklash           : " << std::dec << m_Oasis_Settings.nCurPos<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.fInternal           : " << m_Oasis_Settings.fInternal<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.fAmbient            : " << m_Oasis_Settings.fAmbient<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.backlash            : " << std::dec << m_Oasis_Settings.backlash<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.backlashDirection   : " << m_Oasis_Settings.backlashDirection<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.speed               : " << m_Oasis_Settings.speed<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.beepOnStartup       : " << m_Oasis_Settings.beepOnStartup<< std::endl;
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [parseResponse] m_Oasis_Settings.bluetoothOn         : " << m_Oasis_Settings.bluetoothOn<< std::endl;
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

#if defined PLUGIN_DEBUG && PLUGIN_DEBUG >= 3
    hexdump(cHIDBuffer,  REPORT_SIZE, hexOut);
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [sendSettings] sending : " << std::endl << hexOut << std::endl;
    m_sLogFile.flush();
#endif

    nNbTimeOut = 0;
    while(nNbTimeOut < 3) {
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

    if(nNbTimeOut>=3) {
#ifdef PLUGIN_DEBUG
        m_sLogFile << "["<<getTimeStamp()<<"]"<< " [sendSettings] ERROR Timeout sending command : " << std::endl;
        m_sLogFile.flush();
#endif
        nErr = ERR_CMDFAILED;
    }

#ifdef PLUGIN_DEBUG
    m_sLogFile << "["<<getTimeStamp()<<"]"<< " [sendSettings] nByteWriten : " << nByteWriten << std::endl;
    m_sLogFile.flush();
#endif
    std::this_thread::sleep_for(std::chrono::milliseconds(100)); // give time to the thread to read the returned report
    return nErr;
}

int COasisController::GetNTCTemperature(int ad)
{
    if (ad <= 0)
        ad = 1;
    else if (ad >= AD_MAX)
        ad = AD_MAX - 1;

    float T = (float)(B / (log((AD_MAX - ad) / (float)ad) + B / T25) - K);
    T += (T >= 0) ? 0.005f : -0.005f;

    return (int)(T * 100);
}

int COasisController::Get32(const byte *buffer, int position)
{

    int num = 0;
    for (int i = 0; i < 4; ++i) {
        num = num << 8 | buffer[position + i];
    }
    return num;

}

int COasisController::Get16(const byte *buffer, int position)
{

    return buffer[position] << 8 | buffer[position + 1];

}

void COasisController::put32(byte *buffer, int position, int value)
{
    buffer[position    ] = byte((value>>24)  & 0xff);
    buffer[position + 1] = byte((value>>16)  & 0xff);
    buffer[position + 2] = byte((value>>8)  & 0xff);
    buffer[position + 3] = byte(value & 0xff);
}

void COasisController::put16(byte *buffer, int position, int value)
{
    buffer[position    ] = byte((value>>8)  & 0xff);
    buffer[position + 1] = byte(value & 0xff);
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
