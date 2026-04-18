/**
  Generated main.c file from MPLAB Code Configurator

  @Company
    Microchip Technology Inc.

  @File Name
    main.c

  @Summary
    This is the generated main.c using PIC24 / dsPIC33 / PIC32MM MCUs.

  @Description
    This source file provides main entry point for system initialization and application code development.
    Generation Information :
        Product Revision  :  PIC24 / dsPIC33 / PIC32MM MCUs - 1.171.4
        Device            :  dsPIC33EP512GP504
    The generated drivers are tested against the following:
        Compiler          :  XC16 v2.10
        MPLAB 	          :  MPLAB X v6.05
*/

/*
    (c) 2020 Microchip Technology Inc. and its subsidiaries. You may use this
    software and any derivatives exclusively with Microchip products.

    THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
    EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
    WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
    PARTICULAR PURPOSE, OR ITS INTERACTION WITH MICROCHIP PRODUCTS, COMBINATION
    WITH ANY OTHER PRODUCTS, OR USE IN ANY APPLICATION.

    IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
    INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
    WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
    BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
    FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
    ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
    THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.

    MICROCHIP PROVIDES THIS SOFTWARE CONDITIONALLY UPON YOUR ACCEPTANCE OF THESE
    TERMS.
*/

/**
  Section: Included Files
*/
#include <xc.h>
#include "mcc_generated_files/system.h"
#include "mcc_generated_files/uart1.h"
#include "mcc_generated_files/i2c1.h"
#include "mcc_generated_files/i2c2.h"
#include "mcc_generated_files/pin_manager.h"
#include "mcc_generated_files/can1.h"
#include "mcc_generated_files/can_types.h"
#include "mcc_generated_files/tmr1.h"
#include "mcc_generated_files/tmr2.h"
#include "mcc_generated_files/tmr3.h"
#include "mcc_generated_files/adc1.h"
#include "mcc_generated_files/ext_int.h"
#include "Libpic30.h"
#include "main.h"
#include "Serial.h"
#include "BQ76940.h"
#include "TLC59108.h"
#include "CANBus.h"
#include "BatteryProfile.h"
#include "bootloader.h"

#define EMULATE_XRB             true

#define DEBUG_ENABLED           true       //Enables serial printing of all messages
#define CELL_COUNT              13      //Number of cells in pack
//#define MIN_CELL_MV             3300    //Cell cutoff voltage
#define MIN_CELL_MV             3100    //10/25/25 05:53:30 PM Changed to 3100 to more closely match B2XR behavior
//#define LIMP_CELL_MV            3000    //Cell cutoff voltage in limp mode
#define LIMP_CELL_MV            2900    //10/25/25 05:54:27 PM Changed to 2900 to match LG HG2 minimum voltage (Actual HARD cutoff is about 2800mV)
//#define EMPTY_CELL_MV           3500    //Millivolts to conisder cell fully discharged under no load
#define EMPTY_CELL_MV           3200    //10/25/25 05:55:16 PM Changed to 3200 to more closely match B2XR behavior
#define MAX_CELL_MV             4250    //Maximum cell voltage while charger is not connected (set higher for regen braking)
//#define MAX_CELL_CHG            4120    //Maximum cell voltage while charger is connected
#define MAX_CELL_CHG            4000    //10/25/25 05:56:33 PMChanged to 4000 to more closely match B2XR behavior
//#define CELL_REENABLE_CHG       4050    //After charging has reached full, re-enable charging when cell voltage drops below this value
#define CELL_REENABLE_CHG       3900    //10/25/25 05:56:28 PM Changed to 3900 to more closely match B2XR behavior
#define CHG_CONN_PAUSE_TIME     3000    //When charger is first connected, the voltage sometimes spikes briefly, add a cooldown to prevent stopping charging
#define DO_CELL_BALANCING       1       //Flipswitch for doing balancing using BMS
#define MIN_BALANCING_MV        20      //Number of millivolts between minimum cell voltage and maximum cell voltage
#define BALANCE_MODE            0       //MANUAL SWITCH: set controller in sleep state to allow BMS to balance
#define CHARGE_DETECT_ADC       300     //Raw ADC reading on charge detect pin that indicates connected charger
#define CHARGE_START_DELAY      1000    //number of milliseconds before starting charging (debounce)
#define CHARGE_PRECHARGE_DELAY  1500    //Number of milliseconds to leave precharge circuit enabled
#define BALANCE_LED_TICKS       80      //Number of 50ms ticks for flashing indicator LED
#define BALANCE_LED_REPEAT      3       
#define SHUNT_ADC_RATIO         0.115   //ADC-to-amp conversion factor. For every value increase of 1 ADC value is SHUNT_ADC_RATIO amps
#define CHARGE_FADE_STEP        10
#define CHARGE_CURRENT_THR      -0.5     //Charge current threshold
#define POWEROFF_ADC_MAX        20      //Raw ADC value for output voltage before releasing latch circuit.
#define BUTTON_IDLE_TIME        1000    //Number of milliseconds for counting number of button presses
#define BUTTON_DEBOUNCE_TIME    30      //Number of milliseconds since last interrupt to ignore input
#define BATT_CURR_AVG_COUNT     40      //Number of elements in battery current averaging array


//Wishlist
//ESC 2.1.9 and greater:
//- no reduced brakes
//- reduction in braking and acceleration during high temp
//- built-in Amnesia
//
//ESC Before 2.1.9:
//- reduce oscillation of performance based on voltage; this seems to be related to voltage, but will need confirmation. the closer cells get to 2.5v, the more ESCs will limit power
//- preserve braking at low voltage; you have about 5 seconds after remote yells until mosfets turn off 







// Let's also initiate amp hour counting
//Two approximation strategies (try both)
//
//
//
//   Before I get too confused, there's probably some really basic clarifications that need to be instituted:
//           [Accumulated Amp Hours] should be recorded all the time
//           [SOC Denominator] is determined by from averaging [Accumulated Amp Hours] when the pack has been discharged 3 times, specifically when the lowest cell is below 3100mV or whatever the EMPTY_CELL_MV is specified as.
//           [SOC Numerator] is the same value as [SOC Denominator] when the highest cell is 4000mV or whatever the MAX_CELL_CHG is specified as. 
//           Occasionally, here's how SOC Numerator gets updated: [SOC Numerator] = [SOC Numerator]-[Accumulated Amp Hours]*[Discharge Constant] *Discharge constant is 1 at the moment
//           [SOC Numerator] is 0 when the lowest cell is 3100mV or whatever the EMPTY_CELL_MV is specified as. 
//
//
//
//Strategy 1: (close to XR stock behavior) 
// - Count amp hours 
// - Record accumulated amp hours when lowest cell is below EMPTY_CELL_MV
// - Update SOC denominator with accumulated amp hour value after 2-3 full discharges
// SOC = SOC numerator / SOC denominator





//Strategy 2: (voltage interpolation and amp hour counting)



// - Count amp hours
// - Record accumulated amp hours when lowest cell is below EMPTY_CELL_MV
// - Update SOC denominator with accumulated amp hour value after 2-3 full discharges
// SOC = SOC numerator / SOC denominator

// NEW STUFF: On bootup, read minimum cell voltage and interpolate based off known SOC denominator; 
//   if 100% is 3900mV and 0% is 3100mV and the total measured capacity is 3500mah, and we are currently reading 3500mv at the lowest cell, 
//   the numerator should start in between 0 and 3500, but NOT at 3500. 
//   Current XR battery firmware WILL start at 3500 even though the lowest cell is NOT at 3900mv on bootup

// NEW STUFF: SOC numerator should take into account lowest cell voltage.
//   If the SOC numerator is approaching 0 but lowest cell is NOT sufficiently close enough to 3100mV, the rate of SOC numerator dropping should decelerate proportionally 
//   BIG QUESTIONS: 
//           How close does the SOC numerator have to approach 0?
//           What does "sufficiently close enough to 3100mV" even mean?
//           What sort of proportionality should the SOC numerator rate of dropping be?
//



//// --- ADJUSTED SOC (HEALING) GLOBALS --- Let's comment this all out for now.
//#define HEALTHY_DELTA_THRESHOLD 100
//#define COMPENSATION_CONSTANT 5.0 
//
//volatile float adjustedSOC = 0;
//volatile float adjustedSOCBacktick = 0;
//volatile bool adjustedSOCImprovement = false;
//// --------------------------------------



//SOC Approach 1:







#define versionFromESCID 0x10024020

#define registrationStateESCID 0x10343160

#define firmwareVersionESCID 0x10343440

#define pingESCID 0x103434B0

#define modeESCID 0x103B31A0

//Global Timer Flags
volatile bool updateLED = false;        //Triggers update of the I2C LED chip
volatile bool updateCAN = false;        //Triggers sending of CAN Bus packets
volatile bool updateBMS = false;        //Triggers querying of battery management system parameters
volatile bool updateADC = false;        //Triggers querying of ADC channels
volatile bool updateDebug = false;      //Triggers debug messages printing over UART
//volatile bool updateTemperatureArray = false; //10/25/25 06:18:56 PM Triggers update of the temperatures
volatile bool verboseTemperatures = false; 

//Global Status Flags
volatile bool CANInitialized = false;   //Flag to check if CAN Bus has been initialized successfully
volatile bool BMSInitialized = false;   //Flag to check if BMS Controller has been initialized successfully
volatile bool LEDInitialized = false;   //Flag to check if I2C LED Controller has been initialized successfully
volatile bool ADCInitialized = false;   //Flag to check if ADC has been initialized successfully

//Power globals
volatile bool dischargingEnabled = false;           //Flag set true when discharging is successfully enabled on the BMS chip
volatile bool chargingEnabled = false;              //Flag set true when charging is successfully enabled on the BMS chip
volatile bool chargeSensed = false;                 //Flag set true when the charger input voltage ADC is above CHARGE_DETECT_ADC
volatile bool chargeCurrentDetected = false;        //Flag set true if the measured battery current is at a level that the cells are charging (used to tell if charging is complete)
volatile bool chargerConnected = false;             //Flag set true if the charger is connected (derived from chargeSensed)
volatile bool lastChargerConnected = false;         //Flag used to detect change in charging/not charging. Updates voltage limits on BMS when charging or not charging
volatile bool powerOffWhenChargerNotConnected = false; //DW Flag used to initiate shutoff mode after charger has been removed from the battery
volatile bool powerGood = false;                    //Flag indicating that battery can charge and discharge.
volatile uint8_t batterySOC = 0;                    //Battery percentage estimate based on linear voltage model (0-100%)
volatile uint16_t cellMinMaxDelta = 0;              //Number of millivolts between highest and lowest cells in the pack
volatile float batteryCurrentADC = 0;               //Battery current read from the shunt measured by the microcontroller's ADC
volatile float batteryCurrentBMS = 0;               //Battery current read from the shunt measured by the BMS IC
volatile uint8_t batteryAvgIndex = 0;               //Circular buffer index for the average battery current
volatile long long avgBatteryCurrent = 0;           //Average battery current in mA
volatile long batteryCurrentAvg[BATT_CURR_AVG_COUNT]= {0};  //Array to hold raw current readings. Used to calculate average
volatile bool limpMode = false;                     //Flag to indicate limp mode. Lowers the discharge limit on the BMS from MIN_CELL_MV to LIMP_CELL_MV so you can squeeze more out of the cells in a pinch
volatile bool shutdownFromESCDetected = false;

