/* Microchip Technology Inc. and its subsidiaries.  You may use this software 
 * and any derivatives exclusively with Microchip products. 
 * 
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS".  NO WARRANTIES, WHETHER 
 * EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED 
 * WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A 
 * PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION 
 * WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION. 
 *
 * IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, 
 * INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND 
 * WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS 
 * BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE 
 * FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS 
 * IN ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF 
 * ANY, THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 * MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE 
 * TERMS. 
 */

/* 
 * File:   
 * Author: 
 * Comments:
 * Revision history: 
 */

// This is a guard condition so that contents of this file are not included
// more than once.  
#ifndef BQ76940_H
#define	BQ76940_H

// can be reduced to save some memory if smaller ICs are used
#define MAX_NUMBER_OF_CELLS 15
#define MAX_NUMBER_OF_THERMISTORS 3
 
// IC type/size
#define bq76920 1
#define bq76930 2
#define bq76940 3

// register map
#define SYS_STAT        0x00
#define CELLBAL1        0x01
#define CELLBAL2        0x02
#define CELLBAL3        0x03
#define SYS_CTRL1       0x04
#define SYS_CTRL2       0x05
#define PROTECT1        0x06
#define PROTECT2        0x07
#define PROTECT3        0x08
#define OV_TRIP         0x09
#define UV_TRIP         0x0A
#define CC_CFG          0x0B

#define VC1_HI_BYTE     0x0C
#define VC1_LO_BYTE     0x0D
#define VC2_HI_BYTE     0x0E
#define VC2_LO_BYTE     0x0F
#define VC3_HI_BYTE     0x10
#define VC3_LO_BYTE     0x11
#define VC4_HI_BYTE     0x12
#define VC4_LO_BYTE     0x13
#define VC5_HI_BYTE     0x14
#define VC5_LO_BYTE     0x15
#define VC6_HI_BYTE     0x16
#define VC6_LO_BYTE     0x17
#define VC7_HI_BYTE     0x18
#define VC7_LO_BYTE     0x19
#define VC8_HI_BYTE     0x1A
#define VC8_LO_BYTE     0x1B
#define VC9_HI_BYTE     0x1C
#define VC9_LO_BYTE     0x1D
#define VC10_HI_BYTE    0x1E
#define VC10_LO_BYTE    0x1F
#define VC11_HI_BYTE    0x20
#define VC11_LO_BYTE    0x21
#define VC12_HI_BYTE    0x22
#define VC12_LO_BYTE    0x23
#define VC13_HI_BYTE    0x24
#define VC13_LO_BYTE    0x25
#define VC14_HI_BYTE    0x26
#define VC14_LO_BYTE    0x27
#define VC15_HI_BYTE    0x28
#define VC15_LO_BYTE    0x29

#define BAT_HI_BYTE     0x2A
#define BAT_LO_BYTE     0x2B

#define TS1_HI_BYTE     0x2C
#define TS1_LO_BYTE     0x2D
#define TS2_HI_BYTE     0x2E
#define TS2_LO_BYTE     0x2F
#define TS3_HI_BYTE     0x30
#define TS3_LO_BYTE     0x31

#define CC_HI_BYTE      0x32
#define CC_LO_BYTE      0x33

#define ADCGAIN1        0x50
#define ADCOFFSET       0x51
#define ADCGAIN2        0x59

// function from TI reference design
#define LOW_BYTE(Data)			(uint8_t)(0xff & Data)
#define HIGH_BYTE(Data)			(uint8_t)(0xff & (Data >> 8))

// for bit clear operations of the SYS_STAT register
#define STAT_CC_READY           (0x80)
#define STAT_DEVICE_XREADY      (0x20)
#define STAT_OVRD_ALERT         (0x10)
#define STAT_UV                 (0x08)
#define STAT_OV                 (0x04)
#define STAT_SCD                (0x02)
#define STAT_OCD                (0x01)
#define STAT_FLAGS              (0x3F)

// maps for settings in protection registers



typedef union regSYS_STAT {
  struct
  {
    uint8_t OCD            :1;
    uint8_t SCD            :1;
    uint8_t OV             :1;
    uint8_t UV             :1;
    uint8_t OVRD_ALERT     :1;
    uint8_t DEVICE_XREADY  :1;
    uint8_t WAKE           :1;
    uint8_t CC_READY       :1;
  } bits;
  uint8_t regByte;
} regSYS_STAT_t;

typedef union regSYS_CTRL1 {
  struct
  {
    uint8_t SHUT_B        :1;
    uint8_t SHUT_A        :1;
    uint8_t RSVD1         :1;
    uint8_t TEMP_SEL      :1;
    uint8_t ADC_EN        :1;
    uint8_t RSVD2         :2;
    uint8_t LOAD_PRESENT  :1;
  } bits;
  uint8_t regByte;
} regSYS_CTRL1_t;

