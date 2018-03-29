#include "_global.h"
#include "delay.h"
#include "drv_CC1101.h"
#include "MyMessage.h"
#include "xliNodeConfig.h"
#include "ProtocolParser.h"
#include "LightPwmDrv.h"
#include "timer_4.h"
#include "color_factory.h"
#include "Uart2Dev.h"
#include "FlashDataStorage.h"

/*
License: MIT

Auther: Baoshi Sun
Email: bs.sun@datatellit.com, bs.sun@uwaterloo.ca
Github: https://github.com/sunbaoshi1975
Please visit xlight.ca for product details

RF24L01 connector pinout:
GND    VCC
CE     CSN
SCK    MOSI
MISO   IRQ

Connections:
  PC3 -> CE
  PC4 -> CSN
  PC7 -> MISO
  PC6 -> MOSI
  PC5 -> SCK
  PC2 -> IRQ

*/
#ifdef TEST
void testio()
{
  GPIO_Init(GPIOB , GPIO_PIN_5 , GPIO_MODE_OUT_PP_LOW_SLOW);
  GPIO_Init(GPIOB , GPIO_PIN_4 , GPIO_MODE_OUT_PP_LOW_SLOW);
  GPIO_Init(GPIOB , GPIO_PIN_3 , GPIO_MODE_OUT_PP_LOW_SLOW);
  GPIO_Init(GPIOB , GPIO_PIN_2 , GPIO_MODE_OUT_PP_LOW_SLOW);
  GPIO_Init(GPIOB , GPIO_PIN_1 , GPIO_MODE_OUT_PP_LOW_SLOW);
  GPIO_Init(GPIOB , GPIO_PIN_0 , GPIO_MODE_OUT_PP_LOW_SLOW);
  GPIO_Init(GPIOD , GPIO_PIN_1 , GPIO_MODE_OUT_PP_LOW_SLOW);
  GPIO_Init(GPIOD , GPIO_PIN_7 , GPIO_MODE_OUT_PP_LOW_SLOW);
}
#endif

// Choose Product Name & Type
/// Sunny
#if defined(XSUNNY)
#define XLA_PRODUCT_NAME          "XSunny"
#define XLA_PRODUCT_Type          devtypWSquare60
//#define XLA_PRODUCT_Type          devtypWBlackboard
#endif
/// Rainbow
#if defined(XRAINBOW)
#define XLA_PRODUCT_NAME          "XRainbow"
#define XLA_PRODUCT_Type          devtypCRing3
#endif
/// Mirage
#if defined(XMIRAGE)
#define XLA_PRODUCT_NAME          "XMirage"
#define XLA_PRODUCT_Type          devtypMRing3
#endif

// RF channel for the sensor net, 0-255
#define RF_CHANNEL	   		100
#define RF_ADDRESS                      5

// Window Watchdog
// Uncomment this line if in debug mode
#define DEBUG_NO_WWDG
#define WWDG_COUNTER                    0x7f
#define WWDG_WINDOW                     0x77

#define NO_RESTART_MODE

// System Startup Status
#define SYS_INIT                        0
#define SYS_RESET                       1
#define SYS_WAIT_NODEID                 2
#define SYS_WAIT_PRESENTED              3
#define SYS_RUNNING                     5

// Delay
//#define DELAY_5_ms                      0x1FF           // ticks, about 5ms
//#define DELAY_25_ms                     0xAFF           // ticks, about 25ms
//#define DELAY_120_ms                    0x2FFF          // ticks, about 120ms
//#define DELAY_800_ms                   0x1FFFF          // ticks, about 800ms
#define DELAY_5_ms                      1           // tim4, 5ms intrupt
#define DELAY_10_ms                     2           // tim4, 5ms intrupt
#define DELAY_25_ms                     5          // tim4, 5ms intrupt
#define DELAY_120_ms                    24         // tim4, 5ms intrupt
#define DELAY_300_ms                    119        // tim4, 5ms intrupt
#define DELAY_500_ms                    199        // tim4, 5ms intrupt
#define DELAY_800_ms                    319        // tim4, 5ms intrupt

// For Gu'an Demo Classroom
#define ONOFF_RESET_TIMES               10     // on / off times to reset device, regular value is 3

#define RAPID_PRESENTATION                     // Don't wait for presentation-ack
#define REGISTER_RESET_TIMES            30     // default 5, super large value for show only to avoid ID mess


#define SUNNY_SWITCH_INTERVAL           360000*4  //(3600*4*100 * 10ms) 4Hour
#define SUNNY_RUNNING_MAXTIME           360000*1  //1hours for max continuous running
#define MAINLOOP_TIMEOUT                6000      //60s for mainloop timeout
uint32_t gAgingRunningTimeTick=0;
uint32_t gRunningTimeTick=0;
uint16_t gMainloopTimeTick=0;
// Uncomment this line to enable CCT brightness quadratic function
//#define CCT_BR_QUADRATIC_FUNC

// Unique ID
#if defined(STM8S105) || defined(STM8S005) || defined(STM8AF626x)
  #define     UNIQUE_ID_ADDRESS         (0x48CD)
#endif
#if defined(STM8S103) || defined(STM8S003) ||  defined(STM8S903)
  #define     UNIQUE_ID_ADDRESS         (0x4865)
#endif

// Public variables
Config_t gConfig;
MyMessage_t sndMsg, rcvMsg;
uint8_t *psndMsg = (uint8_t *)&sndMsg;
uint8_t *prcvMsg = (uint8_t *)&rcvMsg;
bool gIsConfigChanged = FALSE;
bool gNeedSaveBackup = FALSE;
bool gIsStatusChanged = FALSE;
bool gResetRF = FALSE;
bool gResetNode = FALSE;
uint8_t _uniqueID[UNIQUE_ID_LEN];

// Moudle variables
uint8_t mStatus = SYS_INIT;
bool mGotNodeID = FALSE;
uint8_t mutex = 0;
uint16_t pwm_Warm = 0;
uint16_t pwm_Cold = 0;

// Keep Alive Timer
uint16_t mTimerKeepAlive = 0;
uint8_t m_cntRFSendFailed = 0;

int32_t offdelaytick = -1;
uint8_t feedingDog = 0;

#ifdef EN_SENSOR_ALS
   uint16_t als_tick = 0;
#endif

#ifdef EN_SENSOR_PIR
   uint16_t pir_tick = 0;
#endif
   
#ifdef EN_SENSOR_PM25       
   uint16_t pm25_tick = 0;
#endif 
   
///////////////////////////////////////////////////////////////////////////////////////////////
uint8_t tx_addr = 0;
///////////////////////////////////////////////////////////////////////////////////////////////   

// Delayed operation in function idleProcess()
typedef void (*OnTick_t)(uint32_t, uint8_t);  // Operation callback function typedef
// func bits
uint8_t delay_func = 0x00;
bool delay_up[DELAY_TIMERS];
uint32_t delay_from[DELAY_TIMERS];
uint32_t delay_to[DELAY_TIMERS];
uint32_t delay_step[DELAY_TIMERS];
uint32_t delay_tick[DELAY_TIMERS];
uint32_t delay_timer[DELAY_TIMERS];
OnTick_t delay_handler[DELAY_TIMERS];
uint8_t delay_tag[DELAY_TIMERS];

// Initialize Window Watchdog
void wwdg_init() {
#ifndef DEBUG_NO_WWDG  
  WWDG_Init(WWDG_COUNTER, WWDG_WINDOW);
#endif  
}

// Feed the Window Watchdog
void feed_wwdg(void) {
#ifndef DEBUG_NO_WWDG  
  if(feedingDog == 1)
  {
    //printlog("isfeeding");
    return;
  }
  else
  {
    feedingDog = 1;
    uint8_t cntValue = WWDG_GetCounter() & WWDG_COUNTER;
    if( cntValue < WWDG_WINDOW ) {
      WWDG_SetCounter(WWDG_COUNTER);
    }
    feedingDog = 0;
  }
#endif  
  gMainloopTimeTick = 0;
}

uint8_t *Read_UniqueID(uint8_t *UniqueID, uint16_t Length)  
{
  Flash_ReadBuf(UNIQUE_ID_ADDRESS, UniqueID, Length);
  return UniqueID;
}

bool isIdentityEmpty(const UC *pId, UC nLen)
{
  for( int i = 0; i < nLen; i++ ) { if(pId[i] > 0) return FALSE; }
  return TRUE;
}

