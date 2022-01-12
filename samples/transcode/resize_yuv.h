/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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
#ifndef EDK_SAMPLES_RESIZE_YUV_H_
#define EDK_SAMPLES_RESIZE_YUV_H_

#ifdef HAVE_CNCV
#include "cncv.h"
#endif
#include "cnrt.h"

#include "easycodec/vformat.h"

#ifdef HAVE_CNCV
class CncvResizeYuv {
 public:
  explicit CncvResizeYuv(int dev_id);
  bool Init();
  bool Process(const edk::CnFrame &src, edk::CnFrame *dst);
  bool Destroy();
  ~CncvResizeYuv();

 private:
  int device_id_ = 0;
  cncvImageDescriptor src_desc_;
  cncvImageDescriptor dst_desc_;
  cnrtQueue_t queue_ = nullptr;
  cncvHandle_t handle_ = nullptr;

  cncvRect src_roi_;
  cncvRect dst_roi_;
  void** mlu_input_ = nullptr;
  void** mlu_output_ = nullptr;
  void** cpu_input_ = nullptr;
  void** cpu_output_ = nullptr;
  void* workspace_ = nullptr;
  size_t workspace_size_ = 0;
  bool initialized_ = false;
};  // class CncvResizeYuv
#endif  // HAVE_CNCV

#endif  // EDK_SAMPLES_RESIZE_YUV_H_
