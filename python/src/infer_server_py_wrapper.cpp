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
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "glog/logging.h"
#include "pybind11/numpy.h"
#include "pybind11/pybind11.h"
#include "pybind11/stl.h"

#include "cnis/infer_server.h"
#include "cnis/processor.h"
#include "common_wrapper.hpp"
#include "processor_py_wrapper.hpp"

namespace py = pybind11;

namespace infer_server {

static DefaultIPreproc default_preproc;

void InferServerWrapper(const py::module& m) {
  py::class_<InferServer, std::shared_ptr<InferServer>>(m, "InferServer")
      .def(py::init<int>(), py::arg("dev_id"))
      .def("create_session",
          [](std::shared_ptr<InferServer> infer_server, SessionDesc desc, std::shared_ptr<Observer> observer) {
            if (!desc.preproc) {
              VLOG(1) << "[InferServer] [PythonAPI] Default preproc will be used in this session";
              desc.preproc = Preprocessor::Create();
              SetPreprocHandler(desc.model->GetKey(), &default_preproc);
            }
            return py::capsule(infer_server->CreateSession(desc, observer));
          })
      .def("create_sync_session",
          [](std::shared_ptr<InferServer> infer_server, SessionDesc desc) {
            if (!desc.preproc) {
              VLOG(1) << "[InferServer] [PythonAPI] Default preproc will be used in this synchronous session";
              desc.preproc = Preprocessor::Create();
              SetPreprocHandler(desc.model->GetKey(), &default_preproc);
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
      .def("load_model",
          [](std::shared_ptr<InferServer> infer_server, const std::string& model_url) {
            return infer_server->LoadModel(model_url, {});
          },
          py::arg("model_url"))

      .def_static("unload_model", &InferServer::UnloadModel)
      .def_static("clear_model_cache", &InferServer::ClearModelCache)
      .def("get_latency",
          [](std::shared_ptr<InferServer> infer_server, py::capsule session) {
            return infer_server->GetLatency(reinterpret_cast<Session_t>(session.get_pointer()));
          })
      .def("get_throughout",
          [](std::shared_ptr<InferServer> infer_server, py::capsule session, const std::string& tag) {
            if (tag.empty()) {
              return infer_server->GetThroughout(reinterpret_cast<Session_t>(session.get_pointer()));
            }
            return infer_server->GetThroughout(reinterpret_cast<Session_t>(session.get_pointer()), tag);
          }, py::arg("session"), py::arg("tag") = "");
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

void DataLayoutWrapper(py::module* m) {  // NOLINT
  py::class_<DataLayout, std::shared_ptr<DataLayout>>(*m, "DataLayout")
      .def(py::init())
      .def(py::init(
          [](DataType dtype, DimOrder order) {
            DataLayout layout{dtype, order};
            return layout;
          }),
          py::arg("dtype"), py::arg("order"))
      .def_readwrite("dtype", &DataLayout::dtype)
      .def_readwrite("order", &DataLayout::order);

  m->def("get_type_size", &GetTypeSize);

  py::enum_<DataType>(*m, "DataType")
      .value("UINT8", DataType::UINT8)
      .value("FLOAT32", DataType::FLOAT32)
      .value("FLOAT16", DataType::FLOAT16)
      .value("INT16", DataType::INT16)
      .value("INT32", DataType::INT32)
      .value("INVALID", DataType::INVALID);

  py::enum_<DimOrder>(*m, "DimOrder")
      .value("NCHW", DimOrder::NCHW)
      .value("NHWC", DimOrder::NHWC)
      .value("HWCN", DimOrder::HWCN)
      .value("TNC", DimOrder::TNC)
      .value("NTC", DimOrder::NTC)
      .value("NONE", DimOrder::NONE)
      .value("INVALID", DimOrder::INVALID);

  py::enum_<NetworkInputFormat>(*m, "NetworkInputFormat")
      .value("RGB", NetworkInputFormat::RGB)
      .value("BGR", NetworkInputFormat::BGR)
      .value("RGBA", NetworkInputFormat::RGBA)
      .value("BGRA", NetworkInputFormat::BGRA)
      .value("ARGB", NetworkInputFormat::ARGB)
      .value("ABGR", NetworkInputFormat::ABGR)
      .value("GRAY", NetworkInputFormat::GRAY)
      .value("TENSOR", NetworkInputFormat::TENSOR)
      .value("INVALID", NetworkInputFormat::INVALID);
}

void BatchStrategyWrapper(const py::module& m) {
  py::enum_<BatchStrategy>(m, "BatchStrategy")
      .value("DYNAMIC", BatchStrategy::DYNAMIC)
      .value("STATIC", BatchStrategy::STATIC)
      .value("SEQUENCE", BatchStrategy::SEQUENCE)
      .value("STRATEGY_COUNT", BatchStrategy::STRATEGY_COUNT);
}

void DeviceWrapper(py::module *m) {
  m->def("set_current_device", &SetCurrentDevice);
  m->def("check_device", &CheckDevice);
  m->def("total_device_count", &TotalDeviceCount);
}

void PerfWrapper(py::module* m) {
  py::class_<LatencyStatistic>(*m, "LatencyStatistic")
      .def(py::init())
      .def_readwrite("unit_cnt", &LatencyStatistic::unit_cnt)
      .def_readwrite("total", &LatencyStatistic::total)
      .def_readwrite("max", &LatencyStatistic::max)
      .def_readwrite("min", &LatencyStatistic::min);

  py::class_<ThroughoutStatistic>(*m, "ThroughoutStatistic")
      .def(py::init())
      .def_readwrite("request_cnt", &ThroughoutStatistic::request_cnt)
      .def_readwrite("unit_cnt", &ThroughoutStatistic::unit_cnt)
      .def_readwrite("rps", &ThroughoutStatistic::rps)
      .def_readwrite("ups", &ThroughoutStatistic::ups)
      .def_readwrite("rps_rt", &ThroughoutStatistic::rps_rt)
      .def_readwrite("ups_rt", &ThroughoutStatistic::ups_rt);
}

}  //  namespace infer_server
