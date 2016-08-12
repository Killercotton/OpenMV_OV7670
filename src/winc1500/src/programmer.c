#include <ff.h>
#include <stdio.h>
#include "fb_alloc.h"

#include "programmer/programmer.h"
#include "spi_flash/include/spi_flash_map.h"

// TODO pass file paths
#define FW_PATH             "/firmware/m2m_aio_3a0.bin"
#define FW_DUMP_PATH        "/firmware/fw_dump.bin"

#define MIN(a,b) \
    ({ __typeof__ (a) _a = (a); \
     __typeof__ (b) _b = (b); \
     _a < _b ? _a : _b; })

/**
 * Program firmware to WINC1500 memory.
 *
 * return M2M_SUCCESS on success, error code otherwise.
 */
int burn_firmware()
{
    FIL fp;
    uint32_t offset = 0;
    UINT bytes = 0, bytes_out=0;

    int ret = M2M_ERR_FAIL;
    uint8_t	*buf = fb_alloc(FLASH_SECTOR_SZ);

    if (f_open(&fp, FW_PATH, FA_READ|FA_OPEN_EXISTING) != FR_OK) {
        goto error;
    }

    // Firmware image size
    uint32_t size = f_size(&fp);

    while (size) {
        // Read a chuck (max FLASH_SECTOR_SZ bytes).
        bytes = MIN(size, FLASH_SECTOR_SZ);
        if (f_read(&fp, buf, bytes, &bytes_out) != FR_OK || bytes != bytes_out) {
            printf("burn_firmware: file read error!\n");
            goto error;
        }

        // Write firmware sector to the WINC1500 memory.
        if (programmer_write_firmware_image(buf, offset, bytes) != M2M_SUCCESS) {
            printf("burn_firmware: write error!\n");
            goto error;
        }

        size -= bytes;
        offset += bytes;
    }

    ret = M2M_SUCCESS;

error:
    fb_free();
    f_close(&fp);
    return ret;
}

/**
 * Verify WINC1500 firmware 
 * return M2M_SUCCESS on success, error code otherwise.
 */
int verify_firmware()
{
    FIL fp;
    uint32_t offset = 0;
    UINT bytes = 0, bytes_out=0;

    int ret = M2M_ERR_FAIL;
    uint8_t	*file_buf = fb_alloc(FLASH_SECTOR_SZ);
    uint8_t	*flash_buf = fb_alloc(FLASH_SECTOR_SZ);

    if (f_open(&fp, FW_PATH, FA_READ|FA_OPEN_EXISTING) != FR_OK) {
        goto error;
    }

    // Firmware image size
    uint32_t size = f_size(&fp);

    while (size) {
        // Firmware chuck size (max FLASH_SECTOR_SZ bytes).
        bytes = MIN(size, FLASH_SECTOR_SZ);

        if (f_read(&fp, file_buf, bytes, &bytes_out) != FR_OK || bytes_out != bytes) {
            printf("burn_firmware: file read error!\n");
            goto error;
        }

        if (programmer_read_firmware_image(flash_buf, offset, bytes) != M2M_SUCCESS) {
            printf("verify_firmware: read access failed on firmware section!\r\n");
            goto error;
        }

        for (int i=0; i<bytes; i++) {
            if (flash_buf[i] != file_buf[i]) {
                printf("verify_firmware: verification failed! offset:%ld flash:%x file:%x\n", offset+i, flash_buf[i], file_buf[i]);
                goto error;
            }
        }

        size -= bytes;
        offset += bytes;
    }

    ret = M2M_SUCCESS;

error:
    fb_free();
    fb_free();
    f_close(&fp);
    return ret;
}

/**
 * dump WINC1500 firmware
 * return M2M_SUCCESS on success, error code otherwise.
 */
int dump_firmware()
{
    FIL fp;
    uint32_t offset = 0;
    UINT bytes = 0, bytes_out=0;

    int ret = M2M_ERR_FAIL;
    uint8_t	*flash_buf = fb_alloc(FLASH_SECTOR_SZ);

    if (f_open(&fp, FW_DUMP_PATH, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        goto error;
    }

    // Firmware image size
    uint32_t size = FLASH_4M_TOTAL_SZ;

    while (size) {
        // Firmware chuck size (max FLASH_SECTOR_SZ bytes).
        bytes = MIN(size, FLASH_SECTOR_SZ);

        if (programmer_read_firmware_image(flash_buf, offset, bytes) != M2M_SUCCESS) {
            printf("verify_firmware: read access failed on firmware section!\r\n");
            goto error;
        }

        if (f_write(&fp, flash_buf, bytes, &bytes_out) != FR_OK || bytes_out != bytes) {
            printf("burn_firmware: file read error!\n");
            goto error;
        }

        size -= bytes;
        offset += bytes;
    }

    ret = M2M_SUCCESS;

error:
    fb_free();
    f_close(&fp);
    return ret;
}
