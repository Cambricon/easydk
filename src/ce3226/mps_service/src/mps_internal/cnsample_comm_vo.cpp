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

#include "cnsample_comm.h"
#include "cn_mipi_tx.h"

#define MIPI_TX_DEV_NODE       "/dev/cn_mipi_tx"

cnS32_t cnsampleCommVoGetWH(cnvoEnIntfSync_t enIntfSync, cnU32_t* pu32W, cnU32_t* pu32H, cnU32_t* pu32Frm)
{
    switch (enIntfSync)
    {
        
        case VO_OUTPUT_1080P30   :
            *pu32W = 1920;
            *pu32H = 1080;
            *pu32Frm = 30;
            break;
        case VO_OUTPUT_720P50    :
            *pu32W = 1280;
            *pu32H = 720;
            *pu32Frm = 50;
            break;
        case VO_OUTPUT_720P60    :
            *pu32W = 1280;
            *pu32H = 720;
            *pu32Frm = 60;
            break;

        case VO_OUTPUT_1080P60   :
            *pu32W = 1920;
            *pu32H = 1080;
            *pu32Frm = 60;
            break;

        case VO_OUTPUT_USER    :
            *pu32W = 720;
            *pu32H = 576;
            *pu32Frm = 25;
            break;
        default:
            CNSAMPLE_TRACE("vo enIntfSync %d not support!\n", enIntfSync);
            return CN_FAILURE;
    }


    return CN_SUCCESS;
}