// coulomb‑counting globals
volatile float mah_consumed = 0.0f;                //milliamp‑hours consumed since boot
static uint64_t last_integration_millis = 0;       //timestamp of last current integration
static uint64_t last_capacity_update = 0;          //timestamp for calling BatteryProfile_UpdateCapacity
extern volatile bool battery_profile_capturing;

// battery profile used for SOC estimation
BatteryProfile batteryProfile = {
    .battery_curve = {3000, 3200, 3350, 3450, 3525,
                      3600, 3675, 3750, 3800, 3850, 3950},
    .battery_historical_mAh = {4000, 0, 0},
    .battery_designed_capacity_mAh = 4000
};




//Counters
volatile uint64_t chargerConnectedTime = 0;         //Number of milliseconds elapsed when the charger was last seen as connected
volatile uint64_t millis_Count = 0;                 //Increment this every millisecond
volatile uint8_t bms_interval = 0;                  //Use this to do BMS updates every 50ms
volatile uint8_t adcChannelNumber = 0;              //Index for which channel to read from since we are doing synchronous reads
volatile uint8_t debugPrint = 0;                    //Index to slow down number of UART prints
volatile uint8_t ledStatusCounter = 0;              //Index for switching between SOC and Balancing Cell Differential mode
volatile uint8_t ledBalancingFlash = 0;             //Index for blinks in the Balancing Cell Differential mode

//CAN Bus variables
volatile uint8_t canTick = 0;                
volatile uint8_t registrationCount = 0;             //Counter for ticks to do registration
volatile uint8_t registerPacketsRemaining = 7;      //After registration, we need to send the packet a few more times
volatile bool batteryRegistered = false;            //Flag to indicate XRB registration
volatile uint64_t registrationTime = 0;             //Time when the XRB is considered "registered"
volatile bool debugPacketStartupSent = false;       //For packet 0x13417, send packet once before registration is done
volatile bool softwareReleasePacketSent = false;    //For packet 0x15415, send this one time, about 3300ms after registration complete
volatile bool calibrationPacket1Sent = false;       //For packet 0x33446, send this twice, at about 3300ms and 5500ms after registration 
volatile bool calibrationPacket2Sent = false;       //For packet 0x33446, send this twice, at about 3300ms and 5500ms after registration 
volatile uint8_t canPacketID = 0;

//LED controller
volatile LED_ARRAY LED;                         //LED struct instance to control the I2C LED controller
volatile uint8_t fadeValue = 250;               //Value for brightness of the pixel that is not fully on when doing charge animation
volatile bool chargeFadeDirection = false;      //Flag to fade down (false) or up (true)
volatile bool showingBalancing = false;         //Flag to have SOC leds show SOC or balancing mV
volatile bool showBalanceComplete = true;       //Flag set true after Differential Balancing blinks finishes the blink sequence. Used to switch back to SOC display
volatile uint8_t lowBattFlasher = 0;            //Value for brightness of the first pixel in the series of 5. Used to blink when SOC is low.

//Button variables
volatile bool buttonRead = false;               //Polled state of main button
volatile uint64_t lastButtonTime = 0;           //Timestamp in ms of when button was last pressed. Used for debouncing
volatile bool buttonPressed = false;            //Flag indicating button was pressed. Set by the button interrupt. Value is debounced using timer
volatile bool buttonReady = false;              //Flag indicates that the user has finished their pressed sequence. There is a settling delay to count the number of presses.
volatile uint8_t buttonCount = 0;               //Number of times the button was pressed sequentially before the settling delay elapsed
volatile bool pairingMode = false;

//Bootloader variables
volatile bool bootloaderDownloadActive = false;     //Flag indicating bootloader is in download mode
volatile uint32_t bootloaderBlocksReceived = 0;     //Number of blocks received during download
volatile uint32_t bootloaderBytesReceived = 0;      //Total bytes received during download
volatile uint64_t bootloaderLastMessageTime = 0;    //Timestamp of last bootloader message
#define BOOTLOADER_STATUS_TIMEOUT        5000       //Timeout in ms for sending periodic status messages
#define BOOTLOADER_DOWNLOAD_TIMEOUT      10000      //Timeout in ms for download activity

// Serial bootloader variables
#define SERIAL_BL_RX_BUFFER_SIZE 256
#define SERIAL_STRING_BUFFER_SIZE SERIAL_BL_RX_BUFFER_SIZE
volatile char serialStringBuffer[SERIAL_STRING_BUFFER_SIZE]; // Buffer for accumulating incoming serial data as a string
volatile uint32_t serialStringIndex = 0;
volatile uint8_t serialBlRxBuffer[SERIAL_BL_RX_BUFFER_SIZE];
volatile uint16_t serialBlRxIndex = 0;
volatile bool serialBlFrameReady = false;

// Serial bootloader command codes
#define SERIAL_BL_CMD_START         0x01
#define SERIAL_BL_CMD_WRITE_BLOCK   0x02
#define SERIAL_BL_CMD_COMMIT        0x03
#define SERIAL_BL_CMD_ABORT         0x05
#define SERIAL_BL_FRAME_START       0xAA
#define SERIAL_BL_FRAME_END         0x55

//ADC variables - Set to max value for error checking
volatile uint16_t adc_OutputVoltageSense = 0xFFFF;          //Voltage of the output connector (what the ESC will see)
volatile uint16_t adc_ChargeVoltageSense = 0xFFFF;          //Voltage on the charge connector (read to determine if the charger is plugged in)
volatile uint16_t adc_PackVoltageSense = 0xFFFF;            //Voltage read from the battery pack (should read close to the BMS overall pack voltage measurement, otherwise a tap wire is broken)
volatile uint16_t adc_PackCurrentSense = 0xFFFF;            //Reads 487 when drawing 2.2A
volatile uint16_t adc_PackShuntReference = 0xFFFF;          //Reads 506 for above sample
volatile uint16_t adc_DischargeThermistorSense = 0xFFFF;    //Voltage read from the thermistor near the discharge FETs
volatile uint16_t adc_ChargeThermistorSense = 0xFFFF;       //Voltage read from the thermistor near the charge FETs
volatile uint16_t adc_BMSRegulatorSense = 0xFFFF;           //Voltage produced by the BMS chip's internal LDO


//Function prototypes
void shutdownSequence(void); //11/02/25 04:34:16 PM
void configureLEDs(void);
bool configureBMS(void);
void updateSOC(void);
void updateTemperatures(void); //10/25/25 06:20:05 PM
void updateADCs(void);
void updateCharging(void);
void updateLEDs(void);
void updateCANBusXRB(void);
void beginCANBusSRB();
void updateCANBusSRB(void);
void mapStatusLED(void);
void mapPercentageToLEDs(uint8_t pct, bool flashLowBattery);
void tryingToStartUp(void);
void tmr_1ms(void);
void tmr_10ms(void);
void tmr_50ms(void);
uint64_t millis(void);

//Bootloader function prototypes
void handleBootloaderMessage(CAN_MSG_OBJ *msg);
void bootloaderSendStatusMessage(void);
void bootloaderExitDownloadMode(void);

// Serial bootloader function prototypes
void serialBootloaderProcessByte(uint8_t byte);
void serialBootloaderProcessFrame(void);
void serialBootloaderSendResponse(uint8_t status, uint8_t blockNum);

