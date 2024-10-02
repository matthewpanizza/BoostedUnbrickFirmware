/*
 * File:   TLC59108.c
 * Author: mligh
 *
 * Created on August 3, 2024, 8:26 PM
 */

#include "mcc_generated_files/system.h"
#include "mcc_generated_files/uart1.h"
#include "mcc_generated_files/i2c1.h"
#include "mcc_generated_files/i2c2.h"
#include "mcc_generated_files/pin_manager.h"
#include "Libpic30.h"
#include "TLC59108.h"
#include "xc.h"

I2C2_MESSAGE_STATUS led_writeRegister(const uint8_t reg, const uint8_t value){
    I2C2_MESSAGE_STATUS stat;
    uint8_t packet[3] = {reg, value};
    I2C2_MasterWrite(packet, 2, 0x40, &stat);
    while(stat == I2C2_MESSAGE_PENDING) __delay32(4000);
    return stat;
}

I2C2_MESSAGE_STATUS led_writeRegisters(const uint8_t reg, const uint8_t values[], const uint8_t numValues){
    I2C2_MESSAGE_STATUS stat;
    uint8_t packet[numValues + 1];
    packet[0] = reg;
    for(int i = 0; i < numValues; i++) packet[i+1] = values[i];
    I2C2_MasterWrite(packet, numValues + 1, 0x40, &stat);
    while(stat == I2C2_MESSAGE_PENDING) __delay32(4000);
    return stat;
}

uint8_t led_readRegister(uint16_t dataAddress, uint8_t *pData, uint16_t nCount){
    I2C2_MESSAGE_STATUS writeStatus;
    I2C2_MESSAGE_STATUS readStatus;
    uint16_t    timeOut, slaveTimeOut;

    readStatus = I2C2_MESSAGE_PENDING;  // this initial value is important
    writeStatus = I2C2_MESSAGE_PENDING;

    timeOut = 0;
    slaveTimeOut = 0;

    //Write one byte of the register to be read
    uint8_t packet[2] = {dataAddress};
    I2C2_MasterWrite(packet, 1, 0x40, &writeStatus);    
    while(writeStatus != I2C2_MESSAGE_FAIL){
        while(writeStatus == I2C2_MESSAGE_PENDING){
            __delay32(4000);    // add some delay here            
            
            if (slaveTimeOut == 1000) return (0);   // timeout checking
            else slaveTimeOut++;
        }  
        
        if (writeStatus == I2C2_MESSAGE_COMPLETE) break;

        if (timeOut == 3)break; // check for max retry
        else timeOut++;
    }
    
    timeOut = 0;
    slaveTimeOut = 0;
    
    //Then read back the byte sent by the peripheral
    while(readStatus != I2C2_MESSAGE_FAIL){
        
        // write one byte to EEPROM (2 is the count of bytes to write)
        I2C2_MasterRead(pData, nCount, 0x40, &readStatus);

        // wait for the message to be sent or status has changed.
        while(readStatus == I2C2_MESSAGE_PENDING){
            __delay32(4000);    // add some delay here
            
            if (slaveTimeOut == 1000) return (0);   // timeout checking
            else slaveTimeOut++;
        }

        if (readStatus == I2C2_MESSAGE_COMPLETE) break;

        if (timeOut == 3) break; // check for max retry and skip this byte
        else timeOut++;
    }
    
    return (1);
}