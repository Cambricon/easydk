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

#include "cnedk_buf_surface_impl_unified.h"

#include <cstdlib>  // for malloc/free
#include <cstring>  // for memset
#include <string>

#include "mps_config.h"
#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_buf_surface_utils.h"
#include "common/utils.hpp"

#if LIBMPS_VERSION_INT >= MPS_VERSION_1_1_0
#include "cn_sys.h"
#endif

namespace cnedk {

#ifdef PLATFORM_CE3226

int MemAllocatorUnified::Create(CnedkBufSurfaceCreateParams *params) {
  create_params_ = *params;

  if (create_params_.batch_size == 0) {
    create_params_.batch_size = 1;
  }
  // FIXME
  uint32_t alignment_w = 64;
  uint32_t alignment_h = 16;
  if (params->force_align_1) {
    alignment_w = alignment_h = 1;
  }

  memset(&plane_params_, 0, sizeof(CnedkBufSurfacePlaneParams));
  block_size_ = params->size;

  if (!block_size_) {
    GetColorFormatInfo(params->color_format, params->width, params->height, alignment_w, alignment_h, &plane_params_);
    for (uint32_t i = 0; i < plane_params_.num_planes; i++) {
      block_size_ += plane_params_.psize[i];
    }
  } else {
    if (create_params_.color_format == CNEDK_BUF_COLOR_FORMAT_INVALID) {
      create_params_.color_format = CNEDK_BUF_COLOR_FORMAT_GRAY8;
    }
    block_size_ = (block_size_ + alignment_w - 1) / alignment_w * alignment_w;
    memset(&plane_params_, 0, sizeof(plane_params_));
  }
  created_ = true;
  return 0;
}

int MemAllocatorUnified::Destroy() {
  created_ = false;
  return 0;
}

int MemAllocatorUnified::Alloc(CnedkBufSurface *surf) {
  void *phy_addr, *vir_addr;
  bool cached = create_params_.mem_type == CNEDK_BUF_MEM_UNIFIED_CACHED;
  if (cached) {
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
    CNRT_SAFECALL(cnrtMallocExtCached(&phy_addr, &vir_addr, "MempoolUnifiedCached", nullptr,
                                      block_size_ * create_params_.batch_size),
                  "[MemAllocatorUnified] Alloc(): failed", -1);
#else
    CNDRV_SAFECALL(cnsysMallocExtCached(&phy_addr, &vir_addr, "MempoolUnifiedCached", nullptr,
                                        block_size_ * create_params_.batch_size),
                  "[MemAllocatorUnified] Alloc(): failed", -1);
#endif

  } else {
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
    CNRT_SAFECALL(cnrtMallocExt(&phy_addr, &vir_addr, "MempoolUnified", nullptr,
                                block_size_ * create_params_.batch_size),
                  "[MemAllocatorUnified] Alloc(): failed", -1);
#else
    CNDRV_SAFECALL(cnsysMallocExt(&phy_addr, &vir_addr, "MempoolUnified", nullptr,
                                  block_size_ * create_params_.batch_size),
                  "[MemAllocatorUnified] Alloc(): failed", -1);
#endif
  }

  memset(surf, 0, sizeof(CnedkBufSurface));
  surf->mem_type = create_params_.mem_type;
  surf->opaque = nullptr;  // will be filled by MemPool
  surf->batch_size = create_params_.batch_size;
  surf->device_id = create_params_.device_id;
  surf->surface_list =
      reinterpret_cast<CnedkBufSurfaceParams *>(malloc(sizeof(CnedkBufSurfaceParams) * surf->batch_size));
  memset(surf->surface_list, 0, sizeof(CnedkBufSurfaceParams) * surf->batch_size);

  uint64_t iova = reinterpret_cast<uint64_t>(phy_addr);
  uint64_t uva = reinterpret_cast<uint64_t>(vir_addr);
  for (uint32_t i = 0; i < surf->batch_size; i++) {
    surf->surface_list[i].color_format = create_params_.color_format;
    surf->surface_list[i].data_ptr = reinterpret_cast<void *>(iova);
    surf->surface_list[i].mapped_data_ptr = reinterpret_cast<void *>(uva);
    surf->surface_list[i].width = create_params_.width;
    surf->surface_list[i].height = create_params_.height;
    surf->surface_list[i].pitch = plane_params_.pitch[0];
    surf->surface_list[i].data_size = block_size_;
    surf->surface_list[i].plane_params = plane_params_;
    iova += block_size_;
    uva += block_size_;
  }
  return 0;
}

int MemAllocatorUnified::Free(CnedkBufSurface *surf) {
  void *phy_addr = surf->surface_list[0].data_ptr;
  void *vir_addr = surf->surface_list[0].mapped_data_ptr;
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  cnrtMunmap(vir_addr, surf->surface_list[0].data_size * surf->batch_size);
  cnrtFree(phy_addr);
#else
  cnsysMunmap(vir_addr, surf->surface_list[0].data_size * surf->batch_size);
  cnsysFree(phy_addr);
#endif
  ::free(reinterpret_cast<void *>(surf->surface_list));
  return 0;
}

#endif

}  // namespace cnedk