int main(void)
{
    // initialize the device
    SYSTEM_Initialize();
    BL_Init();
    Serial_begin();
    ADC1_Initialize();
    I2C1_Initialize();
    I2C2_Initialize();
    TMR1_Initialize();
    TMR2_Initialize();
    TMR3_Initialize();
    PIN_MANAGER_Initialize();
    DMA_Initialize();
    CAN1_Initialize();
    CAN1_TransmitEnable();
    CAN1_ReceiveEnable();
    CAN1_OperationModeSet(CAN_CONFIGURATION_MODE);
    
    TMR1_SetInterruptHandler(&tmr_1ms);     //Point at the function for the 1ms tick
    TMR2_SetInterruptHandler(&tmr_50ms);    //Point at the function for the 50ms tick
    TMR3_SetInterruptHandler(&tmr_10ms);    //Point at the function for the 10ms tick
        
    IO_RA3_SetHigh();   //Set power latch
    IO_RC6_SetHigh();   //BMS Boot
    IO_RB15_SetHigh();  //Green Debug LED, pull low to turn on
    IO_RC9_SetHigh();   //Red Debug LED, pull low to turn on
    IO_RA7_SetHigh();   //Precharge for ESC capacitors
    IO_RA10_SetLow();   //Charger Precharge disabled
    IO_RA2_SetHigh();   //ADC Enable
        
    TMR1_Start();       //Start the 1ms timer
    TMR2_Start();       //Start the 50ms timer
    TMR3_Start();       //Start the 10ms timer
    
    ADC1_Enable();      //Turn on ADC for measuring voltages, currents, and temperature.
    
    delay(250);         //Wait for voltage to stabilize before initializing BMS. Encountered bugs without this.
        
    if(!configureBMS()){    //Try to configure BMS settings. Handle error state if we couldn't initialize.
        Serial_println("ERROR! Could not configure BMS!");
        IO_RC6_SetLow();    //Make sure BMS boot pin is off
        IO_RA7_SetHigh();   //Disable precharge for ESC capacitors if the BMS had an error
        configureLEDs();    //Configure LED array so we can show the error
        LED.G = 128;        //Set the LED to yellow when we can't configure BMS correctly.
        LED.R = 255;
        updateLEDs();       //Update LED I2C chip from LED object.
        IO_RA3_SetLow();    //Set power latch low to attempt to get the board to turn off completely. Usually the BMS will hold it turned on though.
        delay(5000);        //Wait some time before retrying.
        return 0;           //Returning will cause program to restart (and retry BMS communication).
                            //If it keeps rebooting, you'll need to unplug the balancing connector so the BMS loses power and allows full power off.
    }
    
    Serial_println("Initialized BMS!");
    
    configureLEDs();        //Configure the I2C LED driver so we can show status.
        
    updateLEDs();           //Reset the LED controller with default values.
    
    updateSOC();            //Calculate SOC with first read values from BMS
    
    for(int i = 0; i < batterySOC; i++){    //Play animation to fill up 5-dot array up to SOC amount
        mapPercentageToLEDs(i, false);      //Takes a SOC (0-100) and maps it across the 5 leds by updating LED object. Each fully-bright LED is 20%. Brightness scaled linearly.
        updateLEDs();
        __delay32(100000);
    }
    mapPercentageToLEDs(batterySOC, false); //Set 5-dot array to actual SOC after animation
    updateLEDs();                           //Call update LEDs
    
    IO_RC6_SetLow();
    
    while(!ADCInitialized) updateADCs();     //Wait for ADCs to take first samples
    
    while(!IO_RB7_GetValue()) __delay32(4000);        //Wait for button release
    bool running = true;
     
    if(CAN_CONFIGURATION_MODE == CAN1_OperationModeGet())   //Check that CAN Bus is in configuration mode
    {
        if(CAN_OP_MODE_REQUEST_SUCCESS == CAN1_OperationModeSet(CAN_NORMAL_2_0_MODE)){  //Set CAN operation mode to normal for tx/rx
            CANInitialized = true;
        }
    }
    
    if(DO_CELL_BALANCING == 1) bms_EnableAutoBalancing();   //Enable cell balancing if config has it enabled
    
    if(BALANCE_MODE) balanceMode(); //Turns off charging/discharging and allows long-term balancing if enabled
    
    dischargingEnabled = bms_EnableDischarging();           //Try enabling discharging
    chargingEnabled = bms_EnableCharging();                 //Try enabling charging
    powerGood = chargingEnabled && dischargingEnabled;      //If both charging and discharging were enabled, then we should be able to operate the ESC
    
    IO_RA7_SetLow();    //Turn off precharge for the capacitors in the ESC
    if(DEBUG_ENABLED) Serial_println("Turning off precharge!");
        
    if(EMULATE_XRB == false) beginCANBusSRB();      //SRB has four packets sent once on startup
    
    while (running) //Main loop, sits here until we want to power off the battery
    {
        buttonRead = !IO_RB7_GetValue();
        
        if(updateCAN){  //Check if timer set flag for sending CAN messages. Every 10ms for XRB, 50ms for SRB.
            if(EMULATE_XRB == true) updateCANBusXRB();
            else updateCANBusSRB();
            updateCAN = false;
        }
        
        // Process serial bootloader data
        {
            uint8_t rxByte;
            // Read all waiting bytes from interrupt-based ring buffer
            while(UART1_IsRxReady()) {
                rxByte = UART1_Read();
                serialProcessString(rxByte);
            }
            
            if(serialBlFrameReady) {
                serialBootloaderProcessFrame();
                serialBlFrameReady = false;
            }
        }
        
        
        if(updateBMS){  //Check if timer set flag to fetch statuses from BMS. Every 250ms (5 ticks of 50ms timer).
            bms_Update();   //Reads status register, voltages, current, etc from BMS
            batteryCurrentBMS = (float)bms_GetBatteryCurrent()/-1000.0; //Get current in amps from the BMS

            // integrate capacity based on latest current and elapsed time
            {
                uint64_t now = millis();
                if(last_integration_millis == 0) {
                    last_integration_millis = now;
                }
                uint64_t dt = now - last_integration_millis;
                last_integration_millis = now;

                float current_mA = batteryCurrentBMS * 1000.0f;
                mah_consumed += current_mA * ((float)dt / 3600000.0f);      // Positive current = discharge by current convention

                // periodically update the profile history (once per second)
                if(now - last_capacity_update >= 1000) {
                    BatteryProfile_UpdateCapacity(&batteryProfile,
                                                  (uint32_t)mah_consumed,
                                                  (uint16_t)bms_GetMinCellVoltage(),
                                                  (uint16_t)bms_GetMaxCellVoltage(),
                                                  current_mA,
                                                  200 /* idle threshold mA */);
                    last_capacity_update = now;
                }
            }

            chargeCurrentDetected = (batteryCurrentBMS <= CHARGE_CURRENT_THR);  //Check if we're charging
            updateSOC();    //Update the state of charge based on the voltage measured by the BMS
            updateTemperatures(); //10/25/25 06:23:30 PM Update temperature at the same time as SOC
            
            // Check maximum cell voltage and control charging
            uint16_t maxCellVoltage = bms_GetMaxCellVoltage();
            if(chargerConnected && chargingEnabled && maxCellVoltage > MAX_CELL_CHG) {
                // Disable charging if max cell voltage exceeds limit
                if(millis() - chargerConnectedTime > CHG_CONN_PAUSE_TIME){
                    bms_DisableCharging();
                    chargingEnabled = false;
                    if(DEBUG_ENABLED) Serial_printlnf("Charging disabled - max cell voltage %dmV exceeds limit %dmV", maxCellVoltage, MAX_CELL_CHG);
                }
            }
            else if (chargerConnected && !chargingEnabled && maxCellVoltage <= CELL_REENABLE_CHG) {
                // Re-enable charging if charger is connected and max cell voltage is below re-enable threshold
                chargingEnabled = bms_EnableCharging();
                if(DEBUG_ENABLED) Serial_printlnf("Charging re-enabled - max cell voltage %dmV below re-enable threshold %dmV", maxCellVoltage, CELL_REENABLE_CHG);
            }
            else if(!chargerConnected && !chargingEnabled){
                // Re-enable charging when charger is unplugged
                chargingEnabled = bms_EnableCharging();
                if(DEBUG_ENABLED) Serial_println("Charger unplugged - re-enabled charging");
            }
            
            if(bms_CheckStatus() != 0){ //Check if the BMS has an error of some kind
                powerGood = false;  //If there's an error, set powerGood false, will update LED state
            }
            updateBMS = false;
            
        }
        
        if(updateADC){  //Check if the timer set flag to fetch ADC readings. Every 50ms.
            updateADCs();   //Updates ADC value in a circular fashion. Each call to this function reads one channel, next call reads next channel, etc.
            batteryCurrentADC = SHUNT_ADC_RATIO * ((float)adc_PackCurrentSense - (float)adc_PackShuntReference);    //Calculate battery current from shunt amplifier. Should align with BMS current.
            updateCharging();   //Check the ADC input for the charge port and update if we think the pack is charging
            updateAverageCurrent(); //Take the newly read shunt current and calculate the current average
            updateADC = false;
        }
        
        if(updateLED){  //Check if the timer set flag to update the LED IC
            mapStatusLED();     //Updates the RGB LED that is in the power button
            if(showingBalancing) mapBalancingToLEDs(cellMinMaxDelta);   //If we're charging the pack, then show the delta between highest and lowest cell half the time on the 5-segment LEDs
            else mapPercentageToLEDs(batterySOC, true); //When riding the board, or half the time when charging, show the estimated state of charge on the 5-segment LEDs
            updateLEDs();   //Take the data from the LED object and update the LED driver over I2C
            updateLED = false;
        }
        
        if(updateDebug /*&& DEBUG_ENABLED*/){   //Debug message of voltages of all channels on the BMS, currents, and cell balancing status
            //Serial_printlnf("Overall Voltage: %0.3fV", bms_GetBatteryVoltage()/1000.0);
            //Serial_printlnf("Pack Current: %0.3fA, BMS Current: %0.3fA, Pack mAH %0.1f, Pack SOC %d", batteryCurrentADC, batteryCurrentBMS, mah_consumed, (int)batterySOC);
            Serial_printlnf("mAH %0.1f, SOC %d, min %0.3f", mah_consumed, (int)batterySOC, bms_GetMinCellVoltage());
            /*for(int i = 1; i <= 15; i++){
                Serial_printf("C%02d: %04d  ", i, bms_GetCellVoltage(i));
            }
            Serial_println(""); 
            bms_PrintCellBalancingStatus();*/
            updateDebug = false;
        }
        
        if(chargerConnected != lastChargerConnected){   //If the charger has just been plugged in or unplugged
            //if(chargerConnected && DEBUG_ENABLED) Serial_println("Charger connected");
            //else Serial_println("Charger not connected");
            if(chargerConnected){ 
                Serial_println("Charger just connected");
            }
            else{
                Serial_println("Charger just disconnected");
                powerOffWhenChargerNotConnected = true;
                shutdownSequence();
            }
            
            lastChargerConnected = chargerConnected;
            
        }
        
//        if(CAN1_ReceivedMessageCountGet() > 0){
//            Serial_printlnf("I have a can message");
//        }
//        uint8_t payloadData[8] = {0}; 
//        CAN_MSG_OBJ recCanMsg;
//        
//        //  Point the struct to our safe memory array!
//        recCanMsg.data = payloadData; 
//
//        if(CanReceive(&recCanMsg)){
//            //  Mask off the Node ID so the switch statement matches perfectly
//            uint32_t maskedID = recCanMsg.msgId & 0xFFFFFFF0;
//            
//            if(DEBUG_ENABLED) Serial_printlnf("Got a CAN Message %08lx: %02x %02x %02x %02x %02x %02x %02x %02x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
//            
//            switch(maskedID){
//                
//                
//                    //#define versionFromESCID 0x10024020;
//                    //
//                    //#define registrationStateESCID 0x10343160;
//                    //
//                    //#define firmwareVersionESCID 0x10343440;
//                    //
//                    //#define pingESCID 0x103434B0;
//                    //
//                    //#define modeESCID 0x103B31A0;
//                
//                
//                    case versionFromESCID:
//                        Serial_printlnf("versionFromESC ID and Data %lx: %x %x %x %x %x %x %x %x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
//                                            // Now check the data payload
//                    //                        if (recCanMsg.data[0] == 0x02) {
//                    //                            Serial_println("Received Shutdown Command 0x02!");
//                    //                            if(shutdownFromESCDetected==false){ //This way, we only trigger shutdownSequence() once
//                    //                                Serial_println("Shutdown Sequence Triggered");
//                    //                                shutdownFromESCDetected = true;
//                    //                                shutdownSequence();
//                    //
//                    //                            }
//                    //
//                    //                            //shutdown goes here shutdownSequence();
//                    //                        }
//                    //                        else{
//                    //                            Serial_println("Received ESC Ping Message");
//                    //                        }
//                        break;
//                    case registrationStateESCID:
//                        Serial_printlnf("registrationStateESC ID and Data %lx: %x %x %x %x %x %x %x %x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
//                                            // Now check the data payload
//                    //                        if (recCanMsg.data[0] == 0x02) {
//                    //                            Serial_println("Received Shutdown Command 0x02!");
//                    //                            if(shutdownFromESCDetected==false){ //This way, we only trigger shutdownSequence() once
//                    //                                Serial_println("Shutdown Sequence Triggered");
//                    //                                shutdownFromESCDetected = true;
//                    //                                shutdownSequence();
//                    //
//                    //                            }
//                    //
//                    //                            //shutdown goes here shutdownSequence();
//                    //                        }
//                    //                        else{
//                    //                            Serial_println("Received ESC Ping Message");
//                    //                        }
//                        break;
//                    
//                    case firmwareVersionESCID:
//                        Serial_printlnf("firmwareVersionESC ID and Data %lx: %x %x %x %x %x %x %x %x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
//                                        // Now check the data payload
//                    //                        if (recCanMsg.data[0] == 0x02) {
//                    //                            Serial_println("Received Shutdown Command 0x02!");
//                    //                            if(shutdownFromESCDetected==false){ //This way, we only trigger shutdownSequence() once
//                    //                                Serial_println("Shutdown Sequence Triggered");
//                    //                                shutdownFromESCDetected = true;
//                    //                                shutdownSequence();
//                    //
//                    //                            }
//                    //
//                    //                            //shutdown goes here shutdownSequence();
//                    //                        }
//                    //                        else{
//                    //                            Serial_println("Received ESC Ping Message");
//                    //                        }
//                        break;
//                        
//                    case pingESCID:
//                        Serial_printlnf("pingESC ID and Data %lx: %x %x %x %x %x %x %x %x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
//                        // Now check the data payload
//                        if (recCanMsg.data[0] == 0x02) {
//                            Serial_println("Received Shutdown Command 0x02!");
//                            if(shutdownFromESCDetected==false){ //This way, we only trigger shutdownSequence() once
//                                Serial_println("Shutdown Sequence Triggered");
//                                shutdownFromESCDetected = true;
//                                shutdownSequence();
//
//                            }
//
//                            //shutdown goes here shutdownSequence();
//                        }
//                        else{
//                            Serial_println("Received ESC Ping Message");
//                        }
//                        break;
//                        
//                    case modeESCID:
//                        Serial_printlnf("modeESC ID and Data %lx: %x %x %x ", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2]);
//                                                // Now check the data payload
//                            //                        if (recCanMsg.data[0] == 0x02) {
//                            //                            Serial_println("Received Shutdown Command 0x02!");
//                            //                            if(shutdownFromESCDetected==false){ //This way, we only trigger shutdownSequence() once
//                            //                                Serial_println("Shutdown Sequence Triggered");
//                            //                                shutdownFromESCDetected = true;
//                            //                                shutdownSequence();
//                            //
//                            //                            }
//                            //
//                            //                            //shutdown goes here shutdownSequence();
//                            //                        }
//                            //                        else{
//                            //                            Serial_println("Received ESC Ping Message");
//                            //                        }
//                        break;
//                    
//            }
//        }
        if(CAN1_ReceivedMessageCountGet() > 0){
            // Serial_printlnf("I have a can message"); // Commented out to reduce spam
        }
        
        uint8_t payloadData[8] = {0}; 
        CAN_MSG_OBJ recCanMsg;
        
        // Point the struct to our safe memory array!
        recCanMsg.data = payloadData; 

        // CHANGE THIS 'if' TO A 'while' TO DRAIN THE FIFO BUFFER COMPLETELY
        bool fwdl = false;
        while(CanReceive(&recCanMsg)){
            
            // Check for bootloader commands first (CAN IDs 0x100-0x105)
            if(recCanMsg.msgId >= 0x100 && recCanMsg.msgId <= 0x105) {
                handleBootloaderMessage(&recCanMsg);
                while(true){
                    if(CanReceive(&recCanMsg)){
                        if(recCanMsg.msgId >= 0x100 && recCanMsg.msgId <= 0x105){
                            handleBootloaderMessage(&recCanMsg);
                        }
                    }
                }
                
                fwdl = true;
                continue;  // Skip normal message processing
            }
            
            // Mask off the Node ID so the switch statement matches perfectly
            uint32_t maskedID = recCanMsg.msgId & 0xFFFFFFF0;
            
            // Only print if it's NOT a Ping message, to keep the terminal clean
            if(DEBUG_ENABLED && maskedID != pingESCID) {
                /*Serial_printlnf("Got a CAN Message %08lx: %02x %02x %02x %02x %02x %02x %02x %02x", 
                                recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], 
                                recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], 
                                recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);*/
            }
            
            /*switch(maskedID){
                
                    case versionFromESCID:
                        if(DEBUG_ENABLED) Serial_printlnf("versionFromESC   ID and Data %08lx: %02x %02x %02x %02x %02x %02x %02x %02x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
                        break;
                        
                    case registrationStateESCID:
                        if(DEBUG_ENABLED) Serial_printlnf("registrationStateESC ID and Data %08lx: %02x %02x %02x %02x %02x %02x %02x %02x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
                        break;
                    
                    case firmwareVersionESCID:
                        // Uncomment this if you want to see them, but they spam a lot on boot!
                        //if(DEBUG_ENABLED) Serial_printlnf("[FIRMWARE]  ID and Data %08lx: %02x %02x %02x %02x %02x %02x %02x %02x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
                        break;
                        
                    case pingESCID:
                        // Now check the data payload. We only care if the ESC tells us to shut down.
                        if (recCanMsg.data[0] == 0x02) {
                            Serial_println("Received Shutdown Command 0x02 !!!");
                            if(shutdownFromESCDetected==false){ //This way, we only trigger shutdownSequence() once
                                Serial_println("Shutdown Sequence Triggered");
                                shutdownFromESCDetected = true;
                                shutdownSequence();
                            }
                        }
                        // We deleted the "Received ESC Ping Message" to stop the spam!
                        break;
                        
                    case modeESCID:
                        if(DEBUG_ENABLED) Serial_printlnf("modeESC      ID and Data %08lx: %02x %02x %02x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2]);
                        if (recCanMsg.data[1] == 0x15) {
                            Serial_println("ESC ACKNOWLEDGES 5-BUTTON PRESS");
                            Serial_println("TURNING BUTTON LED BLUE");
                            
                        }
                        else if (recCanMsg.data[1] == 0x16) {
                            Serial_println("ESC HAS ANNOUNCED ENDING PAIRING MODE");
                            Serial_println("TURNING BUTTON BACK TO PREVIOUS STATE");
                            
                        }
                        break;
                        
                    // THE CATCH-ALL: This will print ANY message that doesn't match the 5 IDs above
                    default:
                        if(DEBUG_ENABLED) Serial_printlnf("[UNKNOWN]   ID and Data %08lx: %02x %02x %02x %02x %02x %02x %02x %02x", recCanMsg.msgId, recCanMsg.data[0], recCanMsg.data[1], recCanMsg.data[2], recCanMsg.data[3], recCanMsg.data[4], recCanMsg.data[5], recCanMsg.data[6], recCanMsg.data[7]);
                        break;
            }*/
        }
        
        if(buttonPressed && buttonReady){   //Check if a series of button presses has finished (triggered by button interrupt)
            buttonPressed = false;          //Reset these flags such that they will be set again on the next series of button presses
            buttonReady = false;
            
            if(buttonCount == 5){
                pairingMode = true;
                sendButtonStatePacket();
            }
            else if(buttonCount == 6){   //For 6 button presses, go into programming mode. Use this so you don't end up with the BMS in a weird state
                LED.B = 255;        //Set LED to Cyan
                LED.G = 40;
                LED.R = 0;
                updateLEDs();
                while(!(buttonPressed && buttonReady)){ //Sit here and twiddle our thumbs with a funny animation unless the button gets pressed again!
                    for(int i = 0; i <= 100; i++){
                        mapPercentageToLEDs(i, false);
                        updateLEDs();
                        __delay32(50000);
                    }
                    for(int i = 100; i >= 0; i--){
                        mapPercentageToLEDs(i, false);
                        updateLEDs();
                        __delay32(50000);
                    }
                }
                buttonPressed = false;
                buttonReady = false;
            }
            else if(buttonCount == 7){  //For 7 button presses, go into limp mode which lowers the discharge limits (so you can get back home)
                if(limpMode){
                    limpMode = false;
                    bms_SetCellUndervoltageProtection(MIN_CELL_MV, 3); // delay in s    
                }
                else{
                    limpMode = true;
                    bms_SetCellUndervoltageProtection(LIMP_CELL_MV, 3); // delay in s
                }
            }
            else{   //Any other amount of button presses will turn off the skateboard
                running = false;
            }
        }
        
        // Bootloader periodic status sending
        if(bootloaderDownloadActive) {
            bootloaderSendStatusMessage();
        }
        
        __delay32(100);
    }
    
    shutdownSequence();
       
    
}

