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

#include "cnsample_comm.h"
#include "cn_isp_sensor.h"
#include "cn_isp_ae.h"
#include "cn_isp_awb.h"

static cnChar_t *sAeContext = CN_NULL;
static cnChar_t *sAwbContext = CN_NULL;

cnispPub_t ISP_PUB_ATTR_OV08A20_4M_30FPS =
{
    1592, 2688, 
    E_MODE_LINE, 
    E_BIT_WIDTH_12,
    E_SRC_SENSOR_LINE_SITCHED_BY_SENSOR,
    E_BAYER_BGGR,
    30, E_EX_MODE_ONE,
};

cnispPub_t ISP_PUB_ATTR_OV08A20_8M_30FPS =
{
    2160, 3840, 
    E_MODE_LINE, 
    E_BIT_WIDTH_12,
    E_SRC_SENSOR_LINE_SITCHED_BY_SENSOR,
    E_BAYER_BGGR,
    30, E_EX_MODE_ONE,
};
    
cnispPub_t ISP_PUB_ATTR_OV08A20_8M_30FPS_WDR2TO1 =
{
    2160, 3840, 
    E_MODE_DOL, 
    E_BIT_WIDTH_10,
    E_SRC_STICHED_BY_ISP,
    E_BAYER_BGGR,
    30, E_EX_MODE_TWO,
};
    
cnispPub_t ISP_PUB_ATTR_OV08A20_2688x1520_30FPS_WDR2TO1 =
{
    1520, 2688, 
    E_MODE_DOL, 
    E_BIT_WIDTH_10,
    E_SRC_STICHED_BY_ISP,
    E_BAYER_BGGR,
    30, E_EX_MODE_TWO,
};

cnS32_t cnsampleCommIspGetIspAttrBySns(cnEnSampleSnsType_t enSnsType, cnispPub_t* pstPubAttr)
{
    switch (enSnsType)
    {
        case OV_08A20_MIPI_4M_30FPS_12BIT:
            memcpy(pstPubAttr, &ISP_PUB_ATTR_OV08A20_4M_30FPS, sizeof(cnispPub_t));
            break;

        case OV_08A20_MIPI_8M_30FPS_12BIT:
            memcpy(pstPubAttr, &ISP_PUB_ATTR_OV08A20_8M_30FPS, sizeof(cnispPub_t));
            break;
        
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
            memcpy(pstPubAttr, &ISP_PUB_ATTR_OV08A20_8M_30FPS_WDR2TO1, sizeof(cnispPub_t));
            break;

        
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            memcpy(pstPubAttr, &ISP_PUB_ATTR_OV08A20_2688x1520_30FPS_WDR2TO1, sizeof(cnispPub_t));
            break;

        default:
            CNSAMPLE_TRACE("enSnsType %d not support\n", enSnsType);
            return CN_FAILURE;
    }

    return CN_SUCCESS;
}

static cnS32_t cnsampleCommIspGetSnsWorkModeBySns(cnEnSampleSnsType_t enSnsType, cnSensorWorkMode_e* pMode)
{
    switch (enSnsType)
    {
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
            *pMode = CN_SENSOR_WORK_MODE_SLAVE;
            break;
        
        default:
            *pMode = CN_SENSOR_WORK_MODE_MASTER;
            break;
    }
    
    return CN_SUCCESS;
}

