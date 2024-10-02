/*
 * File:   BQ76940.c
 * Author: mligh
 *
 * Created on August 3, 2024, 8:31 PM
 */


#include "mcc_generated_files/system.h"
#include "mcc_generated_files/i2c1.h"
#include "mcc_generated_files/pin_manager.h"
#include "Libpic30.h"
#include "BQ76940.h"
#include "Serial.h"
#include "math.h"
#include "string.h"
#include "main.h"
#include "xc.h"

// Variables
bool crcEnabled;
uint8_t I2CAddress;
uint8_t type;
uint8_t shuntResistorValue_mOhm;
int thermistorBetaValue = 3435;  // typical value for Semitec 103AT-5 thermistor

// indicates if a new current reading or an error is available from BMS IC
	bool alertInterruptFlag = true;   // init with true to check and clear errors at start-up   
	
int numberOfCells;
int cellVoltages[MAX_NUMBER_OF_CELLS];          // mV
uint8_t idCellMaxVoltage;
uint8_t idCellMinVoltage;
long batVoltage;                           // mV
long batCurrent;                                // mA
int temperatures[MAX_NUMBER_OF_THERMISTORS];    // °C/10

//Cell balancing registers
uint8_t cellBalReg1 = 0;
uint8_t cellBalReg2 = 0;
uint8_t cellBalReg3 = 0;
bool cellBalances[MAX_NUMBER_OF_CELLS];

// Current limits (mA)
long maxChargeCurrent;
long maxDischargeCurrent;
int idleCurrentThreshold = 30; // mA
    
// Temperature limits (°C/10)
int minCellTempCharge;
int minCellTempDischarge;
int maxCellTempCharge;
int maxCellTempDischarge;

// Cell voltage limits (mV)
int maxCellVoltage;
int minCellVoltage;
int balancingMinCellVoltage_mV;
uint8_t balancingMaxVoltageDifference_mV;
    
int adcGain;    // uV/LSB
int adcOffset;  // mV
    
int errorStatus = 0;
bool autoBalancingEnabled = false;
bool balancingActive = false;
int balancingMinIdleTime_s = 1800;    // default: 30 minutes
unsigned long idleTimestamp = 0;
unsigned int secSinceErrorCounter = 0;
unsigned long interruptTimestamp = 0;

const int SCD_delay_setting [4] = { 70, 100, 200, 400 }; // us
const int SCD_threshold_setting [8] = { 44, 67, 89, 111, 133, 155, 178, 200 }; // mV

const int OCD_delay_setting [8] = { 8, 20, 40, 80, 160, 320, 640, 1280 }; // ms
const int OCD_threshold_setting [16] = { 17, 22, 28, 33, 39, 44, 50, 56, 61, 67, 72, 78, 83, 89, 94, 100 };  // mV

const uint8_t UV_delay_setting [4] = { 1, 4, 8, 16 }; // s
const uint8_t OV_delay_setting [4] = { 1, 2, 4, 8 }; // s

I2C1_MESSAGE_STATUS bms_Write(const uint8_t reg, bool *succeeded){
    I2C1_MESSAGE_STATUS stat;
    uint8_t packet[2] = {reg, 0};
    uint16_t slaveTimeout = 0;
    __delay32(4000);
    //Serial_println("Writing register");
    I2C1_MasterWrite(packet, 1, I2CAddress, &stat);
    while(stat == I2C1_MESSAGE_PENDING){
        if(slaveTimeout++ >= 1000){
            Serial_println("Timed Out When Waiting For Write");
            *succeeded = false;
            return stat;
        }
        __delay32(4000);
    }
    return stat;
}

I2C1_MESSAGE_STATUS bms_WriteRegister(const uint8_t reg, const uint8_t value, bool *succeeded){
    I2C1_MESSAGE_STATUS stat;
    uint8_t len = 2;
    uint8_t packet[4] = {reg, value};
    uint16_t slaveTimeout = 0;
    if (crcEnabled == true) {
        uint8_t crc = 0;
        len = 3;
        // CRC is calculated over the slave address (including R/W bit), register address, and data.
        crc = _crc8_ccitt_update(crc, (I2CAddress << 1) | 0);
        crc = _crc8_ccitt_update(crc, packet[0]);
        crc = _crc8_ccitt_update(crc, packet[1]);
        packet[2] = crc;
    }
    __delay32(4000);
    //Serial_println("Writing register");
    I2C1_MasterWrite(packet, len, I2CAddress, &stat);
    while(stat == I2C1_MESSAGE_PENDING){
        if(slaveTimeout++ >= 1000){
            Serial_println("Timed Out When Waiting For Write");
            *succeeded = false;
            return stat;
        }
        __delay32(4000);
        
    }
    return stat;
}

