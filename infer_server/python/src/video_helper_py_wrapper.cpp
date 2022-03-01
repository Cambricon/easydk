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
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <utility>

#include "cnis/contrib/video_helper.h"
#include "common_wrapper.hpp"
#include "cxxutil/log.h"

namespace py = pybind11;

namespace infer_server {

extern std::shared_ptr<py::class_<SessionDesc, std::shared_ptr<SessionDesc>>> gPySessionDescRegister;
extern std::shared_ptr<py::class_<InferData, std::shared_ptr<InferData>>> gPyInferDataRegister;

void VideoHelperWrapper(py::module& m) {  // NOLINT
  gPySessionDescRegister->def("set_preproc_params",
                              [](std::shared_ptr<SessionDesc> desc, video::PixelFmt dst_fmt,
                                 video::PreprocessType preproc_type, bool keep_aspect_ratio) {
                                if (desc && desc->preproc) {
                                  desc->preproc->SetParams("dst_format", dst_fmt, "preprocess_type", preproc_type,
                                                           "keep_aspect_ratio", keep_aspect_ratio);
                                }
                              },
                              py::arg("dst_fmt"), py::arg("preproc_type"), py::arg("keep_aspect_ratio") = false);

  gPyInferDataRegister->def("set", [](InferData* data, std::reference_wrapper<video::VideoFrame> vframe) {
    data->Set(std::move(vframe.get()));
  });
  gPyInferDataRegister->def(
      "get_video_frame",
      [](InferData* data) { return std::reference_wrapper<video::VideoFrame>(data->GetLref<video::VideoFrame>()); },
      py::return_value_policy::reference);

  m.def("get_plane_num", &video::GetPlaneNum);

  py::enum_<video::PixelFmt>(m, "VideoPixelFmt")
      .value("I420", video::PixelFmt::I420)
      .value("NV12", video::PixelFmt::NV12)
      .value("NV21", video::PixelFmt::NV21)
      .value("RGB24", video::PixelFmt::RGB24)
      .value("BGR24", video::PixelFmt::BGR24)
      .value("RGBA", video::PixelFmt::RGBA)
      .value("BGRA", video::PixelFmt::BGRA)
      .value("ARGB", video::PixelFmt::ARGB)
      .value("ABGR", video::PixelFmt::ABGR);

  py::class_<video::BoundingBox, std::shared_ptr<video::BoundingBox>>(m, "BoundingBox")
      .def(py::init<>())
      .def(py::init([](float x, float y, float w, float h) {
             video::BoundingBox bbox;
             bbox.x = x;
             bbox.y = y;
             bbox.w = w;
             bbox.h = h;
             return bbox;
           }),
           py::arg("x") = 0, py::arg("y") = 0, py::arg("w") = 0, py::arg("h") = 0)
      .def_readwrite("x", &video::BoundingBox::x)
      .def_readwrite("y", &video::BoundingBox::y)
      .def_readwrite("w", &video::BoundingBox::w)
      .def_readwrite("h", &video::BoundingBox::h);

  py::class_<video::VideoFrame, std::shared_ptr<video::VideoFrame>>(m, "VideoFrame")
      .def(py::init<>())
      .def_property("stride",
                    [](const video::VideoFrame& video_frame) {
                      return py::array_t<size_t>({MAX_PLANE_NUM}, {sizeof(size_t)}, video_frame.stride);
                    },
                    [](std::shared_ptr<video::VideoFrame> video_frame, py::array_t<size_t> strides) {
                      py::buffer_info strides_buf = strides.request();
                      int size = std::min(static_cast<int>(strides_buf.size), MAX_PLANE_NUM);
                      memcpy(video_frame->stride, strides_buf.ptr, size * sizeof(size_t));
                    })
      .def_readwrite("width", &video::VideoFrame::width)
      .def_readwrite("height", &video::VideoFrame::height)
      .def_readwrite("plane_num", &video::VideoFrame::plane_num)
      .def_readwrite("format", &video::VideoFrame::format)
      .def_readwrite("roi", &video::VideoFrame::roi)
      .def("get_plane_size", &video::VideoFrame::GetPlaneSize)
      .def("get_total_size", &video::VideoFrame::GetTotalSize)
      .def("get_plane",
           [](std::shared_ptr<video::VideoFrame> video_frame, int idx) {
             if (idx < MAX_PLANE_NUM) {
               return std::reference_wrapper<Buffer>(video_frame->plane[idx]);
             } else {
               return std::reference_wrapper<Buffer>(video_frame->plane[MAX_PLANE_NUM - 1]);
             }
           })
      .def("set_plane",
           [](std::shared_ptr<video::VideoFrame> video_frame, int idx, std::reference_wrapper<Buffer> plane) {
             if (idx < MAX_PLANE_NUM) {
               video_frame->plane[idx] = std::move(plane.get());
             }
           });

  py::enum_<video::PreprocessType>(m, "VideoPreprocessType")
      .value("UNKNOWN", video::PreprocessType::UNKNOWN)
      .value("RESIZE_CONVERT", video::PreprocessType::RESIZE_CONVERT)
      .value("SCALER", video::PreprocessType::SCALER)
      .value("CNCV_PREPROC", video::PreprocessType::CNCV_PREPROC);

  py::class_<video::VideoInferServer, std::shared_ptr<video::VideoInferServer>>(m, "VideoInferServer")
      .def(py::init<int>(), py::arg("dev_id"))
      .def("request",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session, PackagePtr input,
              py::dict user_data, int timeout) {
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
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session, PackagePtr input,
              std::reference_wrapper<Status> status, PackagePtr response, int timeout) {
             bool ret = infer_server->RequestSync(reinterpret_cast<Session_t>(session.get_pointer()), input,
                                                  &(status.get()), response, timeout);
             return ret;
           },
           py::arg("session"), py::arg("input"), py::arg("status"), py::arg("response"), py::arg("timeout") = -1,
           py::call_guard<py::gil_scoped_release>())
      .def("request",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session,
              std::reference_wrapper<video::VideoFrame> vframe, const std::string& tag, py::dict user_data,
              int timeout) {
             std::shared_ptr<py::dict> dict_ptr = std::shared_ptr<py::dict>(new py::dict(), [](py::dict* t) {
               // py::dict destruct in c++ thread without gil resource, it is important to get gil
               py::gil_scoped_acquire gil;
               delete t;
             });
             (*dict_ptr) = user_data;
             Session_t session_ptr = reinterpret_cast<Session_t>(session.get_pointer());
             return infer_server->Request(session_ptr, vframe.get(), tag, dict_ptr, timeout);
           },
           py::arg("session"), py::arg("vframe"), py::arg("tag"), py::arg("user_data"), py::arg("timeout") = -1)
      .def("request",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session,
              std::reference_wrapper<video::VideoFrame> vframe,
              std::reference_wrapper<std::vector<video::BoundingBox>> objs, const std::string& tag, py::dict user_data,
              int timeout) {
             std::shared_ptr<py::dict> dict_ptr = std::shared_ptr<py::dict>(new py::dict(), [](py::dict* t) {
               // py::dict destruct in c++ thread without gil resource, it is important to get gil
               py::gil_scoped_acquire gil;
               delete t;
             });
             (*dict_ptr) = user_data;
             Session_t session_ptr = reinterpret_cast<Session_t>(session.get_pointer());
             return infer_server->Request(session_ptr, vframe.get(), objs.get(), tag, dict_ptr, timeout);
           },
           py::arg("session"), py::arg("vframe"), py::arg("objs"), py::arg("tag"), py::arg("user_data"),
           py::arg("timeout") = -1)
      .def("request_sync",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session,
              std::reference_wrapper<video::VideoFrame> vframe, const std::string& tag,
              std::reference_wrapper<Status> status, PackagePtr response, int timeout) {
             bool ret = infer_server->RequestSync(reinterpret_cast<Session_t>(session.get_pointer()), vframe.get(), tag,
                                                  &(status.get()), response, timeout);
             return ret;
           },
           py::arg("session"), py::arg("vframe"), py::arg("tag"), py::arg("status"), py::arg("response"),
           py::arg("timeout") = -1, py::call_guard<py::gil_scoped_release>())
      .def("request_sync",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session,
              std::reference_wrapper<video::VideoFrame> vframe,
              std::reference_wrapper<std::vector<video::BoundingBox>> objs, const std::string& tag,
              std::reference_wrapper<Status> status, PackagePtr response, int timeout) {
             bool ret = infer_server->RequestSync(reinterpret_cast<Session_t>(session.get_pointer()), vframe.get(),
                                                  objs.get(), tag, &(status.get()), response, timeout);
             return ret;
           },
           py::arg("session"), py::arg("vframe"), py::arg("objs"), py::arg("tag"), py::arg("status"),
           py::arg("response"), py::arg("timeout") = -1, py::call_guard<py::gil_scoped_release>())
      .def("create_session",
           [](std::shared_ptr<video::VideoInferServer> infer_server, SessionDesc desc,
              std::shared_ptr<Observer> observer) {
             if (!desc.preproc) {
               LOGD(CNIS_PY_API) << "Default preproc will be used";
               desc.preproc = PreprocessorHost::Create();
               desc.preproc->SetParams<PreprocessorHost::ProcessFunction>("process_function", DefaultPreprocExecute);
             }
             return py::capsule(infer_server->CreateSession(desc, observer));
           })
      .def("create_sync_session",
           [](std::shared_ptr<video::VideoInferServer> infer_server, SessionDesc desc) {
             if (!desc.preproc) {
               LOGD(CNIS_PY_API) << "Default preproc will be used";
               desc.preproc = PreprocessorHost::Create();
               desc.preproc->SetParams<PreprocessorHost::ProcessFunction>("process_function", DefaultPreprocExecute);
             }
             return py::capsule(infer_server->CreateSyncSession(desc));
           })
      .def("destroy_session",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session) {
             return infer_server->DestroySession(reinterpret_cast<Session_t>(session.get_pointer()));
           })
      .def("wait_task_done",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session, const std::string& tag) {
             infer_server->WaitTaskDone(reinterpret_cast<Session_t>(session.get_pointer()), tag);
           },
           py::call_guard<py::gil_scoped_release>())
      .def("discard_task",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session, const std::string& tag) {
             infer_server->DiscardTask(reinterpret_cast<Session_t>(session.get_pointer()), tag);
           })
      .def("get_model",
           [](std::shared_ptr<video::VideoInferServer> infer_server, py::capsule session) {
             return infer_server->GetModel(reinterpret_cast<Session_t>(session.get_pointer()));
           })
      .def_static("set_model_dir", &video::VideoInferServer::SetModelDir)
#ifdef CNIS_USE_MAGICMIND
      .def("load_model",
           [](std::shared_ptr<video::VideoInferServer> infer_server, const std::string& model_url,
              const std::vector<Shape>& input_shapes) {
             return infer_server->LoadModel(model_url, input_shapes);
           },
           py::arg("model_url"), py::arg("input_shapes") = std::vector<Shape>{})
#else
      .def("load_model",
           [](std::shared_ptr<video::VideoInferServer> infer_server, const std::string& pattern1,
              const std::string& pattern2) { return infer_server->LoadModel(pattern1, pattern2); },
           py::arg("pattern1"), py::arg("pattern2") = "subnet0")
#endif
      .def_static("unload_model", &video::VideoInferServer::UnloadModel)
      .def_static("clear_model_cache", &video::VideoInferServer::ClearModelCache);
}

}  //  namespace infer_server