typedef union regSYS_CTRL2 {
  struct
  {
    uint8_t CHG_ON      :1;
    uint8_t DSG_ON      :1;
    uint8_t WAKE_T      :2;
    uint8_t WAKE_EN     :1;
    uint8_t CC_ONESHOT  :1;
    uint8_t CC_EN       :1;
    uint8_t DELAY_DIS   :1;
  } bits;
  uint8_t regByte;
} regSYS_CTRL2_t;

typedef union regPROTECT1 {
  struct
  {
      uint8_t SCD_THRESH      :3;
      uint8_t SCD_DELAY       :2;
      uint8_t RSVD            :2;
      uint8_t RSNS            :1;
  } bits;
  uint8_t regByte;
} regPROTECT1_t;

typedef union regPROTECT2 {
  struct
  {
    uint8_t OCD_THRESH      :4;
    uint8_t OCD_DELAY       :3;
    uint8_t RSVD            :1;
  } bits;
  uint8_t regByte;
} regPROTECT2_t;

typedef union regPROTECT3 {
  struct
  {
    uint8_t RSVD            :4;
    uint8_t OV_DELAY        :2;
    uint8_t UV_DELAY        :2;
  } bits;
  uint8_t regByte;
} regPROTECT3_t;

typedef union regCELLBAL
{
  struct
  {
      uint8_t RSVD        :3;
      uint8_t CB5         :1;
      uint8_t CB4         :1;
      uint8_t CB3         :1;
      uint8_t CB2         :1;
      uint8_t CB1         :1;
  } bits;
  uint8_t regByte;
} regCELLBAL_t;

typedef union regVCELL
{
    struct
    {
        uint8_t VC_HI;
        uint8_t VC_LO;
    } bytes;
    uint16_t regWord;
} regVCELL_t;


#include "mcc_generated_files/system.h"
#include "mcc_generated_files/i2c1.h"
#include "mcc_generated_files/pin_manager.h"
#include "Libpic30.h"
#include "BQ76940.h"
#include <xc.h>// include processor files - each processor file is guarded.  

// TODO Insert appropriate #include <>

// TODO Insert C++ class definitions if appropriate

// TODO Insert declarations

// Comment a function and leverage automatic documentation with slash star star
/**
    <p><b>Function prototype:</b></p>
  
    <p><b>Summary:</b></p>

    <p><b>Description:</b></p>

    <p><b>Precondition:</b></p>

    <p><b>Parameters:</b></p>

    <p><b>Returns:</b></p>

    <p><b>Example:</b></p>
    <code>
 
    </code>

    <p><b>Remarks:</b></p>
 */
I2C1_MESSAGE_STATUS bms_WriteRegister(const uint8_t reg, const uint8_t value, bool *succeeded);

uint8_t bms_ReadRegister(uint8_t dataAddress, bool *succeeded);

uint8_t bms_ReadMultiple(uint8_t *data, uint8_t count, bool *succeeded);

const char *byte2char(int x);

uint8_t _crc8_ccitt_update (uint8_t inCrc, uint8_t inData);

void bms_New(uint8_t bqType, int bqI2CAddress);

int bms_Begin();

bool bms_DetermineAddressAndCrc(void);

int bms_CheckStatus();

void bms_Update();

void bms_Shutdown();

bool bms_EnableCharging();

void bms_DisableCharging();

bool bms_EnableDischarging();

void bms_DisableDischarging();

void bms_EnableAutoBalancing(void);

void bms_SetBalancingThresholds(int idleTime_min, int absVoltage_mV, uint8_t voltageDifference_mV);

uint8_t bms_UpdateBalancingSwitches(void);

void bms_SetShuntResistorValue(int res_mOhm);

void bms_SetThermistorBetaValue(int beta_K);

void bms_SetTemperatureLimits(int minDischarge_degC, int maxDischarge_degC, 
  int minCharge_degC, int maxCharge_degC);

void bms_SetIdleCurrentThreshold(int current_mA);

long bms_SetShortCircuitProtection(long current_mA, int delay_us);

long bms_SetOvercurrentChargeProtection(long current_mA, int delay_ms);

long bms_SetOvercurrentDischargeProtection(long current_mA, int delay_ms);

int bms_SetCellUndervoltageProtection(int voltage_mV, int delay_s);

int bms_SetCellOvervoltageProtection(int voltage_mV, int delay_s);

long bms_GetBatteryCurrent();

long bms_GetBatteryVoltage();

int bms_GetMaxCellVoltage();

int bms_GetMinCellVoltage();

int bms_GetCellVoltage(uint8_t idCell);

void bms_PrintCellBalancingStatus(void);

float bms_GetTemperatureDegC(uint8_t channel);

float bms_GetTemperatureDegF(uint8_t channel);

void bms_UpdateTemperatures();

void bms_UpdateCurrent(bool ignoreCCReadyFlag);

void bms_UpdateVoltages();

#ifdef	__cplusplus
extern "C" {
#endif /* __cplusplus */

    // TODO If C++ is being used, regular C code needs function names to have C 
    // linkage so the functions can be used by the c code. 

#ifdef	__cplusplus
}
#endif /* __cplusplus */

#endif	/* XC_HEADER_TEMPLATE_H */