uint8_t bms_ReadRegister(uint8_t dataAddress, bool *succeeded){
    I2C1_MESSAGE_STATUS writeStatus;
    I2C1_MESSAGE_STATUS readStatus;
    uint16_t    timeOut, slaveTimeOut;

    readStatus = I2C1_MESSAGE_PENDING;  // this initial value is important
    writeStatus = I2C1_MESSAGE_PENDING;

    timeOut = 0;
    slaveTimeOut = 0;

    //Write one byte of the register to be read
    __delay32(4000);
    //Serial_println("Requesting register");
    uint8_t packet[2] = {dataAddress};
    I2C1_MasterWrite(packet, 1, I2CAddress, &writeStatus);    
    while(writeStatus == I2C1_MESSAGE_PENDING){
        __delay32(4000);    // add some delay here            
            
        if (slaveTimeOut == 1000){
            Serial_println("Timed Out When Writing For Read");
            *succeeded = false;
            return (0);
        }
        else slaveTimeOut++;
    }  
        
    if (writeStatus == I2C1_MESSAGE_FAIL){
        *succeeded = false;
        Serial_println("Read Failed.");
        return (0);
    }
    
    timeOut = 0;
    slaveTimeOut = 0;
    
    uint8_t count = 1;
//    if(crcEnabled) count = 2;
    uint8_t data[2] = {0, 0};
    
    __delay32(4000);
    //Serial_println("Reading register");
        
    // write one byte to EEPROM (2 is the count of bytes to write)
    I2C1_MasterRead(data, count, I2CAddress, &readStatus);
    // wait for the message to be sent or status has changed.
    while(readStatus == I2C1_MESSAGE_PENDING){
        __delay32(4000);    // add some delay here
        
        if (slaveTimeOut == 1000){
            Serial_println("Timed Out When Reading");
            *succeeded = false;
            return (0);
        }
        else slaveTimeOut++;
    }
    
    __delay32(4000);
    
    return data[0];
}

uint8_t bms_Read(bool *succeeded){
    I2C1_MESSAGE_STATUS readStatus;
    uint16_t    timeOut, slaveTimeOut;

    readStatus = I2C1_MESSAGE_PENDING;  // this initial value is important

    timeOut = 0;
    slaveTimeOut = 0;

    uint8_t data[2] = {0, 0};
    
    __delay32(4000);
    //Serial_println("Reading register");
        
    // write one byte to EEPROM (2 is the count of bytes to write)
    I2C1_MasterRead(data, 1, I2CAddress, &readStatus);
    // wait for the message to be sent or status has changed.
    while(readStatus == I2C1_MESSAGE_PENDING){
        __delay32(4000);    // add some delay here
        
        if (slaveTimeOut == 1000){
            Serial_println("Timed Out When Reading");
            *succeeded = false;
            return (0);
        }
        else slaveTimeOut++;
    }
    
    __delay32(4000);
    
//    if(crcEnabled == true){
//        I2C1_MESSAGE_STATUS readCRCStatus;
//        readCRCStatus = I2C1_MESSAGE_PENDING;  // this initial value is important
//        
//        timeOut = 0;
//        slaveTimeOut = 0;
//    
//        uint8_t data[2] = {0, 0};
//    
//        //Serial_println("Reading CRC register");
//        
//        // write one byte to EEPROM (2 is the count of bytes to write)
//        I2C1_MasterRead(data, 1, I2CAddress, &readCRCStatus);
//        // wait for the message to be sent or status has changed.
//        while(readCRCStatus == I2C1_MESSAGE_PENDING){
//            __delay32(4000);    // add some delay here
//        
//            if (slaveTimeOut == 1000){
//                Serial_println("Timed Out When Reading");
//                return (0);
//            }
//            else slaveTimeOut++;
//        }
//    }
    
    return data[0];
}

uint8_t bms_ReadMultiple(uint8_t *data, uint8_t count, bool *succeeded){
    I2C1_MESSAGE_STATUS readStatus;
    uint16_t    timeOut, slaveTimeOut;

    readStatus = I2C1_MESSAGE_PENDING;  // this initial value is important

    timeOut = 0;
    slaveTimeOut = 0;
    
    __delay32(4000);
    //Serial_println("Reading registers");
        
    // write one byte to EEPROM (2 is the count of bytes to write)
    I2C1_MasterRead(data, count, I2CAddress, &readStatus);
    // wait for the message to be sent or status has changed.
    while(readStatus == I2C1_MESSAGE_PENDING){
        __delay32(4000);    // add some delay here
        
        if (slaveTimeOut == 1000){
            Serial_println("Timed Out When Reading");
            *succeeded = false;
            return (0);
        }
        else slaveTimeOut++;
    }
    
    __delay32(4000);
    
    return 1;
}

const char *byte2char(int x)
{
    static char b[9];
    b[0] = '\0';
    int z;
    for (z = 128; z > 0; z >>= 1) strcat(b, ((x & z) == z) ? "1" : "0");
    return b;
}

// CRC calculation taken from LibreSolar mbed firmware
uint8_t _crc8_ccitt_update (uint8_t inCrc, uint8_t inData)
{
  uint8_t i;
  uint8_t data;
  data = inCrc ^ inData;

  for ( i = 0; i < 8; i++ )
  {
    if (( data & 0x80 ) != 0 )
    {
      data <<= 1;
      data ^= 0x07;
    }
    else data <<= 1;
  }

  return data;
}

