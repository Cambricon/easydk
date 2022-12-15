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
#include <fstream>
#include <future>
#include <iostream>
#include <list>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "../test_base.h"
#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "fixture.h"

namespace infer_server {

static constexpr bool preproc_normalize{false};
static constexpr bool keep_aspect_ratio{true};
static constexpr int pad_value{128};
static constexpr bool transpose{false};

constexpr const char* image_path = "../../unitest/data/500x500.jpg";

class ReqTestObserver : public Observer {
 public:
  ReqTestObserver(std::promise<Status>& get_response) : get_response_(get_response) {}  // NOLINT

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

class PreprocHandleTest : public IPreproc {
 public:
  PreprocHandleTest() = default;
  ~PreprocHandleTest() = default;

 private:
  int OnTensorParams(const CnPreprocTensorParams* params) override { return 0; }
  int OnPreproc(cnedk::BufSurfWrapperPtr src, cnedk::BufSurfWrapperPtr dst,
                const std::vector<CnedkTransformRect>& src_rects) override {
    return 0;
  }
};

class PostprocHandleTest : public IPostproc {
 public:
  ~PostprocHandleTest() { }
  int OnPostproc(const std::vector<InferData*> &data_vec, const ModelIO& model_output,
                  const ModelInfo* model_info) override { return 0; }
};

class InferServerRequestTest : public InferServerTestAPI {
 protected:
  void SetUp() override {
    SetMluContext();
    model_ = server_->LoadModel(GetModelInfoStr("resnet50", "url"));
    if (!model_) {
      LOG(ERROR) << "[EasyDK Tests] [InferServer] Load model failed";
      std::terminate();
    }
    preproc_handler_ = std::make_shared<PreprocHandleTest>();
    postproc_handler_ = std::make_shared<PostprocHandleTest>();
    preproc_ = Preprocessor::Create();
    SetPreprocHandler(model_->GetKey(), preproc_handler_.get());
    postproc_ = Postprocessor::Create();
    SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
    observer_ = std::make_shared<ReqTestObserver>(get_response_);

    InitPlatform();
  }
  void TearDown() override {}

  PreprocInput ConvertToPreprocInput(uint8_t* img_nv12, size_t w, size_t h) {
    CnedkBufSurfaceCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id = device_id_;
    create_params.batch_size = 1;
    create_params.width = w;
    create_params.height = h;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
    create_params.mem_type = CNEDK_BUF_MEM_DEFAULT;

    CnedkBufSurface* surf;
    EXPECT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    cnedk::BufSurfWrapperPtr buf_surface = std::make_shared<cnedk::BufSurfaceWrapper>(surf);
    uint8_t* y_plane = static_cast<uint8_t*>(buf_surface->GetHostData(0, 0));
    uint8_t* uv_plane = static_cast<uint8_t*>(buf_surface->GetHostData(1, 0));
    size_t frame_size = w * h;
    memcpy(y_plane, img_nv12, frame_size);
    memcpy(uv_plane, img_nv12 + frame_size, frame_size / 2);

    PreprocInput frame;
    frame.surf = buf_surface;
    return frame;
  }

  PackagePtr PrepareInput(const std::string& image_path, size_t data_num) {
    PackagePtr in = std::make_shared<Package>();
    cv::Mat img = cv::imread(GetExePath() + image_path);
    size_t frame_size = img.cols * img.rows;
    uint8_t* img_nv12 = new uint8_t[frame_size * 3 / 2];
    cvt_bgr_to_yuv420sp(img, 1, CNEDK_BUF_COLOR_FORMAT_NV12, img_nv12);
    for (size_t i = 0; i < data_num; ++i) {
      in->data.emplace_back(new InferData);
      in->data[i]->Set(ConvertToPreprocInput(img_nv12, img.cols, img.rows));
    }
    delete[] img_nv12;
    return in;
  }

