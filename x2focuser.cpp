
#include "x2focuser.h"

X2Focuser::X2Focuser(const char* pszDisplayName,
												const int& nInstanceIndex,
												SerXInterface						* pSerXIn,
												TheSkyXFacadeForDriversInterface	* pTheSkyXIn,
												SleeperInterface					* pSleeperIn,
												BasicIniUtilInterface				* pIniUtilIn,
												LoggerInterface						* pLoggerIn,
												MutexInterface						* pIOMutexIn,
												TickCountInterface					* pTickCountIn)

{
    char szFocuserSerial[128];
    int nErr = SB_OK;

    m_nPrivateMulitInstanceIndex    = nInstanceIndex;
	m_pTheSkyXForMounts				= pTheSkyXIn;
	m_pSleeper						= pSleeperIn;
	m_pIniUtil						= pIniUtilIn;
	m_pLogger						= pLoggerIn;
	m_pIOMutex						= pIOMutexIn;
	m_pTickCount					= pTickCountIn;

	m_bLinked = false;
	m_nPosition = 0;
    m_sFocuserSerial.clear();
    // Read in settings


    // Read in settings
    if (m_pIniUtil) {
        m_pIniUtil->readString(KEY_X2FOC_ROOT, KEY_SN, "0", szFocuserSerial, 128);
        m_sFocuserSerial.assign(szFocuserSerial);
        if(m_OasisController.isFocuserPresent(m_sFocuserSerial)) {
            m_OasisController.setFocuserSerial(m_sFocuserSerial);
        }
        else
            m_sFocuserSerial.clear();
        nErr = loadFocuserSettings(m_sFocuserSerial);
    }


    if (m_pIniUtil) {
        m_OasisController.setTemperatureSource(m_pIniUtil->readInt(m_sFocuserSerial.c_str(), TEMP_SOURCE, INTERNAL));
    }
}

X2Focuser::~X2Focuser()
{
    //Delete objects used through composition
	if (GetTheSkyXFacadeForDrivers())
		delete GetTheSkyXFacadeForDrivers();
	if (GetSleeper())
		delete GetSleeper();
	if (GetSimpleIniUtil())
		delete GetSimpleIniUtil();
	if (GetLogger())
		delete GetLogger();
	if (GetMutex())
		delete GetMutex();
}

#pragma mark - DriverRootInterface

int	X2Focuser::queryAbstraction(const char* pszName, void** ppVal)
{
    *ppVal = NULL;

    if (!strcmp(pszName, LinkInterface_Name))
        *ppVal = (LinkInterface*)this;

    else if (!strcmp(pszName, FocuserGotoInterface2_Name))
        *ppVal = (FocuserGotoInterface2*)this;

    else if (!strcmp(pszName, ModalSettingsDialogInterface_Name))
        *ppVal = dynamic_cast<ModalSettingsDialogInterface*>(this);

    else if (!strcmp(pszName, X2GUIEventInterface_Name))
        *ppVal = dynamic_cast<X2GUIEventInterface*>(this);

    else if (!strcmp(pszName, FocuserTemperatureInterface_Name))
        *ppVal = dynamic_cast<FocuserTemperatureInterface*>(this);

    else if (!strcmp(pszName, ModalSettingsDialogInterface_Name))
        *ppVal = dynamic_cast<ModalSettingsDialogInterface*>(this);


    return SB_OK;
}

#pragma mark - DriverInfoInterface
void X2Focuser::driverInfoDetailedInfo(BasicStringInterface& str) const
{
#ifdef PLUGIN_DEBUG
    str = "Astroasis Oasis Focuser X2 plugin by Rodolphe Pineau [DEBUG]";
#else
    str = "Astroasis Oasis Focuser X2 plugin by Rodolphe Pineau";
#endif
}

double X2Focuser::driverInfoVersion(void) const
{
	return PLUGIN_VERSION;
}

void X2Focuser::deviceInfoNameShort(BasicStringInterface& str) const
{
    str="Astroasis Oasis";
}

void X2Focuser::deviceInfoNameLong(BasicStringInterface& str) const
{
    deviceInfoNameShort(str);
}

