/*
 * This file is part of the Micro Python project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2013, 2014 Damien P. George
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/nlr.h"
#include "py/runtime.h"
#include "lib/fatfs/ff.h"
#include "extmod/fsusermount.h"

#include "sdcard.h"
#include "pin.h"
#include "genhdr/pins.h"
#include "bufhelper.h"
#include "dma.h"
#include "irq.h"
#include "stdio.h"

#if MICROPY_HW_HAS_SDCARD

#if defined(MCU_SERIES_F7)

// The F7 series calls the peripheral SDMMC rather than SDIO, so provide some
// #defines for backwards compatability.

#define SDIO    SDMMC1

#define SDIO_CLOCK_EDGE_RISING              SDMMC_CLOCK_EDGE_RISING
#define SDIO_CLOCK_EDGE_FALLING             SDMMC_CLOCK_EDGE_FALLING

#define SDIO_CLOCK_BYPASS_DISABLE           SDMMC_CLOCK_BYPASS_DISABLE
#define SDIO_CLOCK_BYPASS_ENABLE            SDMMC_CLOCK_BYPASS_ENABLE

#define SDIO_CLOCK_POWER_SAVE_DISABLE       SDMMC_CLOCK_POWER_SAVE_DISABLE
#define SDIO_CLOCK_POWER_SAVE_ENABLE        SDMMC_CLOCK_POWER_SAVE_ENABLE

#define SDIO_BUS_WIDE_1B                    SDMMC_BUS_WIDE_1B
#define SDIO_BUS_WIDE_4B                    SDMMC_BUS_WIDE_4B
#define SDIO_BUS_WIDE_8B                    SDMMC_BUS_WIDE_8B

#define SDIO_HARDWARE_FLOW_CONTROL_DISABLE  SDMMC_HARDWARE_FLOW_CONTROL_DISABLE
#define SDIO_HARDWARE_FLOW_CONTROL_ENABLE   SDMMC_HARDWARE_FLOW_CONTROL_ENABLE

#define SDIO_TRANSFER_CLK_DIV               SDMMC_TRANSFER_CLK_DIV

#endif

#define UNALIGNED_BUFFER(p)     ((uint32_t)p & 3)
#define CCM_BUFFER(p)           (!(((uint32_t) p) & (1u<<29)))

// TODO: Since SDIO is fundamentally half-duplex, we really only need to
//       tie up one DMA channel. However, the HAL DMA API doesn't
// seem to provide a convenient way to change the direction. I believe that
// its as simple as changing the CR register and the Init.Direction field
// and make DMA_SetConfig public.

// TODO: I think that as an optimization, we can allocate these dynamically
//       if an sd card is detected. This will save approx 260 bytes of RAM
//       when no sdcard was being used.
static SD_HandleTypeDef sd_handle;
static DMA_HandleTypeDef sd_rx_dma, sd_tx_dma;

// Parameters to dma_init() for SDIO tx and rx.
static const DMA_InitTypeDef dma_init_struct_sdio = {
    .Channel             = 0,
    .Direction           = 0,
    .PeriphInc           = DMA_PINC_DISABLE,
    .MemInc              = DMA_MINC_ENABLE,
    .PeriphDataAlignment = DMA_PDATAALIGN_WORD,
    .MemDataAlignment    = DMA_MDATAALIGN_WORD,
    .Mode                = DMA_PFCTRL,
    .Priority            = DMA_PRIORITY_HIGH,
    .FIFOMode            = DMA_FIFOMODE_ENABLE,
    .FIFOThreshold       = DMA_FIFO_THRESHOLD_FULL,
    .MemBurst            = DMA_MBURST_INC4,
    .PeriphBurst         = DMA_PBURST_INC4,
};

void sdcard_init(void) {
    GPIO_InitTypeDef GPIO_Init_Structure;

    // invalidate the sd_handle
    sd_handle.Instance = NULL;

    // configure SD GPIO
    // we do this here an not in HAL_SD_MspInit because it apparently
    // makes it more robust to have the pins always pulled high
    GPIO_Init_Structure.Mode = GPIO_MODE_AF_PP;
    GPIO_Init_Structure.Pull = GPIO_PULLUP;
    GPIO_Init_Structure.Speed = GPIO_SPEED_HIGH;
    GPIO_Init_Structure.Alternate = GPIO_AF12_SDIO;
    GPIO_Init_Structure.Pin = GPIO_PIN_8 | GPIO_PIN_9 | GPIO_PIN_10 | GPIO_PIN_11 | GPIO_PIN_12;
    HAL_GPIO_Init(GPIOC, &GPIO_Init_Structure);
    GPIO_Init_Structure.Pin = GPIO_PIN_2;
    HAL_GPIO_Init(GPIOD, &GPIO_Init_Structure);

    // configure the SD card detect pin
    // we do this here so we can detect if the SD card is inserted before powering it on
    GPIO_Init_Structure.Mode = GPIO_MODE_INPUT;
    GPIO_Init_Structure.Pull = MICROPY_HW_SDCARD_DETECT_PULL;
    GPIO_Init_Structure.Speed = GPIO_SPEED_LOW;
    GPIO_Init_Structure.Pin = MICROPY_HW_SDCARD_DETECT_PIN.pin_mask;
    HAL_GPIO_Init(MICROPY_HW_SDCARD_DETECT_PIN.gpio, &GPIO_Init_Structure);
}

void HAL_SD_MspInit(SD_HandleTypeDef *hsd) {
    // enable SDIO clock
    __SDIO_CLK_ENABLE();

    // NVIC configuration for SDIO interrupts
    HAL_NVIC_SetPriority(SDIO_IRQn, IRQ_PRI_SDIO, IRQ_SUBPRI_SDIO);
    HAL_NVIC_EnableIRQ(SDIO_IRQn);

    // GPIO have already been initialised by sdcard_init
}

void HAL_SD_MspDeInit(SD_HandleTypeDef *hsd) {
    HAL_NVIC_DisableIRQ(SDIO_IRQn);
    __SDIO_CLK_DISABLE();
}

bool sdcard_is_present(void) {
    return HAL_GPIO_ReadPin(MICROPY_HW_SDCARD_DETECT_PIN.gpio, MICROPY_HW_SDCARD_DETECT_PIN.pin_mask) == MICROPY_HW_SDCARD_DETECT_PRESENT;
}

bool sdcard_power_on(void) {
    if (!sdcard_is_present()) {
        return false;
    }
    if (sd_handle.Instance) {
        return true;
    }

    // SD device interface configuration
    sd_handle.Instance = SDIO;
    sd_handle.Init.ClockEdge           = SDIO_CLOCK_EDGE_RISING;
    sd_handle.Init.ClockBypass         = SDIO_CLOCK_BYPASS_DISABLE;
    sd_handle.Init.ClockPowerSave      = SDIO_CLOCK_POWER_SAVE_DISABLE;
    sd_handle.Init.BusWide             = SDIO_BUS_WIDE_1B;
    sd_handle.Init.HardwareFlowControl = SDIO_HARDWARE_FLOW_CONTROL_DISABLE;
    sd_handle.Init.ClockDiv            = SDIO_TRANSFER_CLK_DIV;

    // init the SD interface, with retry if it's not ready yet
    HAL_SD_CardInfoTypedef cardinfo;
    for (int retry = 10; HAL_SD_Init(&sd_handle, &cardinfo) != SD_OK; retry--) {
        if (retry == 0) {
            goto error;
        }
        HAL_Delay(50);
    }

    // configure the SD bus width for wide operation
    if (HAL_SD_WideBusOperation_Config(&sd_handle, SDIO_BUS_WIDE_4B) != SD_OK) {
        HAL_SD_DeInit(&sd_handle);
        goto error;
    }

    return true;

error:
    sd_handle.Instance = NULL;
    return false;
}

void sdcard_power_off(void) {
    if (!sd_handle.Instance) {
        return;
    }
    HAL_SD_DeInit(&sd_handle); 
    sd_handle.Instance = NULL;
}

uint64_t sdcard_get_capacity_in_bytes(void) {
    if (sd_handle.Instance == NULL) {
        return 0;
    }
    HAL_SD_CardInfoTypedef cardinfo;
    HAL_SD_Get_CardInfo(&sd_handle, &cardinfo);
    return cardinfo.CardCapacity;
}

void SDIO_IRQHandler(void) {
    IRQ_ENTER(SDIO_IRQn);
    HAL_SD_IRQHandler(&sd_handle);
    IRQ_EXIT(SDIO_IRQn);
}

mp_uint_t sdcard_read_blocks(uint8_t *dest, uint32_t block_num, uint32_t num_blocks) {
    // check that SD card is initialised
    if (sd_handle.Instance == NULL) {
        return SD_ERROR;
    }

    HAL_SD_ErrorTypedef err = SD_OK;

    if (query_irq() == IRQ_STATE_DISABLED || CCM_BUFFER(dest) || UNALIGNED_BUFFER(dest)) {
        if (UNALIGNED_BUFFER(dest)) {
            printf("unaligned read buf:%p count%lu \n", dest, num_blocks);
        }
        // This transfer has to be done in an atomic section.
        mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
        err = HAL_SD_ReadBlocks_BlockNumber(&sd_handle, (uint32_t*)dest, block_num, SDCARD_BLOCK_SIZE, num_blocks);
        MICROPY_END_ATOMIC_SECTION(atomic_state);
    } else {
        // we must disable USB irqs to prevent MSC contention with SD card
        uint32_t basepri = raise_irq_pri(IRQ_PRI_OTG_FS);

        dma_init(&sd_rx_dma, DMA_STREAM_SDIO_RX, &dma_init_struct_sdio,
            DMA_CHANNEL_SDIO_RX, DMA_PERIPH_TO_MEMORY, &sd_handle);
        sd_handle.hdmarx = &sd_rx_dma;

        err = HAL_SD_ReadBlocks_BlockNumber_DMA(&sd_handle, (uint32_t*)dest, block_num, SDCARD_BLOCK_SIZE, num_blocks);
        if (err == SD_OK) {
            // wait for DMA transfer to finish, with a large timeout
            err = HAL_SD_CheckReadOperation(&sd_handle, 100000000);
        }

        dma_deinit(sd_handle.hdmarx);
        sd_handle.hdmarx = NULL;

        restore_irq_pri(basepri);
    }

    return err;
}

mp_uint_t sdcard_write_blocks(const uint8_t *src, uint32_t block_num, uint32_t num_blocks) {
    // check that SD card is initialised
    if (sd_handle.Instance == NULL) {
        return SD_ERROR;
    }

    HAL_SD_ErrorTypedef err = SD_OK;

    if (query_irq() == IRQ_STATE_DISABLED || CCM_BUFFER(src) || UNALIGNED_BUFFER(src)) {
        if (UNALIGNED_BUFFER(src)) {
            printf("unaligned write buf:%p count%lu \n", src, num_blocks);
        }
        // This transfer has to be done in an atomic section.
        mp_uint_t atomic_state = MICROPY_BEGIN_ATOMIC_SECTION();
        err = HAL_SD_WriteBlocks_BlockNumber(&sd_handle, (uint32_t*)src, block_num, SDCARD_BLOCK_SIZE, num_blocks);
        MICROPY_END_ATOMIC_SECTION(atomic_state);
    } else {
        // we must disable USB irqs to prevent MSC contention with SD card
        uint32_t basepri = raise_irq_pri(IRQ_PRI_OTG_FS);

        dma_init(&sd_tx_dma, DMA_STREAM_SDIO_TX, &dma_init_struct_sdio,
            DMA_CHANNEL_SDIO_TX, DMA_MEMORY_TO_PERIPH, &sd_handle);
        sd_handle.hdmatx = &sd_tx_dma;

        err = HAL_SD_WriteBlocks_BlockNumber_DMA(&sd_handle, (uint32_t*)src, block_num, SDCARD_BLOCK_SIZE, num_blocks);
        if (err == SD_OK) {
            // wait for DMA transfer to finish, with a large timeout
            err = HAL_SD_CheckWriteOperation(&sd_handle, 100000000);
        }
        dma_deinit(sd_handle.hdmatx);
        sd_handle.hdmatx = NULL;

        restore_irq_pri(basepri);
    }

    return err;
}

/******************************************************************************/
// Micro Python bindings
//
// Expose the SD card as an object with the block protocol.

