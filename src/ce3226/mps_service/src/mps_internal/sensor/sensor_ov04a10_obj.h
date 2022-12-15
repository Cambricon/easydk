#ifndef __CN_SENSOR_OV04A10_OBJ_H__
#define __CN_SENSOR_OV04A10_OBJ_H__

#ifdef __cplusplus
extern "C"
{
#endif/*__cplusplus*/
#include <stdio.h>
#include <stdint.h>

#include "cn_comm_sensor.h"

#define cnSensOv04a10Print(format,args...) \
do{ \
    cnLog(CN_DBG_INFO,  CN_ID_ISP, "/%s/%s/%d ov04a10 -- "format, __FILE__,__func__,__LINE__, ##args);\
} while (0)

typedef struct _cn_ov04a10_dynamic_parm
{
    cnSensorVar_s stDistancMode;
    cnSensorVar_s stDistancVar;
    cnSensorVar_s stVts;
    cnSensorVar_s stAgain;
    cnSensorVar_s stDgain;
    cnSensorVar_s stExpTime;
    cnSensorVar_s stAgainShort;
    cnSensorVar_s stDgainShort;
    cnSensorVar_s stExpTimeShort;
    cnSensorVar_s stFSC;
    cnSensorVar_s stVtsBasic;
    cnSensorVar_s stFpsBasic;
    cnSensorVar_s stVtsPre;
    cnSensorVar_s stSlowFps;
}cn_ov04a10_dynamic_parm_t;

typedef struct _cn_ov04a10_ae_init_parm
{
    int32_t s32Again;
    int32_t s32Dgain;
    int32_t s32ExpTime;
    int32_t s32AgainShort;
    int32_t s32DgainShort;
    int32_t s32ExpTimeShort;
}cn_ov04a10_ae_init_parm_t;

typedef struct _sensor_cxt_t{
    int32_t s32Fd;
    char szI2cDev[16];
    uint32_t u32I2cAddr;
    uint32_t u32Pipe;
    cnSensorMode_s astSensorMode[WDR_MODE_COUNT];
    cnSensorParam_s stSensorParm;
    cnSensorParam_s stSensorParmPre;
    cnSensorControl_s stSensorCtrl;
    cn_ov04a10_dynamic_parm_t stDynamicParm;
    cnSensorCfg_s stSensorCfg;
    cnSensorBusInfo_s stBusInfo;
    cnSensorCalibrations_s *pstCalibration;
    cn_ov04a10_ae_init_parm_t stAeInitParam;
}cnSensorCxt04a10;

#ifdef __cplusplus
}
#endif/*__cplusplus*/

#endif