void shutdownSequence(void){
    
    bms_DisableDischarging();   //Disconnect battery from speed controller
    bms_DisableCharging();
    
    while(!IO_RB7_GetValue()){  //Wait for button to be released
        
    }
    
    for(int i = batterySOC; i <= 100; i++){ //Play shutdown animation
        mapPercentageToLEDs(i, false);
        updateLEDs();
        __delay32(50000);
    }
    for(int i = 100; i >= 0; i--){
        mapPercentageToLEDs(i, false);
        updateLEDs();
        __delay32(100000);
    }
    bms_Shutdown(); //Turn off BMS (put in ship mode), otherwise LDO will hold power latch on and the power system won't shut off.

    //Sit here and loop until the ESC capacitors are discharged. If we don't, the stored energy will cause the PCB to wake back up due to the OR circuit.
    while(IO_RB7_GetValue() && adc_OutputVoltageSense > POWEROFF_ADC_MAX){  
        if(updateADC){
            updateADCs();
            batteryCurrentADC = SHUNT_ADC_RATIO * ((float)adc_PackCurrentSense - (float)adc_PackShuntReference);
            updateCharging();
            updateADC = true;
        }
        if(DEBUG_ENABLED) Serial_printlnf("Output ADC: %d", adc_OutputVoltageSense);
        delay(250);
        
    }
    __delay32(40000);
    
    Serial_println("Powering Off"); //Final hurrah before turning off the power latch
    
    IO_RA3_SetLow();        //Clear power latch, now that the BMS is off and the caps are discharged, we should be able to power off fully.
    __delay32(40000000);    //Wait some time after clearing power latch until MCU has brownout.
    
    return 1; 



}

//Configures the TLC59108 LED driver. We're using PWM mode
void configureLEDs(void){
    IO_RC5_SetHigh();   //Set reset pin for LED chip high to enable it
    __delay32(10000);
    led_writeRegister(0x00, 0x01);
    led_writeRegister(0x0C, 0xAA);
    led_writeRegister(0x0D, 0xAA);
}