// there is a singleton SDCard object
const mp_obj_base_t pyb_sdcard_obj = {&pyb_sdcard_type};

STATIC mp_obj_t pyb_sdcard_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    // check arguments
    mp_arg_check_num(n_args, n_kw, 0, 0, false);

    // return singleton object
    return (mp_obj_t)&pyb_sdcard_obj;
}

STATIC mp_obj_t sd_present(mp_obj_t self) {
    return mp_obj_new_bool(sdcard_is_present());
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sd_present_obj, sd_present);

STATIC mp_obj_t sd_power(mp_obj_t self, mp_obj_t state) {
    bool result;
    if (mp_obj_is_true(state)) {
        result = sdcard_power_on();
    } else {
        sdcard_power_off();
        result = true;
    }
    return mp_obj_new_bool(result);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sd_power_obj, sd_power);

STATIC mp_obj_t sd_info(mp_obj_t self) {
    if (sd_handle.Instance == NULL) {
        return mp_const_none;
    }
    HAL_SD_CardInfoTypedef cardinfo;
    HAL_SD_Get_CardInfo(&sd_handle, &cardinfo);
    // cardinfo.SD_csd and cardinfo.SD_cid have lots of info but we don't use them
    mp_obj_t tuple[3] = {
        mp_obj_new_int_from_ull(cardinfo.CardCapacity),
        mp_obj_new_int_from_uint(cardinfo.CardBlockSize),
        mp_obj_new_int(cardinfo.CardType),
    };
    return mp_obj_new_tuple(3, tuple);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(sd_info_obj, sd_info);

// now obsolete, kept for backwards compatibility
STATIC mp_obj_t sd_read(mp_obj_t self, mp_obj_t block_num) {
    uint8_t *dest = m_new(uint8_t, SDCARD_BLOCK_SIZE);
    mp_uint_t ret = sdcard_read_blocks(dest, mp_obj_get_int(block_num), 1);

    if (ret != 0) {
        m_del(uint8_t, dest, SDCARD_BLOCK_SIZE);
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_Exception, "sdcard_read_blocks failed [%u]", ret));
    }

    return mp_obj_new_bytearray_by_ref(SDCARD_BLOCK_SIZE, dest);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_2(sd_read_obj, sd_read);

