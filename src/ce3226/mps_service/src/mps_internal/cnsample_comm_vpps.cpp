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

cnS32_t cnsampleCommVppsStart(vppsGrp_t VppsGrp, cnBool_t* pabChnEnable, cnvppsGrpAttr_t* pstVppsGrpAttr, cnvppsChnAttr_t* pastVppsChnAttr)
{
    vppsChn_t VppsChn;
    cnS32_t s32Ret;
    cnS32_t j;

    if (!pabChnEnable || !pstVppsGrpAttr || !pastVppsChnAttr)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    s32Ret = cnvppsCreateGrp(VppsGrp, pstVppsGrpAttr);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnvppsCreateGrp(grp:%d) failed with %#x!\n", VppsGrp, s32Ret);
        return CN_FAILURE;
    }

    for (j = 0; j < VPPS_MAX_PHY_CHN_NUM; j++)
    {
        if(CN_TRUE == pabChnEnable[j])
        {
            VppsChn = j;
            s32Ret = cnvppsSetChnAttr(VppsGrp, VppsChn, &pastVppsChnAttr[VppsChn]);
            if (s32Ret != CN_SUCCESS)
            {
                CNSAMPLE_TRACE("cnvppsSetChnAttr failed with %#x\n", s32Ret);
                return CN_FAILURE;
            }

            s32Ret = cnvppsEnableChn(VppsGrp, VppsChn);
            if (s32Ret != CN_SUCCESS)
            {
                CNSAMPLE_TRACE("cnvppsEnableChn failed with %#x\n", s32Ret);
                return CN_FAILURE;
            }
        }
    }

    s32Ret = cnvppsStartGrp(VppsGrp);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("cnvppsStartGrp failed with %#x\n", s32Ret);
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}


cnS32_t cnsampleCommVppsStop(vppsGrp_t VppsGrp, cnBool_t* pabChnEnable)
{
    cnS32_t j;
    cnS32_t s32Ret = CN_SUCCESS;
    vppsChn_t VppsChn;
    
    if (!pabChnEnable)
    {
        CNSAMPLE_TRACE("NULL ptr\n");
        return CN_FAILURE;
    }

    s32Ret = cnvppsStopGrp(VppsGrp);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    for (j = 0; j < VPPS_MAX_PHY_CHN_NUM; j++)
    {
        if(CN_TRUE == pabChnEnable[j])
        {
            VppsChn = j;
            s32Ret = cnvppsDisableChn(VppsGrp, VppsChn);

            if (s32Ret != CN_SUCCESS)
            {
                CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
                return CN_FAILURE;
            }
        }
    }

    s32Ret = cnvppsDestroyGrp(VppsGrp);
    if (s32Ret != CN_SUCCESS)
    {
        CNSAMPLE_TRACE("failed with %#x!\n", s32Ret);
        return CN_FAILURE;
    }

    return CN_SUCCESS;
}


#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