//Sets the BMS registers. Returns true or false indicating if there were errors during setup.
bool configureBMS(void){
    bool success = true;
    IO_RC6_SetHigh();       // BMS Boot pin
    __delay32(10000);       // Wait a few milliseconds on BOOT before trying to communicate
    bms_New(3, 0x18);       // Using the BQ76940 with 15 cells on 0x18
    success = bms_Begin();  // Try discovering BMS.
    if(!success) return false;  // Don't bother doing config if we couldn't find BMS chip
    bms_SetTemperatureLimits(-20, 45, 0, 45);   // Set temperature limits
    bms_SetShuntResistorValue(2);   // Shunt resistance on the board is 2 milliOhms
    bms_SetShortCircuitProtection(45000, 200);  // Short circuit protection of 40A, delay of 200us
    bms_SetOvercurrentDischargeProtection(40000, 320); // Overcurrent protection of 30A, delay of 320 ms
    bms_SetCellUndervoltageProtection(MIN_CELL_MV, 3); // delay in s
    bms_SetCellOvervoltageProtection(MAX_CELL_MV, 3);  // delay in s
    bms_SetBalancingThresholds(0, MIN_CELL_MV, MIN_BALANCING_MV);
    bms_SetIdleCurrentThreshold(100);
    bms_Update();   //Fetch first set of data from all cells
    return success;
}

void updateTemperatures(void){
    if(verboseTemperatures==true){
        Serial_println("Temperatures: ");
        Serial_printlnf("Sensor 1: %f", bms_GetTemperatureDegC(1));
        //Serial_printlnf("Sensor 2: %f", bms_GetTemperatureDegC(2));
        //Serial_printlnf("Sensor 3: %f", bms_GetTemperatureDegC(3));
    
    }
    
    
    


}


//SOC calculation using the BatteryProfile helper routines.  The first
//time updateSOC() is called the routine uses a voltage lookup to obtain
//an initial state‑of‑charge and seeds the mAh consumed counter from that
//value.  On subsequent invocations the historical‑capacity algorithm is
//used instead, with the continuously‑integrated mAh value as input.
void updateSOC(void){
    static bool firstCall = true;
    float soc = 0.0f;

    if (firstCall) {
        // voltage‑based estimate for initial boot
        uint16_t minV = (uint16_t)bms_GetMinCellVoltage();
        soc = BatteryProfile_GetSOCFromVoltage(&batteryProfile, minV);
        Serial_printlnf("Initial SOC: %d", (int)soc);
        // initialize consumed counter using the lookup
        mah_consumed = (float)BatteryProfile_ConsumedFromSOC(&batteryProfile, soc);
        Serial_printlnf("Initial mAH consumed: %d", (int)mah_consumed);
        firstCall = false;
    } else {
        // historical capacity approach thereafter
        soc = BatteryProfile_GetSOCFromHistoricalCapacity(&batteryProfile,
                                                         (uint32_t)mah_consumed);
    }

    // clamp results and write the global
    if (soc < 0.0f) soc = 0.0f;
    if (soc > 100.0f) soc = 100.0f;
    batterySOC = (uint8_t)soc;
}

//Checks if the charger has been connected. This is a bit overkill in the current implementation. I believe there is a precharge circuit, but I'm not using it at the moment.
void updateCharging(void){
    if(adc_ChargeVoltageSense > CHARGE_DETECT_ADC && adc_ChargeVoltageSense != 0xFFFF){
        if(!chargeSensed){
            chargerConnectedTime = millis();
        }
        chargeSensed = true;
        //bms_DisableCharging();  
    }
    else{
        chargeSensed = false;
    }
    
    if(chargeSensed && millis() > (chargerConnectedTime + CHARGE_START_DELAY + CHARGE_PRECHARGE_DELAY)){
        chargerConnected = true;
        IO_RB15_SetLow();
        IO_RC9_SetHigh();   //Red debug LED
        //LED.B = 255;
        //IO_RA10_SetLow();
    }
    else if(chargeSensed && millis() > (chargerConnectedTime + CHARGE_START_DELAY)){
        chargerConnected = true;
        IO_RB15_SetHigh();
        IO_RC9_SetLow();
        //LED.B = 0;
        //IO_RA10_SetHigh();
    }
    else{
        chargerConnected = false;
        IO_RB15_SetHigh();
        IO_RC9_SetHigh();
        //LED.B = 0;
    }
}

//Takes past readings from the BMS and calculates a rolling average of the battery current using an array
void updateAverageCurrent(){
    avgBatteryCurrent = 0;      //Reset average battery current
    batteryCurrentAvg[batteryAvgIndex] = bms_GetBatteryCurrent();   //Update array with most recent current reading from BMS
    batteryAvgIndex++;  //Increment index for circular buffer
    if(batteryAvgIndex >= BATT_CURR_AVG_COUNT) batteryAvgIndex = 0; //Loop circular buffer index back to 0 if we've reached the end
    for(uint8_t k = 0; k < BATT_CURR_AVG_COUNT; k++) avgBatteryCurrent += batteryCurrentAvg[k]; //Sum up the currents in the array
    avgBatteryCurrent /= BATT_CURR_AVG_COUNT;   //Divide by number of elements to get average
}


//For XRB Emulation - Packet 0x13417 and 0x23417
void sendDebugPacket(void){
    if(!batteryRegistered){
        if(!debugPacketStartupSent){
            CanSend(0x90134170, 0x00, 0x7D, 0x00, 0x64, 0x00, 0x64, 0x00, 0x00, 1);
            debugPacketStartupSent = true;
        }
        __delay32(100000);
        CanSend(0x90234170, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 1);
    }
    else{
        __delay32(100000);
        CanSend(0x90134170, 0x00, 0x0E, 0xAB, 0x0E, 0x00, 0xBE, 0x00, 0x00, 1);
        __delay32(100000);
        CanSend(0x90234170, 0x02, 0x70, 0x99, 0x14, 0x00, 0x80, 0x4F, 0x12, 1);
    }
}

//For XRB Emulation - Packet 0x15415
void sendSoftwareReleasePacket(void){
    if(batteryRegistered && !softwareReleasePacketSent){
        if(millis() - registrationTime > 3000){ //Wait a few seconds after registration before sending this
            CanSend(0x90154150, 0x00, 0x00, 0x02, 0xFF, 0x9E, 0xFF, 0xFF, 0xFF, 1);
            softwareReleasePacketSent = true;
        }
    }
}

//For XRB Emulation - Packet 0x33440
void sendVersionPacket(void){
    if(!batteryRegistered) CanSend(0x90334400, 0x02, 0x06, 0x09, 0x39, 0x30, 0x38, 0x34, 0x30, 1);
}

//For XRB Emulation - Packet 0x33441
void sendSerialPacket(void){
    CanSend(0x90334410, 0x61, 0xAC, 0xED, 0x12, 0x57, 0x8D, 0x01, 0x41, 1);
}

//For XRB Emulation - Packet 0x33442
void sendBatteryPingPacket(void){
    CanSend(0x90334420, 0x00, 0x00, 0xA2, 0x0F, 0x00, 0x00, 0x00, 0x00, 1);
}

//For XRB Emulation - Packet 0x33443
void sendIdentifierPacket(void){
    CanSend(0x90334430, 0x81, 0x10, 0xC4, 0x09, 0x0D, 0x00, 0x00, 0x00, 1);
}

//For XRB Emulation - Packet 0x33445
void sendVoltagesPacket(void){
    uint32_t overallVolt = (bms_GetBatteryVoltage()/1000.0);
    if(batteryRegistered){
        uint16_t minV = bms_GetMinCellVoltage();
        uint16_t maxV = bms_GetMaxCellVoltage();
        uint32_t b1Volt = (overallVolt >> 8)&255;
        uint32_t b2Volt = (overallVolt >> 16)&255;
        
        CanSend(0x90334450, (minV & 255), (minV >> 8), (maxV & 255), (maxV >> 8), (overallVolt & 255), b1Volt, b2Volt, 0x00, 1);
    }
    else{
        CanSend(0x90334450, 0x00, 0x00, 0x00, 0x00, (overallVolt & 255), (overallVolt >> 8)&255, (overallVolt >> 16)&255, (overallVolt >> 24)&255, 1);
    }
}

//For XRB Emulation - Packet 0x33446
void sendCalibrationPacket(void){
    if(batteryRegistered && !calibrationPacket1Sent){
        if(millis() - registrationTime > 3000){ //Wait a few seconds after registration before sending this
            CanSend(0x90334460, 0x24, 0x0A, 0x73, 0x0A, 0x95, 0x0A, 0x95, 0x0A, 1);
            calibrationPacket1Sent = true; //Set this so we only send this message once
        }
    }
    if(batteryRegistered && !calibrationPacket2Sent){
        if(millis() - registrationTime > 5000){ //Wait a few seconds after registration before sending this
            CanSend(0x90334460, 0x24, 0x0A, 0x73, 0x0A, 0x95, 0x0A, 0x95, 0x0A, 1);
            calibrationPacket2Sent = true; //Set this so we only send this message once
        }
    }
}

//For XRB Emulation - Packet 0x33447
void sendAmperagePacket(void){
    if(batteryRegistered){
        uint32_t battCurrentUnsigned = bms_GetBatteryCurrent();
        uint32_t avgCurrentUnsigned = avgBatteryCurrent;
        CanSend(0x90334470, avgCurrentUnsigned & 255, (avgCurrentUnsigned >> 8) & 255, (avgCurrentUnsigned >> 16) & 255, (avgCurrentUnsigned >> 24) & 255, battCurrentUnsigned & 255, (battCurrentUnsigned >> 8) & 255, (battCurrentUnsigned >> 16) & 255, (battCurrentUnsigned >> 24) & 255, 1);
    }
}

//For XRB Emulation - Packet 0x33448
void sendCounterPacket(void){
    if(batteryRegistered){
        CanSend(0x90334480, 0xB8, 0x88, 0x00, 0x00, 0xC8, 0x33, 0x00, 0x00, 1);
    }
    else{
        CanSend(0x90334480, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 1);
    }
}

//For XRB Emulation - Packet 0x33449
void sendSOCPacket(void){
    if(batteryRegistered){
        //CanSend(0x90334490, 0x9B, 0x07, 0xC4, 0x09, batterySOC, 0x00, 0x05, 0x00, 1);
        CanSend(0x90334490, 0xE4, 0x0D, 0xA2, 0x0F, batterySOC, 0x00, 0x05, 0x00, 1);
    }
}

//For XRB Emulation - Packet 0x3344A
void sendTimerPacket(void){
    if(batteryRegistered){
        CanSend(0x903344A0, chargerConnected ? 0x01:0x00, 0x00, 0x73, 0x78, 0x00, 0x00, millis() % 255, 0x00, 1);
    }
}

//For XRB Emulation - Packet 0x3344E
void sendTimestampPacket(void){
    if(batteryRegistered){
        uint64_t someTime = millis() + 10000;
        CanSend(0x903344E0, (someTime/1000)%60, (someTime/60000)%60, (someTime/86400000)%12, 0x13, 0x04, 0x09, 0x18, 0x00, 1);  //Don't care about date.
    }
}