cnS32_t cnsampleCommIspConvertSensorcfgToIsp(cnSensorCfg_s* pstSensCfg, cnispPub_t* pstIspPub){

    cnS32_t                  s32Ret              = CN_SUCCESS;
    
    pstIspPub->u32ActiveWidth = pstSensCfg->stImageResolution.ushortWidth;
    pstIspPub->u32ActiveHeight = pstSensCfg->stImageResolution.ushortHeight;
    pstIspPub->u32FrameRate = pstSensCfg->uintFrameRate;

    switch(pstSensCfg->enFrameMode){
        case CN_SENSOR_FRAME_MODE_1F:{
            pstIspPub->u32InputMode = E_MODE_LINE;
            pstIspPub->u32WdrMode = E_EX_MODE_ONE;
            pstIspPub->u32LineDataSrc = E_SRC_SENSOR_LINE_SITCHED_BY_SENSOR;
        }break;
        case CN_SENSOR_FRAME_MODE_2F:{
            pstIspPub->u32InputMode = E_MODE_DOL;
            pstIspPub->u32WdrMode = E_EX_MODE_TWO;        
            pstIspPub->u32LineDataSrc = E_SRC_STICHED_BY_ISP;
        }break;
        default : s32Ret = CN_FAILURE;break;
    }

    switch(pstSensCfg->enFramePixelBits){
        case CN_SENSOR_FRAME_PIXEL_BITS_8:  pstIspPub->u32InputBitsWidth = E_BIT_WIDTH_8;  break;
        case CN_SENSOR_FRAME_PIXEL_BITS_10: pstIspPub->u32InputBitsWidth = E_BIT_WIDTH_10; break;
        case CN_SENSOR_FRAME_PIXEL_BITS_12: pstIspPub->u32InputBitsWidth = E_BIT_WIDTH_12; break;
        case CN_SENSOR_FRAME_PIXEL_BITS_14: pstIspPub->u32InputBitsWidth = E_BIT_WIDTH_14; break;
        case CN_SENSOR_FRAME_PIXEL_BITS_16: pstIspPub->u32InputBitsWidth = E_BIT_WIDTH_16; break;
        case CN_SENSOR_FRAME_PIXEL_BITS_20: pstIspPub->u32InputBitsWidth = E_BIT_WIDTH_20; break;        
        default : s32Ret = CN_FAILURE;break;

    }

    switch(pstSensCfg->enBayerPattern){
        case CN_SENSOR_BAYER_PATTERN_RGGB:  pstIspPub->u32BayerPattern = E_BAYER_RGGB;  break;
        case CN_SENSOR_BAYER_PATTERN_GRBG:  pstIspPub->u32BayerPattern = E_BAYER_GRBG;  break;
        case CN_SENSOR_BAYER_PATTERN_GBRG:  pstIspPub->u32BayerPattern = E_BAYER_GBRG;  break;
        case CN_SENSOR_BAYER_PATTERN_BGGR:  pstIspPub->u32BayerPattern = E_BAYER_BGGR;  break;
        default : s32Ret = CN_FAILURE;break;
    }

    return s32Ret;
}

extern cnSensorObj_s sensor_imx305obj;
extern cnSensorObj_s sensor_imx327obj;
extern cnSensorObj_s sensor_ov08a20obj;
extern cnSensorObj_s sensor_ov04a10obj;

cnSensorObj_s* cnsampleCommIspGetSensorObj(cnEnSampleSnsType_t enSnsType)
{
    switch (enSnsType)
    {
        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
            return &sensor_ov04a10obj;
                
        case OV_08A20_MIPI_8M_30FPS_12BIT:
        case OV_08A20_MIPI_8M_60FPS_10BIT:
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
        case OV_08A20_MIPI_4M_30FPS_12BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
            return &sensor_ov08a20obj;
            
        // case SONY_IMX334_MIPI_8M_30FPS_12BIT:
        //    return &sensor_imx334obj;
                
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
            return &sensor_imx305obj;

        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        case SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1:
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
            return &sensor_imx327obj;

        default:
            return CN_NULL;
    }
}


static void *ae_sample_init(uint32_t ViPipe)
{
    CNSAMPLE_TRACE("----ae_sample_init was ran \n");
    sAeContext = (cnChar_t*)malloc(1024);  //according to your need malloc meory
    return (void *)sAeContext;    
}

