//
//
//  Oasis.h
//  Takahashi Oasis X2 plugin
//
//  Created by Rodolphe Pineau on 2012/01/16.


#ifndef __Oasis__
#define __Oasis__

#include <string.h>

#ifdef SB_MAC_BUILD
#include <unistd.h>
#include <arpa/inet.h>
#endif
#ifdef SB_LINUX_BUILD
#include <arpa/inet.h>
#endif
#ifdef SB_WIN_BUILD
#include <winsock.h>
#include <time.h>
#endif

#include <string>
#include <vector>
#include <sstream>
#include <future>
#include <chrono>
#include <mutex>
#include <thread>
#include <iomanip>
#include <fstream>

#include "../../licensedinterfaces/sberrorx.h"

#include "hidapi.h"
#include "StopWatch.h"
#include "protocol.h"

#define PLUGIN_VERSION      1.0

#define PLUGIN_DEBUG 3

#define MAX_TIMEOUT         1000
#define REPORT_SIZE         65 // 64 byte buffer + report ID
#define MAX_GOTO_RETRY      3   // 3 retiries on goto if the focuser didn't move

#define VENDOR_ID           0x338f
#define PRODUCT_ID          0xa0f0

#define VAL_NOT_AVAILABLE   0xDEADBEEF

#define B                   3380
#define K                   273.15f
#define T25                 (K + 25)
#define AD_MAX              4095


#define MASK_MAX_STEP           0x00000001
#define MASK_BACKLASH           0x00000002
#define MASK_BACKLASH_DIRECTION 0x00000004
#define MASK_REVERSE_DIRECTION  0x00000008
#define MASK_SPEED              0x00000010
#define MASK_BEEP_ON_MOVE       0x00000020
#define MASK_BEEP_ON_STARTUP    0x00000040
#define MASK_BLUETOOTH          0x00000080
#define MASK_ALL                0xFFFFFFFF



enum Oasis_Errors    {PLUGIN_OK = 0, NOT_CONNECTED, Oasis_CANT_CONNECT, Oasis_BAD_CMD_RESPONSE, COMMAND_FAILED};
enum MotorDir       {NORMAL = 0 , REVERSE};
enum MotorStatus    {IDLE = 0, MOVING};
enum TempSources    {INTERNAL, EXTERNAL};
typedef unsigned char byte;
typedef unsigned short word;

typedef struct Oasis_setting_atom {
    std::atomic<uint32_t>   nCurPos;
    std::atomic<uint32_t>   nMaxPos;
    std::atomic<uint32_t>   bIsMoving;
    std::atomic<uint32_t>   bIsReversed;
    std::string         sVersion;
    std::string         sModel;
    std::string         sSerial;
    std::atomic<word>   nBackstep;
    std::atomic<word>   nBacklash;
    std::atomic<float>  fInternal;
    std::atomic<float>  fAmbient;

    std::atomic<uint32_t> backlash;
    std::atomic<uint8_t> backlashDirection;
    std::atomic<uint8_t> speed;
    std::atomic<uint8_t> beepOnMove;
    std::atomic<uint8_t> beepOnStartup;
    std::atomic<uint8_t> bluetoothOn;
} Oasis_Settings_Atom;


typedef struct Oasis_setting {
    std::atomic<uint32_t>   nCurPos;
    std::atomic<uint32_t>   nMaxPos;
    std::atomic<bool>   bIsMoving;
    std::atomic<bool>   bIsReversed;
    std::string         sVersion;
    std::string         sModel;
    std::string         sSerial;
    std::atomic<word>   nBackstep;
    std::atomic<word>   nBacklash;
    std::atomic<float>  fInternal;
    std::atomic<float>  fAmbient;

    std::atomic<uint32_t> backlash;
    std::atomic<uint8_t> backlashDirection;
    std::atomic<uint8_t> speed;
    std::atomic<uint8_t> beepOnMove;
    std::atomic<uint8_t> beepOnStartup;
    std::atomic<uint8_t> bluetoothOn;
} Oasis_Settings;


class COasisController
{
public:
    COasisController();
    ~COasisController();