cnS32_t cnsampleCommVoStartDevMipiScreen(voDev_t VoDev, cnvoPubAttr_t *pstPubAttr, cnU32_t u32Frame)
{
    cnU32_t ret;

    ret = cnvoSetPubAttr(VoDev, pstPubAttr);
    if ( ret != CN_SUCCESS ) 
    {
        CNSAMPLE_TRACE("cnvoSetPubAttr fail  ret: 0x%x\n",ret);
        return ret;
    }

    ret = cnvoSetDevFrameRate(VoDev, u32Frame);
    if (CN_SUCCESS != ret)
    {
        CNSAMPLE_TRACE("cnvoSetDevFrameRate failed with 0x%x!\n", ret);
        return ret;
    }
    
    ret = cnvoEnable(VoDev);
    if ( ret != CN_SUCCESS )
    {
        CNSAMPLE_TRACE("cnvoEnable fail  ret: 0x%x\n",ret);
        return ret;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommVoStartDev(voDev_t VoDev, cnvoPubAttr_t *pstPubAttr)
{
    cnU32_t ret;

    ret = cnvoSetPubAttr(VoDev, pstPubAttr);
    if ( ret != CN_SUCCESS ) 
    {
        CNSAMPLE_TRACE("cnvoSetPubAttr fail  ret: 0x%x\n",ret);
        return ret;
    }
    
    ret = cnvoEnable(VoDev);
    if ( ret != CN_SUCCESS )
    {
        CNSAMPLE_TRACE("cnvoEnable fail  ret: 0x%x\n",ret);
        return ret;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommVoStopDev(voDev_t VoDev)
{
    cnS32_t s32Ret = CN_SUCCESS;

    s32Ret = cnvoDisable(VoDev);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommVoStartLayer(voLayer_t VoLayer, const cnvoVideoLayerAttr_t* pstLayerAttr)
{
    cnS32_t s32Ret = CN_SUCCESS;

    s32Ret = cnvoSetVideoLayerAttr(VoLayer, pstLayerAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    s32Ret = cnvoEnableVideoLayer(VoLayer);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return s32Ret;
}

cnS32_t cnsampleCommVoStopLayer(voLayer_t VoLayer)
{
    cnS32_t s32Ret = CN_SUCCESS;

    s32Ret = cnvoDisableVideoLayer(VoLayer);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return s32Ret;
}

cnS32_t cnsampleCommVoStartChn(voLayer_t VoLayer, cnsampleVoMode_t enMode)
{
    cnU32_t i = 0;
    cnS32_t s32Ret    = CN_SUCCESS;
    cnU32_t u32Width  = 0;
    cnU32_t u32Height = 0;
    cnU32_t u32WndNum = 0;
    cnU32_t u32Square = 0;
    cnvoChnAttr_t         stChnAttr;
    cnvoVideoLayerAttr_t stLayerAttr;
    
    switch (enMode)
    {
        case VO_MODE_1MUX:
            u32WndNum = 1;
            u32Square = 1;
            break;
        case VO_MODE_2MUX:
            u32WndNum = 2;
            u32Square = 2;
            break;
        case VO_MODE_4MUX:
            u32WndNum = 4;
            u32Square = 2;
            break;
        case VO_MODE_8MUX:
            u32WndNum = 8;
            u32Square = 3;
            break;
        case VO_MODE_9MUX:
            u32WndNum = 9;
            u32Square = 3;
            break;
        default:
            CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
            return CN_FAILURE;
    }

    s32Ret = cnvoGetVideoLayerAttr(VoLayer, &stLayerAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnvoGetVideoLayerAttr failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }
    
    u32Width  = stLayerAttr.stImageSize.u32Width;
    u32Height = stLayerAttr.stImageSize.u32Height;

    CNSAMPLE_TRACE("u32Width:%d, u32Height:%d, u32Square:%d\n", u32Width, u32Height, u32Square);
    for (i = 0; i < u32WndNum; i++)
    {
        memset(&stChnAttr, 0, sizeof(stChnAttr));
        stChnAttr.stRect.s32X       = ALIGN_DOWN((u32Width / u32Square) * (i % u32Square), 2);
        stChnAttr.stRect.s32Y       = ALIGN_DOWN((u32Height / u32Square) * (i / u32Square), 2);
        stChnAttr.stRect.u32Width   = ALIGN_DOWN(u32Width / u32Square, 2);
        stChnAttr.stRect.u32Height  = ALIGN_DOWN(u32Height / u32Square, 2);
        stChnAttr.u32Priority       = 0;

        s32Ret = cnvoSetChnAttr(VoLayer, i, &stChnAttr);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnvoSetChnAttr failed with %#x!\n", s32Ret);
            return CN_FAILURE;
        }

        s32Ret =cnvoEnableChn(VoLayer, i);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnvoEnableChn failed with %#x!\n", s32Ret);
            return CN_FAILURE;
        }
    }

    return CN_SUCCESS;
}

cnS32_t cnsampleCommVoStopChn(voLayer_t VoLayer, cnsampleVoMode_t enMode)
{
    cnU32_t i;
    cnS32_t s32Ret    = CN_SUCCESS;
    cnU32_t u32WndNum = 0;

    switch (enMode)
    {
        case VO_MODE_1MUX:
        {
            u32WndNum = 1;
            break;
        }
        case VO_MODE_2MUX:
        {
            u32WndNum = 2;
            break;
        }
        case VO_MODE_4MUX:
        {
            u32WndNum = 4;
            break;
        }
        case VO_MODE_8MUX:
        {
            u32WndNum = 8;
            break;
        }
        case VO_MODE_9MUX:
        {
            u32WndNum = 9;
            break;
        }
        default:
            CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
            return CN_FAILURE;
    }

    for (i = 0; i < u32WndNum; i++)
    {
        s32Ret = cnvoDisableChn(VoLayer, i);
        if (s32Ret != CN_SUCCESS)
        {
            CNSAMPLE_TRACE("cnvoDisableChn failed with %#x!\n", s32Ret);
            return CN_FAILURE;
        }
    }

    return s32Ret;
}


cnS32_t cnsampleCommVoStartVo(cnsampleVoConfig_t *pstVoConfig)
{
    voDev_t                 VoDev          = 0;
    voLayer_t               VoLayer        = 0;
    cnsampleVoMode_t       enVoMode       = VO_MODE_1MUX;
    
    cnvoEnIntfType_t         enVoIntfType   = VO_INTF_LCD_24BIT;
    cnvoEnIntfSync_t         enIntfSync     = VO_OUTPUT_1080P30;
    cnvoPubAttr_t          stVoPubAttr    = {0};
    cnvoVideoLayerAttr_t  stLayerAttr    = {0};
    cnS32_t                 s32Ret         = CN_SUCCESS;

    if (NULL == pstVoConfig)
    {
        CNSAMPLE_TRACE("Error:argument can not be NULL\n");
        return CN_FAILURE;
    }
    
    VoDev          = pstVoConfig->VoDev;
    VoLayer        = pstVoConfig->VoDev;
    enVoMode       = pstVoConfig->enVoMode;
    enVoIntfType   = pstVoConfig->enVoIntfType;
    enIntfSync     = pstVoConfig->enIntfSync;

    stVoPubAttr.enIntfType  = enVoIntfType;
    stVoPubAttr.enIntfSync  = enIntfSync;
    stVoPubAttr.u32BgColor  = pstVoConfig->u32BgColor;

    s32Ret = cnsampleCommVoStartDev(VoDev, &stVoPubAttr);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommVoStartDev failed!\n");
        return s32Ret;
    }

    s32Ret = cnsampleCommVoGetWH(stVoPubAttr.enIntfSync,
                                  &stLayerAttr.stDispRect.u32Width, &stLayerAttr.stDispRect.u32Height,
                                  &stLayerAttr.u32DispFrmRt);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommVoGetWH failed!\n");
        cnsampleCommVoStopDev(VoDev);
        return s32Ret;
    }
    
    stLayerAttr.enPixFormat       = pstVoConfig->enPixFormat;
    stLayerAttr.stDispRect = pstVoConfig->stDispRect;
    stLayerAttr.stImageSize.u32Width  = stLayerAttr.stDispRect.u32Width;
    stLayerAttr.stImageSize.u32Height = stLayerAttr.stDispRect.u32Height;
    
    if (pstVoConfig->u32DisBufLen)
    {
        stLayerAttr.stImageSize.u32Width = ALIGN_UP(stLayerAttr.stImageSize.u32Width, DEFAULT_ALIGN);
        s32Ret = cnvoSetDisplayBufLen(VoLayer, pstVoConfig->u32DisBufLen);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("cnvoSetDisplayBufLen failed with %#x!\n",s32Ret);
            cnsampleCommVoStopDev(VoDev);
            return s32Ret;
        }
    }
 
    s32Ret = cnsampleCommVoStartLayer(VoLayer, &stLayerAttr);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommVoStart video layer failed!\n");
        cnsampleCommVoStopDev(VoDev);
        return s32Ret;
    }

    s32Ret = cnsampleCommVoStartChn(VoLayer, enVoMode);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommVoStartChn failed!\n");
        cnsampleCommVoStopLayer(VoLayer);
        cnsampleCommVoStopDev(VoDev);
        return s32Ret;
    }
    
    return CN_SUCCESS;
}