bool isIdentityEqual(const UC *pId1, const UC *pId2, UC nLen)
{
  for( int i = 0; i < nLen; i++ ) { if(pId1[i] != pId2[i]) return FALSE; }
  return TRUE;
}

bool isNodeIdRequired()
{
  return FALSE;
}

bool isNodeIdInvalid()
{
  return( (IS_NOT_DEVICE_NODEID(gConfig.nodeID) && !IS_GROUP_NODEID(gConfig.nodeID)) );
}

// Initialize Node Address and look forward to being assigned with a valid NodeID by the SmartController
void InitNodeAddress() {
  // Whether has preset node id
  if( IS_NOT_DEVICE_NODEID(gConfig.nodeID) && !IS_GROUP_NODEID(gConfig.nodeID) ) {
    gConfig.nodeID = BASESERVICE_ADDRESS; // NODEID_MAINDEVICE; BASESERVICE_ADDRESS; NODEID_DUMMY
  }
}

void UpdateNodeAddress(uint8_t _tx) {
  // TODO
}

// reset rf
void ResetRFModule()
{
  if(gResetRF)
  {
    CC1101_Init(gConfig.rfChannel,gConfig.rfAddr);
    CC1101_EnableIRQ();
    UpdateNodeAddress(NODEID_GATEWAY);
    gResetRF=FALSE;
  }
  if(gResetNode)
  {
    ResetNodeToRegister();
    gResetNode=FALSE;
  }
}

bool IsEntireConfigInvalid() {
  return( gConfig.version > XLA_VERSION || gConfig.version < XLA_MIN_VER_REQUIREMENT 
       || DEVST_Bright > 100 || gConfig.nodeID == 0
       );
}

bool IsStatusInvalid() {
#ifdef XSUNNY
  return( gConfig.version > XLA_VERSION || gConfig.version < XLA_MIN_VER_REQUIREMENT
       || DEVST_WarmCold < CT_MIN_VALUE || DEVST_WarmCold > CT_MAX_VALUE || DEVST_Bright > 100);
#else
  return( gConfig.version > XLA_VERSION || gConfig.version < XLA_MIN_VER_REQUIREMENT
       || DEVST_Bright > 100);
#endif
}

bool IsConfigInvalid() {
  return(gConfig.version > XLA_VERSION || gConfig.version < XLA_MIN_VER_REQUIREMENT || gConfig.nodeID == 0 
         );
}


// Save config to Flash
void SaveBackupConfig()
{
  if( gNeedSaveBackup ) {
    // back config FLASH
    if(Flash_WriteDataBlock(BACKUP_CONFIG_BLOCK_NUM, (uint8_t *)&gConfig, sizeof(gConfig)))
    {
      gNeedSaveBackup = FALSE;
    }
  }
}

// Save status to Flash
void SaveStatusData()
{
  // status data contain nodeid subid and networkid
  if(gIsStatusChanged)
  {
    gNeedSaveBackup = TRUE;
    uint8_t pData[50] = {0};
    uint16_t nLen = (uint8_t *)(&gConfig.rfChannel) - (uint8_t *)(&gConfig);
    memcpy(pData, (uint8_t *)&gConfig, nLen);
    if(Flash_WriteDataBlock(STATUS_DATA_NUM, pData, nLen))
    {
      gIsStatusChanged = FALSE;
    }
  }
}

// Save config data to Flash(can't be called at working time)
void SaveConfig()
{
  if( gIsConfigChanged ) {
    gNeedSaveBackup = TRUE;
    // Overwrite entire config FLASH 
    uint8_t Attmpts = 0;
    while(++Attmpts <= 3) {
      if(Flash_WriteDataBlock(0, (uint8_t *)&gConfig, sizeof(gConfig)))
      {
        gIsConfigChanged = FALSE;
        break;
      }
    }
  }
  if(!gIsConfigChanged)
  { // ensure rf info is up to date
    ResetRFModule();
  }
  SaveStatusData();
}

// Load config from Flash
void LoadConfig()
{
  // Load the config area
  Flash_ReadBuf(FLASH_DATA_START_PHYSICAL_ADDRESS, (uint8_t *)&gConfig, sizeof(gConfig));
  /*printnum(gConfig.version);
  printnum(gConfig.swTimes);
  printnum(gConfig.cntRFReset);
  printnum(gConfig.nodeID);
  printnum(gConfig.rfChannel);
  printlog("\n");*/
  uint16_t nStatusLen = (uint8_t *)(&gConfig.nodeID) - (uint8_t *)(&gConfig);
  if( IsConfigInvalid() ) {
    // If config isn't OK, then try to load config from backup area
    Flash_ReadBuf(BACKUP_CONFIG_ADDRESS+nStatusLen, (uint8_t *)&gConfig.nodeID, sizeof(gConfig)-nStatusLen);
    bool backupInvalid = IsConfigInvalid();
    InitNodeAddress();
    if( backupInvalid ) {
      // If neither valid, then initialize config with default settings
      gConfig.version = XLA_VERSION;
      gConfig.nodeID = BASESERVICE_ADDRESS;
      InitNodeAddress();
      gConfig.type = XLA_PRODUCT_Type;
      gConfig.rfChannel = RF_CHANNEL;
      gConfig.rfAddr = RF_ADDRESS;
      //gConfig.rfPowerLevel = RF24_PA_MAX;
      //gConfig.rfDataRate = RF24_250KBPS;      
      gConfig.hasSiblingMCU = 0;
      gConfig.rptTimes = 1;
      gConfig.wattOption = WATT_RM_NO_RESTRICTION;
#ifdef ENABLE_SDTM
      gConfig.enAutoPowerTest = 0;
#else
      // default enable auto power test mode
      gConfig.enAutoPowerTest = 1;
#endif
      gConfig.senMap = 0;
#ifdef EN_SENSOR_ALS
      gConfig.senMap |= sensorALS;
#endif
#ifdef EN_SENSOR_PIR
      gConfig.senMap |= sensorPIR;
#endif
#ifdef EN_SENSOR_DHT
      gConfig.senMap |= sensorDHT;
#endif
#ifdef EN_SENSOR_PM25
      gConfig.senMap |= sensorDUST;
#endif
      
      gConfig.funcMap = 0;
      gConfig.alsLevel[0] = 70;
      gConfig.alsLevel[1] = 80;
      gConfig.pirLevel[0] = 0;
      gConfig.pirLevel[1] = 0; 
      
      gConfig.ring[0].State = 1;
      gConfig.ring[0].BR = DEFAULT_BRIGHTNESS;
#ifdef XSUNNY
      gConfig.ring[0].CCT = CT_MIN_VALUE;
#else
      gConfig.ring[0].CCT = 0;
      gConfig.ring[0].R = 128;
      gConfig.ring[0].G = 64;
      gConfig.ring[0].B = 100;
#endif      
      gConfig.ring[1] = gConfig.ring[0];
      gConfig.ring[2] = gConfig.ring[0];
    }
    gIsConfigChanged = TRUE;
    SaveConfig();
  } else {
    uint8_t bytVersion;
    Flash_ReadBuf(BACKUP_CONFIG_ADDRESS, (uint8_t *)&bytVersion, sizeof(bytVersion));
    if( bytVersion != gConfig.version ) gNeedSaveBackup = TRUE;
  }
  // Load the most recent status from FLASH
  uint8_t pData[50];
  memset(pData,0x00,sizeof(pData));
  uint16_t nLen = (uint8_t *)(&gConfig.rfChannel) - (uint8_t *)(&gConfig);
  Flash_ReadBuf(STATUS_DATA_ADDRESS, pData, nLen);
  if(pData[0] >= XLA_MIN_VER_REQUIREMENT && pData[0] <= XLA_VERSION)
  { // status data valid    
    memcpy(&gConfig,pData,nStatusLen);
    /*if(isIdentityEqual(gConfig.NetworkID, RF24_BASE_RADIO_ID, ADDRESS_WIDTH) && !isNodeIdInvalid() )
    { // valid nodeid but with default network config,can covered by status data or back data if they are valid
      uint16_t networkOffset = (uint8_t *)(&gConfig.NetworkID) - (uint8_t *)(&gConfig);
      if( !isIdentityEmpty(pData+networkOffset,sizeof(gConfig.NetworkID)) )
      {
        memcpy(gConfig.NetworkID,pData+networkOffset,sizeof(gConfig.NetworkID));
      } 
    } */
  }
  else
  { // load backup area for status data
    Flash_ReadBuf(BACKUP_CONFIG_ADDRESS, pData, nLen);
    if(pData[0] >= XLA_MIN_VER_REQUIREMENT && pData[0] <= XLA_VERSION)
    { // status data valid 
      memcpy(&gConfig,pData,nStatusLen);
      /*if(isIdentityEqual(gConfig.NetworkID, RF24_BASE_RADIO_ID, ADDRESS_WIDTH) && !isNodeIdInvalid())
      { // valid nodeid but with default network config,can covered by status data or back data if they are valid
        uint16_t networkOffset = (uint8_t *)(&gConfig.NetworkID) - (uint8_t *)(&gConfig);
        if( !isIdentityEmpty(pData+networkOffset,sizeof(gConfig.NetworkID)) )
        {
          memcpy(gConfig.NetworkID,pData+networkOffset,sizeof(gConfig.NetworkID));
        }        
      }*/
    }
  }
 
  if(IsStatusInvalid())
  {
    // default status value
    gConfig.version = XLA_VERSION;
    gConfig.ring[0].State = 1;
    gConfig.ring[0].BR = DEFAULT_BRIGHTNESS;
#ifdef XSUNNY
    gConfig.ring[0].CCT = CT_MIN_VALUE;
#else
    gConfig.ring[0].CCT = 0;
    gConfig.ring[0].R = 128;
    gConfig.ring[0].G = 64;
    gConfig.ring[0].B = 100;
#endif      
    gConfig.ring[1] = gConfig.ring[0];
    gConfig.ring[2] = gConfig.ring[0];
  }
  
  // Engineering Code
  if(gConfig.type == devtypWBlackboard)
  {
#ifndef ENABLE_SDTM
    gConfig.nodeID = 1;
    gConfig.subID = 1;
#endif
  }
  if(gConfig.rptTimes == 0 ) gConfig.rptTimes = 2;
}

