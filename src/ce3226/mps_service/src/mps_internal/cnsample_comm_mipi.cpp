/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc.
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *************************************************************************/

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif 

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <math.h>
#include <unistd.h>
#include <signal.h>
#include <sys/prctl.h>

#include "cn_common.h"
#include "cnsample_comm.h"

#include "cn_mipi.h"

#define MIPI_DEV_NODE       "/dev/cn_mipi"

static cnmipiComboDevAttr_t MIPI_2lane_CHN0_SENSOR_OV04A10_10BIT_4M_NOWDR_ATTR =
{  
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            2, 720000,
            VI_DATA_TYPE_RAW10,
            CN_MIPI_WDR_MODE_NONE,
            {0, 0, 0, 0},
            {0, 0,0, 0},
            CN_MIPI_STREAM_MODE_1F
        }
    }
};

static cnmipiComboDevAttr_t MIPI_2lane_CHN0_SENSOR_OV08A20_12BIT_4M_NOWDR_ATTR =
{  
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            2, 1440000,
            VI_DATA_TYPE_RAW12,
            CN_MIPI_WDR_MODE_NONE,
            {0, 0, 0, 0},
            {0, 0,0, 0},
            CN_MIPI_STREAM_MODE_1F
        }
    }
};

static cnmipiComboDevAttr_t MIPI_4lane_CHN0_SENSOR_OV08A20_10BIT_8M_NOWDR_ATTR =
{  
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            4, 1440000,
            VI_DATA_TYPE_RAW10,
            CN_MIPI_WDR_MODE_NONE,
            {0, 0, 0, 0},
            {0, 0,0, 0},
            CN_MIPI_STREAM_MODE_1F
        }
    }
}; 

static cnmipiComboDevAttr_t MIPI_4lane_CHN0_SENSOR_OV08A20_12BIT_8M_NOWDR_ATTR =
{  
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            4, 1440000,
            VI_DATA_TYPE_RAW12,
            CN_MIPI_WDR_MODE_NONE,
            {0, 0, 0, 0},
            {0, 0,0, 0},
            CN_MIPI_STREAM_MODE_1F
        }
    }
};        

static cnmipiComboDevAttr_t MIPI_4lane_CHN0_SENSOR_OV08A20_10BIT_8M_WDR2TO1_ATTR =
{  
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            4, 1440000,
            VI_DATA_TYPE_RAW10,
            CN_MIPI_WDR_MODE_VC,
            {0, 0, 0, 0},
            {0, 0,0, 0},
            CN_MIPI_STREAM_MODE_2F
        }
    }
};

static cnmipiComboDevAttr_t MIPI_4lane_CHN0_SENSOR_IMX334_12BIT_8M_NOWDR_ATTR =
{  
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            4, 1782000,
            VI_DATA_TYPE_RAW12,
            CN_MIPI_WDR_MODE_NONE,
            {0, 0, 0, 0},
            {0, 0,0, 0},
            CN_MIPI_STREAM_MODE_1F
        }
    }
};        

static cnmipiComboDevAttr_t MIPI_2lane_CHN0_SENSOR_IMX327_12BIT_2M_NOWDR_ATTR =
{
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            2, 222750 * 2,
            VI_DATA_TYPE_RAW12,
            CN_MIPI_WDR_MODE_NONE,
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            CN_MIPI_STREAM_MODE_1F
        }
    }
};   


static cnmipiComboDevAttr_t MIPI_4lane_CHN0_SENSOR_IMX327_12BIT_2M_NOWDR_ATTR =
{
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            4, 222750,
            VI_DATA_TYPE_RAW12,
            CN_MIPI_WDR_MODE_NONE,
            {0, 0, 0, 0},
            {0, 0, 0, 0},
            CN_MIPI_STREAM_MODE_1F
        }
    }
};        

static cnmipiComboDevAttr_t MIPI_4lane_CHN0_SENSOR_IMX327_12BIT_2M_WDR2TO1_ATTR =
{
    .devno = 0, 
    .input_mode = VI_MODE_MIPI,
    {
        .mipi_attr =
        {
            4, 222500,
            VI_DATA_TYPE_RAW12,
            CN_MIPI_WDR_MODE_FID,
            {0, 0, 0, 0},
            {0x1, 0x2, 0x4, 0},
            CN_MIPI_STREAM_MODE_2F
        }
    }
};  

