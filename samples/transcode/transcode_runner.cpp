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

#include "transcode_runner.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <iostream>

#include "cxxutil/log.h"
#include "easycodec/easy_encode.h"
#include "easycodec/vformat.h"
#include "resize_yuv.h"

#if CV_VERSION_EPOCH == 2
#define OPENCV_MAJOR_VERSION 2
#elif CV_VERSION_MAJOR >= 3
#define OPENCV_MAJOR_VERSION CV_VERSION_MAJOR
#endif

TranscodeRunner::TranscodeRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                                 const std::string& data_path, const std::string& output_file_name,
                                 int dst_width, int dst_height, double dst_frame_rate)
    : StreamRunner(data_path, decode_type, device_id), dst_frame_rate_(dst_frame_rate),
    dst_width_(dst_width), dst_height_(dst_height), output_file_name_(output_file_name) {
  // Create encoder
  edk::EasyEncode::Attr attr;
  attr.frame_geometry.w = dst_width;
  attr.frame_geometry.h = dst_height;
  attr.pixel_format = edk::PixelFmt::NV12;

  edk::CodecType codec_type = edk::CodecType::H264;
  std::string file_name = output_file_name;
  auto dot = file_name.find_last_of(".");
  if (dot == std::string::npos) {
    THROW_EXCEPTION(edk::Exception::INVALID_ARG, "unknown file type: " + file_name);
  }
  std::transform(file_name.begin(), file_name.end(), file_name.begin(), ::tolower);
  if (file_name.find("hevc") != std::string::npos || file_name.find("h265") != std::string::npos) {
    codec_type = edk::CodecType::H265;
  }
  file_extension_ = file_name.substr(dot + 1);
  file_name_ = file_name.substr(0, dot);
  if (file_extension_ == "jpg" || file_extension_ == "jpeg") {
    codec_type = edk::CodecType::JPEG;
    jpeg_encode_ = true;
  }
  attr.codec_type = codec_type;
  edk::MluContext ctx;
  edk::CoreVersion core_ver = ctx.GetCoreVersion();
  if (core_ver == edk::CoreVersion::MLU220 || core_ver == edk::CoreVersion::MLU270) {
    attr.attr_mlu200.rate_control.frame_rate_den = 10;
    attr.attr_mlu200.rate_control.frame_rate_num =
        std::ceil(static_cast<int>(dst_frame_rate * attr.attr_mlu200.rate_control.frame_rate_den));
  } else if (core_ver == edk::CoreVersion::MLU370) {
    attr.attr_mlu300.frame_rate_den = 10;
    attr.attr_mlu300.frame_rate_num =
        std::ceil(static_cast<int>(dst_frame_rate * attr.attr_mlu300.frame_rate_den));
  } else {
    THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "Not supported core version");
  }

  attr.eos_callback = std::bind(&TranscodeRunner::EosCallback, this);
  attr.packet_callback = std::bind(&TranscodeRunner::PacketCallback, this, std::placeholders::_1);
  encode_ = edk::EasyEncode::New(attr);

#ifdef HAVE_CNCV
  // Create resize yuv
  resize_.reset(new CncvResizeYuv(device_id));
  if (!resize_->Init()) {
    THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "Create CNCV resize yuv failed");
  }
#else
  // TODO(gaoyujia): cpu resize
  THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "Create resize yuv failed, please install CNCV");
#endif

  Start();
}

void TranscodeRunner::PacketCallback(const edk::CnPacket &packet) {
  if (packet.length == 0 || packet.data == 0) {
    LOGW(SAMPLE) << "[TranscodeRunner] PacketCallback received empty packet.";
    return;
  }
  if (packet.codec_type == edk::CodecType::JPEG) {
    output_file_name_ = file_name_ + std::to_string(frame_count_) + "." + file_extension_;
    file_.open(output_file_name_.c_str());
  } else if (!file_.is_open()) {
    file_.open(output_file_name_.c_str());
  }
  if (!file_.is_open()) {
    LOGE(SAMPLE) << "[TranscodeRunner] PacketCallback open output file failed";
  } else {
    file_.write(reinterpret_cast<const char *>(packet.data), packet.length);
    if (packet.codec_type == edk::CodecType::JPEG) {
      file_.close();
    }
  }
  if (packet.slice_type == edk::BitStreamSliceType::FRAME || packet.slice_type == edk::BitStreamSliceType::KEY_FRAME) {
    frame_count_++;
    std::cout << "encode frame count: " << frame_count_<< ", pts: " << packet.pts << std::endl;
  } else {
    std::cout << "encode head sps/pps" << std::endl;
  }
  encode_->ReleaseBuffer(packet.buf_id);
}

void TranscodeRunner::EosCallback() {
  LOGI(SAMPLE) << "[TranscodeRunner] EosCallback ... ";
  std::lock_guard<std::mutex>lg(encode_eos_mut_);
  encode_received_eos_ = true;
  encode_eos_cond_.notify_one();
}

TranscodeRunner::~TranscodeRunner() {
  Stop();
  LOGI(SAMPLE) << "~TranscodeRunner() FeedEos";
  encode_->FeedEos();
  std::unique_lock<std::mutex>lk(encode_eos_mut_);
  // wait 10s for receive eos.
  if (false == encode_eos_cond_.wait_for(lk, std::chrono::seconds(10),
                                         [this] { return encode_received_eos_.load(); })) {
    LOGE(SAMPLE) << "~TranscodeRunner() wait encoder EOS for 10s timeout";
  }
}

void TranscodeRunner::Process(edk::CnFrame frame) {
  edk::CnFrame dst_frame;
  if (!encode_->RequestFrame(&dst_frame)) {
    THROW_EXCEPTION(edk::Exception::INTERNAL, "[TranscodeRunner] Request frame from encoder failed");
  }
#ifdef HAVE_CNCV
  if (!resize_->Process(frame, &dst_frame)) {
    THROW_EXCEPTION(edk::Exception::INTERNAL, "[TranscodeRunner] Resize yuv failed");
  }
#endif
  decoder_->ReleaseFrame(std::move(frame));
  dst_frame.pts = frame.pts;
  if (!encode_->FeedData(dst_frame)) {
    THROW_EXCEPTION(edk::Exception::INTERNAL, "[TranscodeRunner] Feed data to encoder failed");
  }
}