static cnS32_t ae_sample_proc(void *pAeCtx, ae_stats_data_t *pstStats, ae_input_data_t *pstInput, ae_output_data_t *pstOutput)
{
    if (sAeContext != ((cnChar_t*)pAeCtx))
    {
        CNSAMPLE_TRACE("pAeCtx error\n");
        return CN_FAILURE;
    }
    pstOutput->acamera_output->exposure_log2 = 3258697 ;
    pstOutput->acamera_output->exposure_ratio = 64     ;
    pstOutput->acamera_output->ae_converged = 1        ;
    pstOutput->acamera_output->ae_hist_mean = 99       ;

    return CN_SUCCESS;
}

static cnS32_t ae_sample_deinit(void *pAeCtx)
{
    CNSAMPLE_TRACE("ae_sample_deinit was ran \n");
    if (sAeContext != ((cnChar_t*)pAeCtx))
    {
        CNSAMPLE_TRACE("pAeCtx error\n");
        return CN_FAILURE;
    }

    free(sAeContext);
    return CN_SUCCESS;
}

void *awb_sample_init(uint32_t ViPipe)
{
    CNSAMPLE_TRACE("awb_sample_init was ran \n");
    sAwbContext = (cnChar_t*)malloc(1024);  //according to your need malloc meory
    return (void *)sAwbContext;

}

cnS32_t awb_sample_proc(void *pAwbCtx, awb_stats_data_t *pstStats, awb_input_data_t *pstInput, awb_output_data_t *pstOutput)
{
    if (sAwbContext != ((cnChar_t*)pAwbCtx))
    {
        CNSAMPLE_TRACE("pAwbCtx error\n");
        return CN_FAILURE;
    }

    pstOutput->acamera_output->rg_coef = 307                ;
    pstOutput->acamera_output->bg_coef = 212                ;
    pstOutput->acamera_output->temperature_detected = 6944  ;
    pstOutput->acamera_output->p_high = 50                  ;
    pstOutput->acamera_output->light_source_candidate = 3   ;
    pstOutput->acamera_output->avg_GR = 213                 ;
    pstOutput->acamera_output->avg_GB = 309                 ;
    pstOutput->acamera_output->awb_converged = 1            ;
    pstOutput->acamera_output->awb_warming[0] = 256           ;
    pstOutput->acamera_output->awb_warming[1] = 256           ;
    pstOutput->acamera_output->awb_warming[2] = 256           ;

    return CN_SUCCESS;
}

cnS32_t awb_sample_deinit(void *pAwbCtx)
{
    CNSAMPLE_TRACE("awb_sample_deinit was ran \n");
    if (sAwbContext != ((cnChar_t*)pAwbCtx))
    {
        CNSAMPLE_TRACE("pAeCtx error\n");
        return CN_FAILURE;
    }

    free(sAwbContext);
    return CN_SUCCESS;;
}

cnS32_t cnsampleCommIspAeCallback(viPipe_t ViPipe)
{
    cnS32_t ret = 0;
    cnispAeObj_t stCustomAeObj;    
    stCustomAeObj.init = ae_sample_init;  
    stCustomAeObj.proc = ae_sample_proc;  
    stCustomAeObj.deinit = ae_sample_deinit;

    ret = cnispAeRegister(ViPipe, &stCustomAeObj); 
    if (ret)
    {
        CNSAMPLE_TRACE("cnispAeRegister fail with 0x%x\n", ret);
    }
    return ret;
}

cnS32_t cnsampleCommIspAeUnCallback(viPipe_t ViPipe)
{
	cnS32_t ret = 0;
    cnispAeObj_t stCustomAeObj;
    stCustomAeObj.init = ae_sample_init;
    stCustomAeObj.proc = ae_sample_proc;
    stCustomAeObj.deinit = ae_sample_deinit;

    ret = cnispAeUnRegister(ViPipe, &stCustomAeObj);
    if (ret)
    {
        CNSAMPLE_TRACE("cnispAeUnRegister fail with 0x%x\n", ret);
    }
    return ret;
}

