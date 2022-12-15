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


#ifndef __CNSAMPLE_COMM_H__
#define __CNSAMPLE_COMM_H__

#ifdef __cplusplus
#if __cplusplus
    extern "C"{
#endif
#endif /* __cplusplus */

#include <pthread.h>

#include "mps_config.h"

#include "cn_common.h"
#include "cn_comm_sys.h"
#include "cn_comm_vpps.h"
#include "cn_comm_vo.h"
#include "cn_comm_vi.h"
#include "cn_comm_sensor.h"
#include "cn_comm_isp.h"
#include "cn_defines.h"

#include "cn_sys.h"
#include "cn_vi.h"
#include "cn_vpps.h"
#include "cn_vo.h"
#include "cn_isp.h"
#include "cn_buffer.h"
#include "cn_mipi.h"
#include "cnrt.h"


#if LIBMPS_VERSION_INT >= MPS_VERSION_1_1_0
#include "cn_vb.h"
#endif

#define CHECK_CHN_RET(express,Chn,name)\
    do{\
        cnS32_t Ret;\
        Ret = express;\
        if (CN_SUCCESS != Ret)\
        {\
            printf("\033[0;31m%s chn %d failed at %s: LINE: %d with %#x!\033[0;39m\n", name, Chn, __FUNCTION__, __LINE__, Ret);\
            fflush(stdout);\
            return Ret;\
        }\
    }while(0)

#define CHECK_RET(express,name)\
    do{\
        cnS32_t Ret;\
        Ret = express;\
        if (CN_SUCCESS != Ret)\
        {\
            printf("\033[0;31m%s failed at %s: LINE: %d with %#x!\033[0;39m\n", name, __FUNCTION__, __LINE__, Ret);\
            return Ret;\
        }\
    }while(0)

#define CNSAMPLE_TRACE(fmt...)   \
    do {\
        printf("[%s]-%d: ", __FUNCTION__, __LINE__);\
        printf((char*)fmt);\
    }while(0)

#define COLOR_RGB_RED      0xFF0000
#define COLOR_RGB_GREEN    0x00FF00
#define COLOR_RGB_BLUE     0x0000FF
#define COLOR_RGB_BLACK    0x000000
#define COLOR_RGB_YELLOW   0xFFFF00
#define COLOR_RGB_CYN      0x00ffff
#define COLOR_RGB_WHITE    0xffffff

typedef enum cnEnPicSize
{
    PIC_CIF,
    PIC_D1_PAL,    /* 720 * 576 */
    PIC_D1_NTSC,   /* 720 * 480 */
    PIC_720P,      /* 1280 * 720  */
    PIC_1080P,     /* 1920 * 1080 */
    PIC_2560x1440,
    PIC_2592x1520,
    PIC_2592x1944,
    PIC_2688x1592,
    PIC_2688x1520,
    PIC_3840x2160,
    PIC_3584x2160,
    PIC_3584x2172,
    PIC_3840x2173,
    PIC_4096x2160,
    PIC_3000x3000,
    PIC_4000x3000,
    PIC_7680x4320,
    PIC_3840x8640,
    PIC_BUTT
} cnEnPicSize_t;


typedef enum cnEnSampleSnsType
{
    SONY_IMX305_LVDS_3584x2172_40FPS_12BIT,
    SONY_IMX305_LVDS_8M_40FPS_12BIT, //3840x2172 just for abnormal test
    SONY_IMX327_MIPI_2M_30FPS_12BIT,
    SONY_IMX327_MIPI_2M_30FPS_12BIT_WDR2TO1,
    SONY_IMX327_SLAVE_2M_30FPS_12BIT,
    SONY_IMX334_MIPI_8M_30FPS_12BIT = 5,
    OV_08A20_MIPI_8M_30FPS_12BIT,
    OV_08A20_MIPI_8M_60FPS_10BIT,
    OV_08A20_MIPI_8M_30FPS_10BIT_WDR2TO1,
    OV_08A20_MIPI_4M_30FPS_12BIT,
    OV_08A20_MIPI_2688x1520_30FPS_12BIT = 10,
    OV_08A20_MIPI_2688x1520_30FPS_10BIT_WDR2TO1,
    OV_04A10_MIPI_SLAVE_2560x1440_25FPS_10BIT,
    OV_04A10_MIPI_SLAVE_2688x1520_25FPS_10BIT,
    SAMPLE_SNS_TYPE_BUTT
} cnEnSampleSnsType_t;

typedef struct cnsampleSensorInfo
{
    cnEnSampleSnsType_t   enSnsType;
    cnS32_t              s32SnsId;
    cnS32_t              s32BusId;
    cnU32_t              MipiDev;
    cnU32_t              s32SnsClkId;
    cnmipiSnsWclkFreq_t  enSnsClkFreq;
} cnsampleSensorInfo_t;


typedef struct cnsampleDevInfo
{
    viDev_t      ViDev;
    cnEnWdrMode_t  enWDRMode;
} cnsampleDevInfo_t;

typedef struct cnsamplePipeInfo
{
    viPipe_t          pipe;
    cnEnViVppsMode_t  enMastPipeMode;
}cnsamplePipeInfo_t;

typedef struct cnsampleChnInfo
{
    viChn_t                ViChn;
    cnEnPixelFormat_t      enPixFormat;
    cnEnVideoFormat_t      enVideoFormat;
    cnEnCompressMode_t     enCompressMode;
    cnEnDynamicRange_t     enDynamicRange;
} cnsampleChnInfo_t;

typedef struct cnsampleViInfo
{
    cnsampleSensorInfo_t    stSnsInfo;
    cnsampleDevInfo_t       stDevInfo;
    cnsamplePipeInfo_t      stPipeInfo;
    cnsampleChnInfo_t       stChnInfo;
} cnsampleViInfo_t;

typedef struct cnsampleViConfig
{
    cnsampleViInfo_t     astViInfo[VI_MAX_DEV_NUM];
    cnS32_t              as32WorkingViId[VI_MAX_DEV_NUM];
    cnS32_t              s32WorkingViNum;
    cnBool_t             bModeSwitch;
    cnBool_t             bSwitch;
} cnsampleViConfig_t;

typedef enum cnsampleVoMode
{
    VO_MODE_1MUX  ,
    VO_MODE_2MUX  ,
    VO_MODE_4MUX  ,
    VO_MODE_8MUX  ,
    VO_MODE_9MUX  ,
    VO_MODE_16MUX ,
    VO_MODE_25MUX ,
    VO_MODE_36MUX ,
    VO_MODE_49MUX ,
    VO_MODE_64MUX ,
    VO_MODE_2X4   ,
    VO_MODE_BUTT
} cnsampleVoMode_t;


typedef struct cnsampleCommVoLayerConfig
{
    /* for layer */
    voLayer_t                VoLayer;
    cnvoEnIntfSync_t          enIntfSync;
    cnRect_t                  stDispRect;
    cnSize_t                  stImageSize;
    cnEnPixelFormat_t          enPixFormat;

    cnU32_t                  u32DisBufLen;
    /* for chn */
    cnsampleVoMode_t        enVoMode;
}cnsampleCommVoLayerConfig_t;

typedef struct cnsampleVoConfig
{
    /* for device */
    voDev_t                  VoDev;
    cnvoEnIntfType_t          enVoIntfType;
    cnvoEnIntfSync_t          enIntfSync;
    cnEnPicSize_t              enPicSize;
    cnU32_t                  u32BgColor;

    /* for layer */
    cnEnPixelFormat_t          enPixFormat;
    cnRect_t                  stDispRect;
    cnSize_t                  stImageSize;

    cnU32_t                  u32DisBufLen;

    /* for chnnel */
    cnsampleVoMode_t        enVoMode;
} cnsampleVoConfig_t;


cnVoid_t cnsampleCommSysPrintVersion();
cnS32_t cnsampleCommSysReadFile(char *name, cnU64_t addr, cnU32_t bufLen);
cnS32_t cnsampleCommSysWriteFile(char *name, cnU64_t addr, cnU32_t fileLen);
cnS32_t cnsampleCommSysGetPicSize(cnEnPicSize_t enPicSize, cnSize_t* pstSize);
cnS32_t cnsampleCommSysExit(void);
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
cnS32_t cnsampleCommSysInit(cnrtVBConfigs_t* pstVbConfig);
#else
cnS32_t cnsampleCommSysInit(cnVbCfg_t* pstVbConfig);
#endif

cnS32_t cnsampleCommIspGetIspAttrBySns(cnEnSampleSnsType_t enSnsType, cnispPub_t* pstPubAttr);
cnSensorObj_s* cnsampleCommIspGetSensorObj(cnEnSampleSnsType_t enSnsType);
cnS32_t cnsampleCommIspAeCallback(viPipe_t ViPipe);
cnS32_t cnsampleCommIspAeUnCallback(viPipe_t ViPipe);
cnS32_t cnsampleCommIspAwbCallback(viPipe_t ViPipe);
cnS32_t cnsampleCommIspAwbUnCallback(viPipe_t ViPipe);
cnS32_t cnsampleCommIspStart(viPipe_t ViPipe);
cnVoid_t cnsampleCommIspStop(viPipe_t ViPipe);
cnS32_t cnsampleCommIspBindSensor(viPipe_t ViPipe, cnEnSampleSnsType_t enSnsType, cnS32_t s32BusId);

// cnS32_t cnsampleCommGetSensorCfg( cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViSetParam(cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViStartVi(cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViStopVi(cnsampleViConfig_t* pstViConfig);
// cnS32_t cnsampleCommViGetSensorInfo(cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViStartMipi(cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViStopMipi(cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViGetFrameRateBySensor(cnEnSampleSnsType_t enSnsType, cnU32_t* pu32FrameRate);
cnS32_t cnsampleCommViGetSizeBySensor(cnEnSampleSnsType_t enSnsType, cnEnPicSize_t* pSize);
cnS32_t cnsampleCommViGetWdrModeBySensor(cnEnSampleSnsType_t enSnsType, cnEnWdrMode_t* pMode);
cnS32_t cnsampleCommViGetClockBySensor(cnEnSampleSnsType_t enSnsType, cnmipiSnsWclkFreq_t *pSnsClock);

cnS32_t cnsampleCommViStartLvdsTraning(cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViStartMipiTraning(cnsampleViConfig_t* pstViConfig);
cnS32_t cnsampleCommViGetLvdsDelayMode(cnsampleViConfig_t* pstViConfig);

cnS32_t cnsampleCommViBindVpps(viPipe_t ViPipe, viChn_t ViChn, vppsGrp_t VppsGrp);
cnS32_t cnsampleCommViUnBindVpps(viPipe_t ViPipe, viChn_t ViChn, vppsGrp_t VppsGrp);

cnS32_t cnsampleCommVppsStart(vppsGrp_t VppsGrp, cnBool_t* pabChnEnable, cnvppsGrpAttr_t* pstVppsGrpAttr, cnvppsChnAttr_t* pastVppsChnAttr);
cnS32_t cnsampleCommVppsStop(vppsGrp_t VppsGrp, cnBool_t* pabChnEnable);
cnS32_t cnsampleCommVppsBindVo(vppsGrp_t VppsGrp, vppsChn_t VppsChn, voLayer_t VoLayer, voChn_t VoChn);
cnS32_t cnsampleCommVppsUnBindVo(vppsGrp_t VppsGrp, vppsChn_t VppsChn, voLayer_t VoLayer, voChn_t VoChn);

cnS32_t cnsampleCommVoGetWH(cnvoEnIntfSync_t enIntfSync, cnU32_t* pu32W, cnU32_t* pu32H, cnU32_t* pu32Frm);
cnS32_t cnsampleCommVoStartDev(voDev_t VoDev, cnvoPubAttr_t* pstPubAttr);
cnS32_t cnsampleCommVoStopDev(voDev_t VoDev);
cnS32_t cnsampleCommVoStartLayer(voLayer_t VoLayer, const cnvoVideoLayerAttr_t* pstLayerAttr);
cnS32_t cnsampleCommVoStopLayer(voLayer_t VoLayer);
cnS32_t cnsampleCommVoStartChn(voLayer_t VoLayer, cnsampleVoMode_t enMode);
cnS32_t cnsampleCommVoStopChn(voLayer_t VoLayer, cnsampleVoMode_t enMode);
cnS32_t cnsampleCommVoStopVo(cnsampleVoConfig_t *pstVoConfig);
cnS32_t cnsampleCommVoStartVo(cnsampleVoConfig_t *pstVoConfig);
cnS32_t cnsampleCommVoStartLayerChn(cnsampleCommVoLayerConfig_t * pstVoLayerConfig);
cnS32_t cnsampleCommVoStopLayerChn(cnsampleCommVoLayerConfig_t * pstVoLayerConfig);
cnS32_t cnsampleCommVoStartVoMipiScreen(cnsampleVoConfig_t *pstVoConfig);


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* __cplusplus */

#endif
