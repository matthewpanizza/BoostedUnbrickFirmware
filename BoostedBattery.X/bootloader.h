/*
 * File:   bootloader.h
 * Author: 
 * Comments: Bootloader protocol and interface definitions
 *
 * CAN-based firmware update bootloader with dual-image support
 * 
 * CAN Protocol Specification:
 * =============================
 * Frame Format (8 bytes):
 *   Byte 0: Payload length (1-6)
 *   Bytes 1-6: Payload data
 *   Byte 7: Checksum (simple XOR of all previous bytes)
 * 
 * Commands (identified by CAN message ID):
 *   0x100: START_DOWNLOAD    - Initiate firmware download sequence
 *   0x101: WRITE_BLOCK       - Write 6 bytes of firmware data
 *   0x102: COMMIT_IMAGE      - Finalize and validate the firmware
 *   0x103: VERIFY_IMAGE      - Verify active image integrity
 * 
 * Revision history: 
 */

#ifndef BOOTLOADER_H
#define BOOTLOADER_H

#include <xc.h>
#include "bootloader_config.h"
#include "mcc_generated_files/can_types.h"
#include "stdbool.h"

/* CAN Command Message Identifiers */
typedef enum {
    BL_CMD_START_DOWNLOAD   = 0x100,    /* Start firmware download sequence */
    BL_CMD_WRITE_BLOCK      = 0x101,    /* Write block of firmware data */
    BL_CMD_COMMIT_IMAGE     = 0x102,    /* Commit firmware to flash */
    BL_CMD_VERIFY_IMAGE     = 0x103,    /* Verify image integrity */
    BL_CMD_GET_STATUS       = 0x104,    /* Get bootloader status */
    BL_CMD_ABORT            = 0x105     /* Abort current operation */
} BL_CAN_COMMAND_t;

/* Bootloader State Machine */
typedef enum {
    BL_STATE_IDLE,              /* Waiting for command */
    BL_STATE_DOWNLOAD_ACTIVE,   /* Receiving firmware blocks */
    BL_STATE_PROGRAMMING,       /* Writing to flash */
    BL_STATE_VERIFYING,         /* Verifying written data */
    BL_STATE_ERROR              /* Error occurred */
} BL_STATE_t;

/* Image Selection */
typedef enum {
    BL_IMAGE_1,                 /* Application Image 1 */
    BL_IMAGE_2,                 /* Application Image 2 */
    BL_IMAGE_ACTIVE             /* Currently active image */
} BL_IMAGE_t;

/* Return codes */
typedef enum {
    BL_OK = 0,                  /* Operation successful */
    BL_ERR_INVALID_CMD,         /* Invalid command */
    BL_ERR_INVALID_STATE,       /* Invalid state for operation */
    BL_ERR_CHECKSUM,            /* Checksum verification failed */
    BL_ERR_FLASH,               /* Flash operation failed */
    BL_ERR_MEMORY,              /* Memory access error */
    BL_ERR_TIMEOUT,             /* Operation timeout */
    BL_ERR_CRC,                 /* CRC verification failed */
    BL_ERR_FULL,                /* Target image space full */
    BL_ERR_NOT_INITIALIZED      /* Bootloader not initialized */
} BL_STATUS_t;

/* Bootloader control structure */
typedef struct {
    BL_STATE_t state;           /* Current bootloader state */
    BL_IMAGE_t target_image;    /* Target image for download */
    uint32_t write_address;     /* Current write address in target image */
    uint32_t bytes_written;     /* Total bytes written to target image */
    uint32_t image_crc;         /* Running CRC of downloaded image */
    uint32_t block_count;       /* Number of blocks received */
    uint16_t last_error;        /* Last error code */
} BL_CONTROL_t;

/* CAN receive frame structure */
typedef struct {
    uint16_t can_id;            /* CAN message identifier */
    uint8_t length;             /* Payload length (bytes 1-6) */
    uint8_t payload[6];         /* Payload data (max 6 bytes) */
    uint8_t checksum;           /* XOR checksum */
} BL_CAN_FRAME_t;

/* ============ Public Function Prototypes ============ */

/**
 * @brief Initialize the bootloader module
 * @return BL_OK on success, error code otherwise
 */
BL_STATUS_t BL_Init(void);

/**
 * @brief Process received CAN frame
 * @param frame Pointer to CAN frame structure
 * @return BL_OK on success, error code otherwise
 */
BL_STATUS_t BL_HandleCANFrame(CAN_MSG_OBJ *frame);

/**
 * @brief Abort current bootloader operation
 * @return BL_OK on success
 */
BL_STATUS_t BL_Abort(void);

/**
 * @brief Get current bootloader state
 * @return Current bootloader state
 */
BL_STATE_t BL_GetState(void);

/**
 * @brief Get last error code
 * @return Last error code
 */
BL_STATUS_t BL_GetLastError(void);

/**
 * @brief Validate image in flash memory
 * @param image Selected image to validate
 * @return BL_OK if valid, error code otherwise
 */
BL_STATUS_t BL_ValidateImage(BL_IMAGE_t image);

/**
 * @brief Set active image to run on next boot
 * @param image Image to activate
 * @return BL_OK on success, error code otherwise
 */
BL_STATUS_t BL_SetActiveImage(BL_IMAGE_t image);

/**
 * @brief Get currently active image
 * @return Currently active image ID
 */
BL_IMAGE_t BL_GetActiveImage(void);

/**
 * @brief Jump to application image
 * @param image Image to execute
 */
void BL_JumpToImage(BL_IMAGE_t image);

/**
 * @brief Calculate CRC32 of image
 * @param image Selected image to calculate CRC
 * @return CRC32 value
 */
uint32_t BL_CalculateImageCRC(BL_IMAGE_t image);

/**
 * @brief Erase target image space
 * @param image Image space to erase
 * @return BL_OK on success, error code otherwise
 */
BL_STATUS_t BL_EraseImage(BL_IMAGE_t image);

/**
 * @brief Entry point check - determines if bootloader should run
 * @return true if bootloader should run, false to jump to app
 */
bool BL_ShouldEnterBootloader(void);

/**
 * @brief Handle bootloader housekeeping
 * @note Call periodically from main loop
 */
void BL_Task(void);

#endif /* BOOTLOADER_H */