void ResetNodeToRegister()
{
    mStatus = SYS_RESET;
    InitNodeAddress();
}

bool WaitMutex(uint32_t _timeout) {
  while(_timeout--) {
    if( mutex > 0 ) return TRUE;
    feed_wwdg();
  }
  return FALSE;
}

bool NeedUpdateRFAddress(uint8_t _dest) {
  bool rc = FALSE;
  if( sndMsg.header.destination == NODEID_RF_SCANNER && tx_addr != NODEID_RF_SCANNER ) {
    UpdateNodeAddress(NODEID_RF_SCANNER);
    rc = TRUE;
  } else if( sndMsg.header.destination != NODEID_RF_SCANNER && tx_addr != NODEID_GATEWAY ) {
    UpdateNodeAddress(NODEID_GATEWAY);
    rc = TRUE;
  }
  return rc;
}

uint8_t GetSteps(uint32_t _from, uint32_t _to, bool _fast)
{
  uint8_t _step = BRIGHTNESS_STEP;
  uint32_t _gap;
  if( _from > _to ) {
    _gap = _from - _to;
  } else {
    _gap = _to - _from;
  }
  // Max 40 times
  uint8_t _maxSteps = (_fast ? MAX_FASTSTEP_TIMES: MAX_STEP_TIMES);
  if( _step * _maxSteps < _gap ) {
    _step = _gap / _maxSteps + 1;
  }
  return _step;
}

void CCT2ColdWarm(uint32_t ucBright, uint32_t ucWarmCold)
{
  if(ucBright>100)
    ucBright = 100;
  if(ucWarmCold>=CT_MAX_VALUE)
    ucWarmCold = CT_MAX_VALUE;
  if(ucWarmCold<=CT_MIN_VALUE)
    ucWarmCold = CT_MIN_VALUE;
  
  ucWarmCold -= CT_MIN_VALUE;
  ucWarmCold*= 10;
  ucWarmCold/=CT_SCOPE;         // 0 - 1000
  
  // Convert brightness with quadratic function
  // Func 1
  // y = (100 - b) * x * sqrt(x) / 1000 + b
  // , where b = LIGHT_PWM_THRESHOLD
  // Func 2
  // y = (100 - b) * x * x / 10000 + b
  // , where b = LIGHT_PWM_THRESHOLD
#ifdef CCT_BR_QUADRATIC_FUNC
  if( ucBright > 0 ) {
    //float rootBright = sqrt(ucBright);
    //ucBright = (uint32_t)((100 - LIGHT_PWM_THRESHOLD) * ucBright * rootBright / 1000 + LIGHT_PWM_THRESHOLD + 0.5);
    ucBright = (100 - LIGHT_PWM_THRESHOLD) * ucBright * ucBright / 10000 + LIGHT_PWM_THRESHOLD + 0.5;
  }
#endif
  
  pwm_Warm = (1000 - ucWarmCold)*ucBright/1000 ;
  pwm_Cold = ucWarmCold*ucBright/1000 ;
}

// Send message and switch back to receive mode
bool SendMyMessage() {
  if( bMsgReady ) {
    
    // Change tx destination if necessary
    NeedUpdateRFAddress(sndMsg.header.destination);
    
    uint8_t lv_tried = 0;
    uint16_t delay;
    while (lv_tried++ <= gConfig.rptTimes ) {
      feed_wwdg();
      // delay to avoid conflict
      /*
      if( bDelaySend && gConfig.nodeID >= NODEID_MIN_DEVCIE ) {
        //delay_ms(gConfig.nodeID % 25 * 10);
        mutex = 0;
        WaitMutex((gConfig.nodeID - NODEID_MIN_DEVCIE + 1) * (uint32_t)255);
        bDelaySend = FALSE;
      }
      */

      mutex = 0;
//disableInterrupts();
      CC1101_Set_Mode( RX_MODE );
      //TODO
      //CC1101_Tx_Packet( (uint8_t *)g_Ashining, 3 , 1,ADDRESS_CHECK );
      /*if(RF24L01_set_mode_TX_timeout() == -1) 
        break;
      if(RF24L01_write_payload_timeout(psndMsg, PLOAD_WIDTH) == -1) 
        break;*/
//enableInterrupts();
      WaitMutex(0x1FFFF);
#ifndef ENABLE_SDTM      
      if (mutex == 1) {
        m_cntRFSendFailed = 0;
        gConfig.cntRFReset = 0;
        break; // sent sccessfully
      }
      else {
        m_cntRFSendFailed++;
        if( m_cntRFSendFailed >= MAX_RF_FAILED_TIME ) {
          m_cntRFSendFailed = 0;
          gConfig.cntRFReset++;
          if( gConfig.cntRFReset >= MAX_RF_RESET_TIME ) {
            // Save Data
            gIsStatusChanged = TRUE;
            SaveStatusData();
            
#ifdef NO_RESTART_MODE
            // Reset whole node
            mStatus = SYS_RESET;
#else
            // Cold Reset
            WWDG->CR = 0x80;   
#endif
            
            break;
          } else if( gConfig.cntRFReset > 1 ) {
            // Reset whole node
            mStatus = SYS_RESET;
            break;
          }

          // Reset RF module
          //RF24L01_DeInit();
          delay = 0x1FFF;
          while(delay--)feed_wwdg();
          CC1101_Init(gConfig.rfChannel,gConfig.rfAddr);
          CC1101_EnableIRQ();
          UpdateNodeAddress(NODEID_GATEWAY);
          continue;
        }
      }
      
      //The transmission failed, Notes: mutex == 2 doesn't mean failed
      //It happens when rx address defers from tx address
      //asm("nop"); //Place a breakpoint here to see memory
      // Repeat the message if necessary
      delay = 0xFFF;
      while(delay--)feed_wwdg();
#else
      break;
#endif

    }
    
    // Switch back to receive mode
    bMsgReady = 0;
    //RF24L01_set_mode_RX();
    CC1101_Set_Mode( RX_MODE );
    // Reset Keep Alive Timer
    mTimerKeepAlive = 0;
  }

  return(mutex > 0);
}

void GotNodeID() {
  mGotNodeID = TRUE;
  UpdateNodeAddress(NODEID_GATEWAY);
  gNeedSaveBackup = TRUE;
}

void GotPresented() {
  mStatus = SYS_RUNNING;
  gConfig.swTimes = 0;
  gIsStatusChanged = TRUE;
  //SaveStatusData();
}

