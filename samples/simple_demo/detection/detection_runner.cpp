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

#include "detection_runner.h"

#include <glog/logging.h>
#include <opencv2/opencv.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"

#if CV_VERSION_EPOCH == 2
#define OPENCV_MAJOR_VERSION 2
#elif CV_VERSION_MAJOR >= 3
#define OPENCV_MAJOR_VERSION CV_VERSION_MAJOR
#endif


#define SAMPLE_OUTPUT_WIDTH 1280
#define SAMPLE_OUTPUT_HEIGHT 720
#define CLIP(x) ((x) < 0 ? 0 : ((x) > 1 ? 1 : (x)))

static const cv::Size g_out_video_size = cv::Size(SAMPLE_OUTPUT_WIDTH, SAMPLE_OUTPUT_HEIGHT);


class DetectionRunnerObserver : public infer_server::Observer {
 public:
  explicit DetectionRunnerObserver(std::function<void(DetectionFrame*, bool)> callback) : callback_(callback) {}
  void Response(infer_server::Status status, infer_server::PackagePtr data,
                infer_server::any user_data) noexcept override {
    callback_(infer_server::any_cast<DetectionFrame*>(user_data), status == infer_server::Status::SUCCESS);
  }

 private:
  std::function<void(DetectionFrame*, bool)> callback_;
};

CnedkTransformRect KeepAspectRatio(int src_w, int src_h, int dst_w, int dst_h) {
  float src_ratio = static_cast<float>(src_w) / src_h;
  float dst_ratio = static_cast<float>(dst_w) / dst_h;
  CnedkTransformRect res;
  memset(&res, 0, sizeof(res));
  if (src_ratio < dst_ratio) {
    int pad_lenth = dst_w - src_ratio * dst_h;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_w - pad_lenth / 2 < 0) return res;
    res.width = dst_w - pad_lenth;
    res.left = pad_lenth / 2;
    res.top = 0;
    res.height = dst_h;
  } else if (src_ratio > dst_ratio) {
    int pad_lenth = dst_h - dst_w / src_ratio;
    pad_lenth = (pad_lenth % 2) ? pad_lenth - 1 : pad_lenth;
    if (dst_h - pad_lenth / 2 < 0) return res;
    res.height = dst_h - pad_lenth;
    res.top = pad_lenth / 2;
    res.left = 0;
    res.width = dst_w;
  } else {
    res.left = 0;
    res.top = 0;
    res.width = dst_w;
    res.height = dst_h;
  }
  return res;
}

DetectionRunner::DetectionRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
                                 const std::string& model_path, const std::string& label_path,
                                 const std::string& data_path,
                                 bool show, bool save_video)
    : StreamRunner(data_path, decode_type, device_id), show_(show), save_video_(save_video) {
  infer_server_.reset(new infer_server::InferServer(device_id));
  infer_server::SessionDesc desc;
  desc.strategy = infer_server::BatchStrategy::STATIC;
  desc.engine_num = 1;
  desc.priority = 0;
  desc.show_perf = true;
  desc.name = "detection session";

  // load offline model
  desc.model = infer_server::InferServer::LoadModel(model_path);
  // set preproc
  desc.preproc = infer_server::Preprocessor::Create();
  infer_server::SetPreprocHandler(desc.model->GetKey(), this);
  desc.model_input_format = infer_server::NetworkInputFormat::RGB;

  // create post proc
  desc.postproc = infer_server::Postprocessor::Create();
  infer_server::SetPostprocHandler(desc.model->GetKey(), this);

  auto callback_ = [this](DetectionFrame* frame, bool valid) {
    if (!valid) {
      return;
    }

    CnedkBufSurface* surf = frame->surf;
    std::vector<DetectObject>& detect_objs = frame->objs;
    cv::Mat img = this->ConvertToMatAndReleaseBuf(surf);
    osd_.DrawLabel(img, detect_objs);

    if (save_video_) {
      video_writer_->write(img);
    }
    delete frame;
  };

  session_ = infer_server_->CreateSession(desc, std::make_shared<DetectionRunnerObserver>(callback_));

  // init osd
  osd_.LoadLabels(label_path);

  // video writer
  if (save_video_) {
    // For OpenCV version 4.x with FFMPEG=OFF, the output file name must contain number 0 - 9.
#if OPENCV_MAJOR_VERSION > 2
    video_writer_.reset(
        new cv::VideoWriter("out001.avi", cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 25, g_out_video_size));
#else
    video_writer_.reset(new cv::VideoWriter("out001.avi", CV_FOURCC('M', 'J', 'P', 'G'), 25, g_out_video_size));
#endif
    if (!video_writer_->isOpened()) {
      LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] Create output video file failed";
    }
  }

  Start();
}

DetectionRunner::~DetectionRunner() {
  Stop();
  if (infer_server_ && session_) {
    infer_server::RemovePreprocHandler(infer_server_->GetModel(session_)->GetKey());
    infer_server::RemovePostprocHandler(infer_server_->GetModel(session_)->GetKey());
    infer_server_->DestroySession(session_);
  }
}