//For XRB Emulation - Packet 0x3B41A
void sendButtonStatePacket(void){
    CanSend(0x903B41A0, 0x00, 0x0D, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 1);
}

//For XRB Emulation - Packet 0x33417
void sendRegistrationPacket(void){
    if(registrationCount <= 10){
        if(registrationCount == 7){
            sendSerialPacket();      //Based on trace, serial number is send at the third-to-last "unregistered" registration packet
            __delay32(100000);
            sendIdentifierPacket();
            __delay32(100000);
        }
        CanSend(0x90334170, 0x00, 0x7D, 0x00, 0x64, 0x00, 0x64, 0x00, 0x00, 1);
        registrationCount++;
    }
    else if(registerPacketsRemaining > 0){
        batteryRegistered = true;
        registrationTime = millis();
        if(registerPacketsRemaining > 4) CanSend(0x90334170, 0x01, 0x0A, 0x00, 0x64, 0x00, 0x64, 0x00, 0x00, 1);
        else if(registerPacketsRemaining > 3) CanSend(0x90334170, 0x00, 0x7D, 0x00, 0x64, 0x00, 0x64, 0x00, 0x00, 1);
        else CanSend(0x90334170, 0x01, 0x0A, 0x01, 0x39, 0x30, 0x38, 0x34, 0x30, 1);
        registerPacketsRemaining--;
    }
}

//Spoofs all necessary CAN bus packets needed for XRB emulation
void updateCANBusXRB(void){
    //canTick is an interval counter for sending packets at certain intervals
    if(canTick % 2 == 0){           //Every 20ms if called from 10ms tick
        sendRegistrationPacket();   //Sends registration packet upon startup. Also sets batteryRegistered flag after registration packets sent.
        __delay32(100000);
        sendVersionPacket();         //Sends battery seial packet to ESC. Internally gated by resistration status
        __delay32(100000);
    }
    else{                           //Also every 20ms if called from 10ms tick, use this to stagger some packets
        sendVoltagesPacket();
        __delay32(100000);
        sendCounterPacket();
        __delay32(100000);
        sendSoftwareReleasePacket();
        __delay32(100000);
    }
    if(canTick % 5 == 0){           //Every 50ms if called from 10ms tick
        sendDebugPacket();
        __delay32(100000);
        sendTimerPacket();
        __delay32(100000);
    }
    if(canTick >= 24){       //Every 250ms
        sendBatteryPingPacket();
        __delay32(100000);
        sendIdentifierPacket();
        __delay32(100000);
        sendSOCPacket();
        __delay32(100000);
        sendCalibrationPacket();
        __delay32(100000);
        sendTimestampPacket();
        __delay32(100000);
        sendAmperagePacket();
        __delay32(100000);
        sendTimestampPacket();
        canTick = 0;
    }
    else{
        canTick++;
    }
}

//Sends the first set of CAN bus packets needed for SRB emulation
void beginCANBusSRB(void){
    Serial_println("Begin SRB Emulation");
    __delay32(1000000);
    
    CanSend(0x0B57ED00, 0x01, 0x04, 0x01, 0x33, 0x66, 0x34, 0x35, 0x31, 1);   //SRB - BeamBreak
    __delay32(200000);

    CanSend(0x0B57ED01, 0xEE, 0xFF, 0xC0, 0x00, 0xf7, 0x8a, 0x01, 0x01, 1);   //SRB - BeamBreak
    __delay32(200000);
    
    CanSend(0x0B57ED03, 0xD2, 0x0F, 0xCA, 0x08, 0x0C, 0x00, 0x00, 0x00, 1);       //SRB
    __delay32(1000000);
}

//Spoofs all necessary CAN bus packets needed for SRB emulation
void updateCANBusSRB(void){
    Serial_println("SRB Emulation");
    if(canPacketID == 0){
        CanSend(0x0B57ED02, 0x00, 0x00, 0xC4, 0x09, 0x00, 0x00, 0x00, 0x00, 1);
    }
    else if(canPacketID == 1){
        CanSend(0x0B57ED10, 0xF5, 0x0C, 0x00, 0x0D, 0xCD, 0x9B, 0x00, 0x00, 1);
    }
    else if(canPacketID == 2){
        CanSend(0x0B57ED14, 0x9B, 0x07, 0xC4, 0x09, batterySOC, 0x00, 0x05, 0x00, 1);
    }
    else{
        CanSend(0x0B57ED15, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7F, 0x00, 1);
    }
    if(canPacketID < 3) canPacketID++;
    else canPacketID = 0;
}

//Based on the power mode, this changes the color of the RGB LED in the button on the XRB
void mapStatusLED(void){
    if(powerGood){  //Charging and discharging are functional
        if(pairingMode){
            LED.R = 0;
            LED.G = 0;
            LED.B = 255;
        }
        else if(bootloaderDownloadActive){
            LED.R = 255;
            LED.G = 0;
            LED.B = 255;
        }
        else if(showingBalancing){   //On the differential flash mode (when charging only), show RGB LED as blue
            LED.R = 0;
            LED.G = 0;
            LED.B = 255;
        }
        else if(limpMode){      //In Limp Mode, show blue as well
            LED.R = 0;
            LED.G = 0;
            LED.B = 255;
        }
        else if(battery_profile_capturing){     // TODO: keep this? When capturing battery profile, show cyan
            LED.R = 0;
            LED.G = 255;
            LED.B = 255;
        }
        else if(chargerConnected && chargingEnabled){   //Currently charging the battery - fade red
            LED.R = fadeValue;
            LED.G = 0;
            LED.B = 0;
        }
        else if(chargerConnected && !chargingEnabled){  //Finished charging the battery - fade green
            LED.R = 0;
            LED.G = fadeValue;
            LED.B = 0;
        }
        else{
            LED.R = 0;
            LED.G = 255;
            LED.B = 0;
        }
    }
    else{   //Some kind of BMS or other power system error
        LED.R = 255;
        LED.G = 0;
        LED.B = 0;
    }
}

//Takes a SOC (0-100) and maps it across the 5 leds by updating LED object. Each fully-bright LED is 20%. Brightness scaled linearly.
void mapPercentageToLEDs(uint8_t pct, bool flashLowBattery){
    if(pct > 100) pct = 100;
    uint8_t percentage = (pct%20) * 12.75;              //If we're not charging, then linearly fade the LED that is not at 100%
    if(chargeCurrentDetected) percentage = fadeValue;   //If we're charging the battery, then animate the segment that is not 100% bright
    if(pct == 0){
        LED.S1 = lowBattFlasher;
        LED.S2 = 0;
        LED.S3 = 0;
        LED.S4 = 0;
        LED.S5 = 0;
        
    }
    else if(pct <= 20){
        LED.S1 = percentage;
        LED.S2 = 0;
        LED.S3 = 0;
        LED.S4 = 0;
        LED.S5 = 0;
    }
    else if(pct <= 40){
        LED.S1 = 255;
        LED.S2 = percentage;
        LED.S3 = 0;
        LED.S4 = 0;
        LED.S5 = 0;    
    }
    else if(pct <= 60){
        LED.S1 = 255;
        LED.S2 = 255;
        LED.S3 = percentage;
        LED.S4 = 0;
        LED.S5 = 0;    
    }
    else if(pct <= 80){
        LED.S1 = 255;
        LED.S2 = 255;
        LED.S3 = 255;
        LED.S4 = percentage;
        LED.S5 = 0;    
    }
    else{
        LED.S1 = 255;
        LED.S2 = 255;
        LED.S3 = 255;
        LED.S4 = 255;
        LED.S5 = percentage;    
    }
}

//Takes a mV delta (between highest and lowest cell) and flashes the 5-segment LEDs to show mV difference when charging. Each segment represents 100mv, and each flash is 10mV
void mapBalancingToLEDs(uint16_t delta){

    if(delta > 500) delta = 500;    //If delta is > 500mV, cap it since we only have 5 segments
    
    uint16_t deltaFlashes = (delta%100)/10; //Calculate 10's of milliVolts. This is the number of flashes per time period
    uint8_t flashedLEDValue = 0;
    if(ledBalancingFlash%8 == 0 && (ledBalancingFlash >> 3) < deltaFlashes){    //Slow down the flash animation. Turn LED to bright if we're less than the number of flashes needed
        flashedLEDValue = 255;
    }
    ledBalancingFlash++;    //Interval counter for flashing
    if(ledBalancingFlash >= BALANCE_LED_TICKS){ //Finished all of the flashes for this sequence
        ledBalancingFlash = 0;
        showBalanceComplete = true;
    }
    
    if(delta < 100){
        LED.S1 = flashedLEDValue;
        LED.S2 = 0;
        LED.S3 = 0;
        LED.S4 = 0;
        LED.S5 = 0;
    }
    else if(delta < 200){
        LED.S1 = 255;
        LED.S2 = flashedLEDValue;
        LED.S3 = 0;
        LED.S4 = 0;
        LED.S5 = 0;    
    }
    else if(delta < 300){
        LED.S1 = 255;
        LED.S2 = 255;
        LED.S3 = flashedLEDValue;
        LED.S4 = 0;
        LED.S5 = 0;    
    }
    else if(delta < 400){
        LED.S1 = 255;
        LED.S2 = 255;
        LED.S3 = 255;
        LED.S4 = flashedLEDValue;
        LED.S5 = 0;    
    }
    else{
        LED.S1 = 255;
        LED.S2 = 255;
        LED.S3 = 255;
        LED.S4 = 255;
        LED.S5 = flashedLEDValue;    
    }
}

//Takes global LED object and updates the TLC59108 PWM registers
void updateLEDs(void){
    uint8_t LEDData1[8] = {LED.S1,LED.S2,LED.S3,LED.S4,LED.S5,LED.R,LED.G,LED.B};   //Package up data in array
    led_writeRegisters(0xA2, LEDData1, 8); 
}


