/* mbed Microcontroller Library
 * Copyright (c) 2015-2016 Nuvoton
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* Nuvoton mbed enabled targets which support SD card of SD bus mode */
#if defined(TARGET_NUMAKER_PFM_NUC472) || defined(TARGET_NUMAKER_PFM_M487)

#include "NuSDBlockDevice.h"
#include "PeripheralPins.h"
#include "mbed_debug.h"
#include "nu_modutil.h"

#if defined(TARGET_NUMAKER_PFM_NUC472)
#define NU_SDH_DAT0         PF_5
#define NU_SDH_DAT1         PF_4
#define NU_SDH_DAT2         PF_3
#define NU_SDH_DAT3         PF_2
#define NU_SDH_CMD          PF_7
#define NU_SDH_CLK          PF_8
#define NU_SDH_CDn          PF_6

#elif defined(TARGET_NUMAKER_PFM_M487)
#define NU_SDH_DAT0         PC_4
#define NU_SDH_DAT1         PC_3
#define NU_SDH_DAT2         PC_2
#define NU_SDH_DAT3         PC_1
#define NU_SDH_CMD          PE_12
#define NU_SDH_CLK          PC_0
#define NU_SDH_CDn          PE_13

#endif

#if defined(TARGET_NUMAKER_PFM_NUC472)
extern DISK_DATA_T SD_DiskInfo0;
extern DISK_DATA_T SD_DiskInfo1;
extern SD_INFO_T SD0,SD1;
extern int sd0_ok,sd1_ok;

#elif defined(TARGET_NUMAKER_PFM_M487)
extern int SDH_ok;
extern SDH_INFO_T SD0, SD1;

#endif


static const struct nu_modinit_s sdh_modinit_tab[] = {
#if defined(TARGET_NUMAKER_PFM_NUC472)
    {SD_0_0, SDH_MODULE, CLK_CLKSEL0_SDHSEL_PLL, CLK_CLKDIV0_SDH(2), SDH_RST, SD_IRQn, NULL},
    {SD_0_1, SDH_MODULE, CLK_CLKSEL0_SDHSEL_PLL, CLK_CLKDIV0_SDH(2), SDH_RST, SD_IRQn, NULL},
#elif defined(TARGET_NUMAKER_PFM_M487)
    {SD_0, SDH0_MODULE, CLK_CLKSEL0_SDH0SEL_HCLK, CLK_CLKDIV0_SDH0(2), SDH0_RST, SDH0_IRQn, NULL},
    {SD_1, SDH1_MODULE, CLK_CLKSEL0_SDH1SEL_HCLK, CLK_CLKDIV3_SDH1(2), SDH1_RST, SDH1_IRQn, NULL},
#endif

    {NC, 0, 0, 0, 0, (IRQn_Type) 0, NULL}
};



#define SD_BLOCK_DEVICE_ERROR_WOULD_BLOCK        -5001	/*!< operation would block */
#define SD_BLOCK_DEVICE_ERROR_UNSUPPORTED        -5002	/*!< unsupported operation */
#define SD_BLOCK_DEVICE_ERROR_PARAMETER          -5003	/*!< invalid parameter */
#define SD_BLOCK_DEVICE_ERROR_NO_INIT            -5004	/*!< uninitialized */
#define SD_BLOCK_DEVICE_ERROR_NO_DEVICE          -5005	/*!< device is missing or not connected */
#define SD_BLOCK_DEVICE_ERROR_WRITE_PROTECTED    -5006	/*!< write protected */


NuSDBlockDevice::NuSDBlockDevice() :
    _sectors(0),
    _is_initialized(false),
    _dbg(false),
    _sdh_modinit(NULL),
    _sdh((SDName) NC),
    _sdh_base(NULL),
#if defined(TARGET_NUMAKER_PFM_NUC472)
    _sdh_port((uint32_t) -1),
#endif
    _sdh_irq_thunk(this, &NuSDBlockDevice::_sdh_irq),
    _sd_dat0(NU_SDH_DAT0),
    _sd_dat1(NU_SDH_DAT1),
    _sd_dat2(NU_SDH_DAT2),
    _sd_dat3(NU_SDH_DAT3),
    _sd_cmd(NU_SDH_CMD),
    _sd_clk(NU_SDH_CLK),
    _sd_cdn(NU_SDH_CDn)
{
}

