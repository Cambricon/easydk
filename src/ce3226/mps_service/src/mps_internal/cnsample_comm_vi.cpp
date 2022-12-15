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
#endif /* End of #ifdef __cplusplus */

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
#include "cn_isp.h"
#include "cn_isp_sensor.h"

cnviDevAttr_t DEV_ATTR_OV04A10_2560x1440_BASE =
{
    VI_MODE_MIPI,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW10,
    {
        2560,1440
    },
    WDR_MODE_NONE,
    0,{0, 0, 0, 0},
};

cnviDevAttr_t DEV_ATTR_OV08A20_2668x1520_BASE =
{
    VI_MODE_MIPI,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW12,
    {
        2688,1520
    },
    WDR_MODE_NONE,
    0,{0, 0, 0, 0},
};

cnviDevAttr_t DEV_ATTR_OV08A20_4M_BASE =
{
    VI_MODE_MIPI,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW12,
    {
        2688,1592
    },
    WDR_MODE_NONE,
    0,{0, 0, 0, 0},
};

cnviDevAttr_t DEV_ATTR_OV08A20_8M_BASE =
{
    VI_MODE_MIPI,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW12,
    {
        3840,2160
    },
    WDR_MODE_NONE,
    0,{0, 0, 0, 0},
};


cnviDevAttr_t DEV_ATTR_IMX334_8M_BASE =
{
    VI_MODE_MIPI,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW12,
    {
        3840,2160
    },
    WDR_MODE_NONE,
    0,{0, 0, 0, 0},
};

cnviDevAttr_t DEV_ATTR_IMX305_8M_BASE =
{
    VI_MODE_LVDS,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW12,
    {
        3840,2173
    },
    WDR_MODE_NONE,
    0xfc00,{0, 0, 0, 0},
};

cnviDevAttr_t DEV_ATTR_IMX307_2M_BASE =
{
    VI_MODE_LVDS,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW12,
    {
        1280,733
    },
    WDR_MODE_NONE,
    0,{0, 0, 0, 0},
};

cnviDevAttr_t DEV_ATTR_IMX327_2M_BASE =
{
    VI_MODE_MIPI,VI_DATA_SEQ_YVYU,
    {VI_VSYNC_FIELD, VI_VSYNC_NEG_HIGH, VI_HSYNC_VALID_SIGNAL, VI_HSYNC_NEG_HIGH, VI_VSYNC_NORM_PULSE, VI_VSYNC_VALID_NEG_HIGH,
    {0, 0,0,0,0,0,0,0,0}},
    VI_DATA_TYPE_RAW12,
    {
        1920,1080
    },
    WDR_MODE_NONE,
    0,{0, 0, 0, 0},
};
        