void updateADCs(void){
    ADC1_CHANNEL channel;
    
    switch(adcChannelNumber){
        case 0:
            channel = channel_AN0;
            break;
        case 1:
            channel = channel_AN1;
            break;
        case 2:
            channel = channel_AN2;
            break;
        case 3:
            channel = channel_AN4;
            break;
        case 4:
            channel = channel_AN5;
            break;
        case 5:
            channel = channel_AN6;
            break;
        case 6:
            channel = channel_AN7;
            break;
        case 7:
            channel = channel_AN8;
            break;
    }
    
    ADC1_Enable();
    ADC1_ChannelSelect(channel);
    ADC1_SoftwareTriggerEnable();
    __delay32(1000);    //Provide Delay
    ADC1_SoftwareTriggerDisable();
    while(!ADC1_IsConversionComplete(channel));
    uint16_t result = ADC1_ConversionResultGet(channel);
    ADC1_Disable(); 
    
    switch(adcChannelNumber){
        case 0:
            adc_BMSRegulatorSense = result;
            //Serial_printlnf("BMS ADC: %u", result);
            break;
        case 1:
            adc_PackCurrentSense = result;
            //Serial_printlnf("Pack Current ADC: %u", result);
            break;
        case 2:
            adc_PackShuntReference = result;
            //Serial_printlnf("Pack Shunt Ref ADC: %u", result);
            break;
        case 3:
            adc_ChargeThermistorSense = result;
            //Serial_printlnf("Charge Thermistor ADC: %u", result);
            break;
        case 4:
            adc_DischargeThermistorSense = result;
            //Serial_printlnf("Discharge Thermistor ADC: %u", result);
            break;
        case 5:
            adc_PackVoltageSense = result;
            //Serial_printlnf("Pack Voltage ADC: %u", result);
            break;
        case 6:
            adc_ChargeVoltageSense = result;
            //Serial_printlnf("Charge Voltage ADC: %u", result);
            break;
        case 7:
            adc_OutputVoltageSense = result;
            //Serial_printlnf("Output Voltage ADC: %u", result);
            break;
    }
    
    adcChannelNumber++;
    if(adcChannelNumber > 7){
        ADCInitialized = true;
        adcChannelNumber = 0;
    }
}

//Interrupt for button press, called upon release
void EX_INT0_CallBack(void){
    
    if(millis() - lastButtonTime < BUTTON_DEBOUNCE_TIME) return;    //Debounce. Return if it's too soon after the last press
    
    buttonPressed = true;   //Set flag indicating button was pressed once
    
    if(millis() - lastButtonTime > BUTTON_IDLE_TIME){   //Time since last button press was longer than settling time. Consider this a new sequence of presses
        buttonCount = 1; //Just started pressing button, only pressed once
        buttonReady = false; //Just started pressing button, wait for idle time to elapse.
    }
    else {  //We recently pressed the button, so increment the count for sequential button press
        buttonCount++; 
    }
    
    lastButtonTime = millis();  //Take screenshot of clock for debounce
    
    if(DEBUG_ENABLED) Serial_printlnf("Interrupt! Button Presses: %u", buttonCount);
}

//Tick to update counter for millis() function
void tmr_1ms(void){
    millis_Count += 1;
}

//Tick to perform events every 10ms
void tmr_10ms(void){
    updateADC = true;   //Read from one ADC channel every 10ms, all 8 channels takes 80ms
    if(millis() - lastButtonTime > BUTTON_IDLE_TIME && buttonPressed){  
        buttonReady = true; //Check the last time the button was released. If we recently pressed it and the settle time has elapsed, consider the sequence finished
    } 
    if(EMULATE_XRB == true) updateCAN = true;   //On XRB, we need to send CAN packets every 10ms
}

//Tick to perform events every 50ms
void tmr_50ms(void){
    updateLED = true;                       //Trigger LED array updating
    bms_interval += 1;                      //Update interval counter for BMS updating
    if(bms_interval >= 5){                  //Trigger BMS update every 5 ticks (250ms) and reset the interval counter
        lowBattFlasher = ~lowBattFlasher;   //Every 250ms I'm also updating the LED indicator when we're at 0% SOC. Goes from 0->255->0->repeat
        updateBMS = true;                   //Main loop will read this flag
        bms_interval = 0;                   //Reset our interval
    }
    debugPrint += 1;                        //Update interval for printing battery debug info
    if(debugPrint >= 100){                  //Print debug info every 10 ticks (5000ms)
        debugPrint = 0;                     //Reset our interval
        updateDebug = true;                 //Main loop will read this flag
    }
    ledStatusCounter++;                     //If we're charging, this will switch between the SOC display and the Differential Voltage display
    if(ledStatusCounter > BALANCE_LED_TICKS * BALANCE_LED_REPEAT && (showBalanceComplete || !showingBalancing)){
        ledStatusCounter = 0;
        ledBalancingFlash = 0;
        showBalanceComplete = false;    //Flag used to make sure the animation completes on differential mode before switching to SOC mode
        showingBalancing = (!showingBalancing && chargerConnected); //Only show differential mode when charging
    }
    if(chargeFadeDirection) fadeValue += CHARGE_FADE_STEP;  //Counter for animating charge LED fading
    else fadeValue -= CHARGE_FADE_STEP;
    if(fadeValue == 0) chargeFadeDirection = true;
    else if(fadeValue >= 250) chargeFadeDirection = false;
    
    if(EMULATE_XRB == false) updateCAN = true;  //When emulating SRB, we only send CAN packets every 50ms
}

//In long-term balance mode, show delta between min and max cells to user
void balanceMode(void){
    while(IO_RB7_GetValue()){
        mapBalancingToLEDs(cellMinMaxDelta);
        LED.R = 0;
        LED.G = 0;
        LED.B = 1;
        updateLEDs();
        if(updateBMS){
            bms_Update();
            updateSOC();
            updateTemperatures(); //10/25/25 06:24:21 PM Update temperatures at the same time as SOC
            updateBMS = false;
        }
        __delay32(4000);
    }
}

//Returns number of milliseconds that have elapsed since start of program
uint64_t millis(void){
    return millis_Count;
}

//Delays a number of milliseconds in the program
void delay(uint32_t ms){
    volatile uint64_t endTicks = millis_Count + ms;
    while(millis_Count < endTicks) __delay32(10);
}

/**
 * @brief Handle bootloader CAN messages
 * @param msg Pointer to received CAN message
 */
void handleBootloaderMessage(CAN_MSG_OBJ *msg) {
    if(!msg) return;
    
    uint16_t canID = msg->msgId;
    
    // CAN IDs for bootloader commands
    #define BL_CMD_START_DOWNLOAD   0x100
    #define BL_CMD_WRITE_BLOCK      0x101
    #define BL_CMD_COMMIT_IMAGE     0x102
    #define BL_CMD_VERIFY_IMAGE     0x103
    #define BL_CMD_GET_STATUS       0x104
    #define BL_CMD_ABORT            0x105
    
    switch(canID) {
        case BL_CMD_START_DOWNLOAD:
            // Enter bootloader download mode
            bootloaderDownloadActive = true;
            bootloaderBlocksReceived = 0;
            bootloaderBytesReceived = 0;
            bootloaderLastMessageTime = millis();
            
            // Set LED to solid magenta (R=255, G=0, B=255)
            LED.R = 255;
            LED.G = 0;
            LED.B = 255;
            updateLEDs();
            
            Serial_printlnf("Bootloader: Download mode started");
            break;
            
        case BL_CMD_WRITE_BLOCK:
            if(bootloaderDownloadActive) {
                // New frame format: [length | block_num | data(4 bytes) | reserved | checksum]
                uint8_t payloadLen = msg->field.dlc > 0 ? msg->data[0] : 0;
                uint8_t blockNum = msg->field.dlc > 1 ? msg->data[1] : 0;
                
                // Validate payload length (should be 5 for 4 bytes of data + block number)
                if(payloadLen == 5 && msg->field.dlc >= 7) {
                    // Extract 4 bytes of actual firmware data
                    // data[0] = length, data[1] = block_num, data[2:6] = 4 bytes of firmware
                    uint8_t dataLen = 4;  // Always 4 bytes of actual firmware data
                    
                    bootloaderBlocksReceived++;
                    bootloaderBytesReceived += dataLen;
                    bootloaderLastMessageTime = millis();
                    
                    if(DEBUG_ENABLED && (bootloaderBlocksReceived % 10 == 0)) {
                        Serial_printlnf("BL: Block %d, total %lu blocks, %lu bytes", 
                                       blockNum, bootloaderBlocksReceived, bootloaderBytesReceived);
                    }
                    
                    // Send block acknowledgment (CAN ID 0x202)
                    uint8_t ackData[8];
                    ackData[0] = 0x01;      // Payload length = 1 (just block number)
                    ackData[1] = blockNum;  // Block number being acknowledged
                    ackData[2] = 0x00;
                    ackData[3] = 0x00;
                    ackData[4] = 0x00;
                    ackData[5] = 0x00;
                    ackData[6] = 0x00;
                    
                    // Calculate XOR checksum
                    uint8_t ackChecksum = 0;
                    for(int i = 0; i < 7; i++) {
                        ackChecksum ^= ackData[i];
                    }
                    ackData[7] = ackChecksum;
                    
                    // Send acknowledgment
                    CAN_MSG_OBJ ackMsg;
                    ackMsg.msgId = 0x202;  // Block acknowledgment response
                    ackMsg.field.frameType = CAN_FRAME_DATA;
                    ackMsg.field.idType = CAN_FRAME_STD;
                    ackMsg.field.dlc = CAN_DLC_8;
                    ackMsg.data = ackData;
                    
                    CAN1_Transmit(CAN_PRIORITY_HIGH, &ackMsg);
                }
            }
            break;
            
        case BL_CMD_COMMIT_IMAGE:
            // Download complete, verify and commit the image
            Serial_printlnf("Bootloader: Image committed");
            Serial_printlnf("  Total blocks: %lu", bootloaderBlocksReceived);
            Serial_printlnf("  Total bytes:  %lu", bootloaderBytesReceived);
            
            // Call bootloader commit function (this validates CRC and marks image valid)
            // Note: The actual CAN frame will be passed to BL_HandleCANFrame() 
            // which processes the COMMIT command with CRC verification
            BL_STATUS_t bl_status = BL_HandleCANFrame(msg);
            
            if(bl_status == BL_OK) {
                Serial_println("✓ Image validated and committed to flash");
            } else {
                Serial_printlnf("✗ Image commit failed with status: 0x%02X", bl_status);
            }
            
            // Exit download mode (this will jump to new firmware)
            bootloaderExitDownloadMode();
            break;
            
        case BL_CMD_ABORT:
            Serial_println("Bootloader: Download aborted");
            bootloaderExitDownloadMode();
            break;
            
        default:
            break;
    }
}

/**
 * @brief Send periodic status message during bootloader download
 */
