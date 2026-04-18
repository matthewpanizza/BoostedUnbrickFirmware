/*
 * File:   bootloader_config.h
 * Author: 
 * Comments: Memory map and configuration for dual-image bootloader
 *
 * Revision history: 
 */

#ifndef BOOTLOADER_CONFIG_H
#define BOOTLOADER_CONFIG_H

#include <xc.h>

/*
 * dsPIC33EP512GP504 Memory Layout:
 * Total Program Memory: 512K instruction words (0x000000 - 0x15BFFE)
 * In dsPIC linear addressing (24-bit): each instruction = 1 word
 * Total addressable space: 0x000000 to 0x15BFFE (1,441,790 bytes)
 * 
 * Memory Map (dual-image):
 * ===============================================
 * 0x000000 - 0x001FFF  : Bootloader (8 KB)
 * 0x002000 - 0x0AFFFF  : Application Image 1 (688 KB)
 * 0x0B0000 - 0x15AFFF  : Application Image 2 (688 KB)
 * 0x15B000 - 0x15BFFE  : Metadata/Configuration (8 KB)
 * ===============================================
 */

/* Bootloader Memory Layout */
#define BL_START_ADDR           0x000000    /* Bootloader start address */
#define BL_END_ADDR             0x001FFF    /* Bootloader end address */
#define BL_SIZE                 0x002000    /* 8 KB bootloader space */

/* Application Image 1 */
#define APP_IMAGE1_START_ADDR   0x002000    /* Image 1 start */
#define APP_IMAGE1_END_ADDR     0x0AFFFF    /* Image 1 end */
#define APP_IMAGE1_SIZE         0x0AE000    /* ~688 KB */

/* Application Image 2 */
#define APP_IMAGE2_START_ADDR   0x0B0000    /* Image 2 start */
#define APP_IMAGE2_END_ADDR     0x15AFFF    /* Image 2 end */
#define APP_IMAGE2_SIZE         0x0AB000    /* ~688 KB */

/* Metadata Region */
#define METADATA_START_ADDR     0x15B000    /* Metadata start */
#define METADATA_END_ADDR       0x15BFFE    /* Metadata end */
#define METADATA_SIZE           0x001000    /* 4 KB for metadata */

/* Metadata field offsets within EEPROM-simulated sector */
#define METADATA_IMAGE1_VALID   0x000       /* Image 1 validity flag offset */
#define METADATA_IMAGE2_VALID   0x002       /* Image 2 validity flag offset */
#define METADATA_ACTIVE_IMAGE   0x004       /* Active image index offset */
#define METADATA_IMAGE1_CRC     0x006       /* Image 1 CRC32 offset */
#define METADATA_IMAGE2_CRC     0x00A       /* Image 2 CRC32 offset */
#define METADATA_BOOT_COUNT     0x00E       /* Boot counter offset */

/* Validity flags */
#define IMAGE_VALID_FLAG        0xA5A5      /* 0xA5A5 = valid, 0xFFFF = invalid/empty */

/* Flash operation constants */
#define FLASH_PAGE_SIZE         512         /* dsPIC33 page size in bytes (instructions) */
#define FLASH_ROW_SIZE          64          /* dsPIC33 row size in instructions */

/* Timeout values (milliseconds) */
#define BL_ENTRY_TIMEOUT        5000        /* Time to wait for bootloader command */
#define BL_BLOCK_RX_TIMEOUT     1000        /* Timeout for block reception */

#endif /* BOOTLOADER_CONFIG_H */