void X2Focuser::deviceInfoDetailedDescription(BasicStringInterface& str) const
{
	str = "Astroasis Oasis";
}

void X2Focuser::deviceInfoFirmwareVersion(BasicStringInterface& str)
{
    if(!m_bLinked) {
        str="NA";
    }
    else {
        X2MutexLocker ml(GetMutex());
        // get firmware version
        std::string sFirmware;
        m_OasisController.getFirmwareVersion(sFirmware);
        str = sFirmware.c_str();
    }
}

void X2Focuser::deviceInfoModel(BasicStringInterface& str)
{
    std::string sModel;
    str="Astroasis Oasis";
    if(m_bLinked) {
        m_OasisController.getModel(sModel);
        str = sModel.c_str();
    }
}

#pragma mark - LinkInterface
int	X2Focuser::establishLink(void)
{
    int nErr;

    X2MutexLocker ml(GetMutex());
    // get serial port device name
    nErr = m_OasisController.Connect();
    if(nErr)
        m_bLinked = false;
    else
        m_bLinked = true;
    if(nErr)
        nErr = ERR_NOLINK;
    return nErr;
}

int	X2Focuser::terminateLink(void)
{
    if(!m_bLinked)
        return SB_OK;

    X2MutexLocker ml(GetMutex());
    m_OasisController.Disconnect();
    m_bLinked = false;

	return SB_OK;
}

bool X2Focuser::isLinked(void) const
{
	return m_bLinked;
}

#pragma mark - ModalSettingsDialogInterface
int	X2Focuser::initModalSettingsDialog(void)
{
    return SB_OK;
}

int	X2Focuser::execModalSettingsDialog(void)
{
    int nErr = SB_OK;
    bool bPressedOK = false;
    std::vector<std::string> focuserSNList;
    std::stringstream ssTmp;
    bool bFocFound = false;
    int nFocIndex = 0;
    int i;
    if(m_bLinked) {
        m_nCurrentDialog = SETTINGS;

        nErr = doOasisFocuserFeatureConfig();
        return nErr;
    }
    m_nCurrentDialog = SELECT;

    X2GUIExchangeInterface* dx = NULL;//Comes after ui is loaded
    X2ModalUIUtil           uiutil(this, GetTheSkyXFacadeForDrivers());
    X2GUIInterface*         ui = uiutil.X2UI();

    if (NULL == ui)
        return ERR_POINTER;

    nErr = ui->loadUserInterface("OasisFocuserSelect.ui", deviceType(), m_nPrivateMulitInstanceIndex);
    if (nErr)
        return nErr;

    if (NULL == (dx = uiutil.X2DX()))
        return ERR_POINTER;

    //Intialize the user interface
    m_OasisController.listFocusers(focuserSNList);
    if(!focuserSNList.size()) {
        dx->comboBoxAppendString("comboBox","No Focuser found");
        dx->setCurrentIndex("comboBox",0);
    }
    else {
        bFocFound = true;
        nFocIndex = 0;
        for(i = 0; i < focuserSNList.size(); i++) {
            //Populate the camera combo box and set the current index (selection)
            ssTmp <<  " Oasis Focuser [" << focuserSNList[i]<< "]";
            dx->comboBoxAppendString("comboBox",ssTmp.str().c_str());
            if(focuserSNList[i] == m_sFocuserSerial)
                nFocIndex = i;
            std::stringstream().swap(ssTmp);
        }
        dx->setCurrentIndex("comboBox",nFocIndex);
    }


    //Display the user interface
    if ((nErr = ui->exec(bPressedOK)))
        return nErr;

    //Retreive values from the user interface
    if (bPressedOK)
    {
        if(bFocFound) {
            int nFocuser;
            std::string sCameraSerial;
            //Camera
            nFocuser = dx->currentIndex("comboBox");
            m_OasisController.setFocuserSerial(focuserSNList[nFocuser]);
            m_sFocuserSerial.assign(focuserSNList[nFocuser]);
            // store camera ID
            m_pIniUtil->writeString(KEY_X2FOC_ROOT, KEY_SN, m_sFocuserSerial.c_str());
            loadFocuserSettings(m_sFocuserSerial);
        }
    }

    return nErr;


}

