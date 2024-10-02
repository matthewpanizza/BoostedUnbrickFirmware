
/**
  TMR3 Generated Driver API Source File 

  @Company
    Microchip Technology Inc.

  @File Name
    tmr3.c

  @Summary
    This is the generated source file for the TMR3 driver using PIC24 / dsPIC33 / PIC32MM MCUs

  @Description
    This source file provides APIs for driver for TMR3. 
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

#include <stdio.h>
#include "tmr3.h"

/**
 Section: File specific functions
*/
void (*TMR3_InterruptHandler)(void) = NULL;
void TMR3_CallBack(void);

/**
  Section: Data Type Definitions
*/

/** TMR Driver Hardware Instance Object

  @Summary
    Defines the object required for the maintenance of the hardware instance.

  @Description
    This defines the object required for the maintenance of the hardware
    instance. This object exists once per hardware instance of the peripheral.

  Remarks:
    None.
*/

typedef struct _TMR_OBJ_STRUCT
{
    /* Timer Elapsed */
    volatile bool           timerElapsed;
    /*Software Counter value*/
    volatile uint8_t        count;

} TMR_OBJ;

static TMR_OBJ tmr3_obj;

/**
  Section: Driver Interface
*/

void TMR3_Initialize (void)
{
    //TMR3 0; 
    TMR3 = 0x00;
    //Period = 0.0100037992 s; Frequency = 30401250 Hz; PR3 1187; 
    PR3 = 0x4A3;
    //TCKPS 1:256; TON enabled; TSIDL disabled; TCS FOSC/2; TGATE disabled; 
    T3CON = 0x8030;

    if(TMR3_InterruptHandler == NULL)
    {
        TMR3_SetInterruptHandler(&TMR3_CallBack);
    }

    IFS0bits.T3IF = false;
    IEC0bits.T3IE = true;
	
    tmr3_obj.timerElapsed = false;

}


void __attribute__ ( ( interrupt, no_auto_psv ) ) _T3Interrupt (  )
{
    /* Check if the Timer Interrupt/Status is set */

    //***User Area Begin

    // ticker function call;
    // ticker is 1 -> Callback function gets called everytime this ISR executes
    if(TMR3_InterruptHandler) 
    { 
           TMR3_InterruptHandler(); 
    }

    //***User Area End

    tmr3_obj.count++;
    tmr3_obj.timerElapsed = true;
    IFS0bits.T3IF = false;
}

void TMR3_Period16BitSet( uint16_t value )
{
    /* Update the counter values */
    PR3 = value;
    /* Reset the status information */
    tmr3_obj.timerElapsed = false;
}

uint16_t TMR3_Period16BitGet( void )
{
    return( PR3 );
}

void TMR3_Counter16BitSet ( uint16_t value )
{
    /* Update the counter values */
    TMR3 = value;
    /* Reset the status information */
    tmr3_obj.timerElapsed = false;
}

uint16_t TMR3_Counter16BitGet( void )
{
    return( TMR3 );
}


void __attribute__ ((weak)) TMR3_CallBack(void)
{
    // Add your custom callback code here
}

void  TMR3_SetInterruptHandler(void (* InterruptHandler)(void))
{ 
    IEC0bits.T3IE = false;
    TMR3_InterruptHandler = InterruptHandler; 
    IEC0bits.T3IE = true;
}

void TMR3_Start( void )
{
    /* Reset the status information */
    tmr3_obj.timerElapsed = false;

    /*Enable the interrupt*/
    IEC0bits.T3IE = true;

    /* Start the Timer */
    T3CONbits.TON = 1;
}

void TMR3_Stop( void )
{
    /* Stop the Timer */
    T3CONbits.TON = false;

    /*Disable the interrupt*/
    IEC0bits.T3IE = false;
}

bool TMR3_GetElapsedThenClear(void)
{
    bool status;
    
    status = tmr3_obj.timerElapsed;

    if(status == true)
    {
        tmr3_obj.timerElapsed = false;
    }
    return status;
}

int TMR3_SoftwareCounterGet(void)
{
    return tmr3_obj.count;
}

void TMR3_SoftwareCounterClear(void)
{
    tmr3_obj.count = 0; 
}

/**
 End of File
*/
