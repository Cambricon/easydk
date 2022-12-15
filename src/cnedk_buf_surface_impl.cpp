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

#include "cnedk_buf_surface_impl.h"

#include <string>
#include <thread>

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_buf_surface_impl_device.h"
#include "cnedk_buf_surface_impl_system.h"
#ifdef PLATFORM_CE3226
#include "cnedk_buf_surface_impl_unified.h"
#include "cnedk_buf_surface_impl_vb.h"
#endif
#include "cnedk_platform.h"

namespace cnedk {

int MemPool::Create(CnedkBufSurfaceCreateParams *params, uint32_t block_num) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (created_) {
    LOG(ERROR) << "[EasyDK] [MemPool] Create(): Pool has been created";
    return -1;
  }

  if (CheckParams(params) < 0) {
    LOG(ERROR) << "[EasyDK] [MemPool] Create(): Parameters are invalid";
    return -1;
  }
  device_id_ = params->device_id;

  cnrtSetDevice(device_id_);

  CnedkPlatformInfo info;
  if (CnedkPlatformGetInfo(device_id_, &info) < 0) {
    LOG(ERROR) << "[EasyDK] [MemPool] Create(): Get Platfrom information failed";
    return -1;
  }

  if (params->mem_type == CNEDK_BUF_MEM_DEFAULT) {
    if (info.support_unified_addr)
      params->mem_type = CNEDK_BUF_MEM_UNIFIED;
    else
      params->mem_type = CNEDK_BUF_MEM_DEVICE;
  }

  allocator_ = CreateMemAllocator(params->mem_type, block_num);
  if (!allocator_) {
    LOG(ERROR) << "[EasyDK] [MemPool] Create(): Create memory allocator pointer failed";
    return -1;
  }

  if (allocator_->Create(params) < 0) {
    LOG(ERROR) << "[EasyDK] [MemPool] Create(): Memory allocator initialize resources failed";
    return -1;
  }

  is_fake_mapped_ = (params->mem_type == CNEDK_BUF_MEM_DEVICE);
  is_vb_pool_ = (params->mem_type == CNEDK_BUF_MEM_VB || params->mem_type == CNEDK_BUF_MEM_VB_CACHED);
  if (!is_vb_pool_) {
    // cache the blocks
    for (uint32_t i = 0; i < block_num; i++) {
      CnedkBufSurface surf;
      if (allocator_->Alloc(&surf) < 0) {
        LOG(ERROR) << "[EasyDK] [MemPool] Create(): Memory allocator alloc BufSurface failed";
        return -1;
      }
      surf.opaque = reinterpret_cast<void *>(this);
      cache_.push(surf);
    }
  }

  alloc_count_ = 0;
  created_ = true;
  return 0;
}

int MemPool::Destroy() {
  std::unique_lock<std::mutex> lk(mutex_);
  if (!created_) {
    LOG(ERROR) << "[EasyDK] [MemPool] Destroy(): Memory pool is not created";
    return -1;
  }

  cnrtSetDevice(device_id_);

  if (!is_vb_pool_) {
    while (alloc_count_) {
      lk.unlock();
      std::this_thread::yield();
      lk.lock();
    }
    while (!cache_.empty()) {
      auto surf = cache_.front();
      cache_.pop();
      allocator_->Free(&surf);
    }
  }

  // FIXME
  if (allocator_->Destroy() < 0) {
    VLOG(3) << "[EasyDK] [MemPool] Destroy(): Destroy memory allocator failed";
    return -1;
  }
  delete allocator_, allocator_ = nullptr;

  alloc_count_ = 0;
  created_ = false;
  return 0;
}

