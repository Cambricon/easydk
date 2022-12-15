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

#include "cnedk_vin_capture_impl_ce3226.hpp"

#include <cstring>  // for memset

#include "glog/logging.h"
#include "cnrt.h"

namespace cnedk {

int VinCaptureCe3226::Create(CnedkVinCaptureCreateParams *params) {
  create_params_ = *params;
  return 0;
}

int VinCaptureCe3226::Destroy() { return 0; }

int VinCaptureCe3226::Capture(int timeout_ms) {
  cnVideoFrameInfo_t input;
  if (MpsService::Instance().VinCaptureFrame(create_params_.sensor_id, &input, timeout_ms) < 0) {
    LOG(ERROR) << "[EasyDK] [VinCaptureCe3226] Capture(): Capture frame failed";
    create_params_.OnError(-1, create_params_.userdata);
    return -1;
  }

  CnedkBufSurface *surf = nullptr;
  if (create_params_.GetBufSurf(&surf, create_params_.surf_timeout_ms, create_params_.userdata) < 0) {
    LOG(ERROR) << "[EasyDK] [VinCaptureCe3226] Capture(): Get BufSurface failed";
    MpsService::Instance().VinCaptureFrameRelease(create_params_.sensor_id, &input);
    create_params_.OnError(-2, create_params_.userdata);
    return -1;
  }

  cnVideoFrameInfo_t output;
  if (BufSurfaceToVideoFrameInfo(surf, &output) < 0) {
    LOG(ERROR) << "[EasyDK] [VinCaptureCe3226] Capture(): Convert BufSurface to VideoFrameInfo failed";
    return -1;
  }
  // MpsService::Instance().VguScaleCsc(&input, &output);

  // hardcode at the moment, NV12
  cnrtMemcpy2D(reinterpret_cast<void *>(output.stVFrame.u64PhyAddr[0]), output.stVFrame.u32Stride[0],
               reinterpret_cast<void *>(input.stVFrame.u64PhyAddr[0]), input.stVFrame.u32Stride[0],
               output.stVFrame.u32Width, output.stVFrame.u32Height, cnrtMemcpyDevToDev);
  cnrtMemcpy2D(reinterpret_cast<void *>(output.stVFrame.u64PhyAddr[1]), output.stVFrame.u32Stride[1],
               reinterpret_cast<void *>(input.stVFrame.u64PhyAddr[1]), input.stVFrame.u32Stride[1],
               output.stVFrame.u32Width, output.stVFrame.u32Height / 2, cnrtMemcpyDevToDev);

  MpsService::Instance().VinCaptureFrameRelease(create_params_.sensor_id, &input);
  create_params_.OnFrame(surf, create_params_.userdata);
  return 0;
}

}  // namespace cnedk
