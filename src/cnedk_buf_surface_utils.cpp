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

#include "cnedk_buf_surface_utils.h"

#include <cstring>  // for memset
#include <map>
#include <mutex>
#include <string>

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_platform.h"

namespace cnedk {

int GetColorFormatInfo(CnedkBufSurfaceColorFormat fmt, uint32_t width, uint32_t height, uint32_t align_size_w,
                       uint32_t align_size_h, CnedkBufSurfacePlaneParams *params) {
  memset(params, 0, sizeof(CnedkBufSurfacePlaneParams));
  switch (fmt) {
    case CNEDK_BUF_COLOR_FORMAT_GRAY8:
      params->num_planes = 1;
      params->width[0] = width;
      params->height[0] = height;
      params->bytes_per_pix[0] = 1;
      params->pitch[0] = (width * params->bytes_per_pix[0] + align_size_w - 1) / align_size_w * align_size_w;
      params->psize[0] = params->pitch[0] * ((params->height[0] + align_size_h - 1) / align_size_h * align_size_h);
      params->offset[0] = 0;
      return 0;

    case CNEDK_BUF_COLOR_FORMAT_YUV420:
      params->num_planes = 3;
      for (uint32_t i = 0; i < params->num_planes; i++) {
        params->width[i] = i == 0 ? width : width / 2;
        params->height[i] = i == 0 ? height : height / 2;
        params->bytes_per_pix[i] = 1;
        params->pitch[i] =
            (params->width[i] * params->bytes_per_pix[i] + align_size_w - 1) / align_size_w * align_size_w;
        params->psize[i] = params->pitch[i] * ((params->height[i] + align_size_h - 1) / align_size_h * align_size_h);
      }
      params->offset[0] = 0;
      params->offset[1] = params->psize[0];
      params->offset[2] = params->psize[0] + params->psize[1];
      return 0;
    case CNEDK_BUF_COLOR_FORMAT_NV12:
    case CNEDK_BUF_COLOR_FORMAT_NV21:
      params->num_planes = 2;
      for (uint32_t i = 0; i < params->num_planes; i++) {
        params->width[i] = width;
        params->height[i] = (i == 0) ? height : height / 2;
        params->bytes_per_pix[i] = 1;
        params->pitch[i] =
            (params->width[i] * params->bytes_per_pix[i] + align_size_w - 1) / align_size_w * align_size_w;
        params->psize[i] = params->pitch[i] * ((params->height[i] + align_size_h - 1) / align_size_h * align_size_h);
      }
      params->offset[0] = 0;
      params->offset[1] = params->psize[0];
      return 0;
    case CNEDK_BUF_COLOR_FORMAT_ARGB:
    case CNEDK_BUF_COLOR_FORMAT_ABGR:
    case CNEDK_BUF_COLOR_FORMAT_BGRA:
    case CNEDK_BUF_COLOR_FORMAT_RGBA:
      params->num_planes = 1;
      params->width[0] = width;
      params->height[0] = height;
      params->bytes_per_pix[0] = 4;
      params->pitch[0] = (width * params->bytes_per_pix[0] + align_size_w - 1) / align_size_w * align_size_w;
      params->psize[0] = params->pitch[0] * ((params->height[0] + align_size_h - 1) / align_size_h * align_size_h);
      params->offset[0] = 0;
      return 0;
    case CNEDK_BUF_COLOR_FORMAT_RGB:
    case CNEDK_BUF_COLOR_FORMAT_BGR:
      params->num_planes = 1;
      params->width[0] = width;
      params->height[0] = height;
      params->bytes_per_pix[0] = 3;
      params->pitch[0] = (width * params->bytes_per_pix[0] + align_size_w - 1) / align_size_w * align_size_w;
      params->offset[0] = 0;
      params->psize[0] = params->pitch[0] * ((params->height[0] + align_size_h - 1) / align_size_h * align_size_h);
      return 0;
    case CNEDK_BUF_COLOR_FORMAT_ARGB1555:
      params->num_planes = 1;
      params->width[0] = width;
      params->height[0] = height;
      params->bytes_per_pix[0] = 2;
      params->pitch[0] = (width * params->bytes_per_pix[0] + align_size_w - 1) / align_size_w * align_size_w;
      params->psize[0] = params->pitch[0] * ((params->height[0] + align_size_h - 1) / align_size_h * align_size_h);
      params->offset[0] = 0;
      return 0;
    case CNEDK_BUF_COLOR_FORMAT_INVALID:
    default: {
      LOG(ERROR) << "[EasyDK] GetColorFormatInfo(): Unsupported color format: " << fmt;
      return -1;
    }
  }  // switch
}

int CheckParams(CnedkBufSurfaceCreateParams *params) {
  if (params->batch_size == 0) {
    LOG(ERROR) << "[EasyDK] CheckParams(): Invalid batch_size = " << params->batch_size;
    return -1;
  }

  if (params->mem_type != CNEDK_BUF_MEM_SYSTEM) {
    if (params->mem_type < CNEDK_BUF_MEM_DEFAULT || params->mem_type > CNEDK_BUF_MEM_SYSTEM) {
      LOG(ERROR) << "[EasyDK] CheckParams(): Unsupported memory type: " << params->mem_type;
      return -1;
    }
    CnedkPlatformInfo info;
    if (CnedkPlatformGetInfo(params->device_id, &info) < 0) {
      LOG(ERROR) << "[EasyDK] CheckParams(): Get platform information failed";
      return -1;
    }
    // At this moment, CExxxx == supportUnified, MLUxxx == not supportUnified
    //   FIXME later.
    if (info.support_unified_addr) {
      // CExxxx
      if (params->mem_type != CNEDK_BUF_MEM_DEFAULT && params->mem_type != CNEDK_BUF_MEM_DEVICE &&
          params->mem_type != CNEDK_BUF_MEM_UNIFIED && params->mem_type != CNEDK_BUF_MEM_UNIFIED_CACHED &&
          params->mem_type != CNEDK_BUF_MEM_VB && params->mem_type != CNEDK_BUF_MEM_VB_CACHED) {
        LOG(ERROR) << "[EasyDK] CheckParams(): For support unified address, unsupported memory type: "
                   << params->mem_type;
        return -1;
      }
    } else {
      // MLUxxx
      if (params->mem_type != CNEDK_BUF_MEM_DEFAULT && params->mem_type != CNEDK_BUF_MEM_DEVICE &&
          params->mem_type != CNEDK_BUF_MEM_PINNED) {
        LOG(ERROR) << "[EasyDK] CheckParams(): For not support unified address, unsupported memory type: "
                   << params->mem_type;
        return -1;
      }
      if (params->mem_type == CNEDK_BUF_MEM_PINNED && info.can_map_host_memory == false) {
        LOG(ERROR) << "[EasyDK] CheckParams(): For memory type CNEDK_BUF_MEM_PINNED, map host memory must be supported";
        return -1;
      }
    }
    return 0;
  }

  if (params->color_format < CNEDK_BUF_COLOR_FORMAT_INVALID || params->color_format >= CNEDK_BUF_COLOR_FORMAT_LAST) {
    LOG(ERROR) << "[EasyDK] CheckParams(): Unknown color format: " << params->color_format;
    return -1;
  }

  if (params->width * params->height == 0 && params->size == 0) {
    LOG(ERROR) << "[EasyDK] CheckParams(): Invalid width, height or size. w = " << params->width
               << ", h = " << params->height << ", size = " << params->size;
    return -1;
  }

  return 0;
}

}  // namespace cnedk