int MemPool::Alloc(CnedkBufSurface *surf) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (!created_) {
    LOG(ERROR) << "[EasyDK] [MemPool] Alloc(): Memory pool is not created";
    return -1;
  }

  cnrtSetDevice(device_id_);
  if (is_vb_pool_) {
    if (allocator_->Alloc(surf) < 0) {
      VLOG(4) << "[EasyDK] [MemPool] Alloc(): Memory allocator alloc BufSurface failed";
      return -1;
    }
    surf->opaque = reinterpret_cast<void *>(this);
    return 0;
  }

  if (cache_.empty()) {
    VLOG(4) << "[EasyDK] [MemPool] Alloc(): Memory cache is empty";
    return -1;
  }

  *surf = cache_.front();
  cache_.pop();

  ++alloc_count_;
  return 0;
}

int MemPool::Free(CnedkBufSurface *surf) {
  std::unique_lock<std::mutex> lk(mutex_);
  if (!created_) {
    LOG(ERROR) << "[EasyDK] [MemPool] Free(): Memory pool is not created";
    return -1;
  }

  cnrtSetDevice(device_id_);
  if (is_vb_pool_) {
    allocator_->Free(surf);
    return 0;
  }

  if (is_fake_mapped_) {
    // reset mapped_data_ptr to zero
    for (size_t i = 0; i < surf->batch_size; i++) surf->surface_list[i].mapped_data_ptr = nullptr;
  }
  cache_.push(*surf);
  --alloc_count_;
  return 0;
}

//
IMemAllcator *CreateMemAllocator(CnedkBufSurfaceMemType mem_type, uint32_t block_num) {
  if (mem_type == CNEDK_BUF_MEM_VB || mem_type == CNEDK_BUF_MEM_VB_CACHED) {
#ifdef PLATFORM_CE3226
    return new MemAllocatorVb(block_num);
#else
    LOG(ERROR) << "[EasyDK] CreateMemAllocator(): Unsupported memory type: " << mem_type;
    return nullptr;
#endif
  } else if (mem_type == CNEDK_BUF_MEM_UNIFIED || mem_type == CNEDK_BUF_MEM_UNIFIED_CACHED) {
#ifdef PLATFORM_CE3226
    return new MemAllocatorUnified();
#else
    LOG(ERROR) << "[EasyDK] CreateMemAllocator(): Unsupported memory type: " << mem_type;
    return nullptr;
#endif
  } else if (mem_type == CNEDK_BUF_MEM_SYSTEM) {
    return new MemAllocatorSystem();
  } else if (mem_type == CNEDK_BUF_MEM_DEVICE) {
    return new MemAllocatorDevice();
  }
  LOG(ERROR) << "[EasyDK] CreateMemAllocator(): Unsupported memory type: " << mem_type;
  return nullptr;
}

// for non-pool case
int CreateSurface(CnedkBufSurfaceCreateParams *params, CnedkBufSurface *surf) {
  if (CheckParams(params) < 0) {
    LOG(ERROR) << "[EasyDK] CreateSurface(): Parameters are invalid";
    return -1;
  }
  if (params->mem_type == CNEDK_BUF_MEM_VB || params->mem_type == CNEDK_BUF_MEM_VB_CACHED) {
    LOG(ERROR) << "[EasyDK] CreateSurface(): Unsupported memory type: " << params->mem_type;
    return -1;
  }

  CnedkPlatformInfo info;
  if (CnedkPlatformGetInfo(params->device_id, &info) < 0) {
    LOG(ERROR) << "[EasyDK] CreateSurface(): Get platform information failed";
    return -1;
  }

  if (params->mem_type == CNEDK_BUF_MEM_DEFAULT) {
    if (info.support_unified_addr)
      params->mem_type = CNEDK_BUF_MEM_UNIFIED;
    else
      params->mem_type = CNEDK_BUF_MEM_DEVICE;
  }

  if (params->mem_type == CNEDK_BUF_MEM_UNIFIED || params->mem_type == CNEDK_BUF_MEM_UNIFIED_CACHED) {
#ifdef PLATFORM_CE3226
    MemAllocatorUnified allocator;
    if (allocator.Create(params) < 0) {
      LOG(ERROR) << "[EasyDK] CreateSurface(): Memory allocator initialize resources failed. mem_type = "
                 << params->mem_type;
      return -1;
    }
    if (allocator.Alloc(surf) < 0) {
      LOG(ERROR) << "[EasyDK] CreateSurface(): Memory allocator create BufSurface failed. mem_type = "
                 << params->mem_type;
      return -1;
    }
#else
    LOG(ERROR) << "[EasyDK] CreateSurface(): Unsupported memory type: " << params->mem_type;
    return -1;
#endif
    return 0;
  }

  if (params->mem_type == CNEDK_BUF_MEM_SYSTEM) {
    MemAllocatorSystem allocator;
    if (allocator.Create(params) < 0) {
      LOG(ERROR) << "[EasyDK] CreateSurface(): Memory allocator initialize resources failed. mem_type = "
                 << params->mem_type;
      return -1;
    }
    if (allocator.Alloc(surf) < 0) {
      LOG(ERROR) << "[EasyDK] CreateSurface(): Memory allocator create BufSurface failed. mem_type = "
                 << params->mem_type;
      return -1;
    }
    return 0;
  }

  if (params->mem_type == CNEDK_BUF_MEM_DEVICE) {
    MemAllocatorDevice allocator;
    if (allocator.Create(params) < 0) {
      LOG(ERROR) << "[EasyDK] CreateSurface(): Memory allocator initialize resources failed. mem_type = "
                 << params->mem_type;
      return -1;
    }
    if (allocator.Alloc(surf) < 0) {
      LOG(ERROR) << "[EasyDK] CreateSurface(): Memory allocator create BufSurface failed. mem_type = "
                 << params->mem_type;
      return -1;
    }
    return 0;
  }

  LOG(ERROR) << "[EasyDK] CreateSurface(): Unsupported memory type: " << params->mem_type;
  return -1;
}

int DestroySurface(CnedkBufSurface *surf) {
  // FIXME, no resource leaks at the moment
  //   the codes will be refined in the future.
  if (surf->mem_type == CNEDK_BUF_MEM_UNIFIED || surf->mem_type == CNEDK_BUF_MEM_UNIFIED_CACHED) {
#ifdef PLATFORM_CE3226
    MemAllocatorUnified allocator;
    if (allocator.Free(surf) < 0) {
      LOG(ERROR) << "[EasyDK] DestroySurface(): Memory allocator destroy BufSurface failed. mem_type = "
                 << surf->mem_type;
      return -1;
    }
#else
    LOG(ERROR) << "[EasyDK] DestroySurface(): Unsupported memory type: " << surf->mem_type;
    return -1;
#endif
    return 0;
  }

  if (surf->mem_type == CNEDK_BUF_MEM_SYSTEM) {
    MemAllocatorSystem allocator;
    if (allocator.Free(surf) < 0) {
      LOG(ERROR) << "[EasyDK] DestroySurface(): Memory allocator free BufSurface failed. mem_type = "
                 << surf->mem_type;
      return -1;
    }
    if (allocator.Destroy() < 0) {
      LOG(ERROR) << "[EasyDK] DestroySurface(): Memory allocator destroy BufSurface failed. mem_type = "
                 << surf->mem_type;
      return -1;
    }
    return 0;
  }

  if (surf->mem_type == CNEDK_BUF_MEM_DEVICE) {
    MemAllocatorDevice allocator;
    if (allocator.Free(surf) < 0) {
      LOG(ERROR) << "[EasyDK] DestroySurface(): Memory allocator destroy BufSurface failed. mem_type = "
                 << surf->mem_type;
      return -1;
    }
    return 0;
  }

  LOG(ERROR) << "[EasyDK] DestroySurface(): Unsupported memory type: " << surf->mem_type;
  return -1;
}

}  // namespace cnedk
