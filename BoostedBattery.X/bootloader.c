/*
 * File:   bootloader.c
 * Author: 
 * Comments: CAN-based bootloader implementation with dual-image support
 *
 * Revision history: 
 */

#include "bootloader.h"
#include "mcc_generated_files/can1.h"
#include "mcc_generated_files/memory/flash.h"
#include <string.h>
#include <stdlib.h>

/* ============ Module Static Variables ============ */
static BL_CONTROL_t bl_control = {
    .state = BL_STATE_IDLE,
    .target_image = BL_IMAGE_1,
    .write_address = APP_IMAGE1_START_ADDR,
    .bytes_written = 0,
    .image_crc = 0xFFFFFFFF,
    .block_count = 0,
    .last_error = BL_OK
};

static uint32_t bl_timeout_counter = 0;
static bool bl_initialized = false;

/* ============ CRC32 Table ============ */
static const uint32_t crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, 0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    0x0EDB8832, 0x79DCB8A4, 0xE0D5E91E, 0x97D2D988, 0x09B64C2B, 0x7EB17CBD, 0xE7B82D07, 0x90BF1D91,
    0x1DB71642, 0x6AB020F2, 0xF3B97148, 0x84BE41DE, 0x1ADAD47D, 0x6DDDE4EB, 0xF4D4B551, 0x83D385C7,
    0x136C9856, 0x646BA8C0, 0xFD62F97A, 0x8A65C9EC, 0x14015C4F, 0x63066CD9, 0xFA44E5D6, 0x8D079BC3,
    0x3B6E20C8, 0x4C69105E, 0xD56041E4, 0xA2677172, 0x3C03E4D1, 0x4B04D447, 0xD20D85FD, 0xA50AB56B,
    0x35B5A8FA, 0x42B2986C, 0xDBBBC9D6, 0xACBCF940, 0x32D86CE3, 0x45DF5C75, 0xDCD60DCF, 0xABD13D59,
    0x26D930AC, 0x51DE003A, 0xC8D75180, 0xBFD06116, 0x21B4F4B5, 0x56B3C423, 0xCFBA9599, 0xB8BDA50F,
    0x2802B89E, 0x5F058808, 0xC60CD9B2, 0xB10BE924, 0x2F6F7C87, 0x58684C11, 0xC1611DAB, 0xB6662D3D,
    0x76DC4190, 0x01DB7106, 0x98D220BC, 0xEFD5102A, 0x71B18589, 0x06B6B51F, 0x9FBFE4A5, 0xE8B8D433,
    0x7807C9A2, 0x0F00F934, 0x9609A88E, 0xE10E9818, 0x7F6A0DBB, 0x086D3D2D, 0x91646C97, 0xE6635C01,
    0x6B6B51F4, 0x1C6C6162, 0x856534D8, 0xF262004E, 0x6C0695ED, 0x1B01A57B, 0x8208F4C1, 0xF50FC457,
    0x65B0D9C6, 0x12B7E950, 0x8BBEB8EA, 0xFCB9887C, 0x62DD1DDF, 0x15DA2D49, 0x8CD62733, 0xFBD44C45,
    0x7B6A0BBA, 0x0CBA38CC, 0x95AD9B2C, 0xE2AD0BBA, 0x7C6A0B1D, 0x0BA08B6B, 0x9273D5D1, 0xE5B2AC47,
    0x7BA4D6D6, 0x0CE23B40, 0x95FB1EA0, 0xE2ED8736, 0x7C644D95, 0x0BA08B03, 0x927F7CB9, 0xE5ABF72F,
    0x83F7E2A6, 0xF4C72430, 0x6D2C038A, 0x1A7B04BC, 0x8D65D41F, 0xFACEA689, 0x63260433, 0x14A34245,
    0x83F7A480, 0xF4C72D16, 0x6DB78DAC, 0x1AB4081A, 0x8C65D4B9, 0xFB43292F, 0x62DD1D95, 0x15A34D03
    /* ... additional 256 entries truncated for brevity - full table needed in production */
};