//----------------------------------------------------------------------------

void bms_New(uint8_t bqType, int bqI2CAddress)
{
  type = bqType;
  I2CAddress = bqI2CAddress;
  
  if (type == bq76920) {
    numberOfCells = 5;
  }
  else if (type == bq76930) {
    numberOfCells = 10;  
  }
  else {
    numberOfCells = 15;
  }
  
  // prevent errors if someone reduced MAX_NUMBER_OF_CELLS accidentally
  if (numberOfCells > MAX_NUMBER_OF_CELLS) {
    numberOfCells = MAX_NUMBER_OF_CELLS;
  }
}


//-----------------------------------------------------------------------------

int bms_Begin()
{
  I2C1_Initialize();        // join I2C bus

  bool succeeded = true;
  
  // initialize variables
  for (uint8_t i = 0; i < numberOfCells; i++) {
    cellVoltages[i] = 0;
  }
  
  // Boot IC if pin is defined (else: manual boot via push button has to be done before calling this method)
  IO_RC6_SetHigh();
   //__delay32(40000000);
  delay(25);
  IO_RC6_SetLow();
  //__delay32(40000000);
  delay(5);
 
  if (bms_DetermineAddressAndCrc())
  {
    Serial_println("Address and CRC detection successful");
    //Serial_printlnf("Address: %d", I2CAddress);
    //Serial_printlnf("CRC Enabled: %d", crcEnabled);

    __delay32(4000);
    // initial settings for bq769x0
    Serial_println("Writing SYSCTRL1");
    bms_WriteRegister(SYS_CTRL1, 24, &succeeded);  // switch external thermistor (TEMP_SEL) and ADC on (ADC_EN)
    __delay32(4000);
    Serial_println("Writing SYSCTRL2");
    bms_WriteRegister(SYS_CTRL2, 64, &succeeded);  // switch CC_EN on

    // attach ALERT interrupt to this instance
    //attachInterrupt(digitalPinToInterrupt(alertPin), bq769x0::alertISR, RISING);

    // get ADC offset and gain
    Serial_println("Reading ADC Offset");
    adcOffset = (signed int) bms_ReadRegister(ADCOFFSET, &succeeded);  // convert from 2's complement
    Serial_println("Reading ADC Gain");
    adcGain = 365 + (((bms_ReadRegister(ADCGAIN1, &succeeded) & 12) << 1) | ((bms_ReadRegister(ADCGAIN2, &succeeded) & 224) >> 5)); // uV/LSB
    
    return succeeded;
  }

  else
  {
    Serial_println("BMS communication error");
    return false;
  }
}

//----------------------------------------------------------------------------
// automatically find out address and CRC setting

bool bms_DetermineAddressAndCrc(void)
{
    bool succeeded = true;
  Serial_println("Determining i2c address and whether CRC is enabled");

//  Serial_println("0x08: NO CRC");
//  // check for each address and CRC combination while also set CC_CFG to 0x19 as per datasheet
//  I2CAddress = 0x08;
//  crcEnabled = false;
//  bms_WriteRegister(CC_CFG, 0x19);
//  if (bms_ReadRegister(CC_CFG) == 0x19) return true;
//
//  Serial_println("0x18: NO CRC");
//  I2CAddress = 0x18;
//  crcEnabled = false;
//  bms_WriteRegister(CC_CFG, 0x19);
//  if (bms_ReadRegister(CC_CFG) == 0x19) return true;

  Serial_println("0x08: CRC");
  I2CAddress = 0x08;
  crcEnabled = true;
  bms_WriteRegister(CC_CFG, 0x19, &succeeded);
  if (bms_ReadRegister(CC_CFG, &succeeded) == 0x19) return true;

  Serial_println("0x18: CRC");
  I2CAddress = 0x18;
  crcEnabled = true;
  bms_WriteRegister(CC_CFG, 0x19, &succeeded);
  if (bms_ReadRegister(CC_CFG, &succeeded) == 0x19) return true;

  return false;
}

//----------------------------------------------------------------------------
// Fast function to check whether BMS has an error
// (returns 0 if everything is OK)