cnviPipeAttr_t PIPE_ATTR_2688x1592_RAW12_420 =
{
    CN_FALSE,CN_FALSE,
    2688, 1592,
    PIXEL_FORMAT_RAW12_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_12,
    {WDR_MODE_NONE},
    CN_TRUE,
    { -1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};
    
cnviPipeAttr_t PIPE_ATTR_2560x1440_RAW10_420 =
{
    CN_FALSE,CN_FALSE,
    2560, 1440,
    PIXEL_FORMAT_RAW10_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_10,
    {WDR_MODE_NONE},
    CN_TRUE,
    { -1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};    
    
cnviPipeAttr_t PIPE_ATTR_2688x1520_RAW10_420 =
{
    CN_FALSE,CN_FALSE,
    2688, 1520,
    PIXEL_FORMAT_RAW10_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_10,
    {WDR_MODE_NONE},
    CN_TRUE,
    { -1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};  
    
cnviPipeAttr_t PIPE_ATTR_2688x1520_RAW12_420 =
{
    CN_FALSE,CN_FALSE,
    2688, 1520,
    PIXEL_FORMAT_RAW12_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_12,
    {WDR_MODE_NONE},
    CN_TRUE,
    { -1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};    
    
cnviPipeAttr_t PIPE_ATTR_3840x2160_RAW10_420 =
{
    CN_FALSE,CN_FALSE,
    3840, 2160,
    PIXEL_FORMAT_RAW10_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_10,
    {WDR_MODE_NONE},
    CN_TRUE,
    { -1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};

cnviPipeAttr_t PIPE_ATTR_3840x2160_RAW12_420 =
{
    CN_FALSE,CN_FALSE,
    3840, 2160,
    PIXEL_FORMAT_RAW12_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_12,
    {WDR_MODE_NONE},
    CN_TRUE,
    { -1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};

cnviPipeAttr_t PIPE_ATTR_1920x1080_RAW12_420 =
{
    CN_FALSE,CN_FALSE,
    1920, 1080,
    PIXEL_FORMAT_RAW12_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_12,
    {WDR_MODE_NONE},
    CN_TRUE,
    { -1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};


cnviPipeAttr_t PIPE_ATTR_1280x720_RAW12_420 = 
{
    CN_FALSE,CN_FALSE,
    1280,720,
    PIXEL_FORMAT_RAW12_1F,
    COMPRESS_MODE_NONE,
    DATA_BITWIDTH_12, 
    {WDR_MODE_NONE},
    CN_FALSE,       
    {-1, -1},
    CN_FALSE,COMPRESS_MODE_NONE,
};

    
cnviChnAttr_t CHN_ATTR_2688x1592_420_SDR8_LINEAR =
{
    {2688, 1592},
    PIXEL_FORMAT_YUV420_8BIT_SEMI_UV,
    DYNAMIC_RANGE_SDR8,
    VIDEO_FORMAT_LINEAR,
    COMPRESS_MODE_NONE,
    0,
    { -1, -1},
    CN_FALSE, CN_FALSE
};

cnviChnAttr_t CHN_ATTR_2560x1440_420_SDR8_LINEAR =
{
    {2560, 1440},
    PIXEL_FORMAT_YUV420_8BIT_SEMI_UV,
    DYNAMIC_RANGE_SDR8,
    VIDEO_FORMAT_LINEAR,
    COMPRESS_MODE_NONE,
    0,
    { -1, -1},
    CN_FALSE, CN_FALSE
};
    
cnviChnAttr_t CHN_ATTR_2688x1520_420_SDR8_LINEAR =
{
    {2688, 1520},
    PIXEL_FORMAT_YUV420_8BIT_SEMI_UV,
    DYNAMIC_RANGE_SDR8,
    VIDEO_FORMAT_LINEAR,
    COMPRESS_MODE_NONE,
    0,
    { -1, -1},
    CN_FALSE, CN_FALSE
};

cnviChnAttr_t CHN_ATTR_3840x2160_420_SDR8_LINEAR =
{
    {3840, 2160},
    PIXEL_FORMAT_YUV420_8BIT_SEMI_UV,
    DYNAMIC_RANGE_SDR8,
    VIDEO_FORMAT_LINEAR,
    COMPRESS_MODE_NONE,
    0,
    { -1, -1},
    CN_FALSE, CN_FALSE
};

cnviChnAttr_t CHN_ATTR_1920x1080_420_SDR8_LINEAR =
{
    {1920, 1080},
    PIXEL_FORMAT_YUV420_8BIT_SEMI_UV,
    DYNAMIC_RANGE_SDR8,
    VIDEO_FORMAT_LINEAR,
    COMPRESS_MODE_NONE,
    0,
    { -1, -1},
    CN_FALSE, CN_FALSE
};

cnviChnAttr_t CHN_ATTR_1280x720_420_SDR8_LINEAR =
{
    {1280, 720},
    PIXEL_FORMAT_YUV420_8BIT_SEMI_UV,
    DYNAMIC_RANGE_SDR8,
    VIDEO_FORMAT_LINEAR,
    COMPRESS_MODE_NONE,
    0,
    { -1, -1},
    CN_FALSE, CN_FALSE
};


cnBool_t cnsampleCommViIsSlaveModeBySensor(cnEnSampleSnsType_t enSnsType)
{
    switch (enSnsType)
    {
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
            return CN_TRUE;
        
        default:
            return CN_FALSE;
    }

    return CN_FALSE;
}

cnS32_t cnsampleCommViGetPipeAttrBySns(cnEnSampleSnsType_t enSnsType, cnviPipeAttr_t* pstPipeAttr)
{
    switch (enSnsType)
    {
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_2560x1440_RAW10_420, sizeof(cnviPipeAttr_t));
            break;
            
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_2688x1520_RAW10_420, sizeof(cnviPipeAttr_t));
            break;

        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_2688x1520_RAW12_420, sizeof(cnviPipeAttr_t));
            break;
        
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            memcpy(pstPipeAttr, &PIPE_ATTR_2688x1520_RAW12_420, sizeof(cnviPipeAttr_t));
            pstPipeAttr->enBitWidth = DATA_BITWIDTH_10;
            pstPipeAttr->enPixFmt = PIXEL_FORMAT_RAW10_2F;
            break;
        
        case OV_08A20_MIPI_4M_30FPS_12BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_2688x1592_RAW12_420, sizeof(cnviPipeAttr_t));
            break;
        
        case OV_08A20_MIPI_8M_60FPS_10BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_3840x2160_RAW10_420, sizeof(cnviPipeAttr_t));
            break;
        
        case SONY_IMX334_MIPI_8M_30FPS_12BIT:
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
        case OV_08A20_MIPI_8M_30FPS_12BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_3840x2160_RAW12_420, sizeof(cnviPipeAttr_t));
            break;
        
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_3840x2160_RAW12_420, sizeof(cnviPipeAttr_t));
            pstPipeAttr->u32MaxW = 3584;
            pstPipeAttr->u32MaxH = 2160;
            break;

        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
            memcpy(pstPipeAttr, &PIPE_ATTR_3840x2160_RAW12_420, sizeof(cnviPipeAttr_t));
            pstPipeAttr->enBitWidth = DATA_BITWIDTH_10;
            pstPipeAttr->enPixFmt = PIXEL_FORMAT_RAW10_2F;
            break;
        
        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
            memcpy(pstPipeAttr, &PIPE_ATTR_1920x1080_RAW12_420, sizeof(cnviPipeAttr_t));
            break;
        default:
            break;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViGetChnAttrBySns(cnEnSampleSnsType_t enSnsType, cnviChnAttr_t* pstChnAttr)
{
    switch (enSnsType)
    {
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
            memcpy(pstChnAttr,  &CHN_ATTR_2560x1440_420_SDR8_LINEAR, sizeof(cnviChnAttr_t));
            break;
        
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            memcpy(pstChnAttr,  &CHN_ATTR_2688x1520_420_SDR8_LINEAR, sizeof(cnviChnAttr_t));
            break;
        
        case OV_08A20_MIPI_4M_30FPS_12BIT:
            memcpy(pstChnAttr,  &CHN_ATTR_2688x1592_420_SDR8_LINEAR, sizeof(cnviChnAttr_t));
            break;
        
        case SONY_IMX334_MIPI_8M_30FPS_12BIT:
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
        case OV_08A20_MIPI_8M_30FPS_12BIT:
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
        case OV_08A20_MIPI_8M_60FPS_10BIT:
            memcpy(pstChnAttr,  &CHN_ATTR_3840x2160_420_SDR8_LINEAR, sizeof(cnviChnAttr_t));
            break;

        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
            memcpy(pstChnAttr,  &CHN_ATTR_3840x2160_420_SDR8_LINEAR, sizeof(cnviChnAttr_t));
            pstChnAttr->stSize.u32Width = 3584;
            pstChnAttr->stSize.u32Height = 2160;
            break;
        
        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
        default:
            memcpy(pstChnAttr,  &CHN_ATTR_1920x1080_420_SDR8_LINEAR, sizeof(cnviChnAttr_t));
            break;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViGetDevAttrBySns(cnEnSampleSnsType_t enSnsType, cnviDevAttr_t* pstViDevAttr)
{
    switch (enSnsType)
    {
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
           memcpy(pstViDevAttr, &DEV_ATTR_OV08A20_2668x1520_BASE, sizeof(cnviDevAttr_t));
           break;
        
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_OV04A10_2560x1440_BASE, sizeof(cnviDevAttr_t));
            break;

        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_OV08A20_2668x1520_BASE, sizeof(cnviDevAttr_t));
            pstViDevAttr->enInputDataType = VI_DATA_TYPE_RAW10;
            break;
        
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            memcpy(pstViDevAttr, &DEV_ATTR_OV08A20_2668x1520_BASE, sizeof(cnviDevAttr_t));
            pstViDevAttr->enInputDataType = VI_DATA_TYPE_RAW10;
            break;
        
        case OV_08A20_MIPI_4M_30FPS_12BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_OV08A20_4M_BASE, sizeof(cnviDevAttr_t));
            break;
        
        case OV_08A20_MIPI_8M_60FPS_10BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_OV08A20_8M_BASE, sizeof(cnviDevAttr_t));
            pstViDevAttr->enInputDataType = VI_DATA_TYPE_RAW10;
            break;
        
        case OV_08A20_MIPI_8M_30FPS_12BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_OV08A20_8M_BASE, sizeof(cnviDevAttr_t));
            break;
    
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
            memcpy(pstViDevAttr, &DEV_ATTR_OV08A20_8M_BASE, sizeof(cnviDevAttr_t));
            pstViDevAttr->enInputDataType = VI_DATA_TYPE_RAW10;
            break;
    
        case SONY_IMX334_MIPI_8M_30FPS_12BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_IMX334_8M_BASE, sizeof(cnviDevAttr_t));
            break;
        
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_IMX305_8M_BASE, sizeof(cnviDevAttr_t));
            break;
        
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
            memcpy(pstViDevAttr, &DEV_ATTR_IMX305_8M_BASE, sizeof(cnviDevAttr_t));
            pstViDevAttr->stSize.u32Width = 3584;
            pstViDevAttr->stSize.u32Height = 2172;
            break;
        
        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
            memcpy(pstViDevAttr, &DEV_ATTR_IMX327_2M_BASE, sizeof(cnviDevAttr_t));
            break;
        
        default:
            return CN_FAILURE;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViGetFrameRateBySensor(cnEnSampleSnsType_t enSnsType, cnU32_t* pu32FrameRate)
{
    if (!pu32FrameRate)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    switch (enSnsType)
    {
        case OV_08A20_MIPI_8M_60FPS_10BIT:
            *pu32FrameRate = 60;
            break;

        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
            *pu32FrameRate = 25;
            break;

        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
        case OV_08A20_MIPI_4M_30FPS_12BIT:
        case OV_08A20_MIPI_8M_30FPS_12BIT:
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
        case SONY_IMX334_MIPI_8M_30FPS_12BIT:
            *pu32FrameRate = 30;
            break;
        
        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
            *pu32FrameRate = 30;
            break;
        
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
            *pu32FrameRate = 40;
            break;
        
        default:
            *pu32FrameRate = 30;
            break;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViGetSizeBySensor(cnEnSampleSnsType_t enSnsType, cnEnPicSize_t* pSize)
{
    if (!pSize)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    switch (enSnsType)
    {
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
            *pSize = PIC_2560x1440;
            break;
        
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            *pSize = PIC_2688x1520;
            break;
        
        case OV_08A20_MIPI_4M_30FPS_12BIT:
            *pSize = PIC_2688x1592;
            break;
        
        case SONY_IMX334_MIPI_8M_30FPS_12BIT:
        case OV_08A20_MIPI_8M_30FPS_12BIT:
        case OV_08A20_MIPI_8M_60FPS_10BIT:
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
            *pSize = PIC_3840x2160;
            break;
        
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
            *pSize = PIC_3840x2173;
            break;
        
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
            *pSize = PIC_3584x2172;
            break;
        
        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
            *pSize = PIC_1080P;
             break;
        
        default:
            *pSize = PIC_720P;
            break;

    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViGetWdrModeBySensor(cnEnSampleSnsType_t enSnsType, cnEnWdrMode_t* pMode)
{
    if (!pMode)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }
    
    switch (enSnsType)
    {
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
            *pMode = WDR_MODE_2To1_LINE;
             break;
        
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            *pMode = WDR_MODE_2To1_LINE_OVERLAP;
             break;
        
        default:
            *pMode = WDR_MODE_NONE;
             break;
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViDevCrop(viDev_t ViDev, cnEnSampleSnsType_t enSnsType)
{  
    cnS32_t              s32Ret;
    cnviCropInfo_t      stCropInfo;
    
    s32Ret = cnviGetDevCrop(ViDev, &stCropInfo);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviGetDevCrop failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    stCropInfo.bEnable = CN_TRUE;
    stCropInfo.enCropCoordinate = VI_CROP_ABS_COOR;
    
    if (SONY_IMX305_LVDS_8M_40FPS_12BIT == enSnsType)
    {
        stCropInfo.stCropRect = {0, 0, 3840, 2160};
    }
    else if (SONY_IMX305_LVDS_3584x2172_40FPS_12BIT == enSnsType)
    {
        stCropInfo.stCropRect = {0, 0, 3584, 2160};
    }
    else
    {
        CNSAMPLE_TRACE("sns type %d not need crop\n", enSnsType);
        return CN_FAILURE;
    }
    
    s32Ret = cnviSetDevCrop(ViDev, &stCropInfo);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviSetDevCrop failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }
    
    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViStartDev(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t              s32Ret;
    viDev_t              ViDev;
    cnEnSampleSnsType_t    enSnsType;
    cnviDevAttr_t       stViDevAttr;;

    ViDev       = pstViInfo->stDevInfo.ViDev;
    enSnsType    = pstViInfo->stSnsInfo.enSnsType;

    cnsampleCommViGetDevAttrBySns(enSnsType, &stViDevAttr);
    stViDevAttr.stWDRAttr.enWDRMode = pstViInfo->stDevInfo.enWDRMode;
        CNSAMPLE_TRACE("enWDRMode=%d\n", stViDevAttr.stWDRAttr.enWDRMode );

    s32Ret = cnviSetDevAttr(ViDev, &stViDevAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviSetDevAttr failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    if (SONY_IMX305_LVDS_8M_40FPS_12BIT == enSnsType || SONY_IMX305_LVDS_3584x2172_40FPS_12BIT == enSnsType)
    {
        s32Ret = cnsampleCommViDevCrop(ViDev, enSnsType);
        if (s32Ret != CN_SUCCESS)
        {
            return CN_FAILURE;
        }
    }
    
    s32Ret = cnviEnableDev(ViDev);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviEnableDev failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViStopDev(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t s32Ret;
    viDev_t ViDev;

    ViDev   = pstViInfo->stDevInfo.ViDev;
    s32Ret  = cnviDisableDev(ViDev);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviDisableDev failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViBindPipeDev(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t              s32Ret;
    cnviDevBindPipe_t  stDevBindPipe = {0};

    stDevBindPipe.u32Num = 1;
    stDevBindPipe.PipeId[0] = pstViInfo->stPipeInfo.pipe;
    CNSAMPLE_TRACE("dev %d-pie %d\n", pstViInfo->stDevInfo.ViDev, pstViInfo->stPipeInfo.pipe);
    s32Ret = cnviSetDevBindPipe(pstViInfo->stDevInfo.ViDev, &stDevBindPipe);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviSetDevBindPipe failed with %#x! ViDev=%d, u32Num=%d, pipeid=%d\n", s32Ret, pstViInfo->stDevInfo.ViDev, stDevBindPipe.u32Num,stDevBindPipe.PipeId[0]);
        return CN_FAILURE;
    }

    return s32Ret;
}

cnS32_t cnsampleCommViSetParam(cnsampleViConfig_t* pstViConfig)
{
    return CN_SUCCESS;
}
/*
cnS32_t cnsampleCommViGetSensorInfo(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t s32Ret;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommGetSensorCfg(pstViConfig);
    if (s32Ret)
    {
        CNSAMPLE_TRACE("\n\n cnsampleCommGetSensorCfg fail, please check cnsampleConfig.ini \n\n");
        pstViConfig->s32WorkingViNum = 0;
    }

    return s32Ret;
}        
*/    
static cnS32_t cnsampleCommViStartSingleViPipe(viPipe_t ViPipe, cnviPipeAttr_t* pstPipeAttr)
{
    cnS32_t s32Ret;

    s32Ret = cnviCreatePipe(ViPipe, pstPipeAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviCreatePipe failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    s32Ret = cnviStartPipe(ViPipe);
    if (s32Ret != CN_SUCCESS)
    {
        cnviDestroyPipe(ViPipe);
        CNSAMPLE_TRACE("cnviStartPipe failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return s32Ret;
}

static cnU32_t cnsampleCommViStopSingleViPipe(viPipe_t ViPipe)
{
    cnU32_t  s32Ret;

    s32Ret = cnviStopPipe(ViPipe);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviStopPipe failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    s32Ret = cnviDestroyPipe(ViPipe);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviDestroyPipe failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return s32Ret;
}

static cnS32_t cnsampleCommViStartViPipe(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t          s32Ret = CN_SUCCESS;
    viPipe_t         ViPipe;
    cnviPipeAttr_t  stPipeAttr;

    if (pstViInfo->stPipeInfo.pipe < 0  || pstViInfo->stPipeInfo.pipe >= VI_MAX_PIPE_NUM)
    {
        CNSAMPLE_TRACE("pipe %d error!\n", pstViInfo->stPipeInfo.pipe);
        return CN_FAILURE;
    }

    memset(&stPipeAttr, 0, sizeof(stPipeAttr));
    ViPipe = pstViInfo->stPipeInfo.pipe;
    cnsampleCommViGetPipeAttrBySns(pstViInfo->stSnsInfo.enSnsType, &stPipeAttr);
    stPipeAttr.stWDRAttr.enWDRMode = pstViInfo->stDevInfo.enWDRMode;
    CNSAMPLE_TRACE("ViPipe %d bNrEn %d\n", ViPipe, stPipeAttr.bNrEn);

    s32Ret = cnsampleCommViStartSingleViPipe(ViPipe, &stPipeAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViStartSingleViPipe  %d failed!\n", ViPipe);
        goto EXIT;
    }

    return s32Ret;

EXIT:
    cnsampleCommViStopSingleViPipe(ViPipe);
    return s32Ret;
}

static cnS32_t cnsampleCommViStopViPipe(cnsampleViInfo_t* pstViInfo)
{
    if (pstViInfo->stPipeInfo.pipe < 0  || pstViInfo->stPipeInfo.pipe >= VI_MAX_PIPE_NUM)
    {
        CNSAMPLE_TRACE("pipe %d error!\n", pstViInfo->stPipeInfo.pipe);
        return CN_FAILURE;
    }

    return cnsampleCommViStopSingleViPipe(pstViInfo->stPipeInfo.pipe);
}

static cnS32_t cnsampleCommViStartViChn(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t              s32Ret = CN_SUCCESS;
    viPipe_t             ViPipe;
    viChn_t              ViChn;
    cnviChnAttr_t        stChnAttr;
    cnEnViVppsMode_t     enMastPipeMode;

    if (pstViInfo->stPipeInfo.pipe < 0  || pstViInfo->stPipeInfo.pipe >= VI_MAX_PIPE_NUM)
    {
        CNSAMPLE_TRACE("pipe %d error!\n", pstViInfo->stPipeInfo.pipe);
        return CN_FAILURE;
    }
    
    ViPipe = pstViInfo->stPipeInfo.pipe;
    ViChn  = pstViInfo->stChnInfo.ViChn;

    cnsampleCommViGetChnAttrBySns(pstViInfo->stSnsInfo.enSnsType, &stChnAttr);
    stChnAttr.enDynamicRange = pstViInfo->stChnInfo.enDynamicRange;
    stChnAttr.enVideoFormat  = pstViInfo->stChnInfo.enVideoFormat;
    stChnAttr.enPixelFormat  = pstViInfo->stChnInfo.enPixFormat;
    stChnAttr.enCompressMode = pstViInfo->stChnInfo.enCompressMode;

    s32Ret = cnviSetChnAttr(ViPipe, ViChn, &stChnAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnviSetChnAttr failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    enMastPipeMode = pstViInfo->stPipeInfo.enMastPipeMode;
    if (VI_OFFLINE_VPPS_OFFLINE == enMastPipeMode
        || VI_ONLINE_VPPS_OFFLINE == enMastPipeMode)
    {
        s32Ret = cnviEnableChn(ViPipe, ViChn);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnviEnableChn failed with %#x!\n", s32Ret);
            return CN_FAILURE;
        }
    }
        
    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViStopViChn(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t              s32Ret = CN_SUCCESS;
    viPipe_t             ViPipe;
    viChn_t              ViChn;
    cnEnViVppsMode_t      enMastPipeMode;

    if (pstViInfo->stPipeInfo.pipe < 0  || pstViInfo->stPipeInfo.pipe >= VI_MAX_PIPE_NUM)
    {
        CNSAMPLE_TRACE("pipe %d error!\n", pstViInfo->stPipeInfo.pipe);
        return CN_FAILURE;
    }

    ViPipe = pstViInfo->stPipeInfo.pipe;
    ViChn  = pstViInfo->stChnInfo.ViChn;

    enMastPipeMode = pstViInfo->stPipeInfo.enMastPipeMode;
    if (VI_OFFLINE_VPPS_OFFLINE == enMastPipeMode
        || VI_ONLINE_VPPS_OFFLINE == enMastPipeMode)
    {
        s32Ret = cnviDisableChn(ViPipe, ViChn);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnviDisableChn failed with %#x!\n", s32Ret);
            return CN_FAILURE;
        }
    }

    return CN_SUCCESS;
}


static cnS32_t cnsampleCommViCreateSingleVi(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t s32Ret = CN_SUCCESS;

    s32Ret = cnsampleCommViStartDev(pstViInfo);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViStartDev failed !\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViBindPipeDev(pstViInfo);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViBindPipeDev failed !\n");
        goto EXIT1;
    }
    
    s32Ret = cnsampleCommViStartViPipe(pstViInfo);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViStartViPipe failed !\n");
        goto EXIT1;
    }
    
    s32Ret = cnsampleCommViStartViChn(pstViInfo);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViStartViChn failed !\n");
        goto EXIT2;
    }

    return CN_SUCCESS;

EXIT2:
    cnsampleCommViStopViPipe(pstViInfo);

EXIT1:
    cnsampleCommViStopDev(pstViInfo);

    return s32Ret;
}

static cnS32_t cnsampleCommViStartSlaveMode(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i;
    cnS32_t              s32ViNum;
    cnviVsDev_t          VsDev = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnU32_t              u32Fps;
    cnviVsDevAttr_t      stVsDevAttr;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    memset(&stVsDevAttr, 0, sizeof(stVsDevAttr));

    if (SONY_IMX327_SLAVE_2M_30FPS_12BIT == pstViConfig->astViInfo[0].stSnsInfo.enSnsType)
    {
        stVsDevAttr.bVsValid = CN_TRUE;
        stVsDevAttr.bHsValid = CN_TRUE;
        stVsDevAttr.bVsoutValid = CN_FALSE;
        stVsDevAttr.enVsPolarity = VI_SYNC_HIGH;
        stVsDevAttr.enHsPolarity = VI_SYNC_HIGH;
        stVsDevAttr.bAcSyncMode = CN_FALSE;
        stVsDevAttr.stTiming.u64HsDelayVs = 0;

        stVsDevAttr.stTiming.u64HsyncActive =  37125000L * 2200 * 12 / 891000000;
        stVsDevAttr.stTiming.u64HsyncCycle =  stVsDevAttr.stTiming.u64HsyncActive + 37125000L * 192 / 891000000;
        stVsDevAttr.stTiming.u64VsyncActive =  stVsDevAttr.stTiming.u64HsyncCycle * 1125L;
        stVsDevAttr.stTiming.u64VsyncCycle =  stVsDevAttr.stTiming.u64VsyncActive + 4 * stVsDevAttr.stTiming.u64HsyncCycle;
    }
    else  //ov04a10
    {
        stVsDevAttr.bVsValid = CN_TRUE;
        stVsDevAttr.bHsValid = CN_FALSE;
        stVsDevAttr.bVsoutValid = CN_FALSE;
        stVsDevAttr.enVsPolarity = VI_SYNC_HIGH;
        stVsDevAttr.bAcSyncMode = CN_FALSE;

        stVsDevAttr.stTiming.u64VsyncActive =  20; //hight time of sensor fsin
        cnsampleCommViGetFrameRateBySensor(pstViConfig->astViInfo[0].stSnsInfo.enSnsType, &u32Fps);
        stVsDevAttr.stTiming.u64VsyncCycle =  stVsDevAttr.stTiming.u64VsyncActive + 24000000 / u32Fps;  //24M CLOCK
    }
    

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++) 
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        VsDev = pstViInfo->stSnsInfo.s32SnsClkId; 

        s32Ret = cnviSetVsDevAttr(VsDev, &stVsDevAttr);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnviSetVsDevAttr failed with 0x%x!\n", s32Ret);
        }

        s32Ret = cnviSingleTriggerVsDev(VsDev, CN_TRUE);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnviSingleTriggerVsDev failed with 0x%x!\n", s32Ret);
        }
        
        CNSAMPLE_TRACE("VsDev=%d, u64HsyncActive=%llu, u64HsyncCycle=%llu,  u64VsyncActive=%llu, u64VsyncCycle=%llu\n",\
            VsDev, stVsDevAttr.stTiming.u64HsyncActive, stVsDevAttr.stTiming.u64HsyncCycle, stVsDevAttr.stTiming.u64VsyncActive, stVsDevAttr.stTiming.u64VsyncCycle);
    }

    return CN_SUCCESS;
}


static cnS32_t cnsampleCommViStopSlaveMode(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i;
    cnS32_t              s32ViNum;
    cnviVsDev_t          VsDev = 0;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++) 
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        VsDev = pstViInfo->stSnsInfo.s32SnsClkId; 
        
        s32Ret = cnviSingleTriggerVsDev(VsDev, CN_FALSE);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnviSingleTriggerVsDev failed with 0x%x!\n", s32Ret);
        }
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViDestroySingleVi(cnsampleViInfo_t* pstViInfo)
{
    cnsampleCommViStopViChn(pstViInfo);
    cnsampleCommViStopViPipe(pstViInfo);

    if (pstViInfo->stSnsInfo.enSnsType != SAMPLE_SNS_TYPE_BUTT)
    {
         cnsampleCommViStopDev(pstViInfo);
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViCreateVi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i, j;
    cnS32_t              s32ViNum;
    cnS32_t              s32Ret = CN_SUCCESS;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        s32Ret = cnsampleCommViCreateSingleVi(pstViInfo);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnsampleCommViCreateSingleVi failed !\n");
            goto EXIT;
        }
    }
    
    return CN_SUCCESS;
    
EXIT:

    for (j = 0; j < i; j++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[j];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        cnsampleCommViDestroySingleVi(pstViInfo);
    }

    return s32Ret;
}

static cnS32_t cnsampleCommViStartIsp(cnsampleViInfo_t* pstViInfo)
{
    cnS32_t              s32Ret;
    viPipe_t             ViPipe;

    if (!pstViInfo)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    ViPipe = pstViInfo->stPipeInfo.pipe;
    s32Ret = cnispInit(ViPipe);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("ISP Init failed with %#x!\n", s32Ret);
        goto errExit;
    }
    
    s32Ret = cnsampleCommIspBindSensor(ViPipe, pstViInfo->stSnsInfo.enSnsType, pstViInfo->stSnsInfo.s32BusId);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("bind sensor %d to ISP %d failed\n", pstViInfo->stSnsInfo.enSnsType, ViPipe);
        goto errExit;
    }

    s32Ret = cnsampleCommIspStart(ViPipe);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommIspStart failed\n");
        goto errExit;
    }
    
    return CN_SUCCESS;
    
