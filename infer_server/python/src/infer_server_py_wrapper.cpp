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
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "common_wrapper.hpp"
#include "cxxutil/log.h"

namespace py = pybind11;

namespace infer_server {

std::shared_ptr<py::class_<InferData, std::shared_ptr<InferData>>> gPyInferDataRegister;

void InferServerWrapper(const py::module& m) {
  py::class_<InferServer, std::shared_ptr<InferServer>>(m, "InferServer")
      .def(py::init<int>(), py::arg("dev_id"))
      .def("create_session",
           [](std::shared_ptr<InferServer> infer_server, SessionDesc desc, std::shared_ptr<Observer> observer) {
             if (!desc.preproc) {
               LOGD(CNIS_PY_API) << "Default preproc will be used";
               desc.preproc = PreprocessorHost::Create();
               desc.preproc->SetParams<PreprocessorHost::ProcessFunction>("process_function", DefaultPreprocExecute);
             }
             return py::capsule(infer_server->CreateSession(desc, observer));
           })
      .def("create_sync_session",
           [](std::shared_ptr<InferServer> infer_server, SessionDesc desc) {
             if (!desc.preproc) {
               LOGD(CNIS_PY_API) << "Default preproc will be used";
               desc.preproc = PreprocessorHost::Create();
               desc.preproc->SetParams<PreprocessorHost::ProcessFunction>("process_function", DefaultPreprocExecute);
             }
             return py::capsule(infer_server->CreateSyncSession(desc));
           })
      .def("destroy_session",
           [](std::shared_ptr<InferServer> infer_server, py::capsule session) {
             return infer_server->DestroySession(reinterpret_cast<Session_t>(session.get_pointer()));
           })
      .def("request",
           [](std::shared_ptr<InferServer> infer_server, py::capsule session, PackagePtr input, py::dict user_data,
              int timeout) {
             std::shared_ptr<py::dict> dict_ptr = std::shared_ptr<py::dict>(new py::dict(), [](py::dict* t) {
               // py::dict destruct in c++ thread without gil resource, it is important to get gil
               py::gil_scoped_acquire gil;
               delete t;
             });
             (*dict_ptr) = user_data;
             return infer_server->Request(reinterpret_cast<Session_t>(session.get_pointer()), input, dict_ptr, timeout);
           },
           py::arg("session"), py::arg("input"), py::arg("user_data"), py::arg("timeout") = -1)
      .def("request_sync",
           [](std::shared_ptr<InferServer> infer_server, py::capsule session, PackagePtr input,
              std::reference_wrapper<Status> status, PackagePtr response, int timeout) {
             bool ret = infer_server->RequestSync(reinterpret_cast<Session_t>(session.get_pointer()), input,
                                                  &(status.get()), response, timeout);
             return ret;
           },
           py::arg("session"), py::arg("input"), py::arg("status"), py::arg("response"), py::arg("timeout") = -1,
           py::call_guard<py::gil_scoped_release>())
      .def("wait_task_done",
           [](std::shared_ptr<InferServer> infer_server, py::capsule session, const std::string& tag) {
             infer_server->WaitTaskDone(reinterpret_cast<Session_t>(session.get_pointer()), tag);
           },
           py::call_guard<py::gil_scoped_release>())
      .def("discard_task",
           [](std::shared_ptr<InferServer> infer_server, py::capsule session, const std::string& tag) {
             infer_server->DiscardTask(reinterpret_cast<Session_t>(session.get_pointer()), tag);
           })
      .def("get_model",
           [](std::shared_ptr<InferServer> infer_server, py::capsule session) {
             return infer_server->GetModel(reinterpret_cast<Session_t>(session.get_pointer()));
           })
      .def_static("set_model_dir", &InferServer::SetModelDir)
#ifdef CNIS_USE_MAGICMIND
      .def("load_model",
           [](std::shared_ptr<InferServer> infer_server, const std::string& model_url,
              const std::vector<Shape>& input_shapes) {
             return infer_server->LoadModel(model_url, input_shapes);
           },
           py::arg("model_url"), py::arg("input_shapes") = std::vector<Shape>{})
#else
      .def("load_model",
           [](std::shared_ptr<InferServer> infer_server, const std::string& pattern1, const std::string& pattern2) {
             return infer_server->LoadModel(pattern1, pattern2);
           },
           py::arg("pattern1"), py::arg("pattern2") = "subnet0")
#endif
      .def_static("unload_model", &InferServer::UnloadModel)
      .def_static("clear_model_cache", &InferServer::ClearModelCache);
}

void PackageWrapper(const py::module& m) {
  py::class_<Package, std::shared_ptr<Package>>(m, "Package")
      .def(py::init([](uint32_t data_num, const std::string& tag) { return Package::Create(data_num, tag); }),
           py::arg("data_num"), py::arg("tag") = "")
      .def_readwrite("data", &Package::data)
      .def_readwrite("tag", &Package::tag)
      .def_readwrite("perf", &Package::perf)
      .def_readwrite("priority", &Package::priority);

  gPyInferDataRegister = std::make_shared<py::class_<InferData, std::shared_ptr<InferData>>>(m, "InferData");
  (*gPyInferDataRegister)
      .def(py::init<>())
      .def("has_value", &InferData::HasValue)
      // Custom dictionary
      .def("set",
           [](InferData* data, py::dict dict) {
             std::shared_ptr<py::dict> dict_ptr = std::shared_ptr<py::dict>(new py::dict(), [](py::dict* t) {
               // py::dict destruct in c++ thread without gil resource, it is important to get gil
               py::gil_scoped_acquire gil;
               delete t;
             });
             (*dict_ptr) = dict;
             data->Set(dict_ptr);
           })
      .def("get_dict", [](InferData* data) { return *(data->Get<std::shared_ptr<py::dict>>()); })
      // set array (cv mat)
      .def("set",
           [](InferData* data, py::array img_array) {
             cv::Mat cv_img = ArrayToMat(img_array);
             data->Set(std::move(cv_img));
           })
      .def("get_array", [](InferData* data) { return MatToArray(data->GetLref<cv::Mat>()); },
           py::return_value_policy::reference)
      // get ModelIO
      .def("get_model_io", [](InferData* data) { return std::reference_wrapper<ModelIO>(data->GetLref<ModelIO>()); },
           py::return_value_policy::reference)
      .def("set_user_data",
           [](InferData* data, py::dict dict) {
             std::shared_ptr<py::dict> dict_ptr = std::shared_ptr<py::dict>(new py::dict(), [](py::dict* t) {
               // py::dict destruct in c++ thread without gil resource, it is important to get gil
               py::gil_scoped_acquire gil;
               delete t;
             });
             (*dict_ptr) = dict;
             data->SetUserData(dict_ptr);
           })
      .def("get_user_data", [](InferData* data) { return *(data->GetUserData<std::shared_ptr<py::dict>>()); });
}

void StatusWrapper(const py::module& m) {
  py::enum_<Status>(m, "Status")
      .value("SUCCESS", Status::SUCCESS)
      .value("ERROR_READWRITE", Status::ERROR_READWRITE)
      .value("ERROR_MEMORY", Status::ERROR_MEMORY)
      .value("INVALID_PARAM", Status::INVALID_PARAM)
      .value("WRONG_TYPE", Status::WRONG_TYPE)
      .value("ERROR_BACKEND", Status::ERROR_BACKEND)
      .value("NOT_IMPLEMENTED", Status::NOT_IMPLEMENTED)
      .value("TIMEOUT", Status::TIMEOUT)
      .value("STATUS_COUNT", Status::STATUS_COUNT);
}

void DataLayoutWrapper(py::module& m) {  // NOLINT
  py::class_<DataLayout, std::shared_ptr<DataLayout>>(m, "DataLayout")
      .def(py::init())
      .def(py::init([](DataType dtype, DimOrder order) {
             DataLayout layout{dtype, order};
             return layout;
           }),
           py::arg("dtype"), py::arg("order"))
      .def_readwrite("dtype", &DataLayout::dtype)
      .def_readwrite("order", &DataLayout::order);

  m.def("get_type_size", &GetTypeSize);

  py::enum_<DataType>(m, "DataType")
      .value("UINT8", DataType::UINT8)
      .value("FLOAT32", DataType::FLOAT32)
      .value("FLOAT16", DataType::FLOAT16)
      .value("INT16", DataType::INT16)
      .value("INT32", DataType::INT32)
      .value("INVALID", DataType::INVALID);

  py::enum_<DimOrder>(m, "DimOrder")
      .value("NCHW", DimOrder::NCHW)
      .value("NHWC", DimOrder::NHWC)
      .value("HWCN", DimOrder::HWCN)
      .value("TNC", DimOrder::TNC)
      .value("NTC", DimOrder::NTC);
}

void BatchStrategyWrapper(const py::module& m) {
  py::enum_<BatchStrategy>(m, "BatchStrategy")
      .value("DYNAMIC", BatchStrategy::DYNAMIC)
      .value("STATIC", BatchStrategy::STATIC);
}

void DeviceWrapper(py::module& m) {  // NOLINT
  m.def("set_current_device", &SetCurrentDevice);
  m.def("check_device", &CheckDevice);
  m.def("total_device_count", &TotalDeviceCount);
}

}  //  namespace infer_server