NuSDBlockDevice::NuSDBlockDevice(PinName sd_dat0, PinName sd_dat1, PinName sd_dat2, PinName sd_dat3,
    PinName sd_cmd, PinName sd_clk, PinName sd_cdn) :
    _sectors(0),
    _is_initialized(false),
    _dbg(false),
    _sdh_modinit(NULL),
    _sdh((SDName) NC),
    _sdh_base(NULL),
#if defined(TARGET_NUMAKER_PFM_NUC472)
    _sdh_port((uint32_t) -1),
#endif
    _sdh_irq_thunk(this, &NuSDBlockDevice::_sdh_irq)
{
    _sd_dat0 = sd_dat0;
    _sd_dat1 = sd_dat1;
    _sd_dat2 = sd_dat2;
    _sd_dat3 = sd_dat3;
    _sd_cmd = sd_cmd;
    _sd_clk = sd_clk;
    _sd_cdn = sd_cdn;
}

NuSDBlockDevice::~NuSDBlockDevice()
{
    if (_is_initialized) {
        deinit();
    }
}

int NuSDBlockDevice::init()
{
    _lock.lock();
    int err = BD_ERROR_OK;
    
    do {
        err = _init_sdh();
        if (err != BD_ERROR_OK) {
            break;
        }
        
#if defined(TARGET_NUMAKER_PFM_NUC472)
        SD_Open(_sdh_port | CardDetect_From_GPIO);
        SD_Probe(_sdh_port);

        switch (_sdh_port) {
        case SD_PORT0:
            _is_initialized = sd0_ok && (SD0.CardType != SD_TYPE_UNKNOWN);
            break;
    
        case SD_PORT1:
            _is_initialized = sd1_ok && (SD1.CardType != SD_TYPE_UNKNOWN);
            break;
        }
    
#elif defined(TARGET_NUMAKER_PFM_M487)
        MBED_ASSERT(_sdh_modinit != NULL);
        
        NVIC_SetVector(_sdh_modinit->irq_n, _sdh_irq_thunk.entry());
        NVIC_EnableIRQ(_sdh_modinit->irq_n);

        SDH_Open(_sdh_base, CardDetect_From_GPIO);
        SDH_Probe(_sdh_base);
    
        switch (NU_MODINDEX(_sdh)) {
        case 0:
            _is_initialized = SDH_ok && (SD0.CardType != SDH_TYPE_UNKNOWN);
            break;
    
        case 1:
            _is_initialized = SDH_ok && (SD1.CardType != SDH_TYPE_UNKNOWN);
            break;
        }
#endif

        if (!_is_initialized) {
            debug_if(_dbg, "Fail to initialize card\n");
            err = BD_ERROR_DEVICE_ERROR;
        }
        debug_if(_dbg, "init card = %d\n", _is_initialized);
        _sectors = _sd_sectors();
    
    } while (0);

    _lock.unlock();
    
    return err;
}

int NuSDBlockDevice::deinit()
{
    _lock.lock();
   
    if (_sdh_modinit) {
        CLK_DisableModuleClock(_sdh_modinit->clkidx);
    }
    
#if defined(TARGET_NUMAKER_PFM_NUC472)
    // TODO
#elif defined(TARGET_NUMAKER_PFM_M487)
    // TODO
#endif

    _is_initialized = false;
    
    _lock.unlock();
    
    return BD_ERROR_OK;
}