void bootloaderSendStatusMessage(void) {
    static uint64_t lastStatusTime = 0;
    uint64_t currentTime = millis();
    
    // Send status every BOOTLOADER_STATUS_TIMEOUT ms
    if((currentTime - lastStatusTime) >= BOOTLOADER_STATUS_TIMEOUT) {
        lastStatusTime = currentTime;
        
        // Check for timeout (no messages in BOOTLOADER_DOWNLOAD_TIMEOUT ms)
        if((currentTime - bootloaderLastMessageTime) > BOOTLOADER_DOWNLOAD_TIMEOUT) {
            Serial_println("Bootloader: Download timeout");
            bootloaderExitDownloadMode();
            return;
        }
        
        // Send progress status via CAN ID 0x201
        // Format: [Byte0] = blocks received, [Byte1-2] = bytes received (little-endian)
        uint8_t statusData[8];
        statusData[0] = 0x03;  // Payload length = 3 bytes
        statusData[1] = (uint8_t)(bootloaderBlocksReceived & 0xFF);
        statusData[2] = (uint8_t)((bootloaderBytesReceived) & 0xFF);
        statusData[3] = (uint8_t)((bootloaderBytesReceived >> 8) & 0xFF);
        statusData[4] = 0x00;
        statusData[5] = 0x00;
        statusData[6] = 0x00;
        
        // Calculate XOR checksum
        uint8_t checksum = 0;
        for(int i = 0; i < 7; i++) {
            checksum ^= statusData[i];
        }
        statusData[7] = checksum;
        
        // Send status message
        CAN_MSG_OBJ statusMsg;
        statusMsg.msgId = 0x201;  // Bootloader status response
        statusMsg.field.frameType = CAN_FRAME_DATA;
        statusMsg.field.idType = CAN_FRAME_STD;
        statusMsg.field.dlc = CAN_DLC_8;
        statusMsg.data = statusData;
        
        CAN1_Transmit(CAN_PRIORITY_HIGH, &statusMsg);
        
        if(DEBUG_ENABLED) {
            Serial_printlnf("BL Status: %lu blocks, %lu bytes", 
                           bootloaderBlocksReceived, bootloaderBytesReceived);
        }
    }
}

/**
 * @brief Exit bootloader download mode and restore normal operation
 */
void bootloaderExitDownloadMode(void) {
    bootloaderDownloadActive = false;
    bootloaderBlocksReceived = 0;
    bootloaderBytesReceived = 0;
    
    // Turn off magenta LED
    LED.R = 0;
    LED.G = 0;
    LED.B = 0;
    updateLEDs();
    
    Serial_println("Bootloader: Download mode ended");
    
    // Delay to allow message to be sent before restart
    delay(500);
    
    // Jump to the active application image in flash
    // This will restart the device with the new firmware
    BL_IMAGE_t activeImage = BL_GetActiveImage();
    Serial_printlnf("Jumping to application image %d...", activeImage);
    
    // Small delay to ensure serial message is sent
    delay(100);
    
    // This function performs a soft reset to application code
    BL_JumpToImage(activeImage);
    
    // If we get here, something went wrong - restart from bootloader
    __asm__ volatile ("reset");



}

/// @brief A function that reads bytes until it gets a full string command, then processes that command. Used for debugging and testing purposes. Not used in normal operation.
/// @param byte 
void serialProcessString(uint8_t byte){

    if(byte == '\n' || byte == '\r'){   //If we get a newline character, consider the command finished and process it
        serialStringBuffer[serialStringIndex] = '\0';  //Null-terminate the string
        Serial_printlnf("Received command: %s", serialStringBuffer);
        
        // Process command here (for testing/debugging)
        if(strcmp(serialStringBuffer, "status") == 0){
            Serial_printlnf("Battery SOC: %d%%", batterySOC);
            Serial_printlnf("Charger Connected: %s", chargerConnected ? "Yes" : "No");
            Serial_printlnf("Charging Enabled: %s", chargingEnabled ? "Yes" : "No");
            Serial_printlnf("Cell Voltage Delta: %dmV", cellMinMaxDelta);
        }
        
        serialStringIndex = 0;    //Reset index for next command
    }
    else{
        if(serialStringIndex < SERIAL_STRING_BUFFER_SIZE - 1){   //Store byte in buffer if there's space
            serialStringBuffer[serialStringIndex++] = byte;
        }
    }

}


/**
 * @brief Process incoming serial bootloader byte
 * Frame format: [0xAA][CMD][LENGTH][BLOCK_NUM][DATA(4)][CHECKSUM][0x55]
 */
void serialBootloaderProcessByte(uint8_t byte) {
    static bool frameStarted = false;
    
    // Look for frame start marker
    if(!frameStarted && byte == SERIAL_BL_FRAME_START) {
        frameStarted = true;
        serialBlRxIndex = 0;
        serialBlRxBuffer[serialBlRxIndex++] = byte;
        return;
    }
    
    if(!frameStarted) {
        Serial_printlnf("SERIAL_BL: Ignoring byte 0x%02X, waiting for start marker", byte);
        return;  // Ignore bytes before start marker
    }
    
    // Store byte
    if(serialBlRxIndex < SERIAL_BL_RX_BUFFER_SIZE) {
        serialBlRxBuffer[serialBlRxIndex++] = byte;
    } else {
        // Buffer overflow, discard and restart
        Serial_println("SERIAL_BL: Buffer overflow, discarding frame");
        frameStarted = false;
        serialBlRxIndex = 0;
        return;
    }
    
    // Once we have the length field (at byte 2), calculate expected frame size
    if(serialBlRxIndex >= 3) {
        uint8_t length_field = serialBlRxBuffer[2];
        
        // Calculate expected frame size: 1(0xAA) + 1(CMD) + 1(LEN) + LEN + 1(CHK) + 1(0x55)
        uint16_t expected_size = 5 + length_field;
        
        if(serialBlRxIndex >= expected_size) {
            // Check for frame end marker
            if(serialBlRxBuffer[serialBlRxIndex - 1] == SERIAL_BL_FRAME_END) {
                // Frame complete
                Serial_printlnf("SERIAL_BL: Frame received, CMD=0x%02X, LEN=%d", 
                               serialBlRxBuffer[1], length_field);
                bootloaderDownloadActive = true;
                serialBlFrameReady = true;
                frameStarted = false;
            } else {
                // Invalid frame end, look for next start marker
                frameStarted = false;
                serialBlRxIndex = 0;
            }
        }
    }
}

/**
 * @brief Process a complete serial bootloader frame
 */
void serialBootloaderProcessFrame(void) {
    if(serialBlRxIndex < 5) {
        Serial_println("SERIAL_BL: Frame too short, discarding");
        return;  // Invalid frame
    }
    
    uint8_t cmd = serialBlRxBuffer[1];
    uint8_t length_field = serialBlRxBuffer[2];
    
    // Validate checksum
    uint8_t checksum_calc = 0;
    for(int i = 1; i < serialBlRxIndex - 2; i++) {  // Exclude start, end, and checksum bytes
        checksum_calc ^= serialBlRxBuffer[i];
    }
    uint8_t checksum_recv = serialBlRxBuffer[serialBlRxIndex - 2];
    
    if(checksum_calc != checksum_recv) {
        Serial_printlnf("SERIAL_BL: Checksum error! Calculated: 0x%02X, Received: 0x%02X", checksum_calc, checksum_recv);
        return;
    }

    // Process command
    switch(cmd) {
        case SERIAL_BL_CMD_START:
            // Format: [0xAA][0x01][0x01][image][CHECKSUM][0x55]
            if(length_field == 1 && serialBlRxIndex >= 6) {
                uint8_t image_sel = serialBlRxBuffer[3];
                Serial_printlnf("SERIAL_BL: START_DOWNLOAD for image %d", image_sel);
                
                bootloaderDownloadActive = true;
                bootloaderBlocksReceived = 0;
                bootloaderBytesReceived = 0;
                bootloaderLastMessageTime = millis();
                
                // Set LED to magenta
                LED.R = 255;
                LED.G = 0;
                LED.B = 255;
                updateLEDs();
                
                serialBootloaderSendResponse(0x00, 0);  // Status OK
            }
            break;
            
        case SERIAL_BL_CMD_WRITE_BLOCK:
            // Format: [0xAA][0x02][0x05][block_num][data(4)][CHECKSUM][0x55]
            if(length_field == 5 && serialBlRxIndex >= 10 && bootloaderDownloadActive) {
                uint8_t block_num = serialBlRxBuffer[3];
                uint8_t data_len = 4;
                
                bootloaderBlocksReceived++;
                bootloaderBytesReceived += data_len;
                bootloaderLastMessageTime = millis();
                
                if(DEBUG_ENABLED && (bootloaderBlocksReceived % 10 == 0)) {
                    
                }
                
                Serial_printlnf("SERIAL_BL: Block %d, total %lu blocks, %lu bytes", 
                                   block_num, bootloaderBlocksReceived, bootloaderBytesReceived);

                // Send block acknowledgment
                serialBootloaderSendResponse(0x00, block_num);
            }
            break;
            
        case SERIAL_BL_CMD_COMMIT:
            // Format: [0xAA][0x03][0x00][CHECKSUM][0x55] (no additional data)
            if(length_field == 0 && bootloaderDownloadActive) {
                Serial_printlnf("SERIAL_BL: COMMIT_IMAGE");
                Serial_printlnf("  Total blocks: %lu", bootloaderBlocksReceived);
                Serial_printlnf("  Total bytes:  %lu", bootloaderBytesReceived);
                
                // Commit via bootloader
                BL_STATUS_t bl_status = BL_OK;
                
                if(bl_status == BL_OK) {
                    Serial_println("✓ Image validated and committed to flash");
                    serialBootloaderSendResponse(0x00, 0);  // Status OK
                } else {
                    Serial_printlnf("✗ Image commit failed with status: 0x%02X", bl_status);
                    serialBootloaderSendResponse(0x07, 0);  // CRC Error
                }
                
                // Exit download mode (will jump to new firmware)
                bootloaderExitDownloadMode();
            }
            break;
            
        case SERIAL_BL_CMD_ABORT:
            if(bootloaderDownloadActive) {
                Serial_println("SERIAL_BL: ABORT");
                bootloaderExitDownloadMode();
                serialBootloaderSendResponse(0x00, 0);  // Status OK
            }
            break;
            
        default:
            Serial_printlnf("SERIAL_BL: Unknown command 0x%02X", cmd);
            break;
    }
}

/**
 * @brief Send serial bootloader response/acknowledgment
 * Format: [0xAA][0x00][0x01][status_or_block_num][CHECKSUM][0x55]
 */
void serialBootloaderSendResponse(uint8_t status, uint8_t blockNum) {
    uint8_t response[8];
    response[0] = SERIAL_BL_FRAME_START;  // 0xAA
    response[1] = 0x00;                    // Response code
    response[2] = 0x01;                    // Length = 1
    response[3] = status;                  // Status or block number
    
    // Calculate checksum (XOR of bytes 1-3)
    uint8_t checksum = 0;
    for(int i = 1; i < 4; i++) {
        checksum ^= response[i];
    }
    response[4] = checksum;
    response[5] = SERIAL_BL_FRAME_END;    // 0x55
    
    // Send via UART
    for(int i = 0; i < 6; i++) {
        while(UART1_IsTxReady() == false);  // Wait for UART ready
        UART1_Write(response[i]);
    }
    
    // Wait for all bytes to be transmitted (shift register empty)
    while(UART1_IsTxDone() == false);
}


/**
 End of File
*/