void X2Focuser::uiEvent(X2GUIExchangeInterface* uiex, const char* pszEvent)
{
    std::stringstream sTmpBuf;
    int nTmp;

    switch(m_nCurrentDialog) {
        case  SELECT:
            break;
        case SETTINGS:
            if (!strcmp(pszEvent, "on_timer")) {
                if(m_bLinked) {
                    sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(INTERNAL) << " ºC";
                    uiex->setText("focuserTemp", sTmpBuf.str().c_str());
                    if(m_OasisController.isExternalSensorPresent()) {
                        std::stringstream().swap(sTmpBuf);
                        sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(EXTERNAL) << " ºC";
                        uiex->setText("probeTemp", sTmpBuf.str().c_str());
                    }
                }
            }
            else if (!strcmp(pszEvent, "on_pushButton_2_clicked")) {
                uiex->propertyInt("newPos", "value", nTmp);
                m_OasisController.setPosition((unsigned int)nTmp);
            }
            else if (!strcmp(pszEvent, "on_pushButton_3_clicked")) {
                uiex->propertyInt("posLimit", "value", nTmp);
                m_OasisController.setMaxStep((unsigned int)nTmp);
            }
            break;
        default:
            break;
    }
}


int X2Focuser::doOasisFocuserFeatureConfig()
{
    int nErr = SB_OK;
    X2ModalUIUtil uiutil(this, GetTheSkyXFacadeForDrivers());
    X2GUIInterface*                    ui = uiutil.X2UI();
    X2GUIExchangeInterface*            dx = NULL;//Comes after ui is loaded
    bool bPressedOK = false;
    std::stringstream sTmpBuf;
    int nTmp;
    std::string sFriendlyName;
    std::string sBluetoothName;
    char szTmpBuf[FRAME_NAME_LEN+1];

    mUiEnabled = false;

    if (NULL == ui)
        return ERR_POINTER;

    if ((nErr = ui->loadUserInterface("Oasis.ui", deviceType(), m_nPrivateMulitInstanceIndex)))
        return nErr;

    if (NULL == (dx = uiutil.X2DX()))
        return ERR_POINTER;

    X2MutexLocker ml(GetMutex());

    // set controls values
    if(m_bLinked) {
        if(m_OasisController.isExternalSensorPresent()) {
            dx->comboBoxAppendString("comboBox", "Internal");
            dx->comboBoxAppendString("comboBox", "External");
            nTmp = m_OasisController.getTemperatureSource();
            dx->setCurrentIndex("comboBox", nTmp==INTERNAL?0:1);
            sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(INTERNAL) << " ºC";
            dx->setText("internalTemp", sTmpBuf.str().c_str());

            std::stringstream().swap(sTmpBuf);
            sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(EXTERNAL) << " ºC";
            dx->setText("probeTemp", sTmpBuf.str().c_str());
        }
        else {
            dx->comboBoxAppendString("comboBox", "Internal");
            dx->setCurrentIndex("comboBox", 0);
            sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(INTERNAL) << " ºC";
            dx->setText("internalTemp", sTmpBuf.str().c_str());
            dx->setText("probeTemp", "Not present");
            dx->setEnabled("comboBox", false);
        }
        dx->setPropertyInt("newPos", "value", m_OasisController.getPosition());
        dx->setPropertyInt("posLimit", "value", m_OasisController.getPosLimit());
        dx->setChecked("reverseDir", m_OasisController.getReverse());
        dx->setPropertyInt("backlashSteps", "value", m_OasisController.getBacklash());
        dx->setChecked("radioButton", m_OasisController.getBacklashDirection() == 0?true:false);
        dx->setChecked("beepOnConnect", m_OasisController.getBeepOnStartup()?1:0);
        dx->setChecked("beepOnMove", m_OasisController.getBeepOnMove()?1:0);
        dx->setChecked("bluetoothEnable", m_OasisController.getBluetoothEnabled()?1:0);
        m_OasisController.getBluetoothName(sBluetoothName);
        dx->setText("bluetoothName", sBluetoothName.c_str());
        m_OasisController.getFriendlyName(sFriendlyName);
        dx->setText("friendlyName", sFriendlyName.c_str());
    }
    else {
        dx->setEnabled("comboBox", false);
        dx->setText("focuserTemp", "");
        dx->setText("probeTemp", "");
        dx->setEnabled("newPos", false);
        dx->setEnabled("posLimit", false);
        dx->setEnabled("reverseDir", false);
        dx->setEnabled("backlashSteps", false);
        dx->setEnabled("radioButton", false);
        dx->setEnabled("radioButton_2", false);
        dx->setEnabled("beepOnConnect", false);
        dx->setEnabled("beepOnMove", false);
        dx->setEnabled("bluetoothEnable", false);
        dx->setEnabled("bluetoothName", false);
        dx->setEnabled("friendlyName", false);
    }

    //Display the user interface
    mUiEnabled = true;
    if ((nErr = ui->exec(bPressedOK)))
        return nErr;
    mUiEnabled = false;

    //Retreive values from the user interface
    if (bPressedOK) {
        nTmp = dx->currentIndex("comboBox");
        m_OasisController.setTemperatureSource(nTmp==0?INTERNAL:EXTERNAL);
        m_pIniUtil->writeInt(m_sFocuserSerial.c_str(), TEMP_SOURCE, nTmp==0?INTERNAL:EXTERNAL);

        nErr = m_OasisController.setReverse(dx->isChecked("reverseDir")==1);
        if(nErr) // retry
            nErr = m_OasisController.setReverse(dx->isChecked("reverseDir")==1);
        dx->propertyInt("backlashSteps", "value", nTmp);

        nErr = m_OasisController.setBacklash(nTmp);
        if(nErr) // retry
            nErr = m_OasisController.setBacklash(nTmp);

        nErr = m_OasisController.setBacklashDirection( dx->isChecked("radioButton")==1?0:1 );
        if(nErr) // retry
            nErr = m_OasisController.setBacklashDirection( dx->isChecked("radioButton")==1?0:1 );

        nErr = m_OasisController.setBeepOnStartup( dx->isChecked("beepOnConnect")==1?true:false );
        if(nErr) // retry
            nErr = m_OasisController.setBeepOnStartup( dx->isChecked("beepOnConnect")==1?true:false );

        nErr = m_OasisController.setBeepOnStartup( dx->isChecked("beepOnConnect")==1?true:false );
        if(nErr) // retry
            nErr = m_OasisController.setBeepOnStartup( dx->isChecked("beepOnConnect")==1?true:false );

        nErr = m_OasisController.setBeepOnMove( dx->isChecked("beepOnMove")==1?true:false );
        if(nErr) // retry
            nErr = m_OasisController.setBeepOnMove( dx->isChecked("beepOnMove")==1?true:false );

        nErr = m_OasisController.setBluetoothEnabled( dx->isChecked("bluetoothEnable")==1?true:false );
        if(nErr) // retry
            nErr = m_OasisController.setBluetoothEnabled( dx->isChecked("bluetoothEnable")==1?true:false );

        memset(szTmpBuf,0,FRAME_NAME_LEN+1);
        dx->propertyString("bluetoothName", "text", szTmpBuf, FRAME_NAME_LEN);
        nErr = m_OasisController.setBluetoothName(std::string(szTmpBuf));
        if(nErr) // retry
            nErr = m_OasisController.setBluetoothName(std::string(szTmpBuf));

        memset(szTmpBuf,0,FRAME_NAME_LEN+1);
        dx->propertyString("friendlyName", "text", szTmpBuf, FRAME_NAME_LEN);
        nErr = m_OasisController.setFriendlyName(std::string(szTmpBuf));
        if(nErr) // retry
            nErr = m_OasisController.setBluetoothName(std::string(szTmpBuf));

        nErr = m_OasisController.getConfig();
        nErr = m_OasisController.getBluetoothName();
        nErr = m_OasisController.getFriendlyName();
        nErr = SB_OK;
    }
    return nErr;
}

