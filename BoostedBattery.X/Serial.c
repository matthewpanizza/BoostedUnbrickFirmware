/*
 * File:   Serial.c
 * Author: mligh
 *
 * Created on August 3, 2024, 8:50 PM
 */

#include "mcc_generated_files/system.h"
#include "mcc_generated_files/uart1.h"
#include "Libpic30.h"
#include <stdarg.h>
#include "Serial.h"
#include "xc.h"

void Serial_begin(){
    UART1_Initialize();
}

void Serial_print(const char string[]){
    uint16_t index = 0;
    while(string[index]){
        UART1_Write(string[index]);
        index++;
    }
}

void Serial_println(const char string[]){
    Serial_print(string);
    UART1_Write('\n');
}

void Serial_printf(const char *format, ...){ 
    char buffer[256];
    va_list args;
    va_start (args, format);
    vsnprintf (buffer,256,format, args);
    Serial_print(buffer);
    va_end (args);
} 

void Serial_printlnf(const char *format, ...) { 
    char buffer[256];
    va_list args;
    va_start (args, format);
    vsprintf (buffer,format, args);
    Serial_print(buffer);
    va_end (args);
    UART1_Write('\n');
}