// now obsolete, kept for backwards compatibility
STATIC mp_obj_t sd_write(mp_obj_t self, mp_obj_t block_num, mp_obj_t data) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(data, &bufinfo, MP_BUFFER_READ);
    if (bufinfo.len % SDCARD_BLOCK_SIZE != 0) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_ValueError, "writes must be a multiple of %d bytes", SDCARD_BLOCK_SIZE));
    }

    mp_uint_t ret = sdcard_write_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);

    if (ret != 0) {
        nlr_raise(mp_obj_new_exception_msg_varg(&mp_type_Exception, "sdcard_write_blocks failed [%u]", ret));
    }

    return mp_const_none;
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(sd_write_obj, sd_write);

STATIC mp_obj_t pyb_sdcard_readblocks(mp_obj_t self, mp_obj_t block_num, mp_obj_t buf) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_WRITE);
    mp_uint_t ret = sdcard_read_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);
    return mp_obj_new_bool(ret == 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_readblocks_obj, pyb_sdcard_readblocks);

STATIC mp_obj_t pyb_sdcard_writeblocks(mp_obj_t self, mp_obj_t block_num, mp_obj_t buf) {
    mp_buffer_info_t bufinfo;
    mp_get_buffer_raise(buf, &bufinfo, MP_BUFFER_READ);
    mp_uint_t ret = sdcard_write_blocks(bufinfo.buf, mp_obj_get_int(block_num), bufinfo.len / SDCARD_BLOCK_SIZE);
    return mp_obj_new_bool(ret == 0);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_writeblocks_obj, pyb_sdcard_writeblocks);

