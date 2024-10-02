/*
 * File:   CANBus.c
 * Author: mligh
 *
 * Created on August 26, 2024, 12:28 PM
 */


#include "xc.h"
#include "Libpic30.h"
#include "CANBus.h"
#include "mcc_generated_files/can1.h"
#include "mcc_generated_files/can_types.h"

uint8_t addressCounter = 0;

void CanSend(uint32_t Can_addr, uint8_t data0, uint8_t data1, uint8_t data2, uint8_t data3, uint8_t data4, uint8_t data5, uint8_t data6, uint8_t data7, uint8_t extended){
    CAN_MSG_OBJ msg;
    uint8_t data[8] = {data0,data1,data2,data3,data4,data5,data6,data7};
        
    if(addressCounter > 15) addressCounter = 0;
    
        msg.msgId = Can_addr + addressCounter;
        msg.field.frameType = CAN_FRAME_DATA;
        if(extended == 1) msg.field.idType = CAN_FRAME_EXT;
        else msg.field.idType = CAN_FRAME_STD;
        msg.field.dlc = CAN_DLC_8;
        msg.data = data;
    
        CAN1_Transmit(CAN_PRIORITY_HIGH, &msg);
        
        addressCounter++;
}

bool CanReceive(CAN_MSG_OBJ *recCanMsg){
    if(CAN1_ReceivedMessageCountGet() > 0) {
        if(CAN1_Receive(recCanMsg)){
            return true;
        }
    } 
    return false;
}