    int         Connect();
    void        Disconnect(void);
    bool        IsConnected(void) { return m_bIsConnected; };

    int         listFocusers(std::vector<std::string> &focuserSNList);
    bool        isFocuserPresent(std::string sSerial);
    void        setFocuserSerial(std::string sSerial);
    void        setUserConf(bool bUserConf);

    // move commands
    int         haltFocuser();
    int         gotoPosition(long nPos);
    int         moveRelativeToPosision(long nSteps);

    // command complete functions
    int         isGoToComplete(bool &bComplete);

    // getter and setter
    void        getFirmwareVersion(std::string &sFirmware);
    double      getTemperature();
    double      getTemperature(int nSource);
    long        getPosition(void);
    long        getPosLimit(void);

    void        setTemperatureSource(int nSource);
    int         getTemperatureSource();

    void        getVersions(std::string &sVersion);
    void        getModel(std::string &sModel);
    void        getSerial(std::string &sSerial);

    int         setMaxStep(unsigned int nMaxStep);

    void        getBacklash(unsigned int &nBacklash);
    int         setBacklash(unsigned int nBacklash);
    void        getBacklashDirection(unsigned int &nBacklashDir);
    int         setBacklashDirection(unsigned int nBacklashDir);
    void        getReverse(bool &bReversed);
    int         setReverse(bool bReversed);
    void        getMaxSpeed(unsigned int &nSpeed);
    int         setMaxSpeed(unsigned int nSpeed);
    void        getBeepOnMove(bool &bEnabled);
    int         setBeepOnMove(bool bEnabled);
    void        getBeepOnStartup(bool &bEnabled);
    int         setBeepOnStartup(bool bEnabled);
    void        getBluetoothEnabled(bool &bEnabled);
    int         setBluetoothEnabled(bool bEnabled);
    void        getBluetoothName(std::string &sName);
    int         setBluetoothName(std::string sName);
    void        getFriendlyName(std::string &sName);
    int         setFriendlyName(std::string sName);

    void        parseResponse(byte *Buffer, int nLength);
    int         sendSettings();


    std::mutex          m_GlobalMutex;
    std::mutex          m_DevAccessMutex;

#ifdef PLUGIN_DEBUG
    void  logToFile(const std::string sLogLine);
#endif

    std::atomic<bool>   m_bNeedReconnect;
    int    m_nPosBeforeReconnect;
    bool   m_bCheckPosition;
    bool   m_bAutoFanBeforeReconnect;
    bool   m_bSetFanOnBeforeReconnect;

protected:

    void            startThreads();
    void            stopThreads();

    int             Get32(const byte *buffer, int position);
    int             Get16(const byte *buffer, int position);

    void            put32(byte *buffer, int position, int value);
    void            put16(byte *buffer, int position, int value);
    void            copyCurrentSettingsToWriteSettings();

    int         getConfig();
    int         getVersions();
    int         getModel();
    int         getSerial();
    int         GetNTCTemperature(int ad);

    std::string         m_sSerialNumber;
    bool                m_bSetUserConf;
    hid_device          *m_DevHandle;
    bool                m_bDebugLog;
    std::atomic<bool>   m_bIsConnected;

    long                m_nTargetPos;
    bool                m_bPosLimitEnabled;
    int                 m_nGotoTries;

    int                 m_nTempSource;

    // the read thread keep updating these
    Oasis_Settings_Atom m_Oasis_Settings;
    Oasis_Settings      m_Oasis_Settings_Write;

    // threads
    bool                m_ThreadsAreRunning;
    std::promise<void> *m_exitSignal;
    std::future<void>   m_futureObj;
    std::promise<void> *m_exitSignalSender;
    std::future<void>   m_futureObjSender;
    std::thread         m_th;
    std::thread         m_thSender;

    CStopWatch          m_gotoTimer;
    
#ifdef PLUGIN_DEBUG
    void    hexdump(const byte *inputData, int inputSize,  std::string &outHex);
    // timestamp for logs
    const std::string getTimeStamp();
    std::string m_hexOut;
    std::ofstream m_sLogFile;
    std::string m_sPlatform;
    std::string m_sLogfilePath;
#endif


};

#endif //__Oasis__