int bms_CheckStatus()
{
    bool succeeded = true;
   //Serial_print("checkStatus: ");
   //Serial_println(errorStatus);
//  if (alertInterruptFlag == false && errorStatus == 0) {
//    return 0;
//  }
//  else {
    regSYS_STAT_t sys_stat;
    sys_stat.regByte = bms_ReadRegister(SYS_STAT, &succeeded);

    if (sys_stat.bits.CC_READY == 1) {
      //Serial_println("Interrupt: CC ready");
      bms_UpdateCurrent(true);  // automatically clears CC ready flag	
    }
    
    // Serious error occured
    if (sys_stat.regByte & 0x3F)
    {
      if (alertInterruptFlag == true) {
        secSinceErrorCounter = 0;
      }
      errorStatus = sys_stat.regByte;
      
//      int secSinceInterrupt = (millis() - interruptTimestamp) / 1000;
//      
//      // check for overrun of millis() or very slow running program
//      if (abs(secSinceInterrupt - secSinceErrorCounter) > 2) {
//        secSinceErrorCounter = secSinceInterrupt;
//      }
//      
//      // called only once per second
//      if (secSinceInterrupt >= secSinceErrorCounter)
//      {
//        if (sys_stat.regByte & 0x20) { // XR error
//          // datasheet recommendation: try to clear after waiting a few seconds
//          if (secSinceErrorCounter % 3 == 0) {
//            Serial_println("Clearing XR error");
//            bms_WriteRegister(SYS_STAT, 0x20);
//          }
//        }
//        if (sys_stat.regByte & 0x10) { // Alert error
//          if (secSinceErrorCounter % 10 == 0) {
//            Serial_println("Clearing Alert error");
//            bms_WriteRegister(SYS_STAT, 0x10);
//          }
//        }
//        if (sys_stat.regByte & 0x08) { // UV error
//          bms_UpdateVoltages();
//          if (cellVoltages[idCellMinVoltage] > minCellVoltage) {
//            Serial_println("Clearing UV error");
//            bms_WriteRegister(SYS_STAT, 0x08);
//          }
//        }
//        if (sys_stat.regByte & 0x04) { // OV error
//          bms_UpdateVoltages();
//          if (cellVoltages[idCellMaxVoltage] < maxCellVoltage) {
//            Serial_println("Clearing OV error");
//            bms_WriteRegister(SYS_STAT, 0x04);
//          }
//        }
//        if (sys_stat.regByte & 0x02) { // SCD
//          if (secSinceErrorCounter % 60 == 0) {
//            Serial_println("Clearing SCD error");
//            bms_WriteRegister(SYS_STAT, 0x02);
//          }
//        }
//        if (sys_stat.regByte & 0x01) { // OCD
//          if (secSinceErrorCounter % 60 == 0) {
//            Serial_println("Clearing OCD error");
//            bms_WriteRegister(SYS_STAT, 0x01);
//          }
//        }
//        
//        secSinceErrorCounter++;
//      }
    }
    else {
      errorStatus = 0;
    }
    
    return errorStatus;
//
//  }

}

//----------------------------------------------------------------------------
// should be called at least once every 250 ms to get correct coulomb counting

void bms_Update()
{
  //Serial_println("update");
  bms_UpdateCurrent(false);  // will only read new current value if alert was triggered
  bms_UpdateVoltages();
  bms_UpdateTemperatures();
  if(autoBalancingEnabled) bms_UpdateBalancingSwitches();
}

//----------------------------------------------------------------------------
// puts BMS IC into SHIP mode (i.e. switched off)

void bms_Shutdown()
{
    bool succeeded = true;
  bms_WriteRegister(SYS_CTRL1, 0x0, &succeeded);
  bms_WriteRegister(SYS_CTRL1, 0x1, &succeeded);
  bms_WriteRegister(SYS_CTRL1, 0x2, &succeeded);
}

//----------------------------------------------------------------------------

bool bms_EnableCharging()
{
    bool succeeded = true;
  Serial_println("enableCharging");
  if (bms_CheckStatus() == 0 &&
    cellVoltages[idCellMaxVoltage] < maxCellVoltage &&
    temperatures[0] < maxCellTempCharge &&
    temperatures[0] > minCellTempCharge)
  {
    uint8_t sys_ctrl2;
    sys_ctrl2 = bms_ReadRegister(SYS_CTRL2, &succeeded);
    bms_WriteRegister(SYS_CTRL2, sys_ctrl2 | 0x01, &succeeded);  // switch CHG on
    Serial_println("enableCharging: enabled");
    return true;
  }
  else {
    Serial_println("enableCharging: failed");
    return false;
  }
}

void bms_DisableCharging()
{
    bool succeeded = true;
  Serial_println("disableCharging");
  uint8_t sys_ctrl2;
  sys_ctrl2 = bms_ReadRegister(SYS_CTRL2, &succeeded);
  if(sys_ctrl2 & 0xFE) Serial_println("diableCharging: disabled");
  sys_ctrl2 = sys_ctrl2 & 0xFE;
  bms_WriteRegister(SYS_CTRL2, sys_ctrl2, &succeeded);  // switch CHG on
}

//----------------------------------------------------------------------------

bool bms_EnableDischarging()
{
    bool succeeded = true;
  Serial_println("enableDischarging");
  if (bms_CheckStatus() == 0 &&
    cellVoltages[idCellMinVoltage] > minCellVoltage &&
    temperatures[0] < maxCellTempDischarge &&
    temperatures[0] > minCellTempDischarge)
  {
    uint8_t sys_ctrl2;
    sys_ctrl2 = bms_ReadRegister(SYS_CTRL2, &succeeded);
    bms_WriteRegister(SYS_CTRL2, sys_ctrl2 | 0x02, &succeeded);  // switch DSG on
    Serial_println("enableDischarging: enabled");
    return true;
  }
  else {
    Serial_println("enableDischarging: failed");
//    Serial_println(idCellMinVoltage);
//    Serial_println(cellVoltages[idCellMinVoltage] > minCellVoltage);
//    Serial_println(temperatures[0] < maxCellTempDischarge);
//    Serial_println(temperatures[0] > minCellTempDischarge);
    return false;
  }
}