int DetectionRunner::OnTensorParams(const infer_server::CnPreprocTensorParams *params) {
  params_ = *params;
  return 0;
}

int DetectionRunner::OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                               const std::vector<CnedkTransformRect> &src_rects) {
  CnedkBufSurface* src_buf = src->GetBufSurface();
  CnedkBufSurface* dst_buf = dst->GetBufSurface();

  uint32_t batch_size = src->GetNumFilled();
  std::vector<CnedkTransformRect> src_rect(batch_size);
  std::vector<CnedkTransformRect> dst_rect(batch_size);
  CnedkTransformParams params;
  memset(&params, 0, sizeof(params));
  params.transform_flag = 0;
  if (src_rects.size()) {
    params.transform_flag |= CNEDK_TRANSFORM_CROP_SRC;
    params.src_rect = src_rect.data();
    memset(src_rect.data(), 0, sizeof(CnedkTransformRect) * batch_size);
    for (uint32_t i = 0; i < batch_size; i++) {
      CnedkTransformRect *src_bbox = &src_rect[i];
      *src_bbox = src_rects[i];
      // validate bbox
      src_bbox->left -= src_bbox->left & 1;
      src_bbox->top -= src_bbox->top & 1;
      src_bbox->width -= src_bbox->width & 1;
      src_bbox->height -= src_bbox->height & 1;
      while (src_bbox->left + src_bbox->width > src_buf->surface_list[i].width) src_bbox->width -= 2;
      while (src_bbox->top + src_bbox->height > src_buf->surface_list[i].height) src_bbox->height -= 2;
    }
  }

  // configure dst_desc
  CnedkTransformTensorDesc dst_desc;
  dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_RGB;
  dst_desc.data_type = CNEDK_TRANSFORM_UINT8;

  if (params_.input_order == infer_server::DimOrder::NHWC) {
    dst_desc.shape.n = params_.input_shape[0];
    dst_desc.shape.h = params_.input_shape[1];
    dst_desc.shape.w = params_.input_shape[2];
    dst_desc.shape.c = params_.input_shape[3];
  } else if (params_.input_order == infer_server::DimOrder::NCHW) {
    dst_desc.shape.n = params_.input_shape[0];
    dst_desc.shape.c = params_.input_shape[1];
    dst_desc.shape.h = params_.input_shape[2];
    dst_desc.shape.w = params_.input_shape[3];
  } else {
    LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] OnPreproc(): Unsupported input dim order";
    return -1;
  }

  params.transform_flag |= CNEDK_TRANSFORM_CROP_DST;
  params.dst_rect = dst_rect.data();
  memset(dst_rect.data(), 0, sizeof(CnedkTransformRect) * batch_size);
  for (uint32_t i = 0; i < batch_size; i++) {
    CnedkTransformRect *dst_bbox = &dst_rect[i];
    *dst_bbox = KeepAspectRatio(src_buf->surface_list[i].width, src_buf->surface_list[i].height, dst_desc.shape.w,
                                dst_desc.shape.h);
    // validate bbox
    dst_bbox->left -= dst_bbox->left & 1;
    dst_bbox->top -= dst_bbox->top & 1;
    dst_bbox->width -= dst_bbox->width & 1;
    dst_bbox->height -= dst_bbox->height & 1;
    while (dst_bbox->left + dst_bbox->width > dst_desc.shape.w) dst_bbox->width -= 2;
    while (dst_bbox->top + dst_bbox->height > dst_desc.shape.h) dst_bbox->height -= 2;
  }

  params.dst_desc = &dst_desc;

  CnedkTransformConfigParams config;
  memset(&config, 0, sizeof(config));
  config.compute_mode = CNEDK_TRANSFORM_COMPUTE_MLU;
  CnedkTransformSetSessionParams(&config);

  CnedkBufSurfaceMemSet(dst_buf, -1, -1, 0x80);
  if (CnedkTransform(src_buf, dst_buf, &params) < 0) {
    LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] OnPreproc(): CnedkTransform failed";
    return -1;
  }

  return 0;
}

