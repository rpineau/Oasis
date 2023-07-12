
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
        m_OasisController.setTemperatureSource(m_pIniUtil->readInt(KEY_X2FOC_ROOT, TEMP_SOURCE, INTERNAL));
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
    str = "Takahashi Active Focuser Focuser X2 plugin by Rodolphe Pineau [DEBUG]";
#else
    str = "Takahashi Active Focuser Focuser X2 plugin by Rodolphe Pineau";
#endif
}

double X2Focuser::driverInfoVersion(void) const
{
	return PLUGIN_VERSION;
}

void X2Focuser::deviceInfoNameShort(BasicStringInterface& str) const
{
    str="Takahashi Active Focuser";
}

void X2Focuser::deviceInfoNameLong(BasicStringInterface& str) const
{
    deviceInfoNameShort(str);
}

void X2Focuser::deviceInfoDetailedDescription(BasicStringInterface& str) const
{
	str = "Takahashi Active Focuser";
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
    str="Takahashi Active Focuser";
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
        nErr = doOasisFocuserFeatureConfig();
        return nErr;
    }

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

    m_nCurrentDialog = SELECT;

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
    bool bTmp;

    if (!strcmp(pszEvent, "on_timer")) {
        if(m_bLinked) {
            sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(INTERNAL) << "ºC";
            uiex->setText("focuserTemp", sTmpBuf.str().c_str());

            std::stringstream().swap(sTmpBuf);
            sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(EXTERNAL) << "ºC";
            uiex->setText("probeTemp", sTmpBuf.str().c_str());
        }
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

    mUiEnabled = false;

    if (NULL == ui)
        return ERR_POINTER;

    if ((nErr = ui->loadUserInterface("Oasis.ui", deviceType(), m_nPrivateMulitInstanceIndex)))
        return nErr;

    if (NULL == (dx = uiutil.X2DX()))
        return ERR_POINTER;

    X2MutexLocker ml(GetMutex());
/*
    // set controls values
    if(m_bLinked) {
        sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(INTERNAL) << "ºC";
        dx->setText("focuserTemp", sTmpBuf.str().c_str());

        std::stringstream().swap(sTmpBuf);
        sTmpBuf << std::fixed << std::setprecision(2) << m_OasisController.getTemperature(EXTERNAL) << "ºC";
        dx->setText("probeTemp", sTmpBuf.str().c_str());
    }
    else {
        dx->setText("focuserTemp", "");
        dx->setText("probeTemp", "");
    }

    // This doesn't require to be connected as this is the user selection of what temperature source he wants reported to TSX
    dx->setEnabled("comboBox",true);
    nTmp = m_OasisController.getTemperatureSource();
    dx->setCurrentIndex("comboBox", nTmp);

    //Display the user interface
    mUiEnabled = true;
    if ((nErr = ui->exec(bPressedOK)))
        return nErr;
    mUiEnabled = false;

    //Retreive values from the user interface
    if (bPressedOK) {
        nTmp = dx->currentIndex("comboBox");
        m_OasisController.setTemperatureSource(nTmp);
        m_pIniUtil->writeInt(PARENT_KEY, TEMP_SOURCE, nTmp);

        if(dx->isChecked("radioButton")) {
            m_pIniUtil->writeInt(PARENT_KEY, AUTOFAN_STATE, 0);
            m_pIniUtil->writeInt(PARENT_KEY, FAN_STATE, 1);
        }
        else if(dx->isChecked("radioButton_2")){
            m_pIniUtil->writeInt(PARENT_KEY, AUTOFAN_STATE, 0);
            m_pIniUtil->writeInt(PARENT_KEY, FAN_STATE, 0);
        }
        else if(dx->isChecked("radioButton_3")) {
            m_pIniUtil->writeInt(PARENT_KEY, AUTOFAN_STATE, 1);
            m_pIniUtil->writeInt(PARENT_KEY, FAN_STATE, 0);
        }

        if(dx->isChecked("checkBox"))
            m_pIniUtil->writeInt(PARENT_KEY, RESTORE_POSITION, 1);
        else
            m_pIniUtil->writeInt(PARENT_KEY, RESTORE_POSITION, 0);

        nErr = SB_OK;
    }
 */
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