int NuSDBlockDevice::program(const void *b, bd_addr_t addr, bd_size_t size)
{
    if (! is_valid_program(addr, size)) {
        return SD_BLOCK_DEVICE_ERROR_PARAMETER;
    }

    _lock.lock();
    int err = BD_ERROR_OK;
    
    do {
        if (! _is_initialized) {
            err = SD_BLOCK_DEVICE_ERROR_NO_INIT;
        }

#if defined(TARGET_NUMAKER_PFM_NUC472)
        if (SD_Write(_sdh_port, (uint8_t*)b, addr / 512, size / 512) != 0) {
#elif defined(TARGET_NUMAKER_PFM_M487)
        if (SDH_Write(_sdh_base, (uint8_t*)b, addr / 512, size / 512) != 0) {
#endif
            err = BD_ERROR_DEVICE_ERROR;
        }
        
    } while (0);
    
    _lock.unlock();
    
    return err;
}

int NuSDBlockDevice::read(void *b, bd_addr_t addr, bd_size_t size)
{
    if (! is_valid_read(addr, size)) {
        return SD_BLOCK_DEVICE_ERROR_PARAMETER;
    }

    _lock.lock();
    int err = BD_ERROR_OK;
    
    do {
        if (! _is_initialized) {
            err = SD_BLOCK_DEVICE_ERROR_NO_INIT;
        }
        
#if defined(TARGET_NUMAKER_PFM_NUC472)
        if (SD_Read(_sdh_port, (uint8_t*)b, addr / 512, size / 512) != 0) {
#elif defined(TARGET_NUMAKER_PFM_M487)
        if (SDH_Read(_sdh_base, (uint8_t*)b, addr / 512, size / 512) != 0) {
#endif
            err = BD_ERROR_DEVICE_ERROR;
        }
        
    } while (0);
    
    _lock.unlock();
    
    return err;
}

int NuSDBlockDevice::erase(bd_addr_t addr, bd_size_t size)
{
    return BD_ERROR_OK;
}

bd_size_t NuSDBlockDevice::get_read_size() const
{
    return 512;
}

bd_size_t NuSDBlockDevice::get_program_size() const
{
    return 512;
}

bd_size_t NuSDBlockDevice::get_erase_size() const
{
    return 512;
}

bd_size_t NuSDBlockDevice::size() const
{
    return 512 * _sectors;
}

void NuSDBlockDevice::debug(bool dbg)
{
    _dbg = dbg;
}

int NuSDBlockDevice::_init_sdh()
{
    debug_if(_dbg, "SD MPF Setting & Enable SD IP Clock\n");
        
    // Check if all pins belong to the same SD module
    // Merge SD DAT0/1/2/3
    uint32_t sd_dat0_mod = pinmap_peripheral(_sd_dat0, PinMap_SD_DAT0);
    uint32_t sd_dat1_mod = pinmap_peripheral(_sd_dat1, PinMap_SD_DAT1);
    uint32_t sd_dat2_mod = pinmap_peripheral(_sd_dat2, PinMap_SD_DAT2);
    uint32_t sd_dat3_mod = pinmap_peripheral(_sd_dat3, PinMap_SD_DAT3);
    uint32_t sd_dat01_mod = (SDName) pinmap_merge(sd_dat0_mod, sd_dat1_mod);
    uint32_t sd_dat23_mod = (SDName) pinmap_merge(sd_dat2_mod, sd_dat3_mod);
    uint32_t sd_dat0123_mod = (SDName) pinmap_merge(sd_dat01_mod, sd_dat23_mod);
    // Merge SD CMD/CLK/CDn
    uint32_t sd_cmd_mod = pinmap_peripheral(_sd_cmd, PinMap_SD_CMD);
    uint32_t sd_clk_mod = pinmap_peripheral(_sd_clk, PinMap_SD_CLK);
    uint32_t sd_cdn_mod = pinmap_peripheral(_sd_cdn, PinMap_SD_CD);
    uint32_t sd_cmdclk_mod = (SDName) pinmap_merge(sd_cmd_mod, sd_clk_mod);
    uint32_t sd_cmdclkcdn_mod = (SDName) pinmap_merge(sd_cmdclk_mod, sd_cdn_mod);
    // Merge SD DAT0/1/2/3 and SD CMD/CLK/CDn
    uint32_t sd_mod = (SDName) pinmap_merge(sd_dat0123_mod, sd_cmdclkcdn_mod);
    
    if (sd_mod == (uint32_t) NC) {
        debug("SD pinmap error\n");
        return BD_ERROR_DEVICE_ERROR;
    }
    
    _sdh_modinit = get_modinit(sd_mod, sdh_modinit_tab);
    MBED_ASSERT(_sdh_modinit != NULL);
    MBED_ASSERT(_sdh_modinit->modname == sd_mod);
    
    
    // Configure SD multi-function pins
    pinmap_pinout(_sd_dat0, PinMap_SD_DAT0);
    pinmap_pinout(_sd_dat1, PinMap_SD_DAT1);
    pinmap_pinout(_sd_dat2, PinMap_SD_DAT2);
    pinmap_pinout(_sd_dat3, PinMap_SD_DAT3);
    pinmap_pinout(_sd_cmd, PinMap_SD_CMD);
    pinmap_pinout(_sd_clk, PinMap_SD_CLK);
    pinmap_pinout(_sd_cdn, PinMap_SD_CD);
    
    // Configure SD IP clock 
    SYS_UnlockReg();
    
    // Determine SDH port dependent on passed-in pins
    _sdh = (SDName) sd_mod;
    _sdh_base = (SDH_T *) NU_MODBASE(_sdh);
#if defined(TARGET_NUMAKER_PFM_NUC472)
    switch (NU_MODSUBINDEX(_sdh)) {
    case 0:
        _sdh_port = SD_PORT0;
        break;
        
    case 1:
        _sdh_port = SD_PORT1;
        break;
    }
#endif

    SYS_ResetModule(_sdh_modinit->rsetidx);
    CLK_SetModuleClock(_sdh_modinit->clkidx, _sdh_modinit->clksrc, _sdh_modinit->clkdiv);
    CLK_EnableModuleClock(_sdh_modinit->clkidx);
    
    SYS_LockReg();

    return BD_ERROR_OK;
}

uint32_t NuSDBlockDevice::_sd_sectors()
{
    _lock.lock();
   
#if defined(TARGET_NUMAKER_PFM_NUC472)
    switch (_sdh_port) {
    case SD_PORT0:
        _sectors = SD_DiskInfo0.totalSectorN;
        break;
    case SD_PORT1:
        _sectors = SD_DiskInfo1.totalSectorN;
        break;
    }
    
#elif defined(TARGET_NUMAKER_PFM_M487)
    switch (NU_MODINDEX(_sdh)) {
    case 0:
        _sectors = SD0.totalSectorN;
        break;
    case 1:
        _sectors = SD1.totalSectorN;
        break;
    }
    
#endif

    _lock.unlock();
    
    return _sectors;
}

void NuSDBlockDevice::_sdh_irq()
{
#if defined(TARGET_NUMAKER_PFM_NUC472)
    // TODO: Support IRQ
    
#elif defined(TARGET_NUMAKER_PFM_M487)
    // FMI data abort interrupt
    if (_sdh_base->GINTSTS & SDH_GINTSTS_DTAIF_Msk) {
        _sdh_base->GINTSTS = SDH_GINTSTS_DTAIF_Msk;
        /* ResetAllEngine() */
        _sdh_base->GCTL |= SDH_GCTL_GCTLRST_Msk;
    }

    //----- SD interrupt status
    if (_sdh_base->INTSTS & SDH_INTSTS_BLKDIF_Msk) {
        // block down
        extern uint8_t volatile _SDH_SDDataReady;
        _SDH_SDDataReady = TRUE;
        _sdh_base->INTSTS = SDH_INTSTS_BLKDIF_Msk;
    }
    
    // NOTE: On M487, there are two SDH instances which each support port 0 and don't support port 1.
    //       Port 0 (support): INTEN.CDIEN0, INTEN.CDSRC0, INTSTS.CDIF0, INTSTS.CDSTS0
    //       Port 1 (no support): INTEN.CDIEN1, INTEN.CDSRC1, INTSTS.CDIF1, INTSTS.CDSTS1
    if (_sdh_base->INTSTS & SDH_INTSTS_CDIF0_Msk) { // port 0 card detect
        _sdh_base->INTSTS = SDH_INTSTS_CDIF0_Msk;
        // TBD: Support PnP
    }

    // CRC error interrupt
    if (_sdh_base->INTSTS & SDH_INTSTS_CRCIF_Msk) {
        _sdh_base->INTSTS = SDH_INTSTS_CRCIF_Msk;      // clear interrupt flag
    }

    if (_sdh_base->INTSTS & SDH_INTSTS_DITOIF_Msk) {
        _sdh_base->INTSTS = SDH_INTSTS_DITOIF_Msk;
    }

    // Response in timeout interrupt
    if (_sdh_base->INTSTS & SDH_INTSTS_RTOIF_Msk) {
        _sdh_base->INTSTS |= SDH_INTSTS_RTOIF_Msk;
    }
#endif
}


#endif  //#if defined(TARGET_NUMAKER_PFM_NUC472) || defined(TARGET_NUMAKER_PFM_M487)