void PrintDevStatus()
{ 
  uint8_t r_index = 0;
  for( r_index = 0; r_index < MAX_RING_NUM; r_index++ ) {
    printlog("ring ");
    printnum(r_index);
    printlog(" ");
    printnum(RINGST_OnOff(r_index));
    printlog(",");
    printnum(RINGST_Bright(r_index));
    printlog(",");
    printnum(RINGST_WarmCold(r_index));
    printlog("\r\n"); 
  }
}
void ProcessAutoSwitchLight()
{
  if(gConfig.enAutoPowerTest)
  {
    if(gAgingRunningTimeTick >= SUNNY_SWITCH_INTERVAL)
    {// switch light cct
      gAgingRunningTimeTick = 0;
      SetDeviceBrightness(100,0);
      if(DEVST_WarmCold >=3000 && DEVST_WarmCold <= 6000)
      {
        SetDeviceCCT(6500,0);
      }
      else if(DEVST_WarmCold<3000)
      {
        SetDeviceCCT(5000,0);
      }
      else
      {
        SetDeviceCCT(2700,0);
      }      
    }
  }
}

bool SayHelloToDevice(bool infinate) {
  uint8_t _count = 0;
  uint8_t _presentCnt = 0;
  bool _doNow = FALSE;

  // Update RF addresses and Setup RF environment
  UpdateNodeAddress(NODEID_GATEWAY);

  while(mStatus < SYS_RUNNING) {
    feed_wwdg();
    ////////////rfscanner process///////////////////////////////
    ProcessOutputCfgMsg(); 
    ProcessAutoSwitchLight();
    // Save Config if Changed
    SaveConfig();
    SendMyMessage();

    ////////////rfscanner process///////////////////////////////
    if( _count++ == 0 ) {
      
      if( isNodeIdRequired() ) {
        mStatus = SYS_WAIT_NODEID;
        mGotNodeID = FALSE;
        // Request for NodeID
        Msg_RequestNodeID();
      } else {
        mStatus = SYS_WAIT_PRESENTED;
        // Send Presentation Message
        Msg_Presentation();
        _presentCnt++;
#ifdef RAPID_PRESENTATION
        // Don't wait for ack
        mStatus = SYS_RUNNING;
#endif        
      }
           
      if( !SendMyMessage() ) {
        if( !infinate ) return FALSE;
      } else {
        // Wait response
        uint16_t tick = 0xBFFF;
        while(tick-- && mStatus < SYS_RUNNING) {
          // Feed the Watchdog
          feed_wwdg();
          if( mStatus == SYS_WAIT_NODEID && mGotNodeID ) {
            mStatus = SYS_WAIT_PRESENTED;
            _presentCnt = 0;
            _doNow = TRUE;
            break;
          }
        }
      }
    }

    if( mStatus == SYS_RUNNING ) return TRUE;
    
    // Can't presented for a few times, then try request NodeID again
    // Either because SmartController is off, or changed
    if(  mStatus == SYS_WAIT_PRESENTED && _presentCnt >= REGISTER_RESET_TIMES && REGISTER_RESET_TIMES < 100 ) {
      _presentCnt = 0;
      // Reset RF Address
      InitNodeAddress();
      UpdateNodeAddress(NODEID_GATEWAY);
      mStatus = SYS_WAIT_NODEID;
      _doNow = TRUE;
    }
    
    // Reset switch count
    if( _count >= 10 && gConfig.swTimes > 0 ) {
      gConfig.swTimes = 0;
      gIsStatusChanged = TRUE;
      SaveStatusData();
    }
    
    // Feed the Watchdog
    feed_wwdg();

    if( _doNow ) {
      // Send Message Immediately
      _count = 0;
      continue;
    }
    
    // Failed or Timeout, then repeat init-step
    //delay_ms(400);
    mutex = 0;
    WaitMutex(0x1FFFF);
    _count %= 20;  // Every 10 seconds
  }
  
  return TRUE;
}

void RestartCheck()
{
  if(gConfig.enAutoPowerTest) return;
#ifdef XSUNNY
  if(pwm_Cold == 0 && pwm_Warm == 0)
  {
    if( (!gIsStatusChanged && gRunningTimeTick >= SUNNY_RUNNING_MAXTIME) || gMainloopTimeTick >= MAINLOOP_TIMEOUT )
    {
      printlog("need restart!");
      WWDG->CR = 0x80;
    }
  }
#endif
}

int main( void ) {
  uint8_t lv_Brightness;
      
  //After reset, the device restarts by default with the HSI clock divided by 8.
  //CLK_DeInit();
  /* High speed internal clock prescaler: 1 */
  CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV1);  // now: HSI=16M prescale = 1; sysclk = 16M
  //CLK_SYSCLKConfig(CLK_PRESCALER_HSIDIV2);  // now: HSI=16M prescale = 2; sysclk = 8M
  //CLK_HSECmd(ENABLE);
  // Init PWM Timers
  initTim2PWMFunction();

  // Load config from Flash
  FLASH_DeInit();
  Read_UniqueID(_uniqueID, UNIQUE_ID_LEN);
  LoadConfig();

  // on / off 3 times to reset device
  gConfig.swTimes++;
  if( gConfig.swTimes >= ONOFF_RESET_TIMES ) {
    gConfig.swTimes = 0;
    gConfig.enSDTM = 0;
    //engineering code(don't need to change nodeid)
    //gConfig.nodeID = BASESERVICE_ADDRESS;
    InitNodeAddress();
    gIsStatusChanged = TRUE;
  } else {
    gIsStatusChanged = TRUE;
  }
  SaveStatusData();
  
  // Init Watchdog
  wwdg_init();

#ifdef DEBUG_LOG  
  uart2_config(9600);
#endif 
#ifdef TEST
  testio();
#endif
  // Init timer
  TIM4_5ms_handler = idleProcess;
  TIM4_10ms_handler = tmrProcess;
  Time4_Init();
  printlog("start...");
  // Init serial ports
  //uart2_config(9600);
  
  while(1) {
    // Go on only if NRF chip is presented
    gConfig.present = 0;
    CC1101_Init();
    CC1101_Setup(gConfig.rfChannel,gConfig.rfAddr);
    u16 timeoutRFcheck = 0;
    while(!CC1101_Check()) {
      if( timeoutRFcheck > 50 ) {
        WWDG->CR = 0x80;
        break;
      }
      feed_wwdg();
    }

    // Try to communicate with sibling MCUs (STM8S003F), 
    /// if got response, which means the device supports indiviual ring control.
    /// Also need to enable RING_INDIVIDUAL_COLOR condition for indiviual ring control.
    // ToDo:
    // gConfig.hasSiblingMCU = PingSiblingMCU();
    
    // Bring the lights to the most recent or default light-on status
    if( mStatus == SYS_INIT ) {
      if( gConfig.cntRFReset >= MAX_RF_RESET_TIME ) {
        gConfig.cntRFReset = 0;
        gIsStatusChanged = TRUE;
        printlog("rf reset");
      }
      // Restore to previous state
      bool preSwitch = DEVST_OnOff;
      DEVST_OnOff = 0;        // Make sure switch can be turned on if previous state is on
      SetDeviceFilter(gConfig.filter);
      SetDeviceOnOff(preSwitch, RING_ID_ALL);
      //delay_ms(1500);   // about 1.5 sec
      mutex = 0;
      WaitMutex(0xFFFF); // use this line to bring the lights to target brightness
      printlog("restore...");
    }
  
    // IRQ
    CC1101_EnableIRQ();
    PrintDevStatus();
#ifdef ENABLE_SDTM
    gConfig.nodeID = BASESERVICE_ADDRESS;
    //memcpy(gConfig.NetworkID, RF24_BASE_RADIO_ID, ADDRESS_WIDTH);
    UpdateNodeAddress(NODEID_GATEWAY);
    Msg_DevStatus(NODEID_MIN_REMOTE, RING_ID_ALL);
    SendMyMessage();
    mStatus = SYS_RUNNING;
#else
    if( gConfig.enSDTM ) {
      gConfig.nodeID = BASESERVICE_ADDRESS;
      //memcpy(gConfig.NetworkID, RF24_BASE_RADIO_ID, ADDRESS_WIDTH);
      UpdateNodeAddress(NODEID_GATEWAY);
      Msg_DevStatus(NODEID_MIN_REMOTE, RING_ID_ALL);
      SendMyMessage();
      mStatus = SYS_RUNNING;
    } else {
      // Must establish connection firstly
      SayHelloToDevice(TRUE);
      gConfig.enAutoPowerTest = 0;
      gIsStatusChanged = TRUE;
    }
#endif
    //PrintDevStatus();
    while (mStatus == SYS_RUNNING) {
      
      // Feed the Watchdog
      feed_wwdg();
      
      ////////////rfscanner process///////////////////////////////
      ProcessOutputCfgMsg(); 
      // Save Config if Changed
      SaveConfig();
      ////////////rfscanner process///////////////////////////////     
      // Send message if ready
      //printlog("SndS");
      SendMyMessage();
      //printlog("SndE");
      //printlog("SvgE");
      if(offdelaytick == 0)
      {
        printlog("soffS");
        offdelaytick = -1;
        SetDeviceOnOff(0, RING_ID_ALL);
        printlog("soffE");
      }
      RestartCheck();
      // Idle process, do it in timer4
      //idleProcess();
      
      // ToDo: Check heartbeats
      // mStatus = SYS_RESET, if timeout or received a value 3 times consecutively
    }
  }
}