int DetectionRunner::OnPostproc(const std::vector<infer_server::InferData*>& data_vec,
                                const infer_server::ModelIO& model_output,
                                const infer_server::ModelInfo* model_info) {
  cnedk::BufSurfWrapperPtr output0 = model_output.surfs[0];  // data
  cnedk::BufSurfWrapperPtr output1 = model_output.surfs[1];  // bbox

  if (!output0->GetHostData(0)) {
    LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] Postprocess failed, copy data0 to host failed.";
    return -1;
  }
  if (!output1->GetHostData(0)) {
    LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] Postprocess failed, copy data1 to host failed.";
    return -1;
  }

  CnedkBufSurfaceSyncForCpu(output0->GetBufSurface(), -1, -1);
  CnedkBufSurfaceSyncForCpu(output1->GetBufSurface(), -1, -1);

  infer_server::DimOrder input_order = model_info->InputLayout(0).order;
  auto s = model_info->InputShape(0);
  int model_input_w, model_input_h;
  if (input_order == infer_server::DimOrder::NCHW) {
    model_input_w = s[3];
    model_input_h = s[2];
  } else if (input_order == infer_server::DimOrder::NHWC) {
    model_input_w = s[2];
    model_input_h = s[1];
  } else {
    LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] Postprocess failed. Unsupported dim order";
    return -1;
  }


  for (size_t batch_idx = 0; batch_idx < data_vec.size(); batch_idx++) {
    float *data = static_cast<float*>(output0->GetHostData(0, batch_idx));
    int box_num = static_cast<int*>(output1->GetHostData(0, batch_idx))[0];
    if (!box_num) {
      continue;  // no bboxes
    }

    DetectionFrame* frame = data_vec[batch_idx]->GetUserData<DetectionFrame*>();

    const float scaling_w = 1.0f * model_input_w / SAMPLE_OUTPUT_WIDTH;
    const float scaling_h = 1.0f * model_input_h / SAMPLE_OUTPUT_HEIGHT;
    const float scaling = std::min(scaling_w, scaling_h);
    float scaling_factor_w, scaling_factor_h;
    scaling_factor_w = scaling_w / scaling;
    scaling_factor_h = scaling_h / scaling;
    for (int bi = 0; bi < box_num; ++bi) {
      if (threshold_ > 0 && data[2] < threshold_) {
        data += 7;
        continue;
      }

      float l = CLIP(data[3]);
      float t = CLIP(data[4]);
      float r = CLIP(data[5]);
      float b = CLIP(data[6]);
      l = CLIP((l - 0.5f) * scaling_factor_w + 0.5f);
      t = CLIP((t - 0.5f) * scaling_factor_h + 0.5f);
      r = CLIP((r - 0.5f) * scaling_factor_w + 0.5f);
      b = CLIP((b - 0.5f) * scaling_factor_h + 0.5f);
      if (r <= l || b <= t) {
        data += 7;
        continue;
      }
      DetectObject obj;
      uint32_t id = static_cast<uint32_t>(data[1]);
      obj.label = id;
      obj.score = data[2];
      obj.bbox.x = l;
      obj.bbox.y = t;
      obj.bbox.w = std::min(1.0f - l, r - l);
      obj.bbox.h = std::min(1.0f - t, b - t);
      frame->objs.push_back(obj);
      data += 7;
    }
  }
  return 0;
}

cv::Mat DetectionRunner::ConvertToMatAndReleaseBuf(CnedkBufSurface* surf) {
  size_t length = surf->surface_list[0].data_size;

  uint8_t* buffer = new uint8_t[length];
  if (!buffer) return cv::Mat();

  uint32_t frame_stride = surf->surface_list[0].pitch;
  uint32_t frame_h = surf->surface_list[0].height;
  cnrtMemcpy(buffer, surf->surface_list[0].data_ptr, frame_stride * frame_h, cnrtMemcpyDevToHost);
  cnrtMemcpy(buffer + frame_stride * frame_h,
      reinterpret_cast<void*>(reinterpret_cast<uint64_t>(surf->surface_list[0].data_ptr) + frame_stride * frame_h),
      frame_stride * frame_h / 2, cnrtMemcpyDevToHost);

  CnedkBufSurfaceColorFormat fmt = surf->surface_list[0].color_format;
  decoder_->ReleaseFrame(surf);

  // alloc memory to store image
  // yuv to bgr
  cv::Mat yuv(frame_h * 3 / 2, frame_stride, CV_8UC1, buffer);
  cv::Mat img;
  if (fmt == CNEDK_BUF_COLOR_FORMAT_NV12) {
    cv::cvtColor(yuv, img, cv::COLOR_YUV2BGR_NV12);
  } else if (fmt == CNEDK_BUF_COLOR_FORMAT_NV21) {
    cv::cvtColor(yuv, img, cv::COLOR_YUV2BGR_NV21);
  } else {
    LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] Unsupported pixel format";
  }
  delete[] buffer;

  // resize to show
  cv::resize(img, img, cv::Size(1280, 720));
  return img;
}

void DetectionRunner::Process(CnedkBufSurface* surf) {
  std::string stream = "stream_0";
  if (!surf) {
    infer_server_->WaitTaskDone(session_, stream);
    return;
  }
  infer_server::PackagePtr request = infer_server::Package::Create(1, stream);
  infer_server::PreprocInput tmp;

  tmp.surf = std::make_shared<cnedk::BufSurfaceWrapper>(surf, false);
  tmp.has_bbox = false;
  request->data[0]->Set(std::move(tmp));

  DetectionFrame* frame = new DetectionFrame();
  frame->surf = surf;
  request->data[0]->SetUserData(frame);
  if (!infer_server_->Request(session_, std::move(request), frame)) {
    LOG(ERROR) << "[EasyDK Samples] [DetectionRunner] Process(): Request infer_server do inference failed";
    decoder_->ReleaseFrame(surf);
    delete frame;
  }
}