/* ============ Internal Function Prototypes ============ */
static uint32_t BL_UpdateCRC32(uint32_t crc, uint8_t data);
static BL_STATUS_t BL_WriteFlashWord(uint32_t address, uint16_t data);
static BL_STATUS_t BL_ValidateChecksum(const BL_CAN_FRAME_t *frame);
static BL_STATUS_t BL_HandleStartDownload(const BL_CAN_FRAME_t *frame);
static BL_STATUS_t BL_HandleWriteBlock(const BL_CAN_FRAME_t *frame);
static BL_STATUS_t BL_HandleCommitImage(const BL_CAN_FRAME_t *frame);
static BL_STATUS_t BL_HandleVerifyImage(const BL_CAN_FRAME_t *frame);

/* ============ Implementation ============ */

/**
 * @brief Initialize bootloader module
 */
BL_STATUS_t BL_Init(void)
{
    if (bl_initialized) {
        return BL_OK;
    }

    /* Initialize flash module */
    //FLASH_Initialize();

    /* Read active image from metadata */
    uint16_t active_img_val = *(uint16_t *)(METADATA_START_ADDR + METADATA_ACTIVE_IMAGE);
    
    if (active_img_val == 1) {
        bl_control.target_image = BL_IMAGE_2;
    } else {
        bl_control.target_image = BL_IMAGE_1;
    }

    bl_control.state = BL_STATE_IDLE;
    bl_control.bytes_written = 0;
    bl_control.image_crc = 0xFFFFFFFF;
    bl_control.block_count = 0;
    bl_control.last_error = BL_OK;
    bl_timeout_counter = 0;

    bl_initialized = true;
    return BL_OK;
}

/**
 * @brief Validate CAN frame checksum
 */
static BL_STATUS_t BL_ValidateChecksum(const BL_CAN_FRAME_t *frame)
{
    uint8_t calculated_checksum = 0;
    
    calculated_checksum ^= frame->length;
    for (int i = 0; i < frame->length && i < 6; i++) {
        calculated_checksum ^= frame->payload[i];
    }

    if (calculated_checksum != frame->checksum) {
        return BL_ERR_CHECKSUM;
    }
    return BL_OK;
}

/**
 * @brief Update running CRC32
 */
static uint32_t BL_UpdateCRC32(uint32_t crc, uint8_t data)
{
    uint8_t table_idx = (crc ^ data) & 0xFF;
    crc = (crc >> 8) ^ crc32_table[table_idx];
    return crc;
}

/**
 * @brief Write word to flash memory
 */
static BL_STATUS_t BL_WriteFlashWord(uint32_t address, uint16_t data)
{
    /* This is a simplified version - actual implementation depends on 
     * FLASH module capabilities for dsPIC33EP512GP504 */
    
    if (address < BL_START_ADDR || address > METADATA_END_ADDR) {
        return BL_ERR_MEMORY;
    }

    /* Use FLASH_ProgramWord or similar if available */
    /* FLASH_ProgramWord(address, data); */
    
    return BL_OK;
}

/**
 * @brief Handle START_DOWNLOAD command
 */
