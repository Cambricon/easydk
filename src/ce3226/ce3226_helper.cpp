/*************************************************************************
 * Copyright (C) [2022] by Cambricon, Inc. All rights reserved
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
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
#include "ce3226_helper.hpp"

#include <cstring>  // for memset

#include "mps_config.h"
#include "glog/logging.h"
#include "cnrt.h"
#include "../common/utils.hpp"

#if LIBMPS_VERSION_INT >= MPS_VERSION_1_1_0
#include "cn_vb.h"
#endif

namespace cnedk {

CnedkBufSurfaceColorFormat GetSurfFmt(cnEnPixelFormat fmt) {
  switch (fmt) {
    case PIXEL_FORMAT_YUV420_8BIT_SEMI_UV:
      return CNEDK_BUF_COLOR_FORMAT_NV12;
    case PIXEL_FORMAT_YUV420_8BIT_SEMI_VU:
      return CNEDK_BUF_COLOR_FORMAT_NV21;
    default:
      break;
  }
  return CNEDK_BUF_COLOR_FORMAT_INVALID;
}

static cnEnPixelFormat GetMpsFmt(CnedkBufSurfaceColorFormat fmt) {
  switch (fmt) {
    case CNEDK_BUF_COLOR_FORMAT_NV12:
      return PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
    case CNEDK_BUF_COLOR_FORMAT_NV21:
      return PIXEL_FORMAT_YUV420_8BIT_SEMI_VU;
    case CNEDK_BUF_COLOR_FORMAT_BGR:
      return PIXEL_FORMAT_BGR888_PACKED;
    case CNEDK_BUF_COLOR_FORMAT_ARGB1555:
      return PIXEL_FORMAT_ARGB1555_PACKED;
    default:
      break;
  }
  return PIXEL_FORMAT_BUTT;
}
int BufSurfaceToVideoFrameInfo(CnedkBufSurface *surf, cnVideoFrameInfo_t *info, int surfIndex) {
  CnedkBufSurfaceParams *params = &surf->surface_list[surfIndex];
  uint64_t vir_addr = (uint64_t)params->mapped_data_ptr;
  uint64_t phy_addr = (uint64_t)params->data_ptr;
  cnEnPixelFormat fmt = GetMpsFmt(params->color_format);
  if (fmt == PIXEL_FORMAT_BUTT) {
    LOG(ERROR) << "[EasyDK] BufSurfaceToVideoFrameInfo(): Unsupported pixel format: " << params->color_format;
    return -1;
  }

  if (surf->mem_type != CNEDK_BUF_MEM_VB && surf->mem_type != CNEDK_BUF_MEM_VB_CACHED) {
    LOG(ERROR) << "[EasyDK] BufSurfaceToVideoFrameInfo(): surf must be allocated with mem_type VB";
    return -1;
  }
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  uint64_t pool_id = 0;
  uint64_t handle = 0;
  CNRT_SAFECALL(cnrtVBPhy2Handle(phy_addr, &handle), "BufSurfaceToVideoFrameInfo(): failed", -1);
  CNRT_SAFECALL(cnrtVBHandle2PoolId(handle, &pool_id), "BufSurfaceToVideoFrameInfo(): failed", -1);
#else
  vbPool_t pool_id = 0;
  vbBlk_t handle = 0;
  CNDRV_SAFECALL(cnVBPhy2Handle(phy_addr, &handle), "BufSurfaceToVideoFrameInfo(): failed", -1);
  CNDRV_SAFECALL(cnVBHandle2PoolId(handle, &pool_id), "BufSurfaceToVideoFrameInfo(): failed", -1);
#endif

  memset(info, 0, sizeof(cnVideoFrameInfo_t));
  info->enModId = CN_ID_USER;
  info->u64PoolId = static_cast<uint64_t>(pool_id);
  info->stVFrame.u64PTS = surf->pts;
  info->stVFrame.enPixelFormat = GetMpsFmt(params->color_format);
  info->stVFrame.u32Width = params->width;
  info->stVFrame.u32Height = params->height;
  for (uint32_t i = 0; i < params->plane_params.num_planes; i++) {
    info->stVFrame.u32Stride[i] = params->plane_params.pitch[i];
    info->stVFrame.u64VirAddr[i] = vir_addr;
    info->stVFrame.u64PhyAddr[i] = phy_addr;
    vir_addr += params->plane_params.psize[i];
    phy_addr += params->plane_params.psize[i];
  }
  return 0;
}

}  // namespace cnedk
