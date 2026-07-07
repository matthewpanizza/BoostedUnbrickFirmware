/**
  UART1 Generated Driver File 

  @Company
    Microchip Technology Inc.

  @File Name
    uart1.c

  @Summary
    This is the generated source file for the UART1 driver using PIC24 / dsPIC33 / PIC32MM MCUs

  @Description
    This source file provides APIs for driver for UART1. 
    Generation Information : 
        Product Revision  :  PIC24 / dsPIC33 / PIC32MM MCUs - 1.171.4
        Device            :  dsPIC33EP512GP504
    The generated drivers are tested against the following:
        Compiler          :  XC16 v2.10
        MPLAB             :  MPLAB X v6.05
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
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "xc.h"
#include "uart1.h"

/**
  Section: Data Type Definitions
*/

/** UART Driver Queue Status

  @Summary
    Defines the object required for the status of the queue.
*/

static uint8_t * volatile rxTail;
static uint8_t *rxHead;
static uint8_t *txTail;
static uint8_t * volatile txHead;
static bool volatile rxOverflowed;

/** UART Driver Queue Length

  @Summary
    Defines the length of the Transmit and Receive Buffers

*/

/* We add one extra byte than requested so that we don't have to have a separate
 * bit to determine the difference between buffer full and buffer empty, but
 * still be able to hold the amount of data requested by the user.  Empty is
 * when head == tail.  So full will result in head/tail being off by one due to
 * the extra byte.
 */
#define UART1_CONFIG_TX_BYTEQ_LENGTH (8+1)
#define UART1_CONFIG_RX_BYTEQ_LENGTH (8+1)

/** UART Driver Queue

  @Summary
    Defines the Transmit and Receive Buffers

*/

static uint8_t txQueue[UART1_CONFIG_TX_BYTEQ_LENGTH];
static uint8_t rxQueue[UART1_CONFIG_RX_BYTEQ_LENGTH];

void (*UART1_TxDefaultInterruptHandler)(void);
void (*UART1_RxDefaultInterruptHandler)(void);

/**
  Section: Driver Interface
*/

void UART1_Initialize(void)
{
    IEC0bits.U1TXIE = 0;
    IEC0bits.U1RXIE = 0;

    // STSEL 1; IREN disabled; PDSEL 8N; UARTEN enabled; RTSMD disabled; USIDL disabled; WAKE disabled; ABAUD disabled; LPBACK disabled; BRGH enabled; URXINV disabled; UEN TX_RX; 
    // Data Bits = 8; Parity = None; Stop Bits = 1;
    U1MODE = (0x8008 & ~(1<<15));  // disabling UART ON bit
    // UTXISEL0 TX_ONE_CHAR; UTXINV disabled; OERR NO_ERROR_cleared; URXISEL RX_ONE_CHAR; UTXBRK COMPLETED; UTXEN disabled; ADDEN disabled; 
    U1STA = 0x00;
    // BaudRate = 115200; Frequency = 40074375 Hz; BRG 86; 
    U1BRG = 0x56;
    
    txHead = txQueue;
    txTail = txQueue;
    rxHead = rxQueue;
    rxTail = rxQueue;
   
    rxOverflowed = false;

    UART1_SetTxInterruptHandler(&UART1_Transmit_CallBack);

    UART1_SetRxInterruptHandler(&UART1_Receive_CallBack);

    IEC0bits.U1RXIE = 1;
    
    //Make sure to set LAT bit corresponding to TxPin as high before UART initialization
    U1MODEbits.UARTEN = 1;   // enabling UART ON bit
    U1STAbits.UTXEN = 1;
}

/**
    Maintains the driver's transmitter state machine and implements its ISR
*/

void UART1_SetTxInterruptHandler(void (* interruptHandler)(void))
{
    if(interruptHandler == NULL)
    {
        UART1_TxDefaultInterruptHandler = &UART1_Transmit_CallBack;
    }
    else
    {
        UART1_TxDefaultInterruptHandler = interruptHandler;
    }
} 