#if defined(XSUNNY)
// Immediately change brightness and cct
void ChangeDeviceStatus(bool _sw, uint8_t _br, uint16_t _cct, uint8_t _ring) {
  CCT2ColdWarm(_sw ? _br : 0, _cct);
  
  if( gConfig.hasSiblingMCU ) {
    // ring individual control
    /*
    uint8_t sendBuf[8];
    sendBuf[0] = UART_CMD_CCT;
    sendBuf[1] = pwm_Cold;
    sendBuf[2] = pwm_Warm;
    sendBuf[3] = '\n';
    sendBuf[4] = '\0';
    */
    if( _ring == RING_ID_ALL ) { 
      // change ring one by one
      driveColdWarmLightPwm(pwm_Cold, pwm_Warm);
      //Uart2SendString(sendBuf);
    } else if( _ring == RING_ID_1 ) {
      // change one specific ring
      driveColdWarmLightPwm(pwm_Cold, pwm_Warm);
    } else if( _ring == RING_ID_2 ) {
      //Uart2SendString(sendBuf);
    } else if( _ring == RING_ID_3 ) {
    }
  } else {
    // Control all rings together
    driveColdWarmLightPwm(pwm_Cold, pwm_Warm);
  }
}
#endif

#if defined(XRAINBOW) || defined(XMIRAGE)
// Immediately change brightness and rgbw
void ChangeDeviceStatus(bool _sw, uint8_t _br, uint32_t _w, uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _ring) {
  if(!_sw) {
    _br = 0;
  } else if(_br > 100) {
    _br = 100;
  }
  
  if( gConfig.hasSiblingMCU ) {
    // ring individual control
    /*
    uint8_t sendBuf[8];
    sendBuf[0] = UART_CMD_RGBW;
    sendBuf[1] = _br;
    sendBuf[2] = _r;
    sendBuf[3] = _g;
    sendBuf[4] = _b;
    sendBuf[5] = _w;
    sendBuf[6] = '\n';
    sendBuf[7] = '\0';
    */
    if( _ring == RING_ID_ALL ) { 
      // change ring one by one
      LightRGBWBRCtrl(_r, _g, _b, _w, _br);
      //Uart2SendString(sendBuf);
    } else if( _ring == RING_ID_1 ) {
      // change one specific ring
      LightRGBWBRCtrl(_r, _g, _b, _w, _br);
    } else if( _ring == RING_ID_2 ) {
      //Uart2SendString(sendBuf);
    } else if( _ring == RING_ID_3 ) {
    }
  } else {
    // Control all rings together
    LightRGBWBRCtrl(_r, _g, _b, _w, _br);
  }
}
#endif

// Immediately change brightness
void ChangeDeviceBR(uint32_t _br, uint8_t _ring) {
#ifdef RING_INDIVIDUAL_COLOR
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
#if defined(XSUNNY)
  ChangeDeviceStatus(TRUE, (uint8_t)_br, RINGST_WarmCold(r_index), _ring);
#endif
#if defined(XRAINBOW) || defined(XMIRAGE)
  ChangeDeviceStatus(TRUE, (uint8_t)_br, RINGST_W(r_index), RINGST_R(r_index), RINGST_G(r_index), RINGST_B(r_index), _ring);
#endif
#else
#if defined(XSUNNY)
  ChangeDeviceStatus(TRUE, (uint8_t)_br, DEVST_WarmCold, _ring);
#endif
#if defined(XRAINBOW) || defined(XMIRAGE)
  ChangeDeviceStatus(TRUE, (uint8_t)_br, DEVST_W, DEVST_R, DEVST_G, DEVST_B, _ring);
#endif
#endif
}

// Immediately change cct
void ChangeDeviceCCT(uint32_t _cct, uint8_t _ring) {
#ifdef RING_INDIVIDUAL_COLOR
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
#if defined(XSUNNY)
  ChangeDeviceStatus(RINGST_OnOff(r_index), RINGST_Bright(r_index), (uint16_t)_cct, _ring);
#endif
#if defined(XRAINBOW) || defined(XMIRAGE)
  // ToDo: calcualte RGB from Warm White
  ChangeDeviceStatus(RINGST_OnOff(r_index), RINGST_Bright(r_index), (uint8_t)_cct, RINGST_R(r_index), RINGST_G(r_index), RINGST_B(r_index), _ring);
#endif
#else
#if defined(XSUNNY)
  ChangeDeviceStatus(DEVST_OnOff, DEVST_Bright, (uint16_t)_cct, _ring);
#endif
#if defined(XRAINBOW) || defined(XMIRAGE)
  // ToDo: calcualte RGB from Warm White
  uint8_t _w, _r, _g, _b;
  ConvertCCT2RGBW(_cct, &_r, &_g, &_b, &_w);
  ChangeDeviceStatus(DEVST_OnOff, DEVST_Bright, _w, _r, _g, _b, _ring);
#endif
#endif
}

// Immediately change wrgb
void ChangeDeviceWRGB(uint32_t _wrgb, uint8_t _ring) {
#if defined(XRAINBOW) || defined(XMIRAGE)
  uint8_t _w, _r, _g, _b;
  _b = (_wrgb & 0xff);
  _wrgb >>= 8;
  _g = (_wrgb & 0xff);
  _wrgb >>= 8;
  _r = (_wrgb & 0xff);
  _wrgb >>= 8;
  _w = (_wrgb & 0xff);
  
#ifdef RING_INDIVIDUAL_COLOR
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
  ChangeDeviceStatus(TRUE, RINGST_Bright(r_index), _w, _r, _g, _b, _ring);
#else
  ChangeDeviceStatus(TRUE, DEVST_Bright, _w, _r, _g, _b, _ring);
#endif
#endif  
}

void DelaySendMsg(uint16_t _msg, uint8_t _ring) {
  switch( _msg ) {
  case 1:
    // send current on/off status
    Msg_DevOnOff(NODEID_GATEWAY);
    break;
    
  case 2:
    // send current brigntness status
    Msg_DevBrightness(NODEID_GATEWAY);
    break;
  }
  
  if( bMsgReady ) delay_10us(20);
}

// Gradually turn on or off
bool SetDeviceOnOff(bool _sw, uint8_t _ring) {
  printlog("setonoff ");
  printnum(_ring);
  printlog("-");
  printnum(_sw);
  printlog("\r\n");
#ifdef RING_INDIVIDUAL_COLOR
  
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
  if( _sw != RINGST_OnOff(r_index) ) {
    uint8_t _Brightness = (RINGST_Bright(r_index) >= BR_MIN_VALUE ? RINGST_Bright(r_index) : DEFAULT_BRIGHTNESS);

    RINGST_OnOff(r_index) = _sw;
    if( _Brightness != RINGST_Bright(r_index) ) {
      RINGST_Bright(r_index) = _Brightness;
    }
    
#ifdef GRADUAL_ONOFF

    // Smoothly change brightness - set parameters
    if( _sw == DEVICE_SW_OFF && BF_GET(delay_func, DELAY_TIM_BR, 1) ) {
      delay_from[DELAY_TIM_ONOFF] = delay_from[DELAY_TIM_BR];
    } else {
      delay_from[DELAY_TIM_ONOFF] = (_sw ? BR_MIN_VALUE : _Brightness);
    }
    delay_to[DELAY_TIM_ONOFF] = (_sw ? _Brightness : 0);
    delay_up[DELAY_TIM_ONOFF] = (delay_from[DELAY_TIM_ONOFF] < delay_to[DELAY_TIM_ONOFF]);
    delay_step[DELAY_TIM_ONOFF] = GetSteps(delay_from[DELAY_TIM_ONOFF], delay_to[DELAY_TIM_ONOFF], FALSE);

    // Smoothly change brightness - set timer
    delay_timer[DELAY_TIM_ONOFF] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_ONOFF] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_ONOFF] = ChangeDeviceBR;
    delay_tag[DELAY_TIM_ONOFF] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_ONOFF, 1); // Enable OnOff operation
    
