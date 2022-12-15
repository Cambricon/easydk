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

#include <signal.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sched.h>
#include <sys/times.h>
#include <time.h>
#include "cnsample_comm.h"

#define SAMPLE_VER_Z   0

#define CN_PQ_COMMON_CFG_PATH	"/mps/bin/cnisp-tool/"
extern cnS32_t cnTuningServerInit(const cnChar_t *ps8CfgPath);
extern cnS32_t cnTuningServerExit();

cnVoid_t cnsampleCommSysPrintVersion()
{
    cnChar_t szInfo[1024];

    snprintf(szInfo, sizeof(szInfo), "%s %s %s", MOD_VERSION("sample", SAMPLE_VER_Z), __DATE__, __TIME__);
    printf("cnsample Version: %s\n\n", szInfo);
}

cnS32_t cnsampleCommSysWriteFile(char *name, cnU64_t addr, cnU32_t fileLen)
{
    FILE *fp = NULL;
    cnS32_t ret;

    fp = fopen(name, "wb");
    if ( NULL == fp)
    {
        CNSAMPLE_TRACE("open file: %s fail\n", name);
        return 0;
    }

    ret = fwrite((cnVoid_t*)addr, fileLen, 1, fp);
    if (!ret)
    {
        fclose(fp);
        CNSAMPLE_TRACE("write %s fail: ret: %d\n",name,ret);
        return 0;
    }

    fclose(fp);
    return fileLen;

}

cnS32_t cnsampleCommSysReadFile(cnChar_t *name, cnU64_t addr, cnU32_t bufLen)
{
    FILE *fp = NULL;
    cnU32_t fsize;
    struct stat fstat_buf;
    cnS32_t ret;

    fp = fopen(name, "rb");
    if ( NULL == fp)
    {
        CNSAMPLE_TRACE("open file: %s fail\n", name);
        return 0;
    }

    stat(name, &fstat_buf);
    fsize = fstat_buf.st_size;
    if (fsize > bufLen)
    {
        CNSAMPLE_TRACE("file: %s fsize: 0x%x > bufLen : 0x%x\n",name,fsize, bufLen);
        fclose(fp);
        return 0;
    }

    ret = fread((cnVoid_t*)addr, fsize, 1, fp);
    if (!ret)
    {
        fclose(fp);
        CNSAMPLE_TRACE("read %s fail: ret: %d\n",name,ret);
        perror("fread error");
        return 0;
    }

    fclose(fp);
    return fsize;

}


cnS32_t cnsampleCommSysGetPicSize(cnEnPicSize_t enPicSize, cnSize_t* pstSize)
{
    switch (enPicSize)
    {
        case PIC_CIF:   /* 352 * 288 */
            pstSize->u32Width  = 352;
            pstSize->u32Height = 288;
            break;

        case PIC_D1_PAL:   /* 720 * 576 */
            pstSize->u32Width  = 720;
            pstSize->u32Height = 576;
            break;

        case PIC_D1_NTSC:
            pstSize->u32Width  = 720;
            pstSize->u32Height = 480;
            break;

        case PIC_720P:
            pstSize->u32Width  = 1280;
            pstSize->u32Height = 720;
            break;

        case PIC_1080P:
            pstSize->u32Width  = 1920;
            pstSize->u32Height = 1080;
            break;

        case PIC_2560x1440:
            pstSize->u32Width  = 2560;
            pstSize->u32Height = 1440;
            break;

        case PIC_2592x1520:
            pstSize->u32Width  = 2592;
            pstSize->u32Height = 1520;
            break;

        case PIC_2592x1944:
            pstSize->u32Width  = 2592;
            pstSize->u32Height = 1944;
            break;

        case PIC_2688x1592:
            pstSize->u32Width  = 2688;
            pstSize->u32Height = 1592;
            break;

        case PIC_2688x1520:
            pstSize->u32Width  = 2688;
            pstSize->u32Height = 1520;
            break;

        case PIC_3840x2160:
            pstSize->u32Width  = 3840;
            pstSize->u32Height = 2160;
            break;

        case PIC_3584x2172:
            pstSize->u32Width  = 3584;
            pstSize->u32Height = 2172;
            break;

        case PIC_3840x2173:
            pstSize->u32Width  = 3840;
            pstSize->u32Height = 2173;
            break;

        case PIC_3584x2160:
            pstSize->u32Width  = 3584;
            pstSize->u32Height = 2160;
            break;

        case PIC_3000x3000:
            pstSize->u32Width  = 3000;
            pstSize->u32Height = 3000;
            break;

        case PIC_4000x3000:
	    pstSize->u32Width  = 4000;
            pstSize->u32Height = 3000;
            break;

        case PIC_4096x2160:
            pstSize->u32Width  = 4096;
            pstSize->u32Height = 2160;
            break;

        case PIC_7680x4320:
            pstSize->u32Width  = 7680;
            pstSize->u32Height = 4320;
            break;
        case PIC_3840x8640:
            pstSize->u32Width = 3840;
            pstSize->u32Height = 8640;
            break;
        default:
            CNSAMPLE_TRACE("pic size %d!\n", enPicSize);
            return CN_FAILURE;
    }

    return CN_SUCCESS;
}
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
cnS32_t cnsampleCommSysInit(cnrtVBConfigs_t* pstVbConfig)
#else
cnS32_t cnsampleCommSysInit(cnVbCfg_t* pstVbConfig)
#endif
{
    cnS32_t s32Ret = CN_FAILURE;

    if (NULL == pstVbConfig)
    {
        CNSAMPLE_TRACE("input parameter is null, it is invaild!\n");
        return CN_FAILURE;
    }
    /*
    s32Ret = cnInit(0);
    if (CN_SUCCESS != s32Ret)
    {
       CNSAMPLE_TRACE("cnInit failed with %d!\n", s32Ret);
       return CN_FAILURE;
    }

    s32Ret = cnrtInit(0);
    if (cnrtSuccess != s32Ret)
    {
       CNSAMPLE_TRACE("cnrtInit fail with %d\n", s32Ret);
       return CN_FAILURE;
    }

    s32Ret = cnsysExit();
    if (CN_SUCCESS != s32Ret)
    {
       CNSAMPLE_TRACE("cnsysExit failed with %x!\n", s32Ret);
    }

    s32Ret = cnrtVBExit();
    if (CN_SUCCESS != s32Ret)
    {
       CNSAMPLE_TRACE("cnrtVBExit failed with %d!\n", s32Ret);
    }
    */
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
    s32Ret = cnrtVBSetComCfg(pstVbConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnrtVBSetComCfg failed with 0x%x!\n", s32Ret);
        return CN_FAILURE;
    }

    s32Ret = cnrtVBInit();
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnrtVBInit failed with %d!\n", s32Ret);
        return CN_FAILURE;
    }