cnS32_t cnsampleCommVoStopVo(cnsampleVoConfig_t *pstVoConfig)
{
    voDev_t                VoDev     = 0;
    voLayer_t              VoLayer   = 0;
    cnsampleVoMode_t      enVoMode  = VO_MODE_BUTT;
    cnS32_t                 s32Ret  = CN_SUCCESS;

    if (NULL == pstVoConfig)
    {
        CNSAMPLE_TRACE("Error:argument can not be NULL\n");
        return CN_FAILURE;
    }

    VoDev     = pstVoConfig->VoDev;
    VoLayer   = pstVoConfig->VoDev;
    enVoMode  = pstVoConfig->enVoMode;

    s32Ret = cnsampleCommVoStopChn(VoLayer, enVoMode);
    s32Ret |= cnsampleCommVoStopLayer(VoLayer);
    s32Ret |= cnsampleCommVoStopDev(VoDev);

    return s32Ret;
}


cnS32_t cnsampleCommVoStartLayerChn(cnsampleCommVoLayerConfig_t * pstVoLayerConfig)
{
    voLayer_t                VoLayer           = 0;
    cnvoEnIntfSync_t          enIntfSync        = VO_OUTPUT_1080P30;
    cnsampleVoMode_t         enVoMode          = VO_MODE_BUTT;
    cnS32_t                      s32Ret            = CN_SUCCESS;
    cnvoVideoLayerAttr_t        stLayerAttr;
    cnU32_t                  u32Frmt, u32Width, u32Height;

    if (NULL == pstVoLayerConfig)
    {
        CNSAMPLE_TRACE("Error:argument can not be NULL\n");
        return CN_FAILURE;
    }

    VoLayer           = pstVoLayerConfig->VoLayer;
    enIntfSync        = pstVoLayerConfig->enIntfSync;
    enVoMode          = pstVoLayerConfig->enVoMode;

    s32Ret = cnsampleCommVoGetWH(enIntfSync, &u32Width, &u32Height, &u32Frmt);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("Can not get synchronization information!\n");
        return CN_FAILURE;
    }

    stLayerAttr.stDispRect.s32X       = 0;
    stLayerAttr.stDispRect.s32Y       = 0;
    stLayerAttr.stDispRect.u32Width   = u32Width;
    stLayerAttr.stDispRect.u32Height  = u32Height;
    stLayerAttr.stImageSize.u32Width  = u32Width;
    stLayerAttr.stImageSize.u32Height = u32Height;
    stLayerAttr.u32DispFrmRt          = u32Frmt;

    stLayerAttr.enPixFormat           = pstVoLayerConfig->enPixFormat;

    stLayerAttr.stDispRect.s32X = 0;
    stLayerAttr.stDispRect.s32Y = 0;
    stLayerAttr.stDispRect = pstVoLayerConfig->stDispRect;
   
    stLayerAttr.stImageSize.u32Width  = stLayerAttr.stDispRect.u32Width;
    stLayerAttr.stImageSize.u32Height = stLayerAttr.stDispRect.u32Height;

    stLayerAttr.stImageSize.u32Width  = stLayerAttr.stDispRect.u32Width;
    stLayerAttr.stImageSize.u32Height = stLayerAttr.stDispRect.u32Height;
    stLayerAttr.stImageSize = pstVoLayerConfig->stImageSize;

    if (pstVoLayerConfig->u32DisBufLen)
    {
        s32Ret = cnvoSetDisplayBufLen(VoLayer, pstVoLayerConfig->u32DisBufLen);
        if (CN_SUCCESS != s32Ret)
        {
            CNSAMPLE_TRACE("cnvoSetDisplayBufLen failed with %#x!\n",s32Ret);
            return s32Ret;
        }
    }

    s32Ret = cnsampleCommVoStartLayer(VoLayer, &stLayerAttr);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommVoStart video layer failed!\n");
        return s32Ret;
    }

    s32Ret = cnsampleCommVoStartChn(VoLayer, enVoMode);
    if (CN_SUCCESS != s32Ret)
    {
        CNSAMPLE_TRACE("cnsampleCommVoStartChn failed!\n");
        cnsampleCommVoStopLayer(VoLayer);
        return s32Ret;
    }
    return s32Ret;
}

cnS32_t cnsampleCommVoStopLayerChn(cnsampleCommVoLayerConfig_t * pstVoLayerConfig)
{
    voLayer_t              VoLayer   = 0;
    cnsampleVoMode_t      enVoMode  = VO_MODE_BUTT;

    if (NULL == pstVoLayerConfig)
    {
        CNSAMPLE_TRACE("Error:argument can not be NULL\n");
        return CN_FAILURE;
    }

    VoLayer   = pstVoLayerConfig->VoLayer;
    enVoMode  = pstVoLayerConfig->enVoMode;

    cnsampleCommVoStopChn(VoLayer, enVoMode);
    cnsampleCommVoStopLayer(VoLayer);

    return CN_SUCCESS;
}


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
