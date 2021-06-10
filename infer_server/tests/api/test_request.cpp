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

#include <glog/logging.h>
#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fixture.h"
#include "infer_server.h"
#include "processor.h"

#ifdef CNIS_WITH_CONTRIB
#include "video_helper.h"

using infer_server::Buffer;

namespace infer_server {

using video::PixelFmt;
using video::PreprocessorMLU;
using video::PreprocessType;

constexpr const char* model_url =
    "http://video.cambricon.com/models/MLU270/Primary_Detector/ssd/resnet34_ssd.cambricon";
constexpr const char* image_path = "../../../tests/data/1080p.jpg";

class TestObserver : public Observer {
 public:
  TestObserver(std::promise<Status>& get_response) : get_response_(get_response) {}  // NOLINT

  void Response(Status status, PackagePtr data, any user_data) noexcept override {
    std::lock_guard<std::mutex> lk(response_mut_);
    response_list_.emplace_back(std::move(data));
    udata_list_.emplace_back(std::move(user_data));
    if (first_response_) {
      get_response_.set_value(status);
      first_response_ = false;
    }
  }

  PackagePtr GetPackage() {
    std::lock_guard<std::mutex> lk(response_mut_);
    if (response_list_.empty()) {
      return nullptr;
    }

    auto response = std::move(response_list_.front());
    response_list_.pop_front();
    return response;
  }

  std::pair<PackagePtr, any> GetResponse() {
    std::lock_guard<std::mutex> lk(response_mut_);
    if (response_list_.empty()) {
      return {nullptr, nullptr};
    }

    auto response = std::move(response_list_.front());
    auto udata = std::move(udata_list_.front());
    response_list_.pop_front();
    udata_list_.pop_front();
    return std::make_pair(response, udata);
  }

  uint32_t ResponseNum() {
    std::lock_guard<std::mutex> lk(response_mut_);
    return response_list_.size();
  }

 private:
  std::promise<Status>& get_response_;
  std::list<PackagePtr> response_list_;
  std::list<any> udata_list_;
  std::mutex response_mut_;
  bool first_response_{true};
};

auto g_empty_postproc_func = [](InferData*, const ModelIO&, const ModelInfo& m) { return true; };
auto g_empty_preproc_func = [](ModelIO*, const InferData&, const ModelInfo&) { return true; };

class InferServerRequestTest : public InferServerTestAPI {
 protected:
  void SetUp() override {
    SetMluContext();
    model_ = server_->LoadModel(model_url);
    if (!model_) {
      std::cerr << "load model failed";
      std::terminate();
    }
    preproc_mlu_ = PreprocessorMLU::Create();
    preproc_host_ = PreprocessorHost::Create();
    preproc_host_->SetParams<PreprocessorHost::ProcessFunction>("process_function", g_empty_preproc_func);
    empty_preproc_host_ = PreprocessorHost::Create();
    postproc_ = Postprocessor::Create();
    postproc_->SetParams<Postprocessor::ProcessFunction>("process_function", g_empty_postproc_func);

    observer_ = std::make_shared<TestObserver>(get_response_);
  }
  void TearDown() override {}

  video::VideoFrame ConvertToVideoFrame(uint8_t* img_nv12, size_t w, size_t h) {
    size_t frame_size = w * h;
    Buffer y_memory(frame_size, 0);
    Buffer uv_memory(frame_size / 2, 0);
    y_memory.CopyFrom(img_nv12, frame_size);
    uv_memory.CopyFrom(img_nv12 + frame_size, frame_size / 2);
    video::VideoFrame frame;
    frame.plane[0] = y_memory;
    frame.plane[1] = uv_memory;
    frame.stride[0] = 1;
    frame.stride[1] = 1;
    frame.width = w;
    frame.height = h;
    frame.plane_num = 2;
    frame.format = PixelFmt::NV12;
    return frame;
  }