#else
    s32Ret = cnVBSetComCfg(pstVbConfig);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnVBSetComCfg failed with 0x%x!\n", s32Ret);
        return CN_FAILURE;
    }

    s32Ret = cnVBInit();
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnVBInit failed with %d!\n", s32Ret);
        return CN_FAILURE;
    }
#endif
    s32Ret = cnsysInit();
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsysInit failed 0x%x!\n", s32Ret);
        return CN_FAILURE;
    }



    s32Ret = cnsysInit();
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsysInit failed 0x%x!\n", s32Ret);
        return CN_FAILURE;
    }

    // cnTuningServerInit(CN_PQ_COMMON_CFG_PATH);
    return CN_SUCCESS;
}


cnS32_t cnsampleCommSysExit(void)
{
    cnS32_t s32Ret = CN_FAILURE;

    s32Ret = cnsysExit();
    if (CN_SUCCESS != s32Ret)
    {
       CNSAMPLE_TRACE("cnsysExit failed with %x!\n", s32Ret);
    }
    /*
    s32Ret |= cnrtVBExit();
    if (CN_SUCCESS != s32Ret)
    {
       CNSAMPLE_TRACE("cnrtVBExit failed with %d!\n", s32Ret);
    }

    cnrtDestroy();
    */
    // cnTuningServerExit();
    return s32Ret;
}

cnS32_t cnsampleCommViBindVpps(viPipe_t ViPipe, viChn_t ViChn, vppsGrp_t VppsGrp)
{
    cnMediaChn_t stSrcChn;
    cnMediaChn_t stDestChn;

    stSrcChn.enModId   = CN_ID_VI;
    stSrcChn.s32DevId  = ViPipe;
    stSrcChn.s32ChnId  = ViChn;

    stDestChn.enModId  = CN_ID_VPPS;
    stDestChn.s32DevId = VppsGrp;
    stDestChn.s32ChnId = 0;

    CHECK_RET(cnsysBind(&stSrcChn, &stDestChn), "cnsysBind(VI-VPPS)");
    return CN_SUCCESS;
}

cnS32_t cnsampleCommViUnBindVpps(viPipe_t ViPipe, viChn_t ViChn, vppsGrp_t VppsGrp)
{
    cnMediaChn_t stSrcChn;
    cnMediaChn_t stDestChn;

    stSrcChn.enModId   = CN_ID_VI;
    stSrcChn.s32DevId  = ViPipe;
    stSrcChn.s32ChnId  = ViChn;

    stDestChn.enModId  = CN_ID_VPPS;
    stDestChn.s32DevId = VppsGrp;
    stDestChn.s32ChnId = 0;

    CHECK_RET(cnsysUnBind(&stSrcChn, &stDestChn), "cnsysUnBind(VI-VPPS)");
    return CN_SUCCESS;
}

cnS32_t cnsampleCommVppsBindVo(vppsGrp_t VppsGrp, vppsChn_t VppsChn, voLayer_t VoLayer, voChn_t VoChn)
{
    cnMediaChn_t stSrcChn;
    cnMediaChn_t stDestChn;

    stSrcChn.enModId   = CN_ID_VPPS;
    stSrcChn.s32DevId  = VppsGrp;
    stSrcChn.s32ChnId  = VppsChn;

    stDestChn.enModId  = CN_ID_VO;
    stDestChn.s32DevId = VoLayer;
    stDestChn.s32ChnId = VoChn;

    CHECK_RET(cnsysBind(&stSrcChn, &stDestChn), "cnsysBind(VPPS-VO)");
    return CN_SUCCESS;
}

cnS32_t cnsampleCommVppsUnBindVo(vppsGrp_t VppsGrp, vppsChn_t VppsChn, voLayer_t VoLayer, voChn_t VoChn)
{
    cnMediaChn_t stSrcChn;
    cnMediaChn_t stDestChn;

    stSrcChn.enModId   = CN_ID_VPPS;
    stSrcChn.s32DevId  = VppsGrp;
    stSrcChn.s32ChnId  = VppsChn;

    stDestChn.enModId  = CN_ID_VO;
    stDestChn.s32DevId = VoLayer;
    stDestChn.s32ChnId = VoChn;

    CHECK_RET(cnsysUnBind(&stSrcChn, &stDestChn), "cnsysUnBind(VPPS-VO)");
    return CN_SUCCESS;
}

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */


