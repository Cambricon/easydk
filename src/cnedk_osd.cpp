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

#include "cnedk_osd.h"

#include "glog/logging.h"
#include "common/utils.hpp"

#ifdef PLATFORM_CE3226
#include "ce3226/cnedk_osd_impl_ce3226.hpp"
#endif

#ifdef __cplusplus
extern "C" {
#endif

int CnedkDrawRect(CnedkBufSurface *surf, CnedkOsdRectParams *params, uint32_t num) {
  if (surf->mem_type != CNEDK_BUF_MEM_VB && surf->mem_type != CNEDK_BUF_MEM_VB_CACHED) {
    LOG(ERROR) << "[EasyDK] CnedkDrawRect(): Unsupported memory type: " << surf->mem_type;
    return -1;
  }
#ifdef PLATFORM_CE3226
  return cnedk::DrawRectCe3226(surf, params, num);
#endif
  return -1;
}

int CnedkFillRect(CnedkBufSurface *surf, CnedkOsdRectParams *params, uint32_t num) {
  if (surf->mem_type != CNEDK_BUF_MEM_VB && surf->mem_type != CNEDK_BUF_MEM_VB_CACHED) {
    LOG(ERROR) << "[EasyDK] CnedkFillRect(): Unsupported memory type: " << surf->mem_type;
    return -1;
  }
#ifdef PLATFORM_CE3226
  return cnedk::FillRectCe3226(surf, params, num);
#endif
  return -1;
}

int CnedkDrawBitmap(CnedkBufSurface *surf, CnedkOsdBitmapParams *params, uint32_t num) {
  if (surf->mem_type != CNEDK_BUF_MEM_VB && surf->mem_type != CNEDK_BUF_MEM_VB_CACHED) {
    LOG(ERROR) << "[EasyDK] CnedkDrawBitmap(): Unsupported memory type: " << surf->mem_type;
    return -1;
  }
#ifdef PLATFORM_CE3226
  return cnedk::DrawBitmapCe3226(surf, params, num);
#endif
  return -1;
}

#ifdef __cplusplus
}
#endif