  Session_t PrepareSession(const std::string& name, std::shared_ptr<Processor> preproc,
                           std::shared_ptr<Processor> postproc, size_t batch_timeout, BatchStrategy strategy,
                           std::shared_ptr<Observer> observer) {
    SessionDesc desc;
    desc.name = name;
    desc.model = model_;
    desc.strategy = strategy;
    desc.preproc = std::move(preproc);
    desc.postproc = std::move(postproc);
    desc.batch_timeout = batch_timeout;
    desc.engine_num = 2;
    desc.show_perf = true;
    desc.priority = 0;
    desc.model_input_format = infer_server::NetworkInputFormat::RGB;
    if (observer) {
      return server_->CreateSession(desc, observer);
    } else {
      return server_->CreateSyncSession(desc);
    }
  }

  void WaitAsyncDone() {
    auto f = get_response_.get_future();
    ASSERT_NE(f.wait_for(std::chrono::seconds(30)), std::future_status::timeout)
        << "[EasyDK Tests] [InferServer] Wait for response timeout";
    EXPECT_EQ(f.get(), Status::SUCCESS);
  }

  ModelPtr model_;
  std::shared_ptr<Processor> preproc_;
  std::shared_ptr<Processor> postproc_;
  std::shared_ptr<ReqTestObserver> observer_;
  std::shared_ptr<PreprocHandleTest> preproc_handler_;
  std::shared_ptr<PostprocHandleTest> postproc_handler_;

  std::promise<Status> get_response_;
};

class MyPostprocessor : public ProcessorForkable<MyPostprocessor> {
 public:
  MyPostprocessor() : ProcessorForkable<MyPostprocessor>("MyPostprocessor") {}
  ~MyPostprocessor() {}
  Status Process(PackagePtr pack) noexcept override {
    if (!pack->predict_io || !pack->predict_io->HasValue()) return Status::ERROR_BACKEND;
    if (!SetCurrentDevice(dev_id_)) return Status::ERROR_BACKEND;

    auto output = pack->predict_io->Get<ModelIO>();
    for (size_t out_idx = 0; out_idx < output.surfs.size(); ++out_idx) {
      CnedkBufSurfaceCreateParams create_params;
      memset(&create_params, 0, sizeof(create_params));
      create_params.device_id = dev_id_;
      create_params.batch_size = 1;
      create_params.size = output.surfs[out_idx]->GetBufSurface()->surface_list[0].data_size;
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
      create_params.mem_type = CNEDK_BUF_MEM_DEFAULT;

      CnedkBufSurface* surf;
      EXPECT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
      cnedk::BufSurfWrapperPtr buf_surface = std::make_shared<cnedk::BufSurfaceWrapper>(surf);
      pack->data[out_idx]->Set(buf_surface);
    }
    return Status::SUCCESS;
  }
  Status Init() noexcept override {
    constexpr const char* params[] = {"model_info", "device_id"};
    for (auto p : params) {
      if (!HaveParam(p)) {
        LOG(ERROR) << "[EasyDK Tests] [InferServer] " << p << " has not been set!";
        return Status::INVALID_PARAM;
      }
    }
    try {
      model_ = GetParam<ModelPtr>("model_info");
      dev_id_ = GetParam<int>("device_id");
    } catch (bad_any_cast&) {
      LOG(ERROR) << "[EasyDK Tests] [InferServer] Unmatched data type";
      return Status::WRONG_TYPE;
    }
    InitPlatform();

    return Status::SUCCESS;
  }

