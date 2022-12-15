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
#ifndef SAMPLE_ENCODE_MLU_HANDLER_HPP_
#define SAMPLE_ENCODE_MLU_HANDLER_HPP_

#include <map>
#include <memory>
#include <mutex>
#include <future>
#include <string>

#include "cnedk_encode.h"
#include "edk_frame.hpp"

struct VEncHandlerParam {
  uint32_t width;
  uint32_t height;
  int bitrate;
  int gop_size;
  CnedkVencType codec_type = CNEDK_VENC_TYPE_H264;
  double frame_rate = 30;
  std::string filename;
};

class VencMluHandler {
 public:
  explicit VencMluHandler(int dev_id_);
  ~VencMluHandler();

 public:
  int SendFrame(std::shared_ptr<EdkFrame> data);

  void SetParams(const VEncHandlerParam &param) { param_ = param;}

  static int OnFrameBits_(CnedkVEncFrameBits *framebits, void *userdata) {
    VencMluHandler *thiz = reinterpret_cast<VencMluHandler *>(userdata);
    return thiz->OnFrameBits(framebits);
  }
  static int OnEos_(void *userdata) {
    VencMluHandler *thiz = reinterpret_cast<VencMluHandler *>(userdata);
    return thiz->OnEos();
  }
  static int OnError_(int errcode, void *userdata) {
    VencMluHandler *thiz = reinterpret_cast<VencMluHandler *>(userdata);
    return thiz->OnError(errcode);
  }

 private:
  int OnEos();
  int OnError(int errcode);
  int InitEncode(int width, int height, CnedkBufSurfaceColorFormat color_format);
  int OnFrameBits(CnedkVEncFrameBits *framebits);

 private:
  std::unique_ptr<std::promise<void>> eos_promise_;
  void *venc_handle_ = nullptr;
  std::mutex mutex_;
  int dev_id_;
  std::string platform_;
  VEncHandlerParam param_;

  FILE *p_output_file_ = nullptr;
  int stream_id_;
};

#endif