int X2Focuser::loadFocuserSettings(std::string sSerial)
{
    int nErr = PLUGIN_OK;
    int nValue;
    if(!sSerial.size())
        return nErr;

    nValue = m_pIniUtil->readInt(sSerial.c_str(), TEMP_SOURCE, VAL_NOT_AVAILABLE);
    if(nValue!=VAL_NOT_AVAILABLE)
        m_OasisController.setTemperatureSource(nValue);
    else {
        m_OasisController.setUserConf(false); // better not set any bad value and read defaults from camera
        return VAL_NOT_AVAILABLE;
    }

    m_OasisController.setUserConf(true);
    return nErr;


    return nErr;
}

#pragma mark - FocuserGotoInterface2
int	X2Focuser::focPosition(int& nPosition)
{
    if(!m_bLinked)
        return NOT_CONNECTED;

    X2MutexLocker ml(GetMutex());

    nPosition = (int)m_OasisController.getPosition();
    m_nPosition = nPosition;
    return SB_OK;
}

int	X2Focuser::focMinimumLimit(int& nMinLimit)
{
    nMinLimit = 0;
    return SB_OK;
}

int	X2Focuser::focMaximumLimit(int& nPosLimit)
{

    if(!m_bLinked)
        return NOT_CONNECTED;

    nPosLimit = (int)m_OasisController.getPosLimit();
	return SB_OK;
}