 private:
  ModelPtr model_;
  int dev_id_;
};

TEST_F(InferServerRequestTest, EmptyPackage) {
  SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
  Session_t session =
      PrepareSession("empty package process", preproc_, postproc_, 5, BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  EXPECT_EQ(model_, server_->GetModel(session));

  constexpr const char* tag = "EmptyPackage";
  auto in = PrepareInput(image_path, 10);
  in->tag = tag;
  ASSERT_TRUE(server_->Request(session, std::move(in), nullptr));

  in = Package::Create(0, tag);
  ASSERT_TRUE(server_->Request(session, std::move(in), nullptr));

  server_->WaitTaskDone(session, tag);
  WaitAsyncDone();
  EXPECT_EQ(observer_->ResponseNum(), 2u);
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, DynamicBatch) {
  SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
  Session_t session =
      PrepareSession("dynamic batch process", preproc_, postproc_, 5, BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  server_->Request(session, std::move(in), nullptr);

  WaitAsyncDone();
  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, SkipPostproc) {
  // no process_function param
  SetPostprocHandler(model_->GetKey(), nullptr);
  Session_t session =
      PrepareSession("skip postproc process", preproc_, postproc_, 5, BatchStrategy::DYNAMIC, observer_);
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
  session = PrepareSession("skip postproc process", preproc_, nullptr, 5, BatchStrategy::DYNAMIC, nullptr);
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
  SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
  Session_t session = PrepareSession("static batch process", preproc_, postproc_, 5, BatchStrategy::STATIC, observer_);
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
  SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
  Session_t session = PrepareSession(tag, preproc_, postproc_, 5, BatchStrategy::STATIC, nullptr);
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

TEST_F(InferServerRequestTest, DynamicBatchSync) {
  auto postproc = std::make_shared<Postprocessor>();
  SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
  Session_t session = PrepareSession("dynamic batch sync", preproc_, postproc_, 5, BatchStrategy::DYNAMIC, nullptr);
  ASSERT_NE(session, nullptr);

  auto in = PrepareInput(image_path, 10);
  Status status;
  auto out = std::make_shared<Package>();
  EXPECT_TRUE(server_->RequestSync(session, in, &status, out));
  EXPECT_EQ(out->data.size(), in->data.size());
  EXPECT_EQ(status, Status::SUCCESS);
  server_->DestroySession(session);
}

// TEST_F(InferServerRequestTest, DynamicBatchSyncTimeout) {
//   Session_t session =
//       PrepareSession("dynamic batch sync timeout", preproc_, postproc_, 5, BatchStrategy::DYNAMIC, nullptr);
//   ASSERT_NE(session, nullptr);

//   auto in = PrepareInput(image_path, 10);
//   Status status;
//   auto out = std::make_shared<Package>();
//   EXPECT_TRUE(server_->RequestSync(session, std::move(in), &status, out, 5));
//   EXPECT_EQ(out->data.size(), 0u);
//   EXPECT_EQ(status, Status::TIMEOUT);
//   server_->DestroySession(session);
// }


TEST_F(InferServerRequestTest, ParallelInfer) {
  auto my_postproc = MyPostprocessor::Create();
  auto another_empty_preproc_host = Preprocessor::Create();
  SetPreprocHandler(model_->GetKey(), preproc_handler_.get());
  SetPostprocHandler(model_->GetKey(), postproc_handler_.get());

  Session_t session1 =
      PrepareSession("continuous data input 1", preproc_, postproc_, 5, BatchStrategy::STATIC, nullptr);
  ASSERT_NE(session1, nullptr);
  Session_t session2 = PrepareSession("continuous data input 2", another_empty_preproc_host, my_postproc, 5,
                                      BatchStrategy::STATIC, nullptr);
  ASSERT_NE(session2, nullptr);

  size_t data_size = 1;

  size_t len = model_->InputShape(0).BatchDataCount() * GetTypeSize(model_->InputLayout(0).dtype);
  CnedkBufSurfaceCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = device_id_;
  create_params.batch_size = 1;
  create_params.size = len;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
  create_params.mem_type = CNEDK_BUF_MEM_DEFAULT;
  CnedkBufSurface* surf;
  EXPECT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
  cnedk::BufSurfWrapperPtr buf_surface = std::make_shared<cnedk::BufSurfaceWrapper>(surf);
  PreprocInput input;
  input.surf = buf_surface;

  auto in1 = Package::Create(data_size);
  in1->data[0]->Set(input);
  Status status1;
  PackagePtr output1 = std::make_shared<Package>();

  auto in2 = Package::Create(data_size);
  in2->data[0]->Set(input);
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

  EXPECT_EQ(status1, Status::SUCCESS);
  EXPECT_EQ(status2, Status::SUCCESS);
  EXPECT_EQ(output1->data.size(), data_size);
  EXPECT_EQ(output2->data.size(), data_size);
  server_->DestroySession(session1);
  server_->DestroySession(session2);
}

TEST_F(InferServerRequestTest, ResponseOrder) {
  Session_t session = PrepareSession("response order", preproc_, nullptr, 200, BatchStrategy::DYNAMIC, observer_);
  ASSERT_NE(session, nullptr);

  constexpr int test_number = 1000;
  constexpr const char* tag = "test response order";
  constexpr int data_len = 100;
  CnedkBufSurfaceCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = device_id_;
  create_params.batch_size = 1;
  create_params.size = data_len;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
  create_params.mem_type = CNEDK_BUF_MEM_DEFAULT;

  for (int idx = 0; idx < test_number; ++idx) {
    auto in = Package::Create(4);
    in->tag = tag;
    for (auto it : in->data) {
      PreprocInput input;
      CnedkBufSurface* surf;
      EXPECT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
      cnedk::BufSurfWrapperPtr buf_surface = std::make_shared<cnedk::BufSurfaceWrapper>(surf);
      input.surf = buf_surface;
      it->Set<PreprocInput>(std::move(input));
    }
    ASSERT_TRUE(server_->Request(session, std::move(in), idx));
  }

  server_->WaitTaskDone(session, tag);

  int response_number = 0;
  while (response_number < test_number) {
    auto response = observer_->GetResponse();
    if (!response.first) continue;

    EXPECT_EQ(response.first->data.size(), 4u);
    EXPECT_EQ(any_cast<int>(response.second), response_number++);
  }

  server_->DestroySession(session);
}

TEST_F(InferServerRequestTest, MultiSessionProcessDynamic) {
  InferServer s(device_id_);

  cv::Mat img = cv::imread(GetExePath() + image_path);
  uint8_t* img_nv12 = new uint8_t[img.cols * img.rows * 3 / 2];
  cvt_bgr_to_yuv420sp(img, 1, CNEDK_BUF_COLOR_FORMAT_NV12, img_nv12);
  std::vector<std::thread> ts;

  auto proc_func = [this, img_nv12, &img](int id) {
    std::promise<Status> get_response;
    auto postproc = std::make_shared<Postprocessor>();
    SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
    Session_t session =
        PrepareSession("multisession dynamic batch [" + std::to_string(id), std::make_shared<Preprocessor>(), postproc,
                       200, BatchStrategy::DYNAMIC, std::make_shared<ReqTestObserver>(get_response));
    ASSERT_NE(session, nullptr);

    PackagePtr in = std::make_shared<Package>();
    for (int i = 0; i < 10; ++i) {
      in->data.push_back(std::make_shared<InferData>());
      in->data[i]->Set(ConvertToPreprocInput(img_nv12, img.cols, img.rows));
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
  cvt_bgr_to_yuv420sp(img, 1, CNEDK_BUF_COLOR_FORMAT_NV12, img_nv12);
  std::vector<std::thread> ts;

  auto postproc = std::make_shared<Postprocessor>();
  SetPostprocHandler(model_->GetKey(), postproc_handler_.get());
  Session_t session = PrepareSession("multithread dynamic batch", std::make_shared<Preprocessor>(), postproc, 200,
                                     BatchStrategy::DYNAMIC, nullptr);
  ASSERT_NE(session, nullptr);

  auto proc_func = [this, img_nv12, &img, session](int id) {
    PackagePtr in = std::make_shared<Package>();
    for (int i = 0; i < 1; ++i) {
      in->data.push_back(std::make_shared<InferData>());
      in->data[i]->Set(ConvertToPreprocInput(img_nv12, img.cols, img.rows));
    }
    in->tag = std::to_string(id);
    Status status;
    auto out = std::make_shared<Package>();
    ASSERT_TRUE(server_->RequestSync(session, std::move(in), &status, out, 10000));

    EXPECT_EQ(status, Status::SUCCESS);
    EXPECT_EQ(out->data.size(), 1u);
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

}  // namespace infer_server