void bms_DisableDischarging()
{
    bool succeeded = true;
  Serial_println("disableDischarging");
  uint8_t sys_ctrl2;
  sys_ctrl2 = bms_ReadRegister(SYS_CTRL2, &succeeded);
  if(sys_ctrl2 & 0xFE) Serial_println("disableDischarging: disabled");
  sys_ctrl2 = sys_ctrl2 & 0xFD;
  bms_WriteRegister(SYS_CTRL2, sys_ctrl2, &succeeded);  // switch CHG on
}

//----------------------------------------------------------------------------

void bms_EnableAutoBalancing(void)
{
  autoBalancingEnabled = true;
}


//----------------------------------------------------------------------------

void bms_SetBalancingThresholds(int idleTime_min, int absVoltage_mV, uint8_t voltageDifference_mV)
{
  balancingMinIdleTime_s = idleTime_min * 60;
  balancingMinCellVoltage_mV = absVoltage_mV;
  balancingMaxVoltageDifference_mV = voltageDifference_mV;
}

//----------------------------------------------------------------------------
// sets balancing registers if balancing is allowed 
// (sufficient idle time + voltage)

uint8_t bms_UpdateBalancingSwitches(void)
{
    bool succeeded = true;
  //Serial_println("updateBalancingSwitches");
  //long idleSeconds = (millis() - idleTimestamp) / 1000;
  uint8_t numberOfSections = numberOfCells/5;
  
  // check for millis() overflow
//  if (idleSeconds < 0) {
//    idleTimestamp = 0;
//    idleSeconds = millis() / 1000;
//  }
    
  // check if balancing allowed
  if (bms_CheckStatus() == 0 &&
    //idleSeconds >= balancingMinIdleTime_s && 
    cellVoltages[idCellMaxVoltage] > balancingMinCellVoltage_mV &&
    (cellVoltages[idCellMaxVoltage] - cellVoltages[idCellMinVoltage]) > balancingMaxVoltageDifference_mV)
  {
    balancingActive = true;
    //Serial_println("Balancing enabled!");
    
    regCELLBAL_t cellbal;
    uint8_t balancingFlags;
    uint8_t balancingFlagsTarget;
    for (int section = 0; section < numberOfSections; section++)
    {
      balancingFlags = 0;
      for (int i = 0; i < 5; i++)
      {
          if ((cellVoltages[section*5 + i] - cellVoltages[idCellMinVoltage]) > balancingMaxVoltageDifference_mV) {
          
          // try to enable balancing of current cell
          balancingFlagsTarget = balancingFlags | (1 << i);

          // check if attempting to balance adjacent cells
          bool adjacentCellCollision = 
            ((balancingFlagsTarget << 1) & balancingFlags) ||
            ((balancingFlags << 1) & balancingFlagsTarget);
            
          if (adjacentCellCollision == false) {
            balancingFlags = balancingFlagsTarget;
          }          
        }
      }
      if(section == 0) cellBalReg1 = balancingFlags;
      else if(section == 1) cellBalReg2 = balancingFlags;
      else if(section == 2) cellBalReg3 = balancingFlags;
      
      // set balancing register for this section
      bms_WriteRegister(CELLBAL1+section, balancingFlags, &succeeded);
    }
  }
  else if (balancingActive == true)
  {  
    // clear all CELLBAL registers
    for (int section = 0; section < numberOfSections; section++)
    {
      Serial_print("Clearing Register CELLBAL");
      Serial_println(section+1);
      bms_WriteRegister(CELLBAL1+section, 0x0, &succeeded);
    }
    
    balancingActive = false;
  }
  return (0);
}

void bms_SetShuntResistorValue(int res_mOhm)
{
  shuntResistorValue_mOhm = res_mOhm;
}

void bms_SetThermistorBetaValue(int beta_K)
{
  thermistorBetaValue = beta_K;
}

void bms_SetTemperatureLimits(int minDischarge_degC, int maxDischarge_degC, 
  int minCharge_degC, int maxCharge_degC)
{
  // Temperature limits (°C/10)
  minCellTempDischarge = minDischarge_degC * 10;
  maxCellTempDischarge = maxDischarge_degC * 10;
  minCellTempCharge = minCharge_degC * 10;
  maxCellTempCharge = maxCharge_degC * 10;  
}

void bms_SetIdleCurrentThreshold(int current_mA)
{
  idleCurrentThreshold = current_mA;
}


//----------------------------------------------------------------------------

