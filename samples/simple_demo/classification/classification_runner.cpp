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

#include "classification_runner.h"

#include <glog/logging.h>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cnis/processor.h"
#include "cnis/infer_server.h"

#include "cnedk_buf_surface_util.hpp"
#include "cnedk_transform.h"

#include "cnrt.h"


#if CV_VERSION_EPOCH == 2
#define OPENCV_MAJOR_VERSION 2
#elif CV_VERSION_MAJOR >= 3
#define OPENCV_MAJOR_VERSION CV_VERSION_MAJOR
#endif

static const cv::Size g_out_video_size = cv::Size(1280, 720);


class ClassificationRunnerObserver : public infer_server::Observer {
 public:
  explicit ClassificationRunnerObserver(std::function<void(SampleFrame*, bool)> callback) : callback_(callback) {}
  void Response(infer_server::Status status, infer_server::PackagePtr data,
                infer_server::any user_data) noexcept override {
    callback_(infer_server::any_cast<SampleFrame*>(user_data), status == infer_server::Status::SUCCESS);
  }

 private:
  std::function<void(SampleFrame*, bool)> callback_;
};

ClassificationRunner::ClassificationRunner(const VideoDecoder::DecoderType& decode_type, int device_id,
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
  desc.name = "classification session";

  // load offline model
  desc.model = infer_server::InferServer::LoadModel(model_path);

  // set preproc and postproc
  desc.preproc = infer_server::Preprocessor::Create();

  infer_server::SetPreprocHandler(desc.model->GetKey(), this);
  desc.model_input_format = infer_server::NetworkInputFormat::RGB;

  desc.postproc = infer_server::Postprocessor::Create();
  infer_server::SetPostprocHandler(desc.model->GetKey(), this);

  auto callback_ = [this](SampleFrame* frame, bool valid) {
    if (!valid) {
      return;
    }

    CnedkBufSurface* surf = frame->first;
    DetectObject& detect_obj = frame->second;
    cv::Mat img = this->ConvertToMatAndReleaseBuf(surf);
    static int i = 0;
    i++;
    cv::imwrite("tmp/"+std::to_string(i) +".jpg", img);
    osd_.DrawLabel(img, {detect_obj});
    cv::imwrite("tmp/"+std::to_string(i) +"_re.jpg", img);

    if (save_video_) {
      video_writer_->write(img);
    }
    delete frame;
  };

  session_ = infer_server_->CreateSession(desc, std::make_shared<ClassificationRunnerObserver>(callback_));

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
      LOG(ERROR) << "[EasyDK Samples] [ClassificationRunner] Create output video file failed";
    }
  }

  Start();
}

ClassificationRunner::~ClassificationRunner() {
  Stop();
  if (infer_server_ && session_) {
    infer_server::RemovePreprocHandler(infer_server_->GetModel(session_)->GetKey());
    infer_server::RemovePostprocHandler(infer_server_->GetModel(session_)->GetKey());
    infer_server_->DestroySession(session_);
  }
}

int ClassificationRunner::OnTensorParams(const infer_server::CnPreprocTensorParams *params) {
  return 0;
}

int ClassificationRunner::OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                                    const std::vector<CnedkTransformRect> &src_rects) {
  CnedkTransformParams params;
  memset(&params, 0, sizeof(params));

  CnedkTransformConfigParams config;
  memset(&config, 0, sizeof(config));
  config.compute_mode = CNEDK_TRANSFORM_COMPUTE_MLU;
  CnedkTransformSetSessionParams(&config);

  if (CnedkTransform(src->GetBufSurface(), dst->GetBufSurface(), &params) < 0) {
    LOG(ERROR) << "[EasyDK Samples] [ClassificationRunner] OnPreproc(): CnTransform failed";
    return -1;
  }

  return 0;
}

int ClassificationRunner::OnPostproc(const std::vector<infer_server::InferData*>& data_vec,
                                     const infer_server::ModelIO& model_output,
                                     const infer_server::ModelInfo* model_info) {
  cnedk::BufSurfWrapperPtr output = model_output.surfs[0];

  if (!output->GetHostData(0)) {
    LOG(ERROR) << "[EasyDK Samples] [ClassificationRunner] Postprocess failed, copy data to host failed.";
    return -1;
  }
  CnedkBufSurfaceSyncForCpu(output->GetBufSurface(), -1, -1);

  auto len = model_info->OutputShape(0).DataCount();
  for (size_t batch_idx = 0; batch_idx < data_vec.size(); ++batch_idx) {
    float *res = static_cast<float*>(output->GetHostData(0, batch_idx));
    auto score_ptr = res;

    float max_score = 0;
    uint32_t label = 0;
    for (decltype(len) i = 0; i < len; ++i) {
      auto score = *(score_ptr + i);
      if (score > max_score) {
        max_score = score;
        label = i;
      }
    }

    SampleFrame* frame = data_vec[batch_idx]->GetUserData<SampleFrame*>();
    DetectObject& detect_obj = frame->second;
    detect_obj.label = label;
    detect_obj.score = max_score;
  }
  return 0;
}

cv::Mat ClassificationRunner::ConvertToMatAndReleaseBuf(CnedkBufSurface* surf) {
  size_t length = surf->surface_list[0].data_size;

  uint8_t* buffer = new uint8_t[length];
  if (!buffer) return cv::Mat();

  uint32_t frame_h = surf->surface_list[0].height;
  uint32_t frame_stride = surf->surface_list[0].pitch;
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
    LOG(ERROR) << "[EasyDK Samples] [ClassificationRunner] ConvertToMatAndReleaseBuf(): Unsupported pixel format";
  }
  delete[] buffer;

  // resize to show
  cv::resize(img, img, cv::Size(1280, 720));
  return img;
}

void ClassificationRunner::Process(CnedkBufSurface* surf) {
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

  SampleFrame* frame = new SampleFrame();
  frame->first = surf;
  request->data[0]->SetUserData(frame);
  if (!infer_server_->Request(session_, std::move(request), frame)) {
    LOG(ERROR) << "[EasyDK Samples] [ClassificationRunner] Process(): Request infer_server do inference failed";
    decoder_->ReleaseFrame(surf);
    delete frame;
  }
}
