/*************************************************************************
 * Copyright (C) [2020] by Cambricon, Inc. All rights reserved
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

#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_decode.h"
#include "cnedk_platform.h"

#include "easy_module.hpp"

#include "sample_decode.hpp"

int SampleDecode::Process(std::shared_ptr<EdkFrame> frame) {
  if (!eos_send_) {
    cnrtSetDevice(dev_id_);
    CnedkVdecStream packet;
    memset(&packet, 0, sizeof(packet));

    int data_len = 0;
    int64_t pts;
    if (!demuxer_->ReadFrame(reinterpret_cast<uint8_t**>(&packet.bits), &data_len, &pts)) {
      // LOG(ERROR) << "[EasyDK Tests] [Decode] ReadFrame(): Read data failed";
      packet.bits = nullptr;
      if (CnedkVdecSendStream(vdec_, &packet, 5000)) {  // send eos;
        LOG(ERROR) << "[EasyDK Sample] [Decode] SendData(): Send data failed";
      }
      eos_send_ = true;
      return 0;
    }
    packet.len = data_len;
    packet.pts = pts;
    int retry_time = 50;
    while (retry_time--) {
      if (CnedkVdecSendStream(vdec_, &packet, 5000) < 0) {
        LOG(ERROR) << "[EasyDK Sample] [Decode] SendData(): Send data failed";
      } else {
        break;
      }
    }
  }
  fr_controller_->Control();
  return 0;
}

int SampleDecode::Close() {
  if (!vdec_) {  // if vdec not created, send eos
    std::shared_ptr<EdkFrame> new_frame = std::make_shared<EdkFrame>();
    new_frame->stream_id = stream_id_;
    new_frame->is_eos = true;
    new_frame->frame_idx = frame_count_++;
    eos_send_ = true;
    Transmit(new_frame);
  }

  if (!eos_send_) {
    CnedkVdecStream packet;
    memset(&packet, 0, sizeof(packet));
    packet.bits = nullptr;
    if (CnedkVdecSendStream(vdec_, &packet, 5000)) {  // send eos;
      LOG(ERROR) << "[EasyDK Sample] [Decode] SendData(): Send data failed";
    }
    eos_send_ = true;
  }
  return 0;
}

SampleDecode::~SampleDecode() {
  if (vdec_) {
    CnedkVdecDestroy(vdec_);
    vdec_ = nullptr;
  }
  if (surf_pool_) {
    CnedkBufPoolDestroy(surf_pool_);
    surf_pool_ = nullptr;
  }
}

int SampleDecode::Open() {
  cnrtSetDevice(dev_id_);
  demuxer_ = std::unique_ptr<FFmpegDemuxer>{new FFmpegDemuxer()};
  if (demuxer_ == nullptr) {
    return -1;
  }
  int ret = demuxer_->Open(filename_.c_str());
  if (ret < 0) {
    LOG(ERROR) << "[EasyDK Sample] [Decode] Create demuxer failed";
    return -1;
  }
  fr_controller_.reset(new FrController(frame_rate_));

  params_.color_format = CNEDK_BUF_COLOR_FORMAT_NV21;
  params_.device_id = dev_id_;
  switch (demuxer_->GetVideoCodec()) {
  case AV_CODEC_ID_MJPEG:
    params_.type = CNEDK_VDEC_TYPE_JPEG;
    break;
  case AV_CODEC_ID_H264:
    params_.type = CNEDK_VDEC_TYPE_H264;
    break;
  case AV_CODEC_ID_HEVC:
    params_.type = CNEDK_VDEC_TYPE_H265;
    break;
  default:
    LOG(ERROR) << "[EasyDK Sample] [Decode] Not support codec type";
    return -1;
    break;
  }

  params_.userdata = this;
  params_.frame_buf_num = 12;  // for CE3226
  params_.surf_timeout_ms = 5000;

  params_.max_width = 3840;
  params_.max_height = 2160;

  params_.GetBufSurf = GetBufSurface_;
  params_.OnEos = OnEos_;
  params_.OnError = OnError_;
  params_.OnFrame = OnFrame_;
  width_ = 1920;
  height_ = 1080;

  ret = CnedkVdecCreate(&vdec_, &params_);
  if (ret < 0) {
    LOG(ERROR) << "[EasyDK Sample] [Decode] Create decode failed";
    return -1;
  }

  if (CreateSurfacePool(&surf_pool_, width_, height_) < 0) {
    LOG(ERROR) << "[EasyDK Sample] [Decode] Create Surface pool failed";
    return -1;
  }

  fr_controller_->Start();

  return 0;
}

int SampleDecode::CreateSurfacePool(void** surf_pool, int width, int height) {
  CnedkBufSurfaceCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.batch_size = 1;
  create_params.width = width;
  create_params.height = height;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV21;
  create_params.device_id = dev_id_;

  CnedkPlatformInfo platform_info;
  CnedkPlatformGetInfo(dev_id_, &platform_info);
  std::string platform(platform_info.name);
  if (platform == "MLU370") {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  } else if (platform == "CE3226") {
    create_params.mem_type = CNEDK_BUF_MEM_VB_CACHED;
  }

  if (CnedkBufPoolCreate(surf_pool, &create_params, 12) < 0) {
    LOG(ERROR) << "[EasyDK Sample] [Decode] CreateSurfacePool(): Create pool failed";
    return -1;
  }
  return 0;
}

int SampleDecode::OnError(int err_code) {
  LOG(ERROR) << "[EasyDK Sample] [Decode] OnError";
  return 0;
}

int SampleDecode::OnFrame(CnedkBufSurface* surf) {
  if (surf != nullptr) {
    surf->surface_list[0].width -= surf->surface_list[0].width & 1;
    surf->surface_list[0].height -= surf->surface_list[0].height & 1;
    surf->surface_list[0].plane_params.width[0] -= surf->surface_list[0].plane_params.width[0] & 1;
    surf->surface_list[0].plane_params.height[0] -= surf->surface_list[0].plane_params.height[0] & 1;
    surf->surface_list[0].plane_params.width[1] -= surf->surface_list[0].plane_params.width[1] & 1;
    surf->surface_list[0].plane_params.height[1] -= surf->surface_list[0].plane_params.height[1] & 1;

    std::shared_ptr<EdkFrame> new_frame = std::make_shared<EdkFrame>();
    new_frame->stream_id = stream_id_;
    new_frame->is_eos = false;
    new_frame->surf = std::make_shared<cnedk::BufSurfaceWrapper>(surf);

    new_frame->frame_idx = frame_count_++;
    // CnedkBufSurfaceDestroy(surf);

    Transmit(new_frame);
  }

  // size_t length = surf->surface_list[0].data_size;
  // LOG(INFO) << "length: " << length;
  return 0;
}

int SampleDecode::OnEos() {
  LOG(INFO) << "[EasyDK Sample] [Decode] OnEos" << std::endl;
  std::shared_ptr<EdkFrame> new_frame = std::make_shared<EdkFrame>();
  new_frame->stream_id = stream_id_;
  new_frame->is_eos = true;
  new_frame->frame_idx = frame_count_++;
  Transmit(new_frame);

  return 0;
}

int SampleDecode::GetBufSurface(CnedkBufSurface** surf, int width, int height,
                                CnedkBufSurfaceColorFormat fmt, int timeout_ms) {
  if (surf_pool_) {
    int retry_time = 50;
    while (retry_time--) {
      if (CnedkBufSurfaceCreateFromPool(surf, surf_pool_) == 0) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    if (retry_time < 0) {
      LOG(ERROR) << "[EasyDK Sample] [Decode] GetBufSurface(): Get BufSurface from pool failed";
      return -1;
    }
  }
  return 0;
}


// class Source : public Module {
//  public:
//   Source(std::string name, int parallelism, std::string filename, int stream_id) :
//         Module(name, parallelism) {
//     filename_ = filename;
//     stream_id_ = stream_id;
//   }
//   ~Source() = default;
//   int Open() override {
//     return 0;
//   }

//   int Process(std::shared_ptr<EdkFrame> frame) override {
//     // std::cout << "source, " << __FUNCTION__ << ", "<< __LINE__ << std::endl;
//     std::shared_ptr<EdkFrame> new_frame = std::make_shared<EdkFrame>();
//     new_frame->stream_id = stream_id_;
//     new_frame->is_eos = false;

//     new_frame->frame_idx = frame_count_;
//     if (frame_count_++ == 100) {
//       new_frame->is_eos = true;
//     }
//     std::this_thread::sleep_for(std::chrono::microseconds(33));
//     if (!eos_send_) {
//       Transmit(new_frame);
//     }
//     if (new_frame->is_eos) {
//       eos_send_ = true;
//       // std::cout << frame_count_ << std::endl;
//     }
//     return 0;
//   }

//   int Close() override {
//     return 0;
//   }
//  private:
//   bool eos_send_ = false;
//   int stream_id_;
//   std::string filename_;
//   uint64_t frame_count_ = 0;
// };