STATIC mp_obj_t pyb_sdcard_ioctl(mp_obj_t self, mp_obj_t cmd_in, mp_obj_t arg_in) {
    mp_int_t cmd = mp_obj_get_int(cmd_in);
    switch (cmd) {
        case BP_IOCTL_INIT:
            if (!sdcard_power_on()) {
                return MP_OBJ_NEW_SMALL_INT(-1); // error
            }
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case BP_IOCTL_DEINIT:
            sdcard_power_off();
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case BP_IOCTL_SYNC:
            // nothing to do
            return MP_OBJ_NEW_SMALL_INT(0); // success

        case BP_IOCTL_SEC_COUNT:
            return MP_OBJ_NEW_SMALL_INT(0); // TODO

        case BP_IOCTL_SEC_SIZE:
            return MP_OBJ_NEW_SMALL_INT(SDCARD_BLOCK_SIZE);

        default: // unknown command
            return MP_OBJ_NEW_SMALL_INT(-1); // error
    }
}
STATIC MP_DEFINE_CONST_FUN_OBJ_3(pyb_sdcard_ioctl_obj, pyb_sdcard_ioctl);

STATIC const mp_map_elem_t pyb_sdcard_locals_dict_table[] = {
    { MP_OBJ_NEW_QSTR(MP_QSTR_present), (mp_obj_t)&sd_present_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_power), (mp_obj_t)&sd_power_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_info), (mp_obj_t)&sd_info_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_read), (mp_obj_t)&sd_read_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_write), (mp_obj_t)&sd_write_obj },
    // block device protocol
    { MP_OBJ_NEW_QSTR(MP_QSTR_readblocks), (mp_obj_t)&pyb_sdcard_readblocks_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_writeblocks), (mp_obj_t)&pyb_sdcard_writeblocks_obj },
    { MP_OBJ_NEW_QSTR(MP_QSTR_ioctl), (mp_obj_t)&pyb_sdcard_ioctl_obj },
};