cnS32_t cnsampleCommIspAwbCallback(viPipe_t ViPipe)
{
    cnS32_t ret = 0;
    cnispAwbObj_t stCustomAwbObj;
    stCustomAwbObj.init = awb_sample_init;
    stCustomAwbObj.proc = awb_sample_proc;
    stCustomAwbObj.deinit = awb_sample_deinit;

    ret = cnispAwbRegister(ViPipe, &stCustomAwbObj);
    if (ret)
    {
        CNSAMPLE_TRACE("cnispAwbRegister fail with 0x%x\n", ret);
    }
    return ret;
}

cnS32_t cnsampleCommIspAwbUnCallback(viPipe_t ViPipe)
{
    cnS32_t ret = 0;
    cnispAwbObj_t stCustomAwbObj;
    stCustomAwbObj.init = awb_sample_init;
    stCustomAwbObj.proc = awb_sample_proc;
    stCustomAwbObj.deinit = awb_sample_deinit;

    ret = cnispAwbUnRegister(ViPipe, &stCustomAwbObj);
    if (ret)
    {
        CNSAMPLE_TRACE("cnispAwbUnRegister fail with 0x%x\n", ret);
    }
    return ret;
}

cnS32_t cnsampleCommIspStart(viPipe_t ViPipe)
{
    cnS32_t          s32Ret;
    cnispPub_t       stPubAttr;
    cnSensorCfg_s  stSensCfg;

    s32Ret = cnispGetSensorCfg(ViPipe, &stSensCfg);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnispGetSensorCfg failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    s32Ret = cnsampleCommIspConvertSensorcfgToIsp(&stSensCfg, &stPubAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnispMemInit failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }
    
    s32Ret = cnispMemInit(ViPipe);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnispMemInit failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    s32Ret = cnispSetPub(ViPipe, &stPubAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("SetPubAttr failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }
    
    //cnsampleCommIspAwbCallback(ViPipe); //if need call
    //cnsampleCommIspAeCallback(ViPipe);  //if need call
    s32Ret = cnispRun(ViPipe);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnispRun failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }
    return CN_SUCCESS;
}

cnVoid_t cnsampleCommIspStop(viPipe_t ViPipe)
{
    //cnsampleCommIspAwbUnCallback(ViPipe); //if need call
    //cnsampleCommIspAeUnCallback(ViPipe);  //if need call
    cnispExit(ViPipe);
}

cnU32_t cnsampleCommIspGetSensorCfgBySns(cnEnSampleSnsType_t enSnsType, cnSensorFrameFmt_e* pFmt, cnU32_t *pLaneNum, cnU32_t *pFps)
{
    switch (enSnsType)
    {
        case SONY_IMX305_LVDS_8M_40FPS_12BIT:
        {
            *pFmt = IMX305_RES_3840X2160_BITS_12_LINEMODE;
            *pLaneNum = 16;
             break;
        }
        
        case SONY_IMX305_LVDS_3584x2172_40FPS_12BIT:
        {
            *pFmt = IMX305_RES_3584X2160_BITS_12_LINEMODE;
            *pLaneNum = 16;
             break;
        }
        
        case SONY_IMX327_MIPI_2M_30FPS_12BIT:
        {
            *pFmt = IMX327_RES_1920X1080_BITS_12_LINEMODE;
            *pLaneNum = 4;
             break;
        }
        
        case SONY_IMX327_SLAVE_2M_30FPS_12BIT:
        {
            *pFmt = IMX327_RES_1920X1080_BITS_12_LINEMODE;
            *pLaneNum = 2;
             break;
        }
        
        case OV_08A20_MIPI_8M_30FPS_12BIT:
        {
            *pFmt = OS08A20_RES_3840X2160_BITS_12_LINEMODE;
            *pLaneNum = 4;
             break;
        }
        
        case OV_08A20_MIPI_8M_60FPS_10BIT:
        {
            *pFmt = OS08A20_RES_3840X2160_BITS_10_LINEMODE;
            *pLaneNum = 4;
             break;
        }
        
        case OV_08A20_MIPI_4M_30FPS_12BIT:
        {
            *pFmt = OS08A20_RES_2688X1592_BITS_12_LINEMODE;
            *pLaneNum = 2;
             break;
        }

        case OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT:
        {
            *pFmt = OS04A10_RES_2688X1520_BITS_10_2LANE_LINEMODE;
            *pLaneNum = 2;
             break;
        }
        
        case OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT:
        {
            *pFmt = OS04A10_RES_2560X1440_BITS_10_2LANE_LINEMODE;
            *pLaneNum = 2;
             break;
        }

        case OV_08A20_MIPI_2688x1520_30FPS_12BIT:
        {
            *pFmt = OS08A20_RES_2688X1520_BITS_12_LINEMODE;
            *pLaneNum = 2;
             break;
        }
        
        case OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1:
        {
            *pFmt = OS08A20_RES_2688X1520_BITS_10_WDRMODE;
            *pLaneNum = 4;
             break;
        }
        
        case OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1:
        {
            *pFmt = OS08A20_RES_3840X2160_BITS_10_WDRMODE;
            *pLaneNum = 4;
             break;
        }
        
        default:
            CNSAMPLE_TRACE("enSensorBusType %d not support\n", enSnsType);
            return CN_FAILURE;

    }

    cnsampleCommViGetFrameRateBySensor(enSnsType, pFps);
    return CN_SUCCESS;
}


cnS32_t cnsampleCommIspBindSensor(viPipe_t ViPipe, cnEnSampleSnsType_t enSnsType, cnS32_t s32BusId)
{
    cnS32_t s32Ret = CN_FAILURE;
    cnSensorObj_s* pstSnsObj = CN_NULL;
    cnSensorBusInfo_s  stBusInfo;
    cnSensorFrameFmt_e enFmt = OS08A20_RES_3840X2160_BITS_10_WDRMODE;
    cnU32_t u32LaneNum = 0;
    cnU32_t u32Fps = 0;


    pstSnsObj = cnsampleCommIspGetSensorObj(enSnsType);
    if (CN_NULL == pstSnsObj)
    {
        CNSAMPLE_TRACE("sensor %d not exist!\n", enSnsType);
        return CN_FAILURE;
    }

    s32Ret = cnispRegSensor(ViPipe, pstSnsObj);
    if (s32Ret)
    {
        CNSAMPLE_TRACE("cnispRegSensor fail\n");
        return CN_FAILURE;
    }
    
    cnsampleCommIspGetSensorCfgBySns(enSnsType, &enFmt, &u32LaneNum, &u32Fps);
    s32Ret = cnispSetSensorByEnum(ViPipe, enFmt, u32LaneNum, u32Fps);
    if (s32Ret)
    {
        CNSAMPLE_TRACE("cnispSetSensorByEnum fail\n");
        return CN_FAILURE;
    }
    
    stBusInfo.enSensorBusType = CN_SENSOR_BUS_TYPE_I2C;
    stBusInfo.enSensorPresenceType = CN_SENSOR_PRESENCE_TYPE_ENTITY;
    stBusInfo.s8I2cDev = (int8_t)s32BusId;
    s32Ret = cnispSetSensorBusInfo(ViPipe, &stBusInfo);
    if (s32Ret)
    {
        CNSAMPLE_TRACE("cnispSetSensorBusInfo fail\n");
        return CN_FAILURE;
    }

    cnSensorWorkMode_e enWorkMode = CN_SENSOR_WORK_MODE_MASTER;
    cnsampleCommIspGetSnsWorkModeBySns(enSnsType, &enWorkMode);
    s32Ret = cnispSetSensorWorkMode(ViPipe, enWorkMode);
    if (s32Ret)
    {
        CNSAMPLE_TRACE("cnispSetSensorWorkMode  fail\n");
    }
    
    return CN_SUCCESS;
}


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */

