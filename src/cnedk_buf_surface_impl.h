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

#ifndef CNEDK_BUF_SURFACE_IMPL_H_
#define CNEDK_BUF_SURFACE_IMPL_H_

#include <string>
#include <mutex>
#include <queue>

#include "cnedk_buf_surface.h"
#include "cnedk_buf_surface_utils.h"

namespace cnedk {

class IMemAllcator {
 public:
  virtual ~IMemAllcator() {}
  virtual int Create(CnedkBufSurfaceCreateParams *params) = 0;
  virtual int Destroy() = 0;
  virtual int Alloc(CnedkBufSurface *surf) = 0;
  virtual int Free(CnedkBufSurface *surf) = 0;
};

IMemAllcator *CreateMemAllocator(CnedkBufSurfaceMemType mem_type, uint32_t block_num);

class MemPool {
 public:
  MemPool() = default;
  ~MemPool() {
    if (allocator_) delete allocator_, allocator_ = nullptr;
  }
  int Create(CnedkBufSurfaceCreateParams *params, uint32_t block_num);
  int Destroy();
  int Alloc(CnedkBufSurface *surf);
  int Free(CnedkBufSurface *surf);

 private:
  std::mutex mutex_;
  std::queue<CnedkBufSurface> cache_;

  bool created_ = false;
  int device_id_ = 0;
  uint32_t alloc_count_ = 0;
  IMemAllcator *allocator_ = nullptr;
  bool is_vb_pool_ = false;
  bool is_fake_mapped_ = false;
};

//  for non-pool case
int CreateSurface(CnedkBufSurfaceCreateParams *params, CnedkBufSurface *surf);
int DestroySurface(CnedkBufSurface *surf);

}  // namespace cnedk

#endif  // CNEDK_BUF_SURFACE_IMPL_H_