#else

    // To the final status
    ChangeDeviceStatus(RINGST_OnOff(r_index), RINGST_Bright(r_index), RINGST_WarmCold(r_index), _ring);

#endif    

    gIsStatusChanged = TRUE;
    return TRUE;
  }
  
#else
  
  if( _sw != DEVST_OnOff ) {
    uint8_t _Brightness = (DEVST_Bright >= BR_MIN_VALUE ? DEVST_Bright : DEFAULT_BRIGHTNESS);

    DEVST_OnOff = _sw;
    if( _Brightness != DEVST_Bright ) {
      DEVST_Bright = _Brightness;
    }
    
#ifdef GRADUAL_ONOFF

    // Smoothly change brightness - set parameters
    if( _sw == DEVICE_SW_OFF && BF_GET(delay_func, DELAY_TIM_BR, 1) ) {
      delay_from[DELAY_TIM_ONOFF] = delay_from[DELAY_TIM_BR];
    } else {
      delay_from[DELAY_TIM_ONOFF] = (_sw ? BR_MIN_VALUE : _Brightness);
    }
    delay_to[DELAY_TIM_ONOFF] = (_sw ? _Brightness : 0);
    delay_up[DELAY_TIM_ONOFF] = (delay_from[DELAY_TIM_ONOFF] < delay_to[DELAY_TIM_ONOFF]);
    // Get Step
    delay_step[DELAY_TIM_ONOFF] = GetSteps(delay_from[DELAY_TIM_ONOFF], delay_to[DELAY_TIM_ONOFF], FALSE);

    // Smoothly change brightness - set timer
    delay_timer[DELAY_TIM_ONOFF] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_ONOFF] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_ONOFF] = ChangeDeviceBR;
    delay_tag[DELAY_TIM_ONOFF] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_ONOFF, 1); // Enable OnOff operation
    
#else

    // To the final status
    ChangeDeviceStatus(DEVST_OnOff, DEVST_Bright, DEVST_WarmCold, _ring);

#endif    

    gIsStatusChanged = TRUE;
    return TRUE;
  }

#endif
  
  return FALSE;
}

// Gradually change brightness
bool SetDeviceBrightness(uint8_t _br, uint8_t _ring) {
  printlog("setbr ");
  printnum(_ring);
  printlog("-");
  printnum(_br);
  printlog("\r\n");
#ifdef RING_INDIVIDUAL_COLOR
  
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
  if( _br != RINGST_Bright(r_index) ) {
#ifdef GRADUAL_ONOFF    
    // Smoothly change brightness - set parameters
    delay_from[DELAY_TIM_BR] = RINGST_Bright(r_index);
    delay_to[DELAY_TIM_BR] = _br;
    delay_up[DELAY_TIM_BR] = (delay_from[DELAY_TIM_BR] < delay_to[DELAY_TIM_BR]);
    delay_step[DELAY_TIM_BR] = GetSteps(delay_from[DELAY_TIM_BR], delay_to[DELAY_TIM_BR], FALSE);
#endif
    
    bool newSW = (_br >= BR_MIN_VALUE);
    RINGST_Bright(r_index) = _br;
    if( RINGST_OnOff(r_index) != newSW ) {
      RINGST_OnOff(r_index) = newSW;
    }
    
#ifdef GRADUAL_ONOFF
    // Smoothly change brightness - set timer
    delay_timer[DELAY_TIM_BR] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_BR] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_BR] = ChangeDeviceBR;
    delay_tag[DELAY_TIM_BR] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_BR, 1); // Enable BR Dimmer operation
#else    
    ChangeDeviceStatus(RINGST_OnOff(r_index), RINGST_Bright(r_index), RINGST_WarmCold(r_index), _ring);
#endif

    gIsStatusChanged = TRUE;
    return TRUE;
  }
  
#else
  
  if( _br != DEVST_Bright || DEVST_OnOff == DEVICE_SW_OFF) {
#ifdef GRADUAL_ONOFF    
    // Smoothly change brightness - set parameters
    delay_from[DELAY_TIM_BR] = DEVST_Bright;
    delay_to[DELAY_TIM_BR] = _br;
    delay_up[DELAY_TIM_BR] = (delay_from[DELAY_TIM_BR] < delay_to[DELAY_TIM_BR]);
    delay_step[DELAY_TIM_BR] = GetSteps(delay_from[DELAY_TIM_BR], delay_to[DELAY_TIM_BR], FALSE);
#endif
    
    bool newSW = (_br >= BR_MIN_VALUE);
    DEVST_Bright = _br;
    if( DEVST_OnOff != newSW ) {
      DEVST_OnOff = newSW;
    }
    
#ifdef GRADUAL_ONOFF
    // Smoothly change brightness - set timer
    delay_timer[DELAY_TIM_BR] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_BR] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_BR] = ChangeDeviceBR;
    delay_tag[DELAY_TIM_BR] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_BR, 1); // Enable BR Dimmer operation
#else    
    ChangeDeviceStatus(DEVST_OnOff, DEVST_Bright, DEVST_WarmCold, _ring);
#endif

    gIsStatusChanged = TRUE;
    return TRUE;
  }
  
#endif
  
  return FALSE;
}

// Gradually change cct
bool SetDeviceCCT(uint16_t _cct, uint8_t _ring) {
  printlog("setcct ");
  printnum(_ring);
  printlog("-");
  printnum(_cct);
  printlog("\r\n");
  if( _cct > CT_MAX_VALUE ) {
    _cct = CT_MAX_VALUE;
  } else if( _cct < CT_MIN_VALUE ) {
    _cct = CT_MIN_VALUE;
  }
  
#ifdef RING_INDIVIDUAL_COLOR
  
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
  if( _cct != RINGST_WarmCold(r_index) ) {
#ifdef GRADUAL_CCT    
    // Smoothly change CCT - set parameters
    delay_from[DELAY_TIM_CCT] = RINGST_WarmCold(r_index);
    delay_to[DELAY_TIM_CCT] = _cct;
    delay_up[DELAY_TIM_CCT] = (delay_from[DELAY_TIM_CCT] < delay_to[DELAY_TIM_CCT]);
    delay_step[DELAY_TIM_CCT] = CCT_STEP;
#endif
    
    RINGST_WarmCold(r_index) = _cct;
    
#ifdef GRADUAL_CCT
    // Smoothly change CCT - set timer
    delay_timer[DELAY_TIM_CCT] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_CCT] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_CCT] = ChangeDeviceCCT;
    delay_tag[DELAY_TIM_CCT] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_CCT, 1); // Enable CCT Dimmer operation
#else    
    ChangeDeviceStatus(RINGST_OnOff(r_index), RINGST_Bright(r_index), RINGST_WarmCold(r_index), _ring);
#endif
    
    gIsStatusChanged = TRUE;
    return TRUE;
  }
  
#else
  
  if( _cct != DEVST_WarmCold ) {
#ifdef GRADUAL_CCT    
    // Smoothly change CCT - set parameters
    delay_from[DELAY_TIM_CCT] = DEVST_WarmCold;
    delay_to[DELAY_TIM_CCT] = _cct;
    delay_up[DELAY_TIM_CCT] = (delay_from[DELAY_TIM_CCT] < delay_to[DELAY_TIM_CCT]);
    delay_step[DELAY_TIM_CCT] = CCT_STEP;
#endif
    
    DEVST_WarmCold = _cct;
    
#ifdef GRADUAL_CCT
    // Smoothly change CCT - set timer
    delay_timer[DELAY_TIM_CCT] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_CCT] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_CCT] = ChangeDeviceCCT;
    delay_tag[DELAY_TIM_CCT] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_CCT, 1); // Enable CCT Dimmer operation
#else
#if defined(XSUNNY)
    ChangeDeviceStatus(DEVST_OnOff, DEVST_Bright, DEVST_WarmCold, _ring);
#endif    
#if defined(XRAINBOW) || defined(XMIRAGE)
    ChangeDeviceCCT(DEVST_WarmCold, _ring);
#endif    
#endif
    
    gIsStatusChanged = TRUE;
    return TRUE;
  }
  
#endif
  
  return FALSE;
}