long bms_SetShortCircuitProtection(long current_mA, int delay_us)
{
    bool succeeded = true;
  Serial_println("setSCD");
  regPROTECT1_t protect1;
  
  // only RSNS = 1 considered
  protect1.bits.RSNS = 1;

  protect1.bits.SCD_THRESH = 0;
  for (int i = sizeof(SCD_threshold_setting) / sizeof(SCD_threshold_setting[0]) - 1; i > 0; i--) {
    if (current_mA * shuntResistorValue_mOhm / 1000 >= SCD_threshold_setting[i]) {
      protect1.bits.SCD_THRESH = i;
      Serial_print("SCD threshold: ");
      Serial_println(i);
      break;
    }
  }
  
  protect1.bits.SCD_DELAY = 0;
  for (int i = sizeof(SCD_delay_setting) / sizeof(SCD_delay_setting[0]) - 1; i > 0; i--) {
    if (delay_us >= SCD_delay_setting[i]) {
      protect1.bits.SCD_DELAY = i;
      Serial_print("SCD delay: ");
      Serial_println(i);
      break;
    }
  }
  
  bms_WriteRegister(PROTECT1, protect1.regByte, &succeeded);
  
  // returns the actual current threshold value
  return (long)SCD_threshold_setting[protect1.bits.SCD_THRESH] * 1000 / 
    shuntResistorValue_mOhm;
}

//----------------------------------------------------------------------------

//long bms_SetOvercurrentChargeProtection(long current_mA, int delay_ms)
//{
//   ToDo: Software protection for charge overcurrent
//}

//----------------------------------------------------------------------------

long bms_SetOvercurrentDischargeProtection(long current_mA, int delay_ms)
{
    bool succeeded = true;
  Serial_println("setOCD");
  regPROTECT2_t protect2;
  
  // Remark: RSNS must be set to 1 in PROTECT1 register

  protect2.bits.OCD_THRESH = 0;
  for (int i = sizeof(OCD_threshold_setting) / sizeof(OCD_threshold_setting[0]) - 1; i > 0; i--) {
    if (current_mA * shuntResistorValue_mOhm / 1000 >= OCD_threshold_setting[i]) {
      protect2.bits.OCD_THRESH = i;
      Serial_print("OCD threshold: ");
      //LOG_PRINTLN(i);
      break;
    }
  }
  
  protect2.bits.OCD_DELAY = 0;
  for (int i = sizeof(OCD_delay_setting) / sizeof(OCD_delay_setting[0]) - 1; i > 0; i--) {
    if (delay_ms >= OCD_delay_setting[i]) {
      protect2.bits.OCD_DELAY = i;
      Serial_print("OCD delay: ");
      Serial_println(i);
      break;
    }
  }
  
  bms_WriteRegister(PROTECT2, protect2.regByte, &succeeded);
 
  // returns the actual current threshold value
  return (long)OCD_threshold_setting[protect2.bits.OCD_THRESH] * 1000 / 
    shuntResistorValue_mOhm;
}


//----------------------------------------------------------------------------

int bms_SetCellUndervoltageProtection(int voltage_mV, int delay_s)
{
    bool succeeded = true;
  Serial_println("setUVP");
  regPROTECT3_t protect3;
  uint8_t uv_trip = 0;
  
  minCellVoltage = voltage_mV;
  
  protect3.regByte = bms_ReadRegister(PROTECT3, &succeeded);
  
  uv_trip = ((((long)voltage_mV - adcOffset) * 1000 / adcGain) >> 4) & 0x00FF;
  uv_trip += 1;   // always round up for lower cell voltage
  bms_WriteRegister(UV_TRIP, uv_trip, &succeeded);
  
  protect3.bits.UV_DELAY = 0;
  for (int i = sizeof(UV_delay_setting)-1; i > 0; i--) {
    if (delay_s >= UV_delay_setting[i]) {
      protect3.bits.UV_DELAY = i;
      Serial_print("UV_DELAY: ");
      Serial_println(i);
      break;
    }
  }
  
  bms_WriteRegister(PROTECT3, protect3.regByte, &succeeded);
  
  // returns the actual current threshold value
  return ((long)1 << 12 | uv_trip << 4) * adcGain / 1000 + adcOffset;
}

//----------------------------------------------------------------------------

int bms_SetCellOvervoltageProtection(int voltage_mV, int delay_s)
{
    bool succeeded = true;
  Serial_println("setOVP");
  regPROTECT3_t protect3;
  uint8_t ov_trip = 0;

  maxCellVoltage = voltage_mV;
  
  protect3.regByte = bms_ReadRegister(PROTECT3, &succeeded);
  
  ov_trip = ((((long)voltage_mV - adcOffset) * 1000 / adcGain) >> 4) & 0x00FF;
  bms_WriteRegister(OV_TRIP, ov_trip, &succeeded);
    
  protect3.bits.OV_DELAY = 0;
  for (int i = sizeof(OV_delay_setting)-1; i > 0; i--) {
    if (delay_s >= OV_delay_setting[i]) {
      protect3.bits.OV_DELAY = i;
      Serial_print("OV_DELAY: ");
      Serial_println(i);
      break;
    }
  }
  
  bms_WriteRegister(PROTECT3, protect3.regByte, &succeeded);
 
  // returns the actual current threshold value
  return ((long)1 << 13 | ov_trip << 4) * adcGain / 1000 + adcOffset;
}


