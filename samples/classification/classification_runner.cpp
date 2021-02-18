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

#include <opencv2/opencv.hpp>

#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cxxutil/log.h"

#if CV_VERSION_EPOCH == 2
#define OPENCV_MAJOR_VERSION 2
#elif CV_VERSION_MAJOR >= 3
#define OPENCV_MAJOR_VERSION CV_VERSION_MAJOR
#endif

static const cv::Size g_out_video_size = cv::Size(1280, 720);

ClassificationRunner::ClassificationRunner(const std::string& model_path, const std::string& func_name,
                                           const std::string& label_path, const std::string& data_path, bool show,
                                           bool save_video)
    : StreamRunner(data_path), show_(show), save_video_(save_video) {
  // load offline model
  model_ = std::make_shared<edk::ModelLoader>(model_path.c_str(), func_name.c_str());

  // prepare mlu memory operator and memory
  mem_op_.SetModel(model_);

  // init easy_infer
  infer_.Init(model_, 0);

  // create mlu resize and convert operator
  auto& in_shape = model_->InputShape(0);
  edk::MluResizeConvertOp::Attr rc_attr;
  rc_attr.dst_h = in_shape.H();
  rc_attr.dst_w = in_shape.W();
  rc_attr.batch_size = 1;
  rc_attr.core_version = env_.GetCoreVersion();
  rc_op_.SetMluQueue(infer_.GetMluQueue());
  if (!rc_op_.Init(rc_attr)) {
    THROW_EXCEPTION(edk::Exception::INTERNAL, rc_op_.GetLastError());
  }

  // init postproc
  postproc_.reset(new edk::ClassificationPostproc);
  postproc_->set_threshold(0.2);
  CHECK(SAMPLES, postproc_);

  // init osd
  osd_.LoadLabels(label_path);

  // video writer
  if (save_video_) {
#if OPENCV_MAJOR_VERSION > 2
    video_writer_.reset(
        new cv::VideoWriter("out.avi", cv::VideoWriter::fourcc('M', 'J', 'P', 'G'), 25, g_out_video_size));
#else
    video_writer_.reset(new cv::VideoWriter("out.avi", CV_FOURCC('M', 'J', 'P', 'G'), 25, g_out_video_size));
#endif
    if (!video_writer_->isOpened()) {
      THROW_EXCEPTION(edk::Exception::Code::INIT_FAILED, "create output video file failed");
    }
  }

  mlu_input_ = mem_op_.AllocMluInput();
  mlu_output_ = mem_op_.AllocMluOutput();
  cpu_output_ = mem_op_.AllocCpuOutput();

  Start();
}

ClassificationRunner::~ClassificationRunner() {
  Stop();
  if (nullptr != mlu_output_) mem_op_.FreeMluOutput(mlu_output_);
  if (nullptr != cpu_output_) mem_op_.FreeCpuOutput(cpu_output_);
  if (nullptr != mlu_input_) mem_op_.FreeMluInput(mlu_input_);
}

void ClassificationRunner::Process(edk::CnFrame frame) {
  // run resize and convert
  void* rc_output = mlu_input_[0];
  edk::MluResizeConvertOp::InputData input;
  input.planes[0] = frame.ptrs[0];
  input.planes[1] = frame.ptrs[1];
  input.src_w = frame.width;
  input.src_h = frame.height;
  input.src_stride = frame.strides[0];
  rc_op_.BatchingUp(input);
  if (!rc_op_.SyncOneOutput(rc_output)) {
    decode_->ReleaseBuffer(frame.buf_id);
    THROW_EXCEPTION(edk::Exception::INTERNAL, rc_op_.GetLastError());
  }

  // run inference
  infer_.Run(mlu_input_, mlu_output_);
  mem_op_.MemcpyOutputD2H(cpu_output_, mlu_output_);

  // alloc memory to store image
  auto img_data = new uint8_t[frame.strides[0] * frame.height * 3 / 2];

  // copy out frame
  decode_->CopyFrameD2H(img_data, frame);

  // release codec buffer
  decode_->ReleaseBuffer(frame.buf_id);
  // yuv to bgr
  cv::Mat yuv(frame.height * 3 / 2, frame.strides[0], CV_8UC1, img_data);
  cv::Mat img;
  cv::cvtColor(yuv, img, cv::COLOR_YUV2BGR_NV21);
  delete[] img_data;

  // resize to show
  cv::resize(img, img, cv::Size(1280, 720));

  // post process
  std::vector<edk::DetectObject> detect_result;
  std::vector<std::pair<float*, uint64_t>> postproc_param;
  postproc_param.push_back(
      std::make_pair(reinterpret_cast<float*>(cpu_output_[0]), model_->OutputShape(0).DataCount()));
  detect_result = postproc_->Execute(postproc_param);

  std::cout << "----- Classification Result:\n";
  int show_number = 2;
  for (auto& obj : detect_result) {
    std::cout << "[Object] label: " << obj.label << " score: " << obj.score << "\n";
    if (!(--show_number)) break;
  }
  std::cout << "-----------------------------------\n" << std::endl;

  osd_.DrawLabel(img, detect_result);

  if (show_) {
    auto window_name = "classification";
    cv::imshow(window_name, img);
    cv::waitKey(5);
    // std::string fn = std::to_string(frame.frame_id) + ".jpg";
    // cv::imwrite(fn.c_str(), img);
  }
  if (save_video_) {
    video_writer_->write(img);
  }
}