// Gradually change WRGB
bool SetDeviceWRGB(uint8_t _w, uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _ring) {
  
  uint32_t newValue = cf_makeColorValue(_w, _r, _g, _b);
  
#ifdef RING_INDIVIDUAL_COLOR
  
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
  if( _w != RINGST_W(r_index) || _r != RINGST_R(r_index) || _g != RINGST_G(r_index) || _b != RINGST_B(r_index) ) {
#ifdef GRADUAL_RGB    
    // Smoothly change RGBW - set parameters
    delay_from[DELAY_TIM_RGB] = cf_makeColorValue(RINGST_W(r_index), RINGST_R(r_index), RINGST_G(r_index), RINGST_B(r_index));
    delay_to[DELAY_TIM_RGB] = newValue;
    cf_updateInitialColor(delay_from[DELAY_TIM_RGB]);
    cf_updateTargetColor(delay_to[DELAY_TIM_RGB]);
    delay_up[DELAY_TIM_RGB] = (delay_from[DELAY_TIM_RGB] < delay_to[DELAY_TIM_RGB]);
    delay_step[DELAY_TIM_RGB] = RGB_STEP;
    
#endif
    
    RINGST_W(r_index) = _w;
    RINGST_R(r_index) = _r;
    RINGST_G(r_index) = _g;
    RINGST_B(r_index) = _b;
    
#ifdef GRADUAL_RGB
    // Smoothly change RGBW - set timer
    delay_timer[DELAY_TIM_RGB] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_RGB] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_RGB] = ChangeDeviceWRGB;
    delay_tag[DELAY_TIM_RGB] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_RGB, 1); // Enable RGB Dimmer operation
#else    
    ChangeDeviceWRGB(newValue, _ring);
#endif
    
    gIsStatusChanged = TRUE;
    return TRUE;
  }
  
#else
  
  if( _w != DEVST_W || _r != DEVST_R || _g != DEVST_G || _b != DEVST_B ) {
#ifdef GRADUAL_RGB    
    // Smoothly change RGBW - set parameters
    delay_from[DELAY_TIM_RGB] = cf_makeColorValue(DEVST_W, DEVST_R, DEVST_G, DEVST_B);
    delay_to[DELAY_TIM_RGB] = newValue;
    cf_updateInitialColor(delay_from[DELAY_TIM_RGB]);
    cf_updateTargetColor(delay_to[DELAY_TIM_RGB]);
    delay_up[DELAY_TIM_RGB] = (delay_from[DELAY_TIM_RGB] < delay_to[DELAY_TIM_RGB]);
    delay_step[DELAY_TIM_RGB] = RGB_STEP;
#endif
    
    DEVST_W = _w;
    DEVST_R = _r;
    DEVST_G = _g;
    DEVST_B = _b;
    
#ifdef GRADUAL_RGB
    // Smoothly change RGBW - set timer
    delay_timer[DELAY_TIM_RGB] = DELAY_5_ms;  // about 5ms
    delay_tick[DELAY_TIM_RGB] = 0;      // execute next step right away
    delay_handler[DELAY_TIM_RGB] = ChangeDeviceWRGB;
    delay_tag[DELAY_TIM_RGB] = _ring;
    BF_SET(delay_func, 1, DELAY_TIM_RGB, 1); // Enable RGB Dimmer operation
#else
    ChangeDeviceWRGB(newValue, _ring);
#endif
    
    gIsStatusChanged = TRUE;
    return TRUE;
  }
  
#endif
  
  return FALSE;
}

// Gradually change on/off, brightness and cct in one
bool SetDeviceStatus(bool _sw, uint8_t _br, uint16_t _cct, uint8_t _ring) {
#ifdef RING_INDIVIDUAL_COLOR
  
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
  uint8_t oldSW = RINGST_OnOff(r_index);
  uint8_t oldBR = RINGST_Bright(r_index);
  uint8_t oldCCT = RINGST_WarmCold(r_index);
  
  if( _ring == RING_ID_ALL ) {
    RINGST_OnOff(0) = _sw;
    RINGST_OnOff(1) = _sw;
    RINGST_OnOff(2) = _sw;
    RINGST_Bright(0) = _br;
    RINGST_Bright(1) = _br;
    RINGST_Bright(2) = _br;
    RINGST_WarmCold(0) = _cct;
    RINGST_WarmCold(1) = _cct;
    RINGST_WarmCold(2) = _cct;
  } else {
    RINGST_OnOff(r_index) = _sw;
    RINGST_Bright(r_index) = _br;
    RINGST_WarmCold(r_index) = _cct;
  }
  
  if( _sw != oldSW ) {
    RINGST_OnOff(r_index) = oldSW;
    SetDeviceOnOff(_sw, _ring);
    return TRUE;
  }
  
  if( _br != oldBR ) {
    RINGST_Bright(r_index) = oldBR;
    SetDeviceBrightness(_br, _ring);
    return TRUE;    
  }
  
  RINGST_WarmCold(r_index) = oldCCT;
  return SetDeviceCCT(_cct, _ring);

#else  
  
  if( _sw != DEVST_OnOff ) {
    DEVST_Bright = _br;
    DEVST_WarmCold = _cct;
    SetDeviceOnOff(_sw, _ring);
    return TRUE;
  }
  
  if( _br != DEVST_Bright ) {
    DEVST_WarmCold = _cct;
    SetDeviceBrightness(_br, _ring);
    return TRUE;    
  }
  
  return SetDeviceCCT(_cct, _ring);
  
#endif  
}

// Gradually change on/off, brightness and rgbw in one
bool SetDeviceHue(bool _sw, uint8_t _br, uint8_t _w, uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _ring) {
#ifdef RING_INDIVIDUAL_COLOR
  
  uint8_t r_index = (_ring == RING_ID_ALL ? 0 : _ring - 1);
  uint8_t oldSW = RINGST_OnOff(r_index);
  uint8_t oldBR = RINGST_Bright(r_index);
  uint8_t oldW = RINGST_W(r_index);
  uint8_t oldR = RINGST_R(r_index);
  uint8_t oldG = RINGST_G(r_index);
  uint8_t oldB = RINGST_B(r_index);
  
  if( _ring == RING_ID_ALL ) {
    RINGST_OnOff(0) = _sw;
    RINGST_OnOff(1) = _sw;
    RINGST_OnOff(2) = _sw;
    RINGST_Bright(0) = _br;
    RINGST_Bright(1) = _br;
    RINGST_Bright(2) = _br;
    RINGST_W(0) = _w;
    RINGST_W(1) = _w;
    RINGST_W(2) = _w;
    RINGST_R(0) = _r;
    RINGST_R(1) = _r;
    RINGST_R(2) = _r;
    RINGST_G(0) = _g;
    RINGST_G(1) = _g;
    RINGST_G(2) = _g;
    RINGST_B(0) = _b;
    RINGST_B(1) = _b;
    RINGST_B(2) = _b;
  } else {
    RINGST_OnOff(r_index) = _sw;
    RINGST_Bright(r_index) = _br;
    RINGST_W(r_index) = _w;
    RINGST_R(r_index) = _r;
    RINGST_G(r_index) = _g;
    RINGST_B(r_index) = _b;
  }
  
  if( _sw != oldSW ) {
    RINGST_OnOff(r_index) = oldSW;
    SetDeviceOnOff(_sw, _ring);
    return TRUE;
  }
  
  if( _br != oldBR ) {
    RINGST_Bright(r_index) = oldBR;
    SetDeviceBrightness(_br, _ring);
    return TRUE;    
  }

  RINGST_W(r_index) = oldW;
  RINGST_R(r_index) = oldR;
  RINGST_G(r_index) = oldG;
  RINGST_B(r_index) = oldB;
  return SetDeviceWRGB(_w, _r, _g, _b, _ring);

#else  
  
  if( _sw != DEVST_OnOff ) {
    DEVST_Bright = _br;
    DEVST_W = _w;
    DEVST_R = _r;
    DEVST_G = _g;
    DEVST_B = _b;
    SetDeviceOnOff(_sw, _ring);
    return TRUE;
  }
  
  if( _br != DEVST_Bright ) {
    DEVST_W = _w;
    DEVST_R = _r;
    DEVST_G = _g;
    DEVST_B = _b;
    SetDeviceBrightness(_br, _ring);
    return TRUE;    
  }
  
  return SetDeviceWRGB(_w, _r, _g, _b, _ring);
  
#endif  
}

