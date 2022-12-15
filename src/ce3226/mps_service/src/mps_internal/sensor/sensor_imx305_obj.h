#ifndef __CN_SENSOR_305_OBJ_H__
#define __CN_SENSOR_305_OBJ_H__

#ifdef __cplusplus
extern "C"
{
#endif/*__cplusplus*/
#include <stdio.h>
#include <stdint.h>
#include "cn_comm_sensor.h"

#define cnSens305Print(format,args...) \
do{ \
    cnLog(CN_DBG_INFO,  CN_ID_ISP, "/%s/%s/%d imx305 -- "format, __FILE__,__func__,__LINE__, ##args);\
} while (0)

// note high bit 32 is refresh flag
typedef struct _cn_imx305_dynamic_parm
{
    cnSensorVar_s stAgain;
    cnSensorVar_s stDgain;
    cnSensorVar_s stVmax;
    cnSensorVar_s stRHS1;
    cnSensorVar_s stRHS2;
    cnSensorVar_s stSHS;
    cnSensorVar_s stSHS2;
    cnSensorVar_s stSHS3;
    cnSensorVar_s stYOUT;
    cnSensorVar_s stFSC;
    cnSensorVar_s stSlowFps;
    cnSensorVar_s stBasicVmax;
    cnSensorVar_s stBasicFps;
    cnSensorVar_s stVmaxPre;
}cn_imx305_dynamic_parm_t;

typedef struct _cn_imx305_ae_init_parm
{
    int32_t s32Again;
    int32_t s32Dgain;
    int32_t s32ExpTime;
    int32_t s32AgainShort;
    int32_t s32DgainShort;
    int32_t s32ExpTimeShort;
}cn_imx305_ae_init_parm_t;

typedef struct _sensor_cxt_t{
    int32_t s32Fd;
    char szI2cDev[16];
    uint32_t u32I2cAddr;
    uint32_t u32Pipe;
    cnSensorMode_s astSensorMode[WDR_MODE_COUNT];
    cnSensorParam_s stSensorParm;
    cnSensorParam_s stSensorParmPre;
    cnSensorControl_s stSensorCtrl;
    cn_imx305_dynamic_parm_t stDynamicParm;
    cnSensorCfg_s stSensorCfg;
    cnSensorBusInfo_s stBusInfo;
    cn_imx305_ae_init_parm_t stAeInitParam;
}cnSensorCxtImx305;

#define M_MULT_SENSOR_305

#ifdef __cplusplus
}
#endif/*__cplusplus*/

#endif