static cnmipiComboDevAttr_t LVDS_16lane_CHN0_SENSOR_IMX305_12BIT_8M_ATTR =
{
    .devno = 0, 
    .input_mode = VI_MODE_LVDS,
    {
        .lvds_attr =
        {
            16, 297000,
            VI_DATA_TYPE_RAW12,
            CN_MIPI_STREAM_MODE_1F, LVDS_SYNC_MODE_SAV,
            {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
            {{0xab0, 0xb60, 0x800, 0x9d0}, {0}}
        }
    }
};  

static cnS32_t cnsampleCommViGetMipiComboModeBySns(cnEnSampleSnsType_t enSnsType, cnmipiMipiComboMode_t* pstComboMode)
{
    switch (enSnsType)
    {
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
        case OV_08A20_MIPI_4M_30FPS_12BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
            *pstComboMode = MIPI_COMBO_MODE_7;
            break;

        default:
            *pstComboMode = MIPI_COMBO_MODE_0;
            break;
    }
    
    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViGetComboAttrBySns(cnEnSampleSnsType_t enSnsType, cnmipiComboDev_t MipiDev, cnmipiComboDevAttr_t* pstComboAttr)
{
    switch (enSnsType)
    {
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
            memcpy(pstComboAttr, &MIPI_2lane_CHN0_SENSOR_OV04A10_10BIT_4M_NOWDR_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;

        case OV_08A20_MIPI_4M_30FPS_12BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
            memcpy(pstComboAttr, &MIPI_2lane_CHN0_SENSOR_OV08A20_12BIT_4M_NOWDR_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;

        case OV_08A20_MIPI_8M_60FPS_10BIT:
            memcpy(pstComboAttr, &MIPI_4lane_CHN0_SENSOR_OV08A20_10BIT_8M_NOWDR_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;

        case OV_08A20_MIPI_8M_30FPS_12BIT:
            memcpy(pstComboAttr, &MIPI_4lane_CHN0_SENSOR_OV08A20_12BIT_8M_NOWDR_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;

        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
            memcpy(pstComboAttr, &MIPI_4lane_CHN0_SENSOR_OV08A20_10BIT_8M_WDR2TO1_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;
        
        case SONY_IMX334_MIPI_8M_30FPS_12BIT:;
            memcpy(pstComboAttr, &MIPI_4lane_CHN0_SENSOR_IMX334_12BIT_8M_NOWDR_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;
        
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
            memcpy(pstComboAttr, &LVDS_16lane_CHN0_SENSOR_IMX305_12BIT_8M_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;

        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
            memcpy(pstComboAttr, &MIPI_4lane_CHN0_SENSOR_IMX327_12BIT_2M_NOWDR_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;
        
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
            memcpy(pstComboAttr, &MIPI_2lane_CHN0_SENSOR_IMX327_12BIT_2M_NOWDR_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;
        
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
            memcpy(pstComboAttr, &MIPI_4lane_CHN0_SENSOR_IMX327_12BIT_2M_WDR2TO1_ATTR, sizeof(cnmipiComboDevAttr_t));
            break;
        
        default:
            return CN_FAILURE;
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViSetMipiHsMode(cnmipiMipiComboMode_t mipi_combo)
{
    cnS32_t fd;
    cnS32_t res = CN_SUCCESS;

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if(fd < 0)
    {
        CNSAMPLE_TRACE("open %s fail at %d!\r\n", MIPI_DEV_NODE, fd);
        return CN_FAILURE;
    }

    res = ioctl(fd, CN_MIPI_SET_MIPI_COMBO_MODE, &mipi_combo);
    close(fd);
    return res;
}


static cnS32_t cnsampleCommViSetLvdsHsMode(cnmipiLvdsComboMode_t lvds_combo)
{
    cnS32_t fd;
    cnS32_t res = CN_SUCCESS;

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if(fd < 0)
    {
        CNSAMPLE_TRACE("open %s fail at %d!\r\n", MIPI_DEV_NODE, fd);
        return CN_FAILURE;
    }

    res = ioctl(fd, CN_MIPI_SET_LVDS_COMBO_MODE, &lvds_combo);
    close(fd);
    return res;
}

cnS32_t cnsampleCommViGetLvdsDelayMode(cnsampleViConfig_t* pstViConfig)
{
    int fd;
    cnS32_t              i = 0;
    cnU32_t              j = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnsampleViInfo_t*     pstViInfo = CN_NULL;
    cnmipiComboDevAttr_t  stcomboDevAttr;
    cnmipiLvdsDevTraining_t stTraining;

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if(fd < 0)
    {
        CNSAMPLE_TRACE("open %s fail at %d!\r\n", MIPI_DEV_NODE, fd);
        return CN_FAILURE;
    }
    
    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        
        cnsampleCommViGetComboAttrBySns(pstViInfo->stSnsInfo.enSnsType, pstViInfo->stSnsInfo.MipiDev, &stcomboDevAttr);
        stTraining.devno = stcomboDevAttr.devno;
        stTraining.lane_num = stcomboDevAttr.lvds_attr.lane_num;
        stTraining.lane_rate = stcomboDevAttr.lvds_attr.lane_rate;
        stTraining.input_data_type = stcomboDevAttr.lvds_attr.input_data_type;
        stTraining.sync_mode = stcomboDevAttr.lvds_attr.sync_mode;
        
        memcpy(stTraining.sync_code, stcomboDevAttr.lvds_attr.sync_code[0], LVDS_SYNC_CODE_NUM * sizeof(unsigned short));
        cnsampleCommViGetFrameRateBySensor(pstViInfo->stSnsInfo.enSnsType, &stTraining.fps);
        s32Ret = ioctl(fd, CN_MIPI_LVDS_PHY_TRAINING, &stTraining);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("CN_MIPI_LVDS_PHY_TRAINING %d failed\n", i);
            goto EXIT;
        }
        else
        {
            CNSAMPLE_TRACE("CN_MIPI_LVDS_PHY_TRAINING sucess\n");
            printf("lvds sns type %d delay code:", pstViInfo->stSnsInfo.enSnsType);
            for (j = 0; j < stTraining.lane_num; j++)
            {
                printf("0x%02x,", stTraining.result.lane_delay_code[j]);
            }
            printf("\n");
        }
    }
    
EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViSetLvdsDelayMode(cnsampleViConfig_t* pstViConfig)
{
    int fd;
    cnS32_t              i = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;
    cnmipiLvdsDevDelayCode_t stdelaycode;
    int  lane_delay_code[LVDS_LANE_NUM] = {33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33, 33};

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if(fd < 0)
    {
        CNSAMPLE_TRACE("open %s fail at %d!\r\n", MIPI_DEV_NODE, fd);
        return CN_FAILURE;
    }
    
    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        if (SONY_IMX305_LVDS_8M_40FPS_12BIT == pstViInfo->stSnsInfo.enSnsType)
        {
            stdelaycode.devno = pstViInfo->stSnsInfo.MipiDev;
            memcpy(stdelaycode.dev_code.lane_delay_code, lane_delay_code, sizeof(int) * LVDS_LANE_NUM);
            s32Ret = ioctl(fd, CN_MIPI_SET_LVDS_DELAY_CODE, &stdelaycode);
            if (CN_SUCCESS != s32Ret)
            {
                CNSAMPLE_TRACE("CN_MIPI_SET_LVDS_DELAY_CODE %d failed\n", i);
                goto EXIT;
            }
        }
    }
    
EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViEnableMipiClock(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiComboDev_t     devno = 0;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL Ptr\n");
        return CN_FAILURE;
    }

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        devno = pstViInfo->stSnsInfo.MipiDev;
        s32Ret = ioctl(fd, CN_MIPI_ENABLE_MIPI_CLOCK, &devno);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("MIPI_ENABLE_CLOCK %d failed\n", devno);
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViDisableMipiClock(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiComboDev_t     devno = 0;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL Ptr\n");
        return CN_FAILURE;
    }

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        devno = pstViInfo->stSnsInfo.MipiDev;
        s32Ret = ioctl(fd, CN_MIPI_DISABLE_MIPI_CLOCK, &devno);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("MIPI_DISABLE_CLOCK %d failed\n", devno);
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViResetMipi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiComboDev_t           devno = 0;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("null ptr\n");
        return CN_FAILURE;
    }

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        devno = pstViInfo->stSnsInfo.MipiDev;
        s32Ret = ioctl(fd, CN_MIPI_RESET_MIPI, &devno);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("RESET_MIPI %d failed\n", devno);
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViUnresetMipi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiComboDev_t     devno = 0;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL Ptr\n");
        return CN_FAILURE;
    }

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        devno = pstViInfo->stSnsInfo.MipiDev;
        s32Ret = ioctl(fd, CN_MIPI_UNRESET_MIPI, &devno);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("UNRESET_MIPI %d failed\n", devno);
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;

}

static cnS32_t cnsampleCommViSetSensorClock(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiComboDev_t     devno = 0;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;
    cnmipiSnsDevWclkFreq_t stWclkFreq ;

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }
    
    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        stWclkFreq.sns_clk_no = pstViInfo->stSnsInfo.s32SnsClkId;
        stWclkFreq.sns_wclk = pstViInfo->stSnsInfo.enSnsClkFreq;
        s32Ret = ioctl(fd, CN_MIPI_SET_SENSOR_WCLK_FREQ, &stWclkFreq);
        if (CN_SUCCESS != s32Ret)
        {
           CNSAMPLE_TRACE("UNRESET_MIPI %d failed\n", devno);
           goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;

}

static cnS32_t cnsampleCommViEnableSensorClock(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i;
    cnS32_t              s32ViNum;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiSnsRstSource_t SnsDev = 0;
    cnsampleViInfo_t*    pstViInfo = CN_NULL;

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        SnsDev = pstViInfo->stSnsInfo.s32SnsClkId;
        s32Ret = ioctl(fd, CN_MIPI_ENABLE_SENSOR_CLOCK, &SnsDev);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("CN_MIPI_ENABLE_SENSOR_CLOCK failed\n");
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViDisableSensorClock(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i;
    cnS32_t              s32ViNum;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiSnsRstSource_t SnsDev = 0;
    cnsampleViInfo_t*    pstViInfo = CN_NULL;

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        SnsDev = pstViInfo->stSnsInfo.s32SnsClkId;
        s32Ret = ioctl(fd, CN_MIPI_DISABLE_SENSOR_CLOCK, &SnsDev);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("CN_MIPI_ENABLE_SENSOR_CLOCK failed\n");
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;;
}

static cnS32_t cnsampleCommViResetSensor(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiSnsRstSource_t       SnsDev = 0;

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    for (SnsDev = 0; SnsDev < SNS_MAX_RST_SOURCE_NUM; SnsDev++)
    {
        s32Ret = ioctl(fd, CN_MIPI_RESET_SENSOR, &SnsDev);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("CN_MIPI_RESET_SENSOR failed\n");
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViUnresetSensor(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnmipiSnsRstSource_t       SnsDev = 0;

    fd = open(MIPI_DEV_NODE, O_RDWR);

    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    for (SnsDev = 0; SnsDev < SNS_MAX_RST_SOURCE_NUM; SnsDev++)
    {
        s32Ret = ioctl(fd, CN_MIPI_UNRESET_SENSOR, &SnsDev);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("CN_MIPI_UNRESET_SENSOR failed\n");
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;
}

static cnS32_t cnsampleCommViSetMipiAttr(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i = 0;
    cnS32_t              s32ViNum = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnS32_t              fd;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;
    cnmipiComboDevAttr_t    stcomboDevAttr;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL Ptr\n");
        return CN_FAILURE;
    }

    fd = open(MIPI_DEV_NODE, O_RDWR);
    if (fd < 0)
    {
        CNSAMPLE_TRACE("open mipi dev failed\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        cnsampleCommViGetComboAttrBySns(pstViInfo->stSnsInfo.enSnsType, pstViInfo->stSnsInfo.MipiDev, &stcomboDevAttr);
        stcomboDevAttr.devno = pstViInfo->stSnsInfo.MipiDev;
        CNSAMPLE_TRACE("============= MipiDev %d, SetMipiAttr enWDRMode: %d, linenum: %d\n", pstViInfo->stSnsInfo.MipiDev,\
            pstViInfo->stDevInfo.enWDRMode, stcomboDevAttr.mipi_attr.lane_num);

        s32Ret = ioctl(fd, CN_MIPI_SET_DEV_ATTR, &stcomboDevAttr);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("MIPI_SET_DEV_ATTR failed\n");
            goto EXIT;
        }
    }

EXIT:
    close(fd);
    return s32Ret;
}

cnS32_t cnsampleCommViGetClockBySensor(cnEnSampleSnsType_t enSnsType, cnmipiSnsWclkFreq_t *pSnsClock)
{
    if (!pSnsClock)
    {
        return CN_FAILURE;
    }
    
    switch (enSnsType)
    {
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        case OV_08A20_MIPI_4M_30FPS_12BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
        case OV_08A20_MIPI_8M_30FPS_12BIT:
        case OV_08A20_MIPI_8M_60FPS_10BIT:
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            *pSnsClock = SNS_WCLK_24000K;
             break;
        
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
        case SONY_IMX334_MIPI_8M_30FPS_12BIT:
            *pSnsClock = SNS_WCLK_74250K;
             break;
        
        default:
            *pSnsClock = SNS_WCLK_37125K;
             break;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViStartMipi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t s32Ret = CN_SUCCESS;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL Ptr\n");
        return CN_FAILURE;
    }

    cnmipiMipiComboMode_t enComboMode;
    cnsampleCommViGetMipiComboModeBySns(pstViConfig->astViInfo[0].stSnsInfo.enSnsType, &enComboMode);
    s32Ret = cnsampleCommViSetMipiHsMode(enComboMode);
    if(s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetMipiHsMode fail\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetLvdsHsMode(LVDS_COMBO_MODE_0);
    if(s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetLvdsHsMode fail\n");
        return CN_FAILURE;
    }
    
    s32Ret = cnsampleCommViEnableMipiClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViEnableMipiClock failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViResetMipi(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViResetMipi failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetSensorClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetSensorClock failed!\n");
        return CN_FAILURE;
    }           

    s32Ret = cnsampleCommViEnableSensorClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViEnableSensorClock failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViResetSensor(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViResetSensor failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetMipiAttr(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetMipiAttr failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViUnresetMipi(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViUnresetMipi failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViUnresetSensor(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViUnresetSensor failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetLvdsDelayMode(pstViConfig); //only lvds 16 lane mode need
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetLvdsDelayMode failed!\n");
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViStopMipi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t s32Ret = CN_SUCCESS;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL Ptr\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViResetSensor(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViResetSensor failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViDisableSensorClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViDisableSensorClock failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViResetMipi(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViResetMipi failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViDisableMipiClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViDisableMipiClock failed!\n");
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViStartMipiTraning(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t s32Ret = CN_SUCCESS;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL Ptr\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetLvdsHsMode(LVDS_COMBO_MODE_0);  //according to your HW 
    if(s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetLvdsHsMode fail\n");
        return CN_FAILURE;
    }
    
    s32Ret = cnsampleCommViEnableMipiClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViEnableMipiClock failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViResetMipi(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViResetMipi failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetSensorClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetSensorClock failed!\n");
        return CN_FAILURE;
    }           

    s32Ret = cnsampleCommViEnableSensorClock(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViEnableSensorClock failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViResetSensor(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViResetSensor failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViUnresetMipi(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViUnresetMipi failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViUnresetSensor(pstViConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommViUnresetSensor failed!\n");
        return CN_FAILURE;
    }
    return CN_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif 
