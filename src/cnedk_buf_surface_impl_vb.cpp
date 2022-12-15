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

#include "cnedk_buf_surface_impl_vb.h"

#include <unistd.h>
#include <cstdlib>  // for malloc/free
#include <cstring>  // for memset
#include <string>
#include <thread>

#include "glog/logging.h"
#include "cnrt.h"
#if LIBMPS_VERSION_INT >= MPS_VERSION_1_1_0
#include "cn_vb.h"
#endif

#include "cnedk_buf_surface_utils.h"
#include "common/utils.hpp"

namespace cnedk {

#ifdef PLATFORM_CE3226

int MemAllocatorVb::Create(CnedkBufSurfaceCreateParams *params) {
  create_params_ = *params;
  // FIXME
  uint32_t alignment_w = 64;
  uint32_t alignment_h = 16;
  if (params->force_align_1) {
    alignment_w = alignment_h = 1;
  }

  if (create_params_.color_format == CNEDK_BUF_COLOR_FORMAT_INVALID) {
    create_params_.color_format = CNEDK_BUF_COLOR_FORMAT_GRAY8;
  }

  bool cached = create_params_.mem_type == CNEDK_BUF_MEM_VB_CACHED;
  memset(&plane_params_, 0, sizeof(CnedkBufSurfacePlaneParams));
  block_size_ = params->size;

  if (block_size_) {
    block_size_ = (block_size_ + alignment_w - 1) / alignment_w * alignment_w;
  } else {
    GetColorFormatInfo(params->color_format, params->width, params->height, alignment_w, alignment_h, &plane_params_);
    for (uint32_t i = 0; i < plane_params_.num_planes; i++) {
      block_size_ += plane_params_.psize[i];
    }
  }
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  cnrtVBPoolConfigs_t configs;
  memset(&configs, 0, sizeof(configs));
  configs.blkCnt = block_num_;
  configs.blkSize = block_size_ * create_params_.batch_size;
  configs.metaSize = 0;
  configs.vbUid = CNRT_VB_UID_USER;
  configs.mode = cached ? CNRT_VB_REMAP_CACHED : CNRT_VB_REMAP_NOCACHE;
  if (params->force_align_1) {
    configs.blkSize = (configs.blkSize + 63) / 64 * 64;
  }
  CNRT_SAFECALL(cnrtVBCreatePool(&configs, &pool_id_), "[MemAllocatorVb] Create(): failed", -1);
#else
  cnVbPoolCfg_t configs;
  memset(&configs, 0, sizeof(configs));
  configs.blkCnt = block_num_;
  configs.blkSize = block_size_ * create_params_.batch_size;
  configs.metaSize = 0;
  configs.vbUid = VB_ID_USER;
  configs.mode = cached ? VB_RMAP_MODE_CACHED : VB_RMAP_MODE_NOCACHE;
  if (params->force_align_1) {
    configs.blkSize = (configs.blkSize + 63) / 64 * 64;
  }
  CNDRV_SAFECALL(cnVBCreatePool(&configs, &pool_id_), "[MemAllocatorVb] Create(): failed", -1);
#endif
  return 0;
}

int MemAllocatorVb::Destroy() {
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  auto ret = cnrtVBDestroyPool(pool_id_);
#else
  auto ret = cnVBDestroyPool(pool_id_);
#endif
  if (ret != cnrtSuccess) {
    VLOG(3) << "[EasyDK] [MemAllocatorVb] Destroy(): Call cnrtVBDestroyPool failed, pool id = " << pool_id_
            << ", ret = " << ret;
    return -1;
  }

  // system("cat /proc/driver/cambricon/mlus/cabc\\:0328/cn_bmm");
  created_ = false;
  return 0;
}

int MemAllocatorVb::Alloc(CnedkBufSurface *surf) {
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  uint64_t vb_handle, phy_addr, vir_addr;
  auto ret = cnrtVBAllocBlock(pool_id_, &vb_handle, CNRT_VB_UID_USER, block_size_);
  if (ret != cnrtSuccess) {
    VLOG(3) << "[EasyDK] [MemAllocatorVb] Alloc(): Call cnrtVBAllocBlock failed, pool id = " << pool_id_
            << ", ret = " << ret;
    return -1;
  }

  ret = cnrtVBHandle2Phy(&phy_addr, vb_handle);
  if (ret != cnrtSuccess) {
    VLOG(3) << "[EasyDK] [MemAllocatorVb] Alloc(): Call cnrtVBHandle2Phy failed, ret = " << ret;
    cnrtVBFreeBlock(vb_handle, CNRT_VB_UID_USER);
    return -1;
  }

  ret = cnrtVBGetBlkUva(reinterpret_cast<void *>(phy_addr), reinterpret_cast<void **>(&vir_addr));
  if (ret != cnrtSuccess) {
    VLOG(3) << "[EasyDK] [MemAllocatorVb] Alloc(): Call cnrtVBGetBlkUva failed, ret = " << ret;
    cnrtVBFreeBlock(vb_handle, CNRT_VB_UID_USER);
    return -1;
  }
#else
  cnU64_t phy_addr, vir_addr;
  vbBlk_t vb_handle;
  auto ret = cnVBAllocBlock(pool_id_, &vb_handle, VB_ID_USER, block_size_);
  if (ret != CN_SUCCESS) {
    VLOG(3) << "[EasyDK] [MemAllocatorVb] Alloc(): Call cnrtVBAllocBlock failed, pool id = " << pool_id_
            << ", ret = " << ret;
    return -1;
  }

  ret = cnVBHandle2Phy(&phy_addr, vb_handle);
  if (ret != CN_SUCCESS) {
    VLOG(3) << "[EasyDK] [MemAllocatorVb] Alloc(): Call cnrtVBHandle2Phy failed, ret = " << ret;
    cnVBFreeBlock(vb_handle, VB_ID_USER);
    return -1;
  }

  ret = cnVBGetBlkUva(reinterpret_cast<void *>(phy_addr), reinterpret_cast<void **>(&vir_addr));
  if (ret != CN_SUCCESS) {
    VLOG(3) << "[EasyDK] [MemAllocatorVb] Alloc(): Call cnrtVBGetBlkUva failed, ret = " << ret;
    cnVBFreeBlock(vb_handle, VB_ID_USER);
    return -1;
  }
#endif

  memset(surf, 0, sizeof(CnedkBufSurface));
  surf->mem_type = create_params_.mem_type;
  surf->opaque = nullptr;  // will be filled by MemPool
  surf->batch_size = create_params_.batch_size;
  surf->device_id = create_params_.device_id;
  surf->surface_list =
      reinterpret_cast<CnedkBufSurfaceParams *>(malloc(sizeof(CnedkBufSurfaceParams) * surf->batch_size));
  memset(surf->surface_list, 0, sizeof(CnedkBufSurfaceParams) * surf->batch_size);

  uint64_t iova = static_cast<uint64_t>(phy_addr);
  uint64_t uva = static_cast<uint64_t>(vir_addr);
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

int MemAllocatorVb::Free(CnedkBufSurface *surf) {
  uint64_t phy_addr, vir_addr;
  phy_addr = reinterpret_cast<uint64_t>(surf->surface_list[0].data_ptr);
  vir_addr = reinterpret_cast<uint64_t>(surf->surface_list[0].mapped_data_ptr);
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  uint64_t vb_handle;
  cnrtVBPhy2Handle(phy_addr, &vb_handle);
  cnrtVBPutBlkUva(reinterpret_cast<void *>(phy_addr), reinterpret_cast<void *>(vir_addr));
#else
  vbBlk_t vb_handle;
  cnVBPhy2Handle(phy_addr, &vb_handle);
  cnVBPutBlkUva(reinterpret_cast<void *>(phy_addr), reinterpret_cast<void *>(vir_addr));
#endif

  while (1) {
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
    cnrtRet_t ret = cnrtVBFreeBlock(vb_handle, CNRT_VB_UID_USER);
    if (ret == cnrtSuccess) break;
    if (ret == cnrtErrorCndrvFuncCall) {
      usleep(1000);
      continue;  // FIXME
    }
#else
    auto ret = cnVBFreeBlock(vb_handle, VB_ID_USER);
    if (ret == CN_SUCCESS) break;
    usleep(1000);
    continue;  // FIXME
#endif
    LOG(ERROR) << "[EasyDK] [MemAllocatorVb] Free(): cnrtVBFreeBlock failed, ret = " << ret;
    return -1;
  }
  ::free(reinterpret_cast<void *>(surf->surface_list));
  return 0;
}
#endif

}  // namespace cnedk
