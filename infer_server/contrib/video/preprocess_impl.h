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

#ifndef INFER_SERVER_PREPROCESS_IMPL_H_
#define INFER_SERVER_PREPROCESS_IMPL_H_

#include <cn_codec_common.h>
#include <cnrt.h>
#include <glog/logging.h>
#include <algorithm>
#include <future>
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include "easybang/resize_and_colorcvt.h"
#include "infer_server.h"
#include "model/model.h"
#include "processor.h"
#include "shape.h"
#include "util/threadsafe_queue.h"
#include "video_helper.h"

namespace infer_server {
namespace video {
namespace detail {

void ClipBoundingBox(BoundingBox* box) noexcept;

class PreprocessBase {
 public:
  virtual ~PreprocessBase() {}
  virtual bool Init(PixelFmt src_fmt, PixelFmt dst_fmt) noexcept = 0;
  virtual bool Execute(Package* pack, Buffer* output) = 0;
};  // class PreprocessBase

class ResizeConvert : virtual public PreprocessBase {
 public:
  ResizeConvert(ModelPtr model, int dev_id, edk::CoreVersion core_version, int core_number, bool keep_aspect_ratio)
      : model_(model),
        dev_id_(dev_id),
        core_version_(core_version),
        core_number_(core_number),
        keep_aspect_ratio_(keep_aspect_ratio) {}
  ~ResizeConvert() { op.Destroy(); }

  bool Init(PixelFmt src_fmt, PixelFmt dst_fmt) noexcept override;
  bool Execute(Package* pack, Buffer* output) override;

 private:
  edk::MluResizeConvertOp op;
  ModelPtr model_;
  int dev_id_;
  edk::CoreVersion core_version_;
  int core_number_;
  bool keep_aspect_ratio_;
};  // class ResizeConvert

class ScalerWorker {
 public:
  static ScalerWorker* GetInstance(int device_id) noexcept {
    static std::map<int, std::unique_ptr<ScalerWorker>> instance_map;
    static std::mutex map_mutex;
    std::unique_lock<std::mutex> lk(map_mutex);
    if (!instance_map.count(device_id)) {
      instance_map.emplace(device_id, std::unique_ptr<ScalerWorker>(new ScalerWorker(device_id)));
    }
    return instance_map.at(device_id).get();
  }

  ~ScalerWorker() {
    is_done_ = true;
    cv_.notify_all();
    for (auto& th : th_) {
      if (th.joinable()) {
        th.join();
      }
    }
  }

  template <typename Callable>
  std::future<bool> PushTask(Callable&& f) noexcept {
    auto pck = std::make_shared<std::packaged_task<bool(int)>>(std::forward<Callable>(f));
    task_q_.Push(pck);
    cv_.notify_one();
    return pck->get_future();
  }

 private:
  explicit ScalerWorker(int device_id) noexcept : device_id_(device_id) {
    th_.reserve(2);
    th_.emplace_back(&ScalerWorker::WorkLoop, this, 0);
    th_.emplace_back(&ScalerWorker::WorkLoop, this, 1);
  }

  void WorkLoop(int instance_id) noexcept;

  std::vector<std::thread> th_;
  TSQueue<std::shared_ptr<std::packaged_task<bool(int)>>> task_q_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::atomic<bool> is_done_{false};
  int device_id_;
};  // class ScalerWorker

void ScalerWorker::WorkLoop(int instance_id) noexcept {
  std::shared_ptr<std::packaged_task<bool(int)>> t;
  bool have_task = task_q_.TryPop(t);
  while (true) {
    while (have_task) {
      (*t)(instance_id);
      t.reset();
      have_task = task_q_.TryPop(t);
    }

    std::unique_lock<std::mutex> lk(mutex_);
    cv_.wait(lk, [this, &have_task, &t]() {
      have_task = task_q_.TryPop(t);
      return have_task || is_done_;
    });

    if (!have_task) return;
  }
}

class Scaler : virtual public PreprocessBase {
 public:
  Scaler(Shape s, int dev_id) : shape_(s), dev_id_(dev_id) {}
  ~Scaler() {}

  bool Init(PixelFmt src_fmt, PixelFmt dst_fmt) noexcept override;
  bool Execute(Package* pack, Buffer* output) override;
  bool Process(VideoFrame* frame, Buffer* model_input, int instance_id, int batch_idx);