//----------------------------------------------------------------------------

long bms_GetBatteryCurrent()
{
  return batCurrent;
}

//----------------------------------------------------------------------------

long bms_GetBatteryVoltage()
{
//  Serial_print("Overall Voltage: ");
//  char voltageStr[15];
//  snprintf(voltageStr, 15, "%ld", batVoltage);
//  Serial_println(voltageStr);
  
  return batVoltage;
}

//----------------------------------------------------------------------------

int bms_GetMaxCellVoltage()
{
  return cellVoltages[idCellMaxVoltage];
}

int bms_GetMinCellVoltage()
{
  return cellVoltages[idCellMinVoltage];
}

//----------------------------------------------------------------------------

int bms_GetCellVoltage(uint8_t idCell)
{
  return cellVoltages[idCell-1];
}

void bms_PrintCellBalancingStatus(void){
    Serial_printf("Setting CELLBAL%d",1);
    Serial_print(" register to: ");
    Serial_println(byte2char(cellBalReg1));
    Serial_printf("Setting CELLBAL%d",2);
    Serial_print(" register to: ");
    Serial_println(byte2char(cellBalReg2));
    Serial_printf("Setting CELLBAL%d",3);
    Serial_print(" register to: ");
    Serial_println(byte2char(cellBalReg3));
} 

//----------------------------------------------------------------------------

float bms_GetTemperatureDegC(uint8_t channel)
{
  if (channel >= 1 && channel <= 3) {
    return (float)temperatures[channel-1] / 10.0;
  }
  else
    return -273.15;   // Error: Return absolute minimum temperature
}

//----------------------------------------------------------------------------

float bms_GetTemperatureDegF(uint8_t channel)
{
  return bms_GetTemperatureDegC(channel) * 1.8 + 32;
}


//----------------------------------------------------------------------------

void bms_UpdateTemperatures()
{
  float tmp = 0;
  int adcVal = 0;
  int vtsx = 0;
  unsigned long rts = 0;
  bool succeeded = true;
  
  bms_Write(0x2C, &succeeded);
  //Serial_println("Reading back temperature");
  uint8_t buf[2] = {0, 0};
  bms_ReadMultiple(buf, 2, &succeeded);
  uint8_t adc_H = buf[0];
  uint8_t adc_L = buf[1];
  
  // calculate R_thermistor according to bq769x0 datasheet
  adcVal = ((adc_H & 0x3F) << 8) | adc_L;
  vtsx = adcVal * 0.382; // mV
  rts = 10000.0 * vtsx / (3300.0 - vtsx); // Ohm
        
  // Temperature calculation using Beta equation
  // - According to bq769x0 datasheet, only 10k thermistors should be used
  // - 25°C reference temperature for Beta equation assumed
  tmp = 1.0/(1.0/(273.15+25) + 1.0/thermistorBetaValue*log(rts/10000.0)); // K
    
  temperatures[0] = (tmp - 273.15) * 10.0;
}


//----------------------------------------------------------------------------
// If ignoreCCReadFlag == true, the current is read independent of an interrupt
// indicating the availability of a new CC reading

void bms_UpdateCurrent(bool ignoreCCReadyFlag)
{
    bool succeeded = true;
  //Serial_println("updateCurrent");
  int16_t adcVal = 0;
  regSYS_STAT_t sys_stat;
  sys_stat.regByte = bms_ReadRegister(SYS_STAT, &succeeded);
  
  if (ignoreCCReadyFlag == true || sys_stat.bits.CC_READY == 1)
  {
    adcVal = (bms_ReadRegister(0x32, &succeeded) << 8) | bms_ReadRegister(0x33, &succeeded);
    batCurrent = adcVal * 8.44 / shuntResistorValue_mOhm;  // mA

    if (batCurrent > -10 && batCurrent < 10)
    {
      batCurrent = 0;
    }
    
    // reset idleTimestamp
    if (batCurrent > idleCurrentThreshold) {
      //idleTimestamp = millis();
    }

    // no error occured which caused alert
    if (!(sys_stat.regByte & 0x3F)) {
      alertInterruptFlag = false;
    }

    bms_WriteRegister(SYS_STAT, 0x80, &succeeded);  // Clear CC ready flag	
    //Serial_println("updateCurrent: updated, CC flag cleared");
  }
}

//----------------------------------------------------------------------------
// reads all cell voltages and updates batVoltage
// now supports CRC, taken from mbed version of LibreSolar firmware (thanks to mikethezipper)