  PackagePtr PrepareInput(const std::string& image_path, size_t data_num) {
    PackagePtr in = std::make_shared<Package>();
    cv::Mat img = cv::imread(GetExePath() + image_path);
    size_t frame_size = img.cols * img.rows;
    uint8_t* img_nv12 = new uint8_t[frame_size * 3 / 2];
    cvt_bgr_to_yuv420sp(img, 1, PixelFmt::NV12, img_nv12);
    for (size_t i = 0; i < data_num; ++i) {
      in->data.emplace_back(new InferData);
      in->data[i]->Set(ConvertToVideoFrame(img_nv12, img.cols, img.rows));
    }
    delete[] img_nv12;
    return in;
  }

  Session_t PrepareSession(const std::string& name, std::shared_ptr<Processor> preproc,
                           std::shared_ptr<Processor> postproc, size_t batch_timeout, BatchStrategy strategy,
                           std::shared_ptr<Observer> observer, bool cncv_used = false) {
    if (version_ != edk::CoreVersion::MLU220 && version_ != edk::CoreVersion::MLU270) {
      std::cerr << "Unsupport core version" << static_cast<int>(version_) << std::endl;
      return nullptr;
    }
    SessionDesc desc;
    desc.name = name;
    desc.model = model_;
    desc.strategy = strategy;
    desc.preproc = std::move(preproc);
    desc.postproc = std::move(postproc);
    desc.batch_timeout = batch_timeout;
    desc.engine_num = 1;
    desc.show_perf = true;
    desc.priority = 0;
    desc.host_output_layout = {infer_server::DataType::FLOAT32, infer_server::DimOrder::NCHW};
    auto p_type = cncv_used ? PreprocessType::CNCV_RESIZE_CONVERT : PreprocessType::RESIZE_CONVERT;
    if (version_ == edk::CoreVersion::MLU220) p_type = PreprocessType::SCALER;
    desc.preproc->SetParams("dst_format", PixelFmt::ARGB, "preprocess_type", p_type);
    if (observer) {
      return server_->CreateSession(desc, observer);
    } else {
      return server_->CreateSyncSession(desc);
    }
  }

  void WaitAsyncDone() {
    auto f = get_response_.get_future();
    ASSERT_NE(f.wait_for(std::chrono::seconds(1)), std::future_status::timeout) << "wait for response timeout";
    EXPECT_EQ(f.get(), Status::SUCCESS);
  }

  ModelPtr model_;
  std::shared_ptr<Processor> preproc_mlu_;
  std::shared_ptr<Processor> preproc_host_;
  std::shared_ptr<Processor> empty_preproc_host_;
  std::shared_ptr<Processor> postproc_;
  std::shared_ptr<TestObserver> observer_;

  std::promise<Status> get_response_;
};

class MyPostprocessor : public ProcessorForkable<MyPostprocessor> {
 public:
  MyPostprocessor() : ProcessorForkable<MyPostprocessor>("MyPostprocessor") {}
  ~MyPostprocessor() {}
  Status Process(PackagePtr pack) noexcept override {
    if (!pack->predict_io || !pack->predict_io->HasValue()) return Status::ERROR_BACKEND;
    try {
      edk::MluContext ctx;
      ctx.SetDeviceId(dev_id_);
      ctx.BindDevice();
    } catch (edk::Exception& e) {
      LOG(ERROR) << e.what();
      return Status::ERROR_BACKEND;
    }

    auto output = pack->predict_io->Get<ModelIO>();
    auto& shape = output.shapes[0];
    for (uint32_t idx = 0; idx < pack->data.size(); ++idx) {
      auto buf_size = output.buffers[0].MemorySize() / shape[0];
      Buffer buf(buf_size, dev_id_);
      buf.CopyFrom(output.buffers[0](idx * shape.DataCount()), buf_size);
      pack->data[idx]->Set(buf);
    }
    return Status::SUCCESS;
  }
  Status Init() noexcept override {
    constexpr const char* params[] = {"model_info", "device_id"};
    for (auto p : params) {
      if (!HaveParam(p)) {
        LOG(ERROR) << p << " has not been set!";
        return Status::INVALID_PARAM;
      }
    }
    try {
      model_ = GetParam<ModelPtr>("model_info");
      dev_id_ = GetParam<int>("device_id");
    } catch (bad_any_cast&) {
      LOG(ERROR) << "Unmatched data type";
      return Status::WRONG_TYPE;
    }
    return Status::SUCCESS;
  }