errExit:
    cnsampleCommIspStop(ViPipe);
    return CN_FAILURE;
}

static cnS32_t cnsampleCommViStopIsp(cnsampleViInfo_t* pstViInfo)
{
    if (!pstViInfo)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    cnsampleCommIspStop(pstViInfo->stPipeInfo.pipe);
    return  CN_SUCCESS;
}

static cnS32_t cnsampleCommViCreateIsp(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t              i;
    cnS32_t              s32ViNum;
    cnS32_t              s32Ret;
    cnispPub_t           stPubAttr;
    cnsampleViInfo_t*   pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        if (CN_FALSE == pstViConfig->bModeSwitch)
        {
            s32Ret = cnsampleCommViStartIsp(pstViInfo);
            if (s32Ret != CN_SUCCESS)
            {
                CNSAMPLE_TRACE("SAMPLE_COMM_VI_StartIsp failed !\n");
                return CN_FAILURE;
            }
        }
        else
        {
            if (CN_TRUE == pstViConfig->bSwitch)
            {
                s32Ret = cnsampleCommViStartIsp(pstViInfo);
                if (s32Ret != CN_SUCCESS)
                {
                    CNSAMPLE_TRACE("SAMPLE_COMM_VI_StartIsp failed !\n");
                    return CN_FAILURE;
                }
            }
            else
            {
                s32Ret = cnsampleCommIspGetIspAttrBySns(pstViInfo->stSnsInfo.enSnsType, &stPubAttr);
                if (s32Ret != CN_SUCCESS)
                {
                    CNSAMPLE_TRACE("cnsampleCommIspGetIspAttrBySns failed !\n");
                    return CN_FAILURE;
                }
                
                s32Ret = cnispSetPub(pstViInfo->stPipeInfo.pipe, &stPubAttr);
                if (s32Ret != CN_SUCCESS)
                {
                    CNSAMPLE_TRACE("cnispSetPub failed with 0x%x!\n",s32Ret);
                    cnsampleCommIspStop(pstViInfo->stPipeInfo.pipe);
                    return CN_FAILURE;
                }
            }
        }
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViDestroyIsp(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t            i;
    cnS32_t            s32ViNum;
    cnS32_t            s32Ret;
    cnsampleViInfo_t* pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];

        s32Ret = cnsampleCommViStopIsp(pstViInfo);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnsampleCommVI_StopIsp failed !\n");
            return CN_FAILURE;
        }
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommViDestroyVi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t            i;
    cnS32_t            s32ViNum;
    cnsampleViInfo_t* pstViInfo = CN_NULL;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    for (i = 0; i < pstViConfig->s32WorkingViNum; i++)
    {
        s32ViNum  = pstViConfig->as32WorkingViId[i];
        pstViInfo = &pstViConfig->astViInfo[s32ViNum];
        cnsampleCommViDestroySingleVi(pstViInfo);
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommViStartVi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t s32Ret = CN_SUCCESS;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetParam(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetParam failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViCreateVi(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViCreateVi failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViStartMipi(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        cnsampleCommViDestroyVi(pstViConfig);
        CNSAMPLE_TRACE("cnsampleCommViStartMIPI failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViCreateIsp(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        cnsampleCommViStopMipi(pstViConfig);
        cnsampleCommViDestroyVi(pstViConfig);
        CNSAMPLE_TRACE("cnsampleCommViCreateIsp failed!\n");
        return CN_FAILURE;
    }
    
    if (CN_TRUE == cnsampleCommViIsSlaveModeBySensor(pstViConfig->astViInfo[0].stSnsInfo.enSnsType))
    {
        s32Ret = cnsampleCommViStartSlaveMode(pstViConfig);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnsampleCommViStartSlaveMode failed!\n");
            return CN_FAILURE;
        }
    }
    return s32Ret;
}

cnS32_t cnsampleCommViStopVi(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t s32Ret = CN_SUCCESS;
 
    if (CN_TRUE == cnsampleCommViIsSlaveModeBySensor(pstViConfig->astViInfo[0].stSnsInfo.enSnsType))
    {
        s32Ret = cnsampleCommViStopSlaveMode(pstViConfig);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnsampleCommViStopSlaveMode failed!\n");
        }
    }
    
    if (CN_FALSE == pstViConfig->bModeSwitch)
    {
        s32Ret |= cnsampleCommViDestroyIsp(pstViConfig);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnsampleCommViDestroyIsp failed !\n");
        }
    }
    else
    {
        if (CN_TRUE == pstViConfig->bSwitch)
        {
            s32Ret |= cnsampleCommViDestroyIsp(pstViConfig);
            if (s32Ret != CN_SUCCESS)
            {
                CNSAMPLE_TRACE("cnsampleCommViDestroyIsp failed !\n");
            }
        }
    }
    
    s32Ret |= cnsampleCommViStopMipi(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
       CNSAMPLE_TRACE("cnsampleCommViStopMIPI failed !\n");
    }

    s32Ret |= cnsampleCommViDestroyVi(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViDestroyVi failed !\n");
        return CN_FAILURE;
    }
    
    return s32Ret;
}


cnS32_t cnsampleCommViStartLvdsTraning(cnsampleViConfig_t* pstViConfig)
{
    cnS32_t s32Ret = CN_SUCCESS;

    if (!pstViConfig)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViSetParam(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViSetParam failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViCreateVi(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnsampleCommViCreateVi failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViStartMipiTraning(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        cnsampleCommViDestroyVi(pstViConfig);
        CNSAMPLE_TRACE("cnsampleCommViStartMIPI failed!\n");
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommViCreateIsp(pstViConfig);
    if (s32Ret != CN_SUCCESS)
    {
        cnsampleCommViStopMipi(pstViConfig);
        cnsampleCommViDestroyVi(pstViConfig);
        CNSAMPLE_TRACE("cnsampleCommViCreateIsp failed!\n");
        return CN_FAILURE;
    }
    return s32Ret;
}


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