 private:
  Shape shape_;
  ScalerWorker* worker_{nullptr};
  cncodecPixelFormat src_fmt_;
  cncodecPixelFormat dst_fmt_;
  int dev_id_{0};
};  // class ResizeConvert

std::ostream& operator<<(std::ostream& os, PixelFmt fmt) noexcept { return os << static_cast<int>(fmt); }

bool ResizeConvert::Init(PixelFmt src_fmt, PixelFmt dst_fmt) noexcept {
  enum : int64_t {
#define JOINT(src, dst) (static_cast<int64_t>(src) << 32 | static_cast<int>(dst))
    YUV2RGBA_NV21 = JOINT(PixelFmt::NV21, PixelFmt::RGBA),
    YUV2RGBA_NV12 = JOINT(PixelFmt::NV12, PixelFmt::RGBA),

    YUV2BGRA_NV21 = JOINT(PixelFmt::NV21, PixelFmt::BGRA),
    YUV2BGRA_NV12 = JOINT(PixelFmt::NV12, PixelFmt::BGRA),

    YUV2ARGB_NV21 = JOINT(PixelFmt::NV21, PixelFmt::ARGB),
    YUV2ARGB_NV12 = JOINT(PixelFmt::NV12, PixelFmt::ARGB),

    YUV2ABGR_NV21 = JOINT(PixelFmt::NV21, PixelFmt::ABGR),
    YUV2ABGR_NV12 = JOINT(PixelFmt::NV12, PixelFmt::ABGR)
  } color_cvt_mode = static_cast<decltype(color_cvt_mode)>(JOINT(src_fmt, dst_fmt));
#undef JOINT

  static const std::map<decltype(color_cvt_mode), edk::MluResizeConvertOp::ColorMode> cvt_mode_map = {
      {YUV2RGBA_NV21, edk::MluResizeConvertOp::ColorMode::YUV2RGBA_NV21},
      {YUV2RGBA_NV12, edk::MluResizeConvertOp::ColorMode::YUV2RGBA_NV12},
      {YUV2BGRA_NV21, edk::MluResizeConvertOp::ColorMode::YUV2BGRA_NV21},
      {YUV2BGRA_NV12, edk::MluResizeConvertOp::ColorMode::YUV2BGRA_NV12},
      {YUV2ARGB_NV21, edk::MluResizeConvertOp::ColorMode::YUV2ARGB_NV21},
      {YUV2ARGB_NV12, edk::MluResizeConvertOp::ColorMode::YUV2ARGB_NV12},
      {YUV2ABGR_NV21, edk::MluResizeConvertOp::ColorMode::YUV2ABGR_NV21},
      {YUV2ABGR_NV12, edk::MluResizeConvertOp::ColorMode::YUV2ABGR_NV12}};

  LOG_IF(FATAL, cvt_mode_map.count(color_cvt_mode) == 0)
      << "Unsupport color convert mode. src pixel format : " << src_fmt << ", dst pixel format : " << dst_fmt;

  edk::MluResizeConvertOp::Attr op_attr;
  // image shape is always nhwc
  op_attr.dst_h = model_->InputShape(0)[1];
  op_attr.dst_w = model_->InputShape(0)[2];
  op_attr.color_mode = cvt_mode_map.at(color_cvt_mode);
  op_attr.batch_size = model_->BatchSize();
  op_attr.core_version = core_version_;
  op_attr.keep_aspect_ratio = keep_aspect_ratio_;
  op_attr.core_number = core_number_;
  if (!op.Init(op_attr)) {
    LOG(ERROR) << "Init resize convert op failed: " << op.GetLastError();
    return false;
  }
  return true;
}

bool ResizeConvert::Execute(Package* pack, Buffer* model_input) {
  for (size_t i = 0; i < pack->data.size(); i++) {
    VideoFrame& frame = pack->data[i]->GetLref<VideoFrame>();

    if (frame.format != PixelFmt::NV12 && frame.format != PixelFmt::NV21) {
      LOG(ERROR) << "Not supported!";
      return false;
    }
    edk::MluResizeConvertOp::InputData input_data;
    input_data.src_w = frame.width;
    input_data.src_h = frame.height;
    input_data.src_stride = frame.stride[0];
    input_data.planes[0] = frame.plane[0].MutableData();
    input_data.planes[1] = frame.plane[1].MutableData();

    ClipBoundingBox(&frame.roi);
    int32_t crop_x = frame.roi.x * frame.width;
    int32_t crop_y = frame.roi.y * frame.height;
    input_data.crop_x = crop_x;
    input_data.crop_y = crop_y;
    input_data.crop_w = frame.roi.w * frame.width;
    input_data.crop_h = frame.roi.h * frame.height;
    op.BatchingUp(input_data);
  }
  op.SyncOneOutput(model_input->MutableData());
  return true;
}

static cncodecPixelFormat CastPixelFormat(PixelFmt fmt) noexcept {
  switch (fmt) {
    case PixelFmt::NV12:
      return CNCODEC_PIX_FMT_NV12;
    case PixelFmt::NV21:
      return CNCODEC_PIX_FMT_NV21;
    case PixelFmt::ABGR:
      return CNCODEC_PIX_FMT_ABGR;
    case PixelFmt::ARGB:
      return CNCODEC_PIX_FMT_ARGB;
    case PixelFmt::BGRA:
      return CNCODEC_PIX_FMT_BGRA;
    case PixelFmt::RGBA:
      return CNCODEC_PIX_FMT_RGBA;
    default:
      LOG(ERROR) << "Unsupport pixel format";
      return CNCODEC_PIX_FMT_TOTAL_COUNT;
  }
}

bool Scaler::Init(PixelFmt src_fmt, PixelFmt dst_fmt) noexcept {
  worker_ = ScalerWorker::GetInstance(dev_id_);
  src_fmt_ = CastPixelFormat(src_fmt);
  dst_fmt_ = CastPixelFormat(dst_fmt);
  return true;
}

bool Scaler::Process(VideoFrame* frame, Buffer* model_input, int instance_id, int batch_idx) {
  CHECK(frame->plane[0].OnMlu()) << "memory is on CPU, which shoule be on MLU";
  void* src_y = frame->plane[0].MutableData();
  void* src_uv = frame->plane[1].MutableData();
  void* dst = (*model_input)(shape_.DataCount() * batch_idx).MutableData();
  cncodecWorkInfo work_info;
  cncodecFrame src_frame;
  cncodecFrame dst_frame;
  memset(&work_info, 0, sizeof(work_info));
  memset(&src_frame, 0, sizeof(src_frame));
  memset(&dst_frame, 0, sizeof(dst_frame));

  src_frame.pixelFmt = src_fmt_;
  src_frame.colorSpace = CNCODEC_COLOR_SPACE_BT_709;
  src_frame.width = frame->width;
  src_frame.height = frame->height;
  src_frame.planeNum = frame->plane_num;
  src_frame.stride[0] = frame->stride[0];
  src_frame.stride[1] = frame->stride[1];
  src_frame.plane[0].size = frame->stride[0] * frame->height;
  src_frame.plane[0].addr = reinterpret_cast<u64_t>(src_y);
  src_frame.plane[1].size = frame->stride[0] * frame->height >> 1;  // FIXME
  src_frame.plane[1].addr = reinterpret_cast<u64_t>(src_uv);
  src_frame.channel = 1;  // FIXME
  src_frame.deviceId = dev_id_;

  static auto align_to_128 = [](uint32_t x) { return (x + 127) & ~127; };
  auto row_align = align_to_128(shape_[2] * 4);
  dst_frame.width = shape_[2];
  dst_frame.height = shape_[1];
  dst_frame.pixelFmt = dst_fmt_;
  dst_frame.planeNum = 1;
  dst_frame.plane[0].size = row_align * shape_[1];
  dst_frame.stride[0] = row_align;
  dst_frame.plane[0].addr = reinterpret_cast<u64_t>(dst);

  work_info.inMsg.instance = instance_id;

  ClipBoundingBox(&frame->roi);
  cncodecRectangle roi;
  roi.left = frame->roi.x * src_frame.width;
  roi.top = frame->roi.y * src_frame.height;
  roi.right = (frame->roi.x + frame->roi.w) * src_frame.width;
  roi.bottom = (frame->roi.y + frame->roi.h) * src_frame.height;

  i32_t ret;
  if (roi.left == 0 && roi.top == 0 && roi.right == 0 && roi.bottom == 0) {
    ret = cncodecImageTransform(&dst_frame, nullptr, &src_frame, nullptr, CNCODEC_Filter_BiLinear, &work_info);
  } else {
    ret = cncodecImageTransform(&dst_frame, &roi, &src_frame, nullptr, CNCODEC_Filter_BiLinear, &work_info);
  }

  if (CNCODEC_SUCCESS != ret) {
    LOG(ERROR) << "scaler failed, error code:" << ret;
    return false;
  }
  return true;
}

bool Scaler::Execute(Package* pack, Buffer* model_input) {
  // submit task
  std::vector<std::future<bool>> res;
  res.reserve(pack->data.size());
  for (size_t batch_idx = 0; batch_idx < pack->data.size(); ++batch_idx) {
    VideoFrame& frame = pack->data[batch_idx]->GetLref<VideoFrame>();
    res.emplace_back(worker_->PushTask(std::bind(&Scaler::Process, this, &frame, model_input, std::placeholders::_1, batch_idx)));
  }

  // wait for task done
  bool ret = true;
  for (auto& it : res) {
    ret &= it.get();
  }

  return ret;
}

}  // namespace detail
}  // namespace video
}  // namespace infer_server

#endif  // INFER_SERVER_PREPROCESS_IMPL_H_