 private:
  ModelPtr model_;
  int dev_id_;
};

TEST_F(InferServerRequestTest, EmptyPackage) {
  Session_t session =
      PrepareSession("empty package process", preproc_mlu_, postproc_, 5, BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  EXPECT_EQ(model_, server_->GetModel(session));

  constexpr const char* tag = "EmptyPackage";
  auto in = PrepareInput(image_path, 10);
  in->tag = tag;
  ASSERT_TRUE(server_->Request(session, std::move(in), nullptr));

  in = Package::Create(0, tag);
  ASSERT_TRUE(server_->Request(session, std::move(in), nullptr));

  video::VideoInferServer vs(device_id_);
  ASSERT_TRUE(vs.Request(session, video::VideoFrame{}, {}, tag, nullptr));

  server_->WaitTaskDone(session, tag);
  WaitAsyncDone();
  EXPECT_EQ(observer_->ResponseNum(), 3u);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, DynamicBatch) {
  Session_t session =
      PrepareSession("dynamic batch process", preproc_mlu_, postproc_, 5, BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  server_->Request(session, std::move(in), nullptr);

  WaitAsyncDone();
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, SkipPostproc) {
  // no process_function param
  postproc_->PopParam<Postprocessor::ProcessFunction>("process_function");
  Session_t session =
      PrepareSession("skip postproc process", preproc_mlu_, postproc_, 5, BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  constexpr size_t data_number = 10;
  auto in = PrepareInput(image_path, data_number);
  server_->Request(session, std::move(in), nullptr);

  WaitAsyncDone();
  auto response = observer_->GetPackage();
  EXPECT_NO_THROW(response->data[0]->Get<ModelIO>());
  EXPECT_EQ(response->data.size(), data_number);
  server_->DestroySession(session);

  // no postprocessor
  session = PrepareSession("skip postproc process", preproc_mlu_, nullptr, 5, BatchStrategy::DYNAMIC, nullptr);
  ASSERT_NE(session, nullptr);

  in = PrepareInput(image_path, data_number);
  response.reset(new Package);
  Status status;
  server_->RequestSync(session, std::move(in), &status, response, 1000);

  EXPECT_NO_THROW(response->data[0]->Get<ModelIO>());
  EXPECT_EQ(response->data.size(), data_number);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, StaticBatch) {
  Session_t session =
      PrepareSession("static batch process", preproc_mlu_, postproc_, 5, BatchStrategy::STATIC, observer_);
  ASSERT_NE(session, nullptr);

  constexpr size_t data_number = 10;
  auto in = PrepareInput(image_path, data_number);
  server_->Request(session, std::move(in), nullptr);

  WaitAsyncDone();
  auto response = observer_->GetPackage();
  EXPECT_EQ(response->data.size(), data_number);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, ProcessFailed) {
  std::string tag = "process failed";
  Session_t session = PrepareSession(tag, preproc_mlu_, postproc_, 5, BatchStrategy::STATIC, nullptr);
  ASSERT_NE(session, nullptr);

  constexpr size_t data_number = 1;
  auto in = Package::Create(data_number, "");
  Status s;
  PackagePtr out = Package::Create(0);
  server_->RequestSync(session, std::move(in), &s, out);

  server_->WaitTaskDone(session, tag);
  EXPECT_NE(s, Status::SUCCESS);
  auto fut = std::async(std::launch::async, [this, session]() { server_->DestroySession(session); });
  EXPECT_NE(std::future_status::timeout, fut.wait_for(std::chrono::seconds(1)));
}

TEST_F(InferServerRequestTest, DynamicBatchPreprocessHost) {
  Session_t session = PrepareSession("dynamic batch process with preprocess host", preproc_host_, postproc_, 5,
                                     BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  constexpr size_t data_number = 10;
  auto in = PrepareInput(image_path, data_number);
  server_->Request(session, std::move(in), nullptr);

  WaitAsyncDone();
  auto response = observer_->GetPackage();
  EXPECT_EQ(response->data.size(), data_number);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, InputContinuousData) {
  Session_t session =
      PrepareSession("continuous data input", empty_preproc_host_, postproc_, 5, BatchStrategy::STATIC, nullptr);
  ASSERT_NE(session, nullptr);

  size_t data_size = 12;
  auto in = Package::Create(data_size);
  ModelIO input;
  input.buffers = model_->AllocMluInput(0);
  in->predict_io.reset(new InferData);
  in->predict_io->Set(input);
  Status status;
  PackagePtr output = std::make_shared<Package>();
  server_->RequestSync(session, std::move(in), &status, output);
  EXPECT_EQ(output->data.size(), data_size);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, DynamicBatchSync) {
  auto postproc = std::make_shared<Postprocessor>();
  postproc->SetParams<Postprocessor::ProcessFunction>("process_function", g_empty_postproc_func);
  Session_t session = PrepareSession("dynamic batch sync", preproc_mlu_, postproc_, 5, BatchStrategy::DYNAMIC, nullptr);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  Status status;
  auto out = std::make_shared<Package>();
  EXPECT_TRUE(server_->RequestSync(session, in, &status, out));
  EXPECT_EQ(out->data.size(), in->data.size());
  EXPECT_EQ(status, Status::SUCCESS);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, DynamicBatchSyncTimeout) {
  Session_t session =
      PrepareSession("dynamic batch sync timeout", preproc_mlu_, postproc_, 5, BatchStrategy::DYNAMIC, nullptr);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  Status status;
  auto out = std::make_shared<Package>();
  EXPECT_TRUE(server_->RequestSync(session, std::move(in), &status, out, 5));
  EXPECT_EQ(out->data.size(), 0u);
  EXPECT_EQ(status, Status::TIMEOUT);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, OutputMluData) {
  std::shared_ptr<Processor> postproc = MyPostprocessor::Create();
  Session_t session = PrepareSession("output mlu data", preproc_mlu_, postproc, 5, BatchStrategy::DYNAMIC, nullptr);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  Status status;
  auto out = std::make_shared<Package>();
  EXPECT_TRUE(server_->RequestSync(session, in, &status, out));
  ASSERT_EQ(out->data.size(), in->data.size());
  ASSERT_EQ(status, Status::SUCCESS);
  for (auto& it : out->data) {
    Buffer buf;
    ASSERT_NO_THROW(buf = it->Get<Buffer>());
    EXPECT_TRUE(buf.OnMlu());
    EXPECT_TRUE(buf.OwnMemory());
  }
  out->data.clear();
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, ParallelInfer) {
  auto my_postproc = MyPostprocessor::Create();
  auto another_empty_preproc_host = PreprocessorHost::Create();
  Session_t session1 =
      PrepareSession("continuous data input 1", empty_preproc_host_, postproc_, 5, BatchStrategy::STATIC, nullptr);
  ASSERT_NE(session1, nullptr);
  Session_t session2 = PrepareSession("continuous data input 2", another_empty_preproc_host, my_postproc, 5,
                                      BatchStrategy::STATIC, nullptr);
  ASSERT_NE(session2, nullptr);

  size_t data_size = 12;
  ModelIO input;
  input.buffers = model_->AllocMluInput(0);

  auto in1 = Package::Create(data_size);
  in1->predict_io.reset(new InferData);
  in1->predict_io->Set(input);
  Status status1;
  PackagePtr output1 = std::make_shared<Package>();

  auto in2 = Package::Create(data_size);
  in2->predict_io.reset(new InferData);
  in2->predict_io->Set(input);
  Status status2;
  PackagePtr output2 = std::make_shared<Package>();

  auto fut1 = std::async(std::launch::async, [this, session1, &in1, &status1, &output1]() {
    return server_->RequestSync(session1, std::move(in1), &status1, output1);
  });
  auto fut2 = std::async(std::launch::async, [this, session2, &in2, &status2, &output2]() {
    return server_->RequestSync(session2, std::move(in2), &status2, output2);
  });

  ASSERT_TRUE(fut1.get());
  ASSERT_TRUE(fut2.get());

  ASSERT_EQ(status1, Status::SUCCESS);
  ASSERT_EQ(status2, Status::SUCCESS);
  EXPECT_EQ(output1->data.size(), data_size);
  EXPECT_EQ(output2->data.size(), data_size);
  server_->DestroySession(session1);
  server_->DestroySession(session2);
}

TEST_F(InferServerRequestTest, ResponseOrder) {
  Session_t session = PrepareSession("response order", preproc_host_, nullptr, 200, BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  constexpr int test_number = 1000;
  constexpr const char* tag = "test response order";
  for (int idx = 0; idx < test_number; ++idx) {
    auto in = std::make_shared<Package>();
    in->data.clear();
    in->data.emplace_back(new InferData);
    in->data[0]->Set(nullptr);
    in->tag = tag;
    ASSERT_TRUE(server_->Request(session, std::move(in), idx));
  }

  server_->WaitTaskDone(session, tag);

  int response_number = 0;
  while (response_number < test_number) {
    auto response = observer_->GetResponse();
    if (!response.first) continue;

    EXPECT_EQ(response.first->data.size(), 1u);
    EXPECT_EQ(any_cast<int>(response.second), response_number++);
  }

  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, MultiSessionProcessDynamic) {
  InferServer s(device_id_);

  cv::Mat img = cv::imread(GetExePath() + image_path);
  uint8_t* img_nv12 = new uint8_t[img.cols * img.rows * 3 / 2];
  cvt_bgr_to_yuv420sp(img, 1, PixelFmt::NV12, img_nv12);
  std::vector<std::thread> ts;

  auto proc_func = [this, img_nv12, &img](int id) {
    std::promise<Status> get_response;
    auto postproc = std::make_shared<Postprocessor>();
    postproc->SetParams<Postprocessor::ProcessFunction>("process_function", g_empty_postproc_func);
    Session_t session =
        PrepareSession("multisession dynamic batch [" + std::to_string(id), std::make_shared<PreprocessorMLU>(),
                       postproc, 200, BatchStrategy::DYNAMIC, std::make_shared<TestObserver>(get_response));
    ASSERT_NE(session, nullptr);

    PackagePtr in = std::make_shared<Package>();
    for (int i = 0; i < 10; ++i) {
      in->data.push_back(std::make_shared<InferData>());
      in->data[i]->Set(ConvertToVideoFrame(img_nv12, img.cols, img.rows));
    }
    server_->Request(session, std::move(in), nullptr);

    auto f = get_response.get_future();
    ASSERT_NE(f.wait_for(std::chrono::seconds(1)), std::future_status::timeout) << "wait for response timeout";
    EXPECT_EQ(f.get(), Status::SUCCESS);
    server_->DestroySession(session);
  };

  for (int i = 0; i < 10; i++) {
    ts.emplace_back(proc_func, i);
  }

  for (int i = 0; i < 10; ++i) {
    if (ts[i].joinable()) {
      ts[i].join();
    }
  }
  delete[] img_nv12;
}

TEST_F(InferServerRequestTest, MultiThreadProcessDynamic) {
  InferServer s(device_id_);

  cv::Mat img = cv::imread(GetExePath() + image_path);
  uint8_t* img_nv12 = new uint8_t[img.cols * img.rows * 3 / 2];
  cvt_bgr_to_yuv420sp(img, 1, PixelFmt::NV12, img_nv12);
  std::vector<std::thread> ts;

  auto postproc = std::make_shared<Postprocessor>();
  postproc->SetParams<Postprocessor::ProcessFunction>("process_function", g_empty_postproc_func);
  Session_t session = PrepareSession("multithread dynamic batch", std::make_shared<PreprocessorMLU>(), postproc, 200,
                                     BatchStrategy::DYNAMIC, nullptr);
  ASSERT_NE(session, nullptr);

  auto proc_func = [this, img_nv12, &img, session](int id) {
    PackagePtr in = std::make_shared<Package>();
    for (int i = 0; i < 10; ++i) {
      in->data.push_back(std::make_shared<InferData>());
      in->data[i]->Set(ConvertToVideoFrame(img_nv12, img.cols, img.rows));
    }
    in->tag = std::to_string(id);
    Status status;
    auto out = std::make_shared<Package>();
    ASSERT_TRUE(server_->RequestSync(session, std::move(in), &status, out, 1000));

    EXPECT_EQ(status, Status::SUCCESS);
    EXPECT_EQ(out->data.size(), 10u);
  };

  for (int i = 0; i < 10; i++) {
    ts.emplace_back(proc_func, i);
  }

  for (int i = 0; i < 10; ++i) {
    if (ts[i].joinable()) {
      ts[i].join();
    }
  }
  server_->DestroySession(session);
  delete[] img_nv12;
}

#ifdef CNIS_HAVE_CNCV
TEST_F(InferServerRequestTest, DynamicBatch_CNCV) {
  Session_t session =
      PrepareSession("dynamic batch process", preproc_mlu_, postproc_, 5, BatchStrategy::DYNAMIC, observer_, true);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  server_->Request(session, std::move(in), nullptr);

  WaitAsyncDone();
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, StaticBatch_CNCV) {
  Session_t session =
      PrepareSession("static batch process", preproc_mlu_, postproc_, 5, BatchStrategy::STATIC, observer_, true);
  ASSERT_NE(session, nullptr);

  constexpr size_t data_number = 10;
  auto in = PrepareInput(image_path, data_number);
  server_->Request(session, std::move(in), nullptr);

  WaitAsyncDone();
  auto response = observer_->GetPackage();
  EXPECT_EQ(response->data.size(), data_number);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, ProcessFailed_CNCV) {
  std::string tag = "process failed";
  Session_t session = PrepareSession(tag, preproc_mlu_, postproc_, 5, BatchStrategy::STATIC, nullptr, true);
  ASSERT_NE(session, nullptr);

  constexpr size_t data_number = 1;
  auto in = Package::Create(data_number, "");
  Status s;
  PackagePtr out = Package::Create(0);
  server_->RequestSync(session, std::move(in), &s, out);

  server_->WaitTaskDone(session, tag);
  EXPECT_NE(s, Status::SUCCESS);
  auto fut = std::async(std::launch::async, [this, session]() { server_->DestroySession(session); });
  EXPECT_NE(std::future_status::timeout, fut.wait_for(std::chrono::seconds(1)));
}

TEST_F(InferServerRequestTest, DynamicBatchSyncTimeout_CNCV) {
  Session_t session =
      PrepareSession("dynamic batch sync timeout", preproc_mlu_, postproc_, 5, BatchStrategy::DYNAMIC, nullptr, true);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  Status status;
  auto out = std::make_shared<Package>();
  EXPECT_TRUE(server_->RequestSync(session, std::move(in), &status, out, 5));
  EXPECT_EQ(out->data.size(), 0u);
  EXPECT_EQ(status, Status::TIMEOUT);
  server_->DestroySession(session);
}
#endif  // CNIS_HAVE_CNCV

}  // namespace infer_server
#endif  // CNIS_WITH_CONTRIB
