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
#include <memory>
#include <string>
#include <utility>

#include "sample_sync_inference.hpp"

#include "preprocess/preprocess_yolov3.hpp"
#include "postprocess/postprocess_yolov3.hpp"
#include "preprocess/preprocess_classification.hpp"
#include "postprocess/postprocess_classification.hpp"

int SampleSyncInference::Open() {
  infer_server_.reset(new infer_server::InferServer(device_id_));

  std::string lower_model_name = model_name_;
  std::transform(lower_model_name.begin(), lower_model_name.end(), lower_model_name.begin(), ::tolower);

  if (lower_model_name == "yolov3") {
    preproc_ = std::make_shared<PreprocYolov3>();
    postproc_ = std::make_shared<PostprocYolov3>();
  } else if (lower_model_name == "resnet") {
    preproc_ = std::make_shared<PreprocClassification>();
    postproc_ = std::make_shared<PostprocClassification>();
  } else {
    LOG(ERROR) << "[EasyDK Samples] [SampleSyncInference] Open: not support this model";
    return -1;
  }

  preproc_ = std::make_shared<PreprocYolov3>();
  postproc_ = std::make_shared<PostprocYolov3>();
  return 0;
}

int SampleSyncInference::Process(std::shared_ptr<EdkFrame> frame) {
  std::unique_lock<std::mutex> lk(ctx_mutex_);
  if (!infer_ctx_.count(frame->stream_id)) {
    infer_server::SessionDesc desc;
    desc.strategy = infer_server::BatchStrategy::DYNAMIC;
    desc.engine_num = 4;
    desc.priority = 0;
    desc.show_perf = false;
    desc.name = "SampleInference session";

    // load offline model
    desc.model = infer_server::InferServer::LoadModel(model_path_);
    // set preproc
    desc.preproc = infer_server::Preprocessor::Create();

    infer_server::SetPreprocHandler(desc.model->GetKey(), preproc_.get());
    desc.model_input_format = infer_server::NetworkInputFormat::RGB;

    // set post proc
    desc.postproc = infer_server::Postprocessor::Create();
    infer_server::SetPostprocHandler(desc.model->GetKey(), postproc_.get());

    infer_ctx_[frame->stream_id] = infer_server_->CreateSyncSession(desc);
  }
  lk.unlock();

  if (!frame->is_eos)  {
    std::string stream = "stream_0";
    infer_server::PackagePtr input = infer_server::Package::Create(1, stream);
    infer_server::PreprocInput tmp;

    tmp.surf = frame->surf;
    tmp.has_bbox = false;
    input->data[0]->Set(std::move(tmp));

    input->data[0]->SetUserData(frame);

    infer_server::PackagePtr output = std::make_shared<infer_server::Package>();
    infer_server::Status status;
    if (!infer_server_->RequestSync(infer_ctx_[frame->stream_id], std::move(input), &status, output)) {
      LOG(ERROR) << "[EasyDK Samples] [SampleSyncInference] Process(): Request infer_server do inference failed";
      return -1;
    }
  }
  Transmit(frame);
  return 0;
}


int SampleSyncInference::Close() {
  if (infer_server_) {
    for (auto& iter : infer_ctx_) {
      infer_server::RemovePreprocHandler(infer_server_->GetModel(iter.second)->GetKey());
      infer_server::RemovePostprocHandler(infer_server_->GetModel(iter.second)->GetKey());
      infer_server_->DestroySession(iter.second);
    }
  }
  return 0;
}


// class Infer : public Module {
//  public:
//   Infer(std::string name, int parallelism) : Module(name, parallelism) {

//   }

//   ~Infer() = default;

//   int Open() override {
//     return 0;
//   }

//   int Process(std::shared_ptr<EdkFrame> frame) override {
//     // std::cout << "Infer: " << __FUNCTION__ << std::endl;
//     std::cout << "frame stream id: "<< frame->stream_id << ", frame idx: " << frame->frame_idx << std::endl;
//     // std::this_thread::sleep_for(std::chrono::microseconds(200));
//     // std::cout << "stream_id: " << frame->stream_id << ", is eos: " << frame->is_eos << std::endl;
//     Transmit(frame);
//     return 0;
//   }

//   int Close() override {
//     return 0;
//   }

// };