void __attribute__ ( ( interrupt, no_auto_psv ) ) _U1TXInterrupt ( void )
{
    if(UART1_TxDefaultInterruptHandler)
    {
        UART1_TxDefaultInterruptHandler();
    }

    if(txHead == txTail)
    {
        IEC0bits.U1TXIE = 0;
    }
    else
    {
        IFS0bits.U1TXIF = 0;

        while(!(U1STAbits.UTXBF == 1))
        {
            U1TXREG = *txHead++;

            if(txHead == (txQueue + UART1_CONFIG_TX_BYTEQ_LENGTH))
            {
                txHead = txQueue;
            }

            // Are we empty?
            if(txHead == txTail)
            {
                break;
            }
        }
    }
}

void __attribute__ ((weak)) UART1_Transmit_CallBack ( void )
{ 

}

void UART1_SetRxInterruptHandler(void (* interruptHandler)(void))
{
    if(interruptHandler == NULL)
    {
        UART1_RxDefaultInterruptHandler = &UART1_Receive_CallBack;
    }
    else
    {
        UART1_RxDefaultInterruptHandler = interruptHandler;
    }
}

void __attribute__ ( ( interrupt, no_auto_psv ) ) _U1RXInterrupt( void )
{
    if(UART1_RxDefaultInterruptHandler)
    {
        UART1_RxDefaultInterruptHandler();
    }
    
    IFS0bits.U1RXIF = 0;
	
    while((U1STAbits.URXDA == 1))
    {
        *rxTail = U1RXREG;

        // Will the increment not result in a wrap and not result in a pure collision?
        // This is most often condition so check first
        if ( ( rxTail    != (rxQueue + UART1_CONFIG_RX_BYTEQ_LENGTH-1)) &&
             ((rxTail+1) != rxHead) )
        {
            rxTail++;
        } 
        else if ( (rxTail == (rxQueue + UART1_CONFIG_RX_BYTEQ_LENGTH-1)) &&
                  (rxHead !=  rxQueue) )
        {
            // Pure wrap no collision
            rxTail = rxQueue;
        } 
        else // must be collision
        {
            rxOverflowed = true;
        }
    }
}

void __attribute__ ((weak)) UART1_Receive_CallBack(void)
{

}

void __attribute__ ( ( interrupt, no_auto_psv ) ) _U1ErrInterrupt( void )
{
    if ((U1STAbits.OERR == 1))
    {
        U1STAbits.OERR = 0;
    }
    
    IFS4bits.U1EIF = 0;
}

/**
  Section: UART Driver Client Routines
*/

uint8_t UART1_Read( void)
{
    uint8_t data = 0;

    while (rxHead == rxTail )
    {
    }
    
    data = *rxHead;

    rxHead++;

    if (rxHead == (rxQueue + UART1_CONFIG_RX_BYTEQ_LENGTH))
    {
        rxHead = rxQueue;
    }
    return data;
}

void UART1_Write( uint8_t byte)
{
    while(UART1_IsTxReady() == 0)
    {
    }

    *txTail = byte;

    txTail++;
    
    if (txTail == (txQueue + UART1_CONFIG_TX_BYTEQ_LENGTH))
    {
        txTail = txQueue;
    }

    IEC0bits.U1TXIE = 1;
}

bool UART1_IsRxReady(void)
{    
    return !(rxHead == rxTail);
}

bool UART1_IsTxReady(void)
{
    uint16_t size;
    uint8_t *snapshot_txHead = (uint8_t*)txHead;
    
    if (txTail < snapshot_txHead)
    {
        size = (snapshot_txHead - txTail - 1);
    }
    else
    {
        size = ( UART1_CONFIG_TX_BYTEQ_LENGTH - (txTail - snapshot_txHead) - 1 );
    }
    
    return (size != 0);
}

bool UART1_IsTxDone(void)
{
    if(txTail == txHead)
    {
        return (bool)U1STAbits.TRMT;
    }
    
    return false;
}


/*******************************************************************************

  !!! Deprecated API !!!
  !!! These functions will not be supported in future releases !!!

*******************************************************************************/

static uint8_t UART1_RxDataAvailable(void)
{
    uint16_t size;
    uint8_t *snapshot_rxTail = (uint8_t*)rxTail;
    
    if (snapshot_rxTail < rxHead) 
    {
        size = ( UART1_CONFIG_RX_BYTEQ_LENGTH - (rxHead-snapshot_rxTail));
    }
    else
    {
        size = ( (snapshot_rxTail - rxHead));
    }
    
    if(size > 0xFF)
    {
        return 0xFF;
    }
    
    return size;
}