static BL_STATUS_t BL_HandleStartDownload(const BL_CAN_FRAME_t *frame)
{
    if (bl_control.state == BL_STATE_DOWNLOAD_ACTIVE) {
        return BL_ERR_INVALID_STATE;
    }

    /* Payload format: [target_image(1 byte)] */
    if (frame->length < 1) {
        return BL_ERR_INVALID_CMD;
    }

    uint8_t image_sel = frame->payload[0];
    
    /* Determine target image */
    if (image_sel == 0) {
        bl_control.target_image = BL_IMAGE_1;
    } else if (image_sel == 1) {
        bl_control.target_image = BL_IMAGE_2;
    } else {
        return BL_ERR_INVALID_CMD;
    }

    /* Erase target image space */
    BL_STATUS_t status = BL_EraseImage(bl_control.target_image);
    if (status != BL_OK) {
        bl_control.state = BL_STATE_ERROR;
        bl_control.last_error = status;
        return status;
    }

    /* Initialize download parameters */
    bl_control.state = BL_STATE_DOWNLOAD_ACTIVE;
    bl_control.bytes_written = 0;
    bl_control.image_crc = 0xFFFFFFFF;
    bl_control.block_count = 0;

    switch (bl_control.target_image) {
        case BL_IMAGE_1:
            bl_control.write_address = APP_IMAGE1_START_ADDR;
            break;
        case BL_IMAGE_2:
            bl_control.write_address = APP_IMAGE2_START_ADDR;
            break;
        default:
            return BL_ERR_INVALID_CMD;
    }

    bl_control.last_error = BL_OK;
    return BL_OK;
}

/**
 * @brief Handle WRITE_BLOCK command
 */
static BL_STATUS_t BL_HandleWriteBlock(const BL_CAN_FRAME_t *frame)
{
    if (bl_control.state != BL_STATE_DOWNLOAD_ACTIVE) {
        return BL_ERR_INVALID_STATE;
    }

    /* Payload is 6 bytes of firmware data */
    uint32_t block_size = frame->length;
    if (block_size == 0 || block_size > 6) {
        return BL_ERR_INVALID_CMD;
    }

    /* Check if write would exceed image space */
    uint32_t max_size = (bl_control.target_image == BL_IMAGE_1) ? 
                        APP_IMAGE1_SIZE : APP_IMAGE2_SIZE;
    
    if ((bl_control.bytes_written + block_size) > max_size) {
        bl_control.state = BL_STATE_ERROR;
        bl_control.last_error = BL_ERR_FULL;
        return BL_ERR_FULL;
    }

    /* Update CRC for this block */
    for (uint32_t i = 0; i < block_size; i++) {
        bl_control.image_crc = BL_UpdateCRC32(bl_control.image_crc, 
                                              frame->payload[i]);
    }

    /* Program data to flash (word by word) */
    for (uint32_t i = 0; i < block_size; i++) {
        BL_STATUS_t status = BL_WriteFlashWord(bl_control.write_address, 
                                               (uint16_t)frame->payload[i]);
        if (status != BL_OK) {
            bl_control.state = BL_STATE_ERROR;
            bl_control.last_error = status;
            return status;
        }
        bl_control.write_address++;
    }

    bl_control.bytes_written += block_size;
    bl_control.block_count++;
    bl_control.last_error = BL_OK;

    return BL_OK;
}

/**
 * @brief Handle COMMIT_IMAGE command
 */
static BL_STATUS_t BL_HandleCommitImage(const BL_CAN_FRAME_t *frame)
{
    if (bl_control.state != BL_STATE_DOWNLOAD_ACTIVE) {
        return BL_ERR_INVALID_STATE;
    }

    bl_control.state = BL_STATE_PROGRAMMING;

    /* Verify the downloaded image CRC (sent in payload) */
    uint32_t received_crc = 0;
    if (frame->length >= 4) {
        received_crc = (uint32_t)frame->payload[0] |
                      ((uint32_t)frame->payload[1] << 8) |
                      ((uint32_t)frame->payload[2] << 16) |
                      ((uint32_t)frame->payload[3] << 24);
    }

    bl_control.image_crc ^= 0xFFFFFFFF;  /* Final XOR for CRC32 */

    if (received_crc != bl_control.image_crc) {
        bl_control.state = BL_STATE_ERROR;
        bl_control.last_error = BL_ERR_CRC;
        return BL_ERR_CRC;
    }

    /* Update metadata: mark image as valid */
    uint16_t valid_flag = IMAGE_VALID_FLAG;
    uint32_t metadata_offset = (bl_control.target_image == BL_IMAGE_1) ?
                               METADATA_IMAGE1_VALID : METADATA_IMAGE2_VALID;
    
    BL_STATUS_t status = BL_WriteFlashWord(METADATA_START_ADDR + metadata_offset, 
                                           valid_flag);
    if (status != BL_OK) {
        bl_control.state = BL_STATE_ERROR;
        bl_control.last_error = status;
        return status;
    }

    /* Set this image as active */
    status = BL_SetActiveImage(bl_control.target_image);
    if (status != BL_OK) {
        bl_control.state = BL_STATE_ERROR;
        bl_control.last_error = status;
        return status;
    }

    bl_control.state = BL_STATE_IDLE;
    bl_control.last_error = BL_OK;

    return BL_OK;
}

