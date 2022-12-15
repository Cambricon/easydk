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

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <iostream>

#include "glog/logging.h"

#include "cnrt.h"
#include "cnedk_platform.h"

#include "sample_encode.hpp"

SampleEncode::~SampleEncode() {
  ivenc_.clear();
}

int SampleEncode::Open() {
  auto dot = filename_.find_last_of(".");
  if (dot == std::string::npos) {
    LOG(ERROR) << "[EasyDK Sample] [Encode] Open() unknown file type \"" << filename_ << "\"";
  }

  std::string lower_file_name = filename_;
  std::transform(lower_file_name.begin(), lower_file_name.end(), lower_file_name.begin(), ::tolower);

  if (lower_file_name.find("h264") != std::string::npos) {
    param_.codec_type = CNEDK_VENC_TYPE_H264;
  } else if (lower_file_name.find("h265") != std::string::npos) {
    param_.codec_type = CNEDK_VENC_TYPE_H265;
  } else {
    LOG(ERROR) << "[EasyDK Sample] [Encode] Not support output file type";
    return -1;
  }

  param_.bitrate = 8000000;
  param_.gop_size = 10;
  param_.frame_rate = 30;
  param_.filename = filename_;
  return 0;
}

int SampleEncode::Process(std::shared_ptr<EdkFrame> frame) {
  std::unique_lock<std::mutex> lk(venc_mutex_);
  if (!ivenc_.count(frame->stream_id)) {   // create  context
    ivenc_[frame->stream_id] = std::make_shared<VencMluHandler>(device_id_);
    if (ivenc_[frame->stream_id] == nullptr) {
      LOG(ERROR) << "[EasyDK Sample] [Encode] Create encode failed";
    } else {
      auto dot = filename_.find_last_of(".");
      std::string file_name = filename_.substr(0, dot);
      std::string ext_name = filename_.substr(dot + 1);
      param_.filename = file_name + "_" + std::to_string(frame->stream_id) + "." + ext_name;

      ivenc_[frame->stream_id]->SetParams(param_);
    }
  }
  lk.unlock();

  if (ivenc_[frame->stream_id] != nullptr) {
    ivenc_[frame->stream_id]->SendFrame(frame);
  }

  Transmit(frame);
  return 0;
}

int Close() {
  return 0;
}

int SampleEncode::Close() {
  return 0;
}

