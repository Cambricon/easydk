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

#include "cnedk_buf_surface_impl_system.h"

#include <cstdlib>  // for malloc/free
#include <cstring>  // for memset
#include <string>

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_buf_surface_utils.h"
#include "common/utils.hpp"

namespace cnedk {
int MemAllocatorSystem::Create(CnedkBufSurfaceCreateParams *params) {
  create_params_ = *params;
  uint32_t alignment = 4;
  if (create_params_.batch_size == 0) {
    create_params_.batch_size = 1;
  }
  if (params->force_align_1) {
    alignment = 1;
  }

  memset(&plane_params_, 0, sizeof(CnedkBufSurfacePlaneParams));
  block_size_ = params->size;

  if (block_size_) {
    if (create_params_.color_format == CNEDK_BUF_COLOR_FORMAT_INVALID) {
      create_params_.color_format = CNEDK_BUF_COLOR_FORMAT_GRAY8;
    }
    block_size_ = (block_size_ + alignment - 1) / alignment * alignment;
    memset(&plane_params_, 0, sizeof(plane_params_));
  } else {
    GetColorFormatInfo(params->color_format, params->width, params->height, alignment, alignment, &plane_params_);
    for (uint32_t i = 0; i < plane_params_.num_planes; i++) {
      block_size_ += plane_params_.psize[i];
    }
  }
  created_ = true;
  return 0;
}

int MemAllocatorSystem::Destroy() {
  created_ = false;
  return 0;
}

int MemAllocatorSystem::Alloc(CnedkBufSurface *surf) {
  void *addr = reinterpret_cast<void *>(malloc(block_size_));
  if (!addr) {
    LOG(ERROR) << "[EasyDK] [MemAllocatorSystem] Alloc(): malloc failed";
    return -1;
  }
  memset(surf, 0, sizeof(CnedkBufSurface));
  surf->mem_type = create_params_.mem_type;
  surf->opaque = nullptr;  // will be filled by MemPool
  surf->batch_size = create_params_.batch_size;
  surf->device_id = create_params_.device_id;
  surf->surface_list =
      reinterpret_cast<CnedkBufSurfaceParams *>(malloc(sizeof(CnedkBufSurfaceParams) * surf->batch_size));
  memset(surf->surface_list, 0, sizeof(CnedkBufSurfaceParams) * surf->batch_size);
  uint8_t *addr8 = reinterpret_cast<uint8_t *>(addr);
  for (uint32_t i = 0; i < surf->batch_size; i++) {
    surf->surface_list[i].color_format = create_params_.color_format;
    surf->surface_list[i].data_ptr = addr8;
    addr8 += block_size_;
    surf->surface_list[i].width = create_params_.width;
    surf->surface_list[i].height = create_params_.height;
    surf->surface_list[i].pitch = plane_params_.pitch[0];
    surf->surface_list[i].data_size = block_size_;
    surf->surface_list[i].plane_params = plane_params_;
  }
  return 0;
}

int MemAllocatorSystem::Free(CnedkBufSurface *surf) {
  void *addr = surf->surface_list[0].data_ptr;
  ::free(addr);
  ::free(reinterpret_cast<void *>(surf->surface_list));
  return 0;
}

}  // namespace cnedk