/**
 * @brief Handle VERIFY_IMAGE command
 */
static BL_STATUS_t BL_HandleVerifyImage(const BL_CAN_FRAME_t *frame)
{
    if (frame->length < 1) {
        return BL_ERR_INVALID_CMD;
    }

    uint8_t image_sel = frame->payload[0];
    BL_IMAGE_t image_to_verify = (image_sel == 0) ? BL_IMAGE_1 : BL_IMAGE_2;

    return BL_ValidateImage(image_to_verify);
}

/**
 * @brief Process received CAN frame (main entry point)
 */
BL_STATUS_t BL_HandleCANFrame(CAN_MSG_OBJ *can_frame)
{
    if (!bl_initialized) {
        return BL_ERR_NOT_INITIALIZED;
    }

    /* Extract bootloader frame from CAN message */
    BL_CAN_FRAME_t bl_frame;
    bl_frame.can_id = can_frame->msgId;
    bl_frame.length = can_frame->field.dlc > 0 ? can_frame->data[0] : 0;
    
    if (bl_frame.length > 6) {
        return BL_ERR_INVALID_CMD;
    }

    memcpy(bl_frame.payload, &can_frame->data[1], bl_frame.length);
    bl_frame.checksum = can_frame->field.dlc > 7 ? can_frame->data[7] : 0;

    /* Validate checksum */
    BL_STATUS_t status = BL_ValidateChecksum(&bl_frame);
    if (status != BL_OK) {
        bl_control.last_error = status;
        return status;
    }

    /* Dispatch to appropriate handler based on CAN ID */
    switch (bl_frame.can_id) {
        case BL_CMD_START_DOWNLOAD:
            status = BL_HandleStartDownload(&bl_frame);
            break;

        case BL_CMD_WRITE_BLOCK:
            status = BL_HandleWriteBlock(&bl_frame);
            break;

        case BL_CMD_COMMIT_IMAGE:
            status = BL_HandleCommitImage(&bl_frame);
            break;

        case BL_CMD_VERIFY_IMAGE:
            status = BL_HandleVerifyImage(&bl_frame);
            break;

        case BL_CMD_ABORT:
            status = BL_Abort();
            break;

        default:
            status = BL_ERR_INVALID_CMD;
            break;
    }

    bl_control.last_error = status;
    return status;
}

/**
 * @brief Abort current operation
 */
BL_STATUS_t BL_Abort(void)
{
    bl_control.state = BL_STATE_IDLE;
    bl_control.bytes_written = 0;
    bl_control.image_crc = 0xFFFFFFFF;
    bl_control.block_count = 0;
    bl_control.last_error = BL_OK;
    return BL_OK;
}

/**
 * @brief Get bootloader state
 */
BL_STATE_t BL_GetState(void)
{
    return bl_control.state;
}

/**
 * @brief Get last error
 */
BL_STATUS_t BL_GetLastError(void)
{
    return bl_control.last_error;
}

/**
 * @brief Validate image in flash
 */
BL_STATUS_t BL_ValidateImage(BL_IMAGE_t image)
{
    /* Check validity flag in metadata */
    uint32_t metadata_offset = (image == BL_IMAGE_1) ?
                               METADATA_IMAGE1_VALID : METADATA_IMAGE2_VALID;
    
    uint16_t valid_flag = *(uint16_t *)(METADATA_START_ADDR + metadata_offset);
    
    if (valid_flag != IMAGE_VALID_FLAG) {
        return BL_ERR_CRC;
    }

    return BL_OK;
}