int	X2Focuser::focAbort()
{   int nErr;

    if(!m_bLinked)
        return NOT_CONNECTED;

    X2MutexLocker ml(GetMutex());
    nErr = m_OasisController.haltFocuser();
    return nErr;
}

int	X2Focuser::startFocGoto(const int& nRelativeOffset)
{
    if(!m_bLinked)
        return NOT_CONNECTED;

    X2MutexLocker ml(GetMutex());
    m_OasisController.moveRelativeToPosision(nRelativeOffset);
    return SB_OK;
}

int	X2Focuser::isCompleteFocGoto(bool& bComplete) const
{
    int nErr;

    if(!m_bLinked)
        return NOT_CONNECTED;

    X2Focuser* pMe = (X2Focuser*)this;
    X2MutexLocker ml(pMe->GetMutex());
	nErr = pMe->m_OasisController.isGoToComplete(bComplete);

    return nErr;
}

int	X2Focuser::endFocGoto(void)
{
    if(!m_bLinked)
        return NOT_CONNECTED;

    X2MutexLocker ml(GetMutex());
    m_nPosition = (int)m_OasisController.getPosition();
    return SB_OK;
}

int X2Focuser::amountCountFocGoto(void) const
{
	return 9;
}

int	X2Focuser::amountNameFromIndexFocGoto(const int& nZeroBasedIndex, BasicStringInterface& strDisplayName, int& nAmount)
{
	switch (nZeroBasedIndex)
	{
		default:
		case 0: strDisplayName="10 steps"; nAmount=10;break;
        case 1: strDisplayName="50 steps"; nAmount=10;break;
		case 2: strDisplayName="100 steps"; nAmount=100;break;
        case 3: strDisplayName="250 steps"; nAmount=100;break;
        case 4: strDisplayName="500 steps"; nAmount=100;break;
		case 5: strDisplayName="1000 steps"; nAmount=1000;break;
        case 6: strDisplayName="2500 steps"; nAmount=1000;break;
        case 7: strDisplayName="5000 steps"; nAmount=1000;break;
        case 8: strDisplayName="10000 steps"; nAmount=10000;break;
	}
	return SB_OK;
}

int	X2Focuser::amountIndexFocGoto(void)
{
	return 0;
}

#pragma mark - FocuserTemperatureInterface
int X2Focuser::focTemperature(double &dTemperature)
{
    int nErr = SB_OK;

    if(!m_bLinked) {
        dTemperature = -100.0;
        return NOT_CONNECTED;
    }
    X2MutexLocker ml(GetMutex());

    dTemperature = m_OasisController.getTemperature();


    return nErr;
}