static uint8_t UART1_TxDataAvailable(void)
{
    uint16_t size;
    uint8_t *snapshot_txHead = (uint8_t*)txHead;
    
    if (txTail < snapshot_txHead)
    {
        size = (snapshot_txHead - txTail - 1);
    }
    else
    {
        size = ( UART1_CONFIG_TX_BYTEQ_LENGTH - (txTail - snapshot_txHead) - 1 );
    }
    
    if(size > 0xFF)
    {
        return 0xFF;
    }
    
    return size;
}

unsigned int __attribute__((deprecated)) UART1_ReadBuffer( uint8_t *buffer ,  unsigned int numbytes)
{
    unsigned int rx_count = UART1_RxDataAvailable();
    unsigned int i;
    
    if(numbytes < rx_count)
    {
        rx_count = numbytes;
    }
    
    for(i=0; i<rx_count; i++)
    {
        *buffer++ = UART1_Read();
    }
    
    return rx_count;    
}

unsigned int __attribute__((deprecated)) UART1_WriteBuffer( uint8_t *buffer , unsigned int numbytes )
{
    unsigned int tx_count = UART1_TxDataAvailable();
    unsigned int i;
    
    if(numbytes < tx_count)
    {
        tx_count = numbytes;
    }
    
    for(i=0; i<tx_count; i++)
    {
        UART1_Write(*buffer++);
    }
    
    return tx_count;  
}

UART1_TRANSFER_STATUS __attribute__((deprecated)) UART1_TransferStatusGet (void )
{
    UART1_TRANSFER_STATUS status = 0;
    uint8_t rx_count = UART1_RxDataAvailable();
    uint8_t tx_count = UART1_TxDataAvailable();
    
    switch(rx_count)
    {
        case 0:
            status |= UART1_TRANSFER_STATUS_RX_EMPTY;
            break;
        case UART1_CONFIG_RX_BYTEQ_LENGTH:
            status |= UART1_TRANSFER_STATUS_RX_FULL;
            break;
        default:
            status |= UART1_TRANSFER_STATUS_RX_DATA_PRESENT;
            break;
    }
    
    switch(tx_count)
    {
        case 0:
            status |= UART1_TRANSFER_STATUS_TX_FULL;
            break;
        case UART1_CONFIG_RX_BYTEQ_LENGTH:
            status |= UART1_TRANSFER_STATUS_TX_EMPTY;
            break;
        default:
            break;
    }

    return status;    
}

uint8_t __attribute__((deprecated)) UART1_Peek(uint16_t offset)
{
    uint8_t *peek = rxHead + offset;
    
    while(peek > (rxQueue + UART1_CONFIG_RX_BYTEQ_LENGTH))
    {
        peek -= UART1_CONFIG_RX_BYTEQ_LENGTH;
    }
    
    return *peek;
}

bool __attribute__((deprecated)) UART1_ReceiveBufferIsEmpty (void)
{
    return (UART1_RxDataAvailable() == 0);
}

bool __attribute__((deprecated)) UART1_TransmitBufferIsFull(void)
{
    return (UART1_TxDataAvailable() == 0);
}

uint16_t __attribute__((deprecated)) UART1_StatusGet (void)
{
    return U1STA;
}

unsigned int __attribute__((deprecated)) UART1_TransmitBufferSizeGet(void)
{
    if(UART1_TxDataAvailable() != 0)
    { 
        if(txHead > txTail)
        {
            return((txHead - txTail) - 1);
        }
        else
        {
            return((UART1_CONFIG_TX_BYTEQ_LENGTH - (txTail - txHead)) - 1);
        }
    }
    return 0;
}

unsigned int __attribute__((deprecated)) UART1_ReceiveBufferSizeGet(void)
{
    if(UART1_RxDataAvailable() != 0)
    {
        if(rxHead > rxTail)
        {
            return((rxHead - rxTail) - 1);
        }
        else
        {
            return((UART1_CONFIG_RX_BYTEQ_LENGTH - (rxTail - rxHead)) - 1);
        } 
    }
    return 0;
}

void __attribute__((deprecated)) UART1_Enable(void)
{
    U1MODEbits.UARTEN = 1;
    U1STAbits.UTXEN = 1;
}

void __attribute__((deprecated)) UART1_Disable(void)
{
    U1MODEbits.UARTEN = 0;
    U1STAbits.UTXEN = 0;
}