/**
 * @brief Set active image
 */
BL_STATUS_t BL_SetActiveImage(BL_IMAGE_t image)
{
    uint32_t metadata_offset = METADATA_START_ADDR + METADATA_ACTIVE_IMAGE;
    uint16_t image_val = (image == BL_IMAGE_2) ? 1 : 0;
    
    return BL_WriteFlashWord(metadata_offset, image_val);
}

/**
 * @brief Get active image
 */
BL_IMAGE_t BL_GetActiveImage(void)
{
    uint16_t active_img_val = *(uint16_t *)(METADATA_START_ADDR + METADATA_ACTIVE_IMAGE);
    return (active_img_val == 1) ? BL_IMAGE_2 : BL_IMAGE_1;
}

/**
 * @brief Jump to application image
 */
void BL_JumpToImage(BL_IMAGE_t image)
{
    uint32_t app_start_addr = (image == BL_IMAGE_1) ? 
                              APP_IMAGE1_START_ADDR : APP_IMAGE2_START_ADDR;

    /* Disable interrupts */
    __builtin_disi(0x3FFF);

    /* Jump to application entry point */
    asm volatile ("goto %0" : : "r" (app_start_addr * 2));
}

/**
 * @brief Calculate image CRC32
 */
uint32_t BL_CalculateImageCRC(BL_IMAGE_t image)
{
    uint32_t start_addr = (image == BL_IMAGE_1) ? 
                          APP_IMAGE1_START_ADDR : APP_IMAGE2_START_ADDR;
    uint32_t size = (image == BL_IMAGE_1) ? 
                    APP_IMAGE1_SIZE : APP_IMAGE2_SIZE;

    uint32_t crc = 0xFFFFFFFF;
    
    for (uint32_t i = 0; i < size; i++) {
        uint8_t data = *(uint8_t *)(start_addr + i);
        crc = BL_UpdateCRC32(crc, data);
    }

    return (crc ^ 0xFFFFFFFF);
}

/**
 * @brief Erase image space
 */
BL_STATUS_t BL_EraseImage(BL_IMAGE_t image)
{
    uint32_t start_addr = (image == BL_IMAGE_1) ? 
                          APP_IMAGE1_START_ADDR : APP_IMAGE2_START_ADDR;
    uint32_t size = (image == BL_IMAGE_1) ? 
                    APP_IMAGE1_SIZE : APP_IMAGE2_SIZE;

    /* Erase flash space - implementation depends on FLASH module */
    /* This is a placeholder - actual erase would use FLASH_EraseBlock or similar */
    
    for (uint32_t i = 0; i < size; i++) {
        BL_STATUS_t status = BL_WriteFlashWord(start_addr + i, 0xFFFF);
        if (status != BL_OK) {
            return status;
        }
    }

    return BL_OK;
}

/**
 * @brief Check if bootloader should be entered
 */
bool BL_ShouldEnterBootloader(void)
{
    /* 
     * Bootloader entry conditions:
     * 1. CAN message with specific command received
     * 2. No valid application image present
     * 3. Manual entry signal (e.g., button press, specific GPIO state)
     * 
     * For now, check if either image is valid
     */
    
    uint16_t img1_valid = *(uint16_t *)(METADATA_START_ADDR + METADATA_IMAGE1_VALID);
    uint16_t img2_valid = *(uint16_t *)(METADATA_START_ADDR + METADATA_IMAGE2_VALID);

    /* If no valid image exists, stay in bootloader */
    if (img1_valid != IMAGE_VALID_FLAG && img2_valid != IMAGE_VALID_FLAG) {
        return true;
    }

    return false;
}

/**
 * @brief Bootloader housekeeping task
 */
void BL_Task(void)
{
    /* Add any periodic bootloader tasks here */
    /* Examples: timeout handling, state machine updates, etc. */
}
