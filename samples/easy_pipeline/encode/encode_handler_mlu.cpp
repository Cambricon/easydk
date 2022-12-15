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

#include <map>
#include <memory>
#include <string>

#include "glog/logging.h"
#include "cnedk_platform.h"

#include "encode_handler_mlu.hpp"

VencMluHandler::VencMluHandler(int dev_id) { dev_id_ = dev_id; }
VencMluHandler::~VencMluHandler() {
  std::unique_lock<std::mutex> guard(mutex_);
  if (!eos_promise_) {
    if (CnedkVencSendFrame(venc_handle_, nullptr, 2000) < 0) {  // send eos
      LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] CnedkVencSend Eos error";
    } else {
      eos_promise_.reset(new std::promise<void>);
    }
  }
  if (venc_handle_) {
    CnedkVencDestroy(venc_handle_);
    venc_handle_ = nullptr;
  }
  if (eos_promise_) {
    eos_promise_->get_future().wait();
    eos_promise_.reset(nullptr);
  }
}

int VencMluHandler::InitEncode(int width, int height, CnedkBufSurfaceColorFormat color_format) {
  CnedkVencCreateParams params;
  memset(&params, 0, sizeof(params));
  params.type = param_.codec_type;
  params.device_id = dev_id_;
  params.width = width;
  params.height = height;

  params.color_format = color_format;
  params.frame_rate = param_.frame_rate;
  params.key_interval = 0;  // not used by CE3226
  params.input_buf_num = 3;  // not used by CE3226
  params.gop_size = param_.gop_size;
  params.bitrate = param_.bitrate;
  params.OnFrameBits = VencMluHandler::OnFrameBits_;
  params.OnEos = VencMluHandler::OnEos_;
  params.OnError = VencMluHandler::OnError_;
  params.userdata = this;
  int ret = CnedkVencCreate(&venc_handle_, &params);
  if (ret < 0) {
    LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] CnedkVencCreate failed";
    return -1;
  }
  return 0;
}

int VencMluHandler::SendFrame(std::shared_ptr<EdkFrame> data) {
  if (!data || data->is_eos) {
    if (CnedkVencSendFrame(venc_handle_, nullptr, 2000) < 0) {  // send eos
      LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] CnedkVencSend Eos error";
      return -1;
    }
    eos_promise_.reset(new std::promise<void>);
    return 0;
  }

  cnrtSetDevice(dev_id_);

  if (!data->surf) {
    LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] surface is nullptr";
    return -1;
  }

  std::unique_lock<std::mutex> guard(mutex_);
  if (!venc_handle_) {
    uint32_t width = param_.width;
    uint32_t height = param_.height;
    if (width == 0) {
      width = data->surf->GetWidth();
    }
    if (height == 0) {
      height = data->surf->GetHeight();
    }

    InitEncode(width, height, data->surf->GetColorFormat());
    stream_id_ = data->stream_id;

    CnedkPlatformInfo platform;
    CnedkPlatformGetInfo(dev_id_, &platform);
    platform_ = platform.name;
  }

  guard.unlock();

  if (CnedkVencSendFrame(venc_handle_, data->surf->GetBufSurface(), 2000) < 0) {
    LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] CnedkVencSendFrame failed";
    return -1;
  }

  return 0;
}

int VencMluHandler::OnEos() {
  cnrtSetDevice(dev_id_);
  if (eos_promise_) eos_promise_->set_value();
  LOG(INFO) << "[EasyDK Sample] [Encode Mlu Handle] VEncode::OnEos() called";
  return 0;
}

int VencMluHandler::OnError(int errcode) {
  LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] VEncode::OnError() called, FIXME"
             << std::hex << errcode << std::dec;
  return 0;
}

int VencMluHandler::OnFrameBits(CnedkVEncFrameBits *framebits) {
  std::string output_file = param_.filename;
  size_t length = framebits->len;

  if (p_output_file_ == NULL) p_output_file_ = fopen(output_file.c_str(), "wb");
  if (p_output_file_ == NULL) {
    LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] Open output file failed";
    return -1;
  }

  size_t written;
  written = fwrite(framebits->bits, 1, length, p_output_file_);
  if (written != length) {
    LOG(ERROR) << "[EasyDK Sample] [Encode Mlu Handle] Written size " << written << " != data length " << length;
    return -1;
  }
  return 0;
}