void bms_UpdateVoltages()
{
  //Serial_println("updateVoltages");
    bool succeeded = true;
  long adcVal = 0;
  uint8_t buf[4];
  int connectedCells = 0;
  idCellMaxVoltage = 0; //resets to zero before writing values to these vars
  idCellMinVoltage = 0;

  uint8_t crc;
  crc = 0;
  buf[0] = (char) VC1_HI_BYTE; // start with the first cell
  
  bms_Write(buf[0], &succeeded);     // tell slave that this is the address it is interested in

  /****************************************************\
    Note that each cell voltage is 14 bits stored across two 8 bit register locations in the BQ769x0 chip.
    This means that first we need to read register HI (in first instance this is VC1_HI_BYTE), 
    however this 8 bit piece of data has two worthless first digits - garbage.
    To remove the first two bits, the bitwise & is used. By saying A & 00111111, only the last 6 bits of A are used. 
    Meanwhile all of the 8 bits on the low side are used. So the overall reading is the last 6 bits of high in front of the 8 bits from low.
    To add the hi and lo readings together, the << is used to shift the hi value over by 8 bits, then adding it to the 8 bits.
    This is done by using the OR operator |. So the total thing looks like: adcVal = (VC_HI_BYTE & 0b00111111) << 8 | VC_LO_BYTE;
  \****************************************************/

  // will run once for each cell up to the total num of cells
  for (int i = 0; i < numberOfCells; i++) {
    if (crcEnabled == true) {
      // refer to datasheet at 10.3.1.4 "Communications Subsystem"
        bms_ReadMultiple(buf, 4, &succeeded);
//      buf[0] = bms_Read();             // VCx_HI - note that only bottom 6 bits are good
//      buf[1] = bms_Read();             // VCx_HI CRC - done on address and data
//      buf[2] = bms_Read();             // VCx_LO - all 8 bits are used
//      buf[3] = bms_Read();             // VCx_LO CRC - done on just the data byte
      
      // check if CRC matches data bytes
      // CRC of first byte includes slave address (including R/W bit) and data
      crc = _crc8_ccitt_update(0, (I2CAddress << 1) | 1);
      crc = _crc8_ccitt_update(crc, buf[0]);
      // if (crc != buf[1]){
      //   LOG_PRINTLN("VCxHI CRC doesn't match");
      //   return; // to exit without saving corrupted value
      // }
        
      // CRC of subsequent bytes contain only data
      crc = _crc8_ccitt_update(0, buf[2]);
      // if (crc != buf[3]) {
      //   LOG_PRINTLN("VCxLO CRC doesn't match");
      //   return; // to exit without saving corrupted value
      // }
    } 

    // if CRC is disabled only read 2 bytes and call it a day :)
    else { 
        uint8_t tmpBuf[2] = {0, 0};
        bms_ReadMultiple(tmpBuf, 2, &succeeded);
      buf[0] = tmpBuf[0]; // VCx_HI - note that only bottom 6 bits are good
      buf[2] = tmpBuf[1]; // VCx_LO - all 8 bits are used
    }

    // combine VCx_HI and VCx_LO bits and calculate cell voltage
    adcVal = ((buf[0] & 0x3F) << 8) | buf[2];           // read VCx_HI bits and drop the first two bits, shift left then append VCx_LO bits
//    char tempStr[20];
//    sprintf(tempStr, "ADC%d: %lu", i, adcVal);
//    Serial_print("ADC");
//    Serial_print(i);
//    Serial_print(": ");
//    Serial_println(tempStr);
//    Serial_print("Gain");
//    Serial_print(i);
//    Serial_print(": ");
//    Serial_println(adcGain);
    cellVoltages[i] = (adcVal * adcGain) / 1000 + adcOffset;  // calculate real voltage in mV
    
//    char tempStr2[20];
//    sprintf(tempStr2, "Cell%d: %d", i, cellVoltages[i]);
//    Serial_println(tempStr2);
    
    // filter out voltage readings from unconnected cell(s)
    if (cellVoltages[i] > 500) {  
      connectedCells++; // add one to the temporary cell counter var - only readings above 500mV are counted towards real cell count
    }

    if (cellVoltages[i] > cellVoltages[idCellMaxVoltage]) {
      idCellMaxVoltage = i;
    }

    if (cellVoltages[i] < cellVoltages[idCellMinVoltage] && cellVoltages[i] > 500) {
      idCellMinVoltage = i;
    }
  }
  
  long adcValPack = ((bms_ReadRegister(BAT_HI_BYTE, &succeeded) << 8) | bms_ReadRegister(BAT_LO_BYTE, &succeeded)) & 0xFFFF;
  batVoltage = 4 * adcGain * adcValPack / 1000 + (connectedCells * adcOffset); // in original LibreSolar, connectedCells is converted to byte, maybe to reduce bit size
}



//void bms_SetAlertInterruptFlag()
//{
//  interruptTimestamp = millis();
//  alertInterruptFlag = true;
//}
//
////----------------------------------------------------------------------------
//// The bq769x0 drives the ALERT pin high if the SYS_STAT register contains
//// a new value (either new CC reading or an error)
//
//void bms_AlertISR()
//{
//  if (instancePointer != 0)
//  {
//    instancePointer->setAlertInterruptFlag();
//  }
//}
