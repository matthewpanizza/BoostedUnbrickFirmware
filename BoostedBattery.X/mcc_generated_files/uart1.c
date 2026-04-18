/**
  UART1 Generated Driver File

  @Company
    Microchip Technology Inc.

  @File Name
    uart1.c

  @Summary
    This is the generated driver implementation file for the UART1 driver using PIC24 / dsPIC33 / PIC32MM MCUs

  @Description
    This header file provides implementations for driver APIs for UART1.
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
#include <xc.h>
#include "uart1.h"

/**
  Section: Ring Buffer for Interrupt-Based Receive
*/
#define UART1_RX_BUFFER_SIZE 512

static volatile uint8_t uart1RxBuffer[UART1_RX_BUFFER_SIZE];
static volatile uint16_t uart1RxHead = 0;  // Write pointer (ISR writes here)
static volatile uint16_t uart1RxTail = 0;  // Read pointer (application reads here)
static volatile bool uart1LineReady = false;  // Set to true when CR or LF received

/**
  Section: UART1 APIs
*/

void UART1_Initialize(void)
{
/**    
     Set the UART1 module to the options selected in the user interface.
     Make sure to set LAT bit corresponding to TxPin as high before UART initialization
*/
    // STSEL 1; IREN disabled; PDSEL 8N; UARTEN enabled; RTSMD disabled; USIDL disabled; WAKE disabled; ABAUD disabled; LPBACK disabled; BRGH enabled; URXINV disabled; UEN TX_RX; 
    // Data Bits = 8; Parity = None; Stop Bits = 1;
    U1MODE = (0x8008 & ~(1<<15));  // disabling UARTEN bit
    // UTXISEL0 TX_ONE_CHAR; UTXINV disabled; OERR NO_ERROR_cleared; URXISEL RX_ONE_CHAR; UTXBRK COMPLETED; UTXEN disabled; ADDEN disabled; 
    U1STA = 0x00;
    // BaudRate = 115200; Frequency = 40074375 Hz; BRG 86; 
    U1BRG = 0x56;
    
    U1MODEbits.UARTEN = 1;   // enabling UART ON bit
    U1STAbits.UTXEN = 1;
    
    // Initialize ring buffer pointers
    uart1RxHead = 0;
    uart1RxTail = 0;
    uart1LineReady = false;
}

uint8_t UART1_Read(void)
{
    while(!(U1STAbits.URXDA == 1))
    {
        
    }

    if ((U1STAbits.OERR == 1))
    {
        U1STAbits.OERR = 0;
    }
    
    return U1RXREG;
}

void UART1_Write(uint8_t txData)
{
    while(U1STAbits.UTXBF == 1)
    {
        
    }

    U1TXREG = txData;    // Write the data byte to the USART.
}

bool UART1_IsRxReady(void)
{
    return U1STAbits.URXDA;
}

bool UART1_IsTxReady(void)
{
    return ((!U1STAbits.UTXBF) && U1STAbits.UTXEN );
}

bool UART1_IsTxDone(void)
{
    return U1STAbits.TRMT;
}

/**
  @Description
    Set up interrupt-based receive with ring buffer.
    Enables UART1 receive interrupt and initializes ring buffer.
*/
void UART1_SetupInterrupt(void)
{
    // Enable UART1 receive interrupt (U1RXIF)
    IEC0bits.U1RXIE = 1;  // Enable UART1 RX interrupt
    IPC2bits.U1RXIP = 5;  // Set to priority level 5 (medium-high)
}

/**
  @Description
    Non-blocking read from receive ring buffer.
    Returns next byte from buffer, or 0xFF if empty.
*/
bool UART1_ReadFromBuffer(uint8_t *data)
{
    if(uart1RxTail == uart1RxHead) {
        return false;  // Buffer empty
    }
    
    *data = uart1RxBuffer[uart1RxTail];
    uart1RxTail = (uart1RxTail + 1) % UART1_RX_BUFFER_SIZE;
    
    return true;
}

/**
  @Description
    Check if data is available in receive ring buffer.
*/
bool UART1_IsBufferDataAvailable(void)
{
    return (uart1RxTail != uart1RxHead);
}

/**
  @Description
    Get number of bytes available in receive ring buffer.
*/
uint16_t UART1_GetBufferCount(void)
{
    if(uart1RxHead >= uart1RxTail) {
        return uart1RxHead - uart1RxTail;
    } else {
        return (UART1_RX_BUFFER_SIZE - uart1RxTail) + uart1RxHead;
    }
}

/**
  @Description
    Check if a complete line (CR or LF) has been received.
*/
bool UART1_IsLineReady(void)
{
    return uart1LineReady;
}

/**
  @Description
    Clear the line ready flag.
*/
void UART1_ClearLineReady(void)
{
    uart1LineReady = false;
}

/**
  @Description
    UART1 Receive Interrupt Handler.
    Called when a byte is received on UART1 RX.
*/
void __attribute__((interrupt, no_auto_psv)) _U1RXInterrupt(void)
{
    // Read all available bytes from UART1 FIFO
    while(U1STAbits.URXDA) {
        uint8_t data = U1RXREG;
        
        // Store in ring buffer
        uint16_t nextHead = (uart1RxHead + 1) % UART1_RX_BUFFER_SIZE;
        
        // Check for buffer overflow (tail caught up with head)
        if(nextHead == uart1RxTail) {
            // Buffer full, drop this byte
            break;
        }
        
        uart1RxBuffer[uart1RxHead] = data;
        uart1RxHead = nextHead;
        
        // Set line ready flag on CR or LF
        if(data == '\r' || data == '\n') {
            uart1LineReady = true;
        }
    }
    
    // Clear interrupt flag
    IFS0bits.U1RXIF = 0;
}


/*******************************************************************************

  !!! Deprecated API !!!
  !!! These functions will not be supported in future releases !!!

*******************************************************************************/

uint16_t __attribute__((deprecated)) UART1_StatusGet (void)
{
    return U1STA;
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