// Start breathing effect
void StartDeviceBreath(bool _init, bool _fast) {
   // Smoothly change brightness - set parameters
  if( _init || delay_to[DELAY_TIM_BR] > BR_MIN_VALUE ) {
    delay_from[DELAY_TIM_BR] = DEVST_Bright;
    delay_to[DELAY_TIM_BR] = BR_MIN_VALUE;    
    delay_up[DELAY_TIM_BR] = FALSE;
  } else {
    delay_from[DELAY_TIM_BR] = BR_MIN_VALUE;
    delay_to[DELAY_TIM_BR] = DEVST_Bright;
    delay_up[DELAY_TIM_BR] = TRUE;
  }
  delay_step[DELAY_TIM_BR] = GetSteps(BR_MIN_VALUE, DEVST_Bright, TRUE);
    
  // Smoothly change brightness - set timer
  delay_timer[DELAY_TIM_BR] = (_fast ? DELAY_25_ms : DELAY_120_ms);
  delay_tick[DELAY_TIM_BR] = (_fast ? DELAY_500_ms : DELAY_800_ms);
  delay_handler[DELAY_TIM_BR] = ChangeDeviceBR;
  delay_tag[DELAY_TIM_BR] = RING_ID_ALL;
  BF_SET(delay_func, DEVST_OnOff, DELAY_TIM_BR, 1); // Enable BR Dimmer operation
}

#if defined(XRAINBOW) || defined(XMIRAGE)
// Start color-fade effect
void StartDeviceColorFade(bool _init, bool _fast) {
  if( _init ) {
    cf_initStep(); 
    delay_from[DELAY_TIM_RGB] = cf_makeColorValue(DEVST_W, DEVST_R, DEVST_G, DEVST_B);
    delay_to[DELAY_TIM_RGB] = cf_getTargetColorVal();
  } else {
    cf_nextStep();
    delay_from[DELAY_TIM_RGB] = cf_getInitialColorVal();
    delay_to[DELAY_TIM_RGB] = cf_getTargetColorVal();
  }
  
  // Smoothly change color - set parameters
  delay_up[DELAY_TIM_RGB] = (delay_from[DELAY_TIM_RGB] < delay_to[DELAY_TIM_RGB]);
  delay_step[DELAY_TIM_RGB] = RGB_STEP;

  // Smoothly change color - set timer
  delay_timer[DELAY_TIM_RGB] = (_fast ? DELAY_10_ms : DELAY_25_ms);
  delay_tick[DELAY_TIM_RGB] = (_fast ? DELAY_300_ms : DELAY_500_ms);
  delay_handler[DELAY_TIM_RGB] = ChangeDeviceWRGB;
  delay_tag[DELAY_TIM_RGB] = RING_ID_ALL;
  BF_SET(delay_func, DEVST_OnOff, DELAY_TIM_RGB, 1);
}
#endif

bool SetDeviceFilter(uint8_t _filter) {
  // Start filter
  if( _filter == FILTER_SP_EF_BREATH || _filter == FILTER_SP_EF_FAST_BREATH ) {
    // Set brightness to lowest, then restore back
    StartDeviceBreath(TRUE, _filter == FILTER_SP_EF_FAST_BREATH);
  }
#if defined(XRAINBOW) || defined(XMIRAGE)    
  else if( _filter == FILTER_SP_EF_FLORID || _filter == FILTER_SP_EF_FAST_FLORID ) {
    StartDeviceColorFade(TRUE, _filter == FILTER_SP_EF_FAST_FLORID);
  }
#endif
  
  if( _filter != gConfig.filter ) {
    gConfig.filter = _filter;
    gIsStatusChanged = TRUE;
    if( _filter == FILTER_SP_EF_NONE ) {
      // Stop Timers
      BF_SET(delay_func, 0, DELAY_TIM_ONOFF, 4);
      // Restore normal brightness
      ChangeDeviceBR(DEVST_Bright, RING_ID_ALL);
    }
    return TRUE;
  }

  return FALSE;
}

bool isTimerCompleted(uint8_t _tmr) {
  bool bFinished;
  
  if( _tmr == DELAY_TIM_RGB ) {
    // Gradual color changing
    uint32_t new_value = cf_fadeColor(delay_from[_tmr], delay_to[_tmr], delay_step[_tmr]);
    delay_from[_tmr] = new_value;
    bFinished = ( delay_from[_tmr] == delay_to[_tmr] );
  } else {
    if( delay_up[_tmr] ) {
      // Up
      delay_from[_tmr] += delay_step[_tmr];
      bFinished = ( delay_from[_tmr] >= delay_to[_tmr] );
    } else {
      // Down
      if( delay_from[_tmr] > delay_to[_tmr] + delay_step[_tmr] ) {
        delay_from[_tmr] -= delay_step[_tmr];
        bFinished = FALSE;
      } else {
        bFinished = TRUE;
      }
    }
  }
  
  // Execute operation
  if( delay_handler[_tmr] ) {
    if( bFinished ) {
      // Completed - to the final status
      (*delay_handler[_tmr])(delay_to[_tmr], delay_tag[_tmr]);
    } else {
      // Progress
      (*delay_handler[_tmr])(delay_from[_tmr], delay_tag[_tmr]);
    }
  }
  
  return bFinished;
}

// Execute timer operations
void tmrProcess() {
  // Tick
  mTimerKeepAlive++;
   if(offdelaytick > 0)
  {
    offdelaytick--; 
  }  
  if(gConfig.enAutoPowerTest)
  {
    if(gAgingRunningTimeTick < SUNNY_SWITCH_INTERVAL)
    {
      gAgingRunningTimeTick++;
    }
  }
  if(gRunningTimeTick < SUNNY_RUNNING_MAXTIME)
  {
    gRunningTimeTick++;
  }
  if(gMainloopTimeTick < MAINLOOP_TIMEOUT)
  {
    gMainloopTimeTick++;
  }
  // Save config into backup area
   SaveBackupConfig();
   RestartCheck();
}

// Execute delayed operations
void idleProcess() {
  for( uint8_t _tmr = 0; _tmr < DELAY_TIMERS; _tmr++ ) {
    if( BF_GET(delay_func, _tmr, 1) ) {
      // Timer is enabled
      if( delay_tick[_tmr] == 0 ) {
        // Timer reached, reset it
        delay_tick[_tmr] = delay_timer[_tmr];
        // Move a step and execute operation
        if( isTimerCompleted(_tmr) ) {
          // Stop timer - disable further operation
          if( DEVST_OnOff != DEVICE_SW_ON ) {
            BF_SET(delay_func, 0, DELAY_TIM_ONOFF, 4);
          } else {
            BF_SET(delay_func, 0, _tmr, 1);
            
            // Special effect - looper
            if( _tmr <= DELAY_TIM_RGB && gConfig.filter > 0 ) {
              if( gConfig.filter == FILTER_SP_EF_BREATH || gConfig.filter == FILTER_SP_EF_FAST_BREATH ) {
                if( _tmr == DELAY_TIM_ONOFF ) delay_to[DELAY_TIM_BR] = DEVST_Bright;
                StartDeviceBreath(FALSE, gConfig.filter == FILTER_SP_EF_FAST_BREATH);
              }
#if defined(XRAINBOW) || defined(XMIRAGE)
              else if( gConfig.filter == FILTER_SP_EF_FLORID || gConfig.filter == FILTER_SP_EF_FAST_FLORID ) {
                StartDeviceColorFade(FALSE, gConfig.filter == FILTER_SP_EF_FAST_FLORID);
              }
#endif
            }
          }
        }
      } else {
        // Timer not reached, tick it
        delay_tick[_tmr]--;
      }
      break; // Only one time at a time
    }
  }
}
uint8_t g_RF24L01RxBuffer[ 32 ] = { 0 }; 
INTERRUPT_HANDLER(EXTI_PORTC_IRQHandler, 5) {
  if(CC1101_Get_RxCounter() > 0)
  {
      int i = CC1101_Rx_Packet( g_RF24L01RxBuffer );		//�����ֽ�
      if( 0 != i )
      {
        //brecv = 1;
        memcpy(prcvMsg,g_RF24L01RxBuffer+1,i-1);
        bMsgReady = ParseProtocol();
        CC1101_Clear_RxBuffer( );
        CC1101_Set_Mode( RX_MODE );
      }
  }

/*#ifdef TEST
  PD7_High;
#endif
  if(RF24L01_is_data_available()) {
    //Packet was received
    RF24L01_clear_interrupts();
    RF24L01_read_payload(prcvMsg, PLOAD_WIDTH);
    bMsgReady = ParseProtocol();
#ifdef TEST
    PD7_Low;
#endif
    return;
  }
 
  uint8_t sent_info;
  if (sent_info = RF24L01_was_data_sent()) {
    //Packet was sent or max retries reached
    RF24L01_clear_interrupts();
    mutex = sent_info;
#ifdef TEST
    PD7_Low;
#endif
    return;
  }
   RF24L01_clear_interrupts();
#ifdef TEST
  PD7_Low;
#endif*/
}