STATIC MP_DEFINE_CONST_DICT(pyb_sdcard_locals_dict, pyb_sdcard_locals_dict_table);

const mp_obj_type_t pyb_sdcard_type = {
    { &mp_type_type },
    .name = MP_QSTR_SDCard,
    .make_new = pyb_sdcard_make_new,
    .locals_dict = (mp_obj_t)&pyb_sdcard_locals_dict,
};

void sdcard_init_vfs(fs_user_mount_t *vfs) {
    vfs->flags |= FSUSER_NATIVE | FSUSER_HAVE_IOCTL;
    vfs->readblocks[0] = (mp_obj_t)&pyb_sdcard_readblocks_obj;
    vfs->readblocks[1] = (mp_obj_t)&pyb_sdcard_obj;
    vfs->readblocks[2] = (mp_obj_t)sdcard_read_blocks; // native version
    vfs->writeblocks[0] = (mp_obj_t)&pyb_sdcard_writeblocks_obj;
    vfs->writeblocks[1] = (mp_obj_t)&pyb_sdcard_obj;
    vfs->writeblocks[2] = (mp_obj_t)sdcard_write_blocks; // native version
    vfs->u.ioctl[0] = (mp_obj_t)&pyb_sdcard_ioctl_obj;
    vfs->u.ioctl[1] = (mp_obj_t)&pyb_sdcard_obj;
}

#endif // MICROPY_HW_HAS_SDCARD
