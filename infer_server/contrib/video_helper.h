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

#ifndef INFER_SERVER_VIDEO_HELPER_H_
#define INFER_SERVER_VIDEO_HELPER_H_

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "buffer.h"
#include "infer_server.h"
#include "processor.h"

namespace infer_server {
namespace video {

enum class PixelFmt {
  I420 = 0,
  NV12 = 1,
  NV21 = 2,

  RGB24 = 3,
  BGR24 = 4,
  RGBA = 5,
  BGRA = 6,
  ARGB = 7,
  ABGR = 8,
};

struct BoundingBox {
  float x{0.f};  ///< The x-axis coordinate in the upper left corner of the bounding box.
  float y{0.f};  ///< The y-axis coordinate in the upper left corner of the bounding box.
  float w{0.f};  ///< The width of the bounding box.
  float h{0.f};  ///< The height of the bounding box.
};

size_t GetPlaneNum(PixelFmt format) noexcept;

struct VideoFrame {
#define MAX_PLANE_NUM 3
  Buffer plane[MAX_PLANE_NUM];
  size_t stride[MAX_PLANE_NUM] = {0, 0, 0};
  size_t width;
  size_t height;
  size_t plane_num;
  PixelFmt format;
  BoundingBox roi;

  size_t GetPlaneSize(size_t plane_idx) const noexcept;
  size_t GetTotalSize() const noexcept;
};

enum class PreprocessType {
  UNKNOWN = 0,
  RESIZE_CONVERT = 1,
  SCALER = 2,
  CNCV_RESIZE_CONVERT = 3,
};

struct PreprocessorMLUPrivate;
class PreprocessorMLU : public ProcessorForkable<PreprocessorMLU> {
 public:
  PreprocessorMLU() noexcept;
  ~PreprocessorMLU();
  Status Process(PackagePtr data) noexcept override;
  Status Init() noexcept override;

 private:
  PreprocessorMLUPrivate* priv_;
};  // class PreprocessorMLU

class VideoInferServer : public InferServer {
 public:
  explicit VideoInferServer(int device_id) noexcept : InferServer(device_id) {}
  VideoInferServer() = delete;

  /**
   * @brief send a inference request
   *
   * @warning async api, can be invoked with async Session only.
   * @note redefine to unhide method with same name of base class
   */
  bool Request(Session_t session, PackagePtr input, any user_data, int timeout = -1) noexcept {
    return InferServer::Request(session, std::move(input), std::move(user_data), timeout);
  }

  /**
   * @brief send a inference request and wait for response
   *
   * @warning synchronous api, can be invoked with synchronous Session only.
   * @note redefine to unhide method with same name of base class
   */
  bool RequestSync(Session_t session, PackagePtr input, Status* status, PackagePtr response,
                   int timeout = -1) noexcept {
    return InferServer::RequestSync(session, std::move(input), status, std::move(response), timeout);
  }

  /**
   * @brief image process helper, send a inference request
   *
   * @warning async api, can be invoked with async Session only.
   *          @see CreateSession(SessionDesc, std::shared_ptr<Observer>)
   *
   * @param session infer session
   * @param vframe image data
   * @param tag tag of request
   * @param user_data user data
   * @param timeout timeout threshold (milliseconds), -1 for endless
   */
  bool Request(Session_t session, const VideoFrame& vframe,
           const std::string& tag, any user_data, int timeout = -1) noexcept {
    PackagePtr in = Package::Create(1, tag);
    in->data[0]->Set(vframe);
    in->tag = tag;
    return Request(session, std::move(in), std::move(user_data), timeout);
  }

  /**
   * @brief image with object process helper, send a inference request
   *
   * @warning async api, can be invoked with async Session only.
   *          @see CreateSession(SessionDesc, std::shared_ptr<Observer>)
   *
   * @param session infer session
   * @param vframe image data
   * @param objs objects detected in video frame (model process on objs)
   * @param tag tag of request
   * @param user_data user data
   * @param timeout timeout threshold (milliseconds), -1 for endless
   */
  bool Request(Session_t session, const VideoFrame& vframe, const std::vector<BoundingBox>& objs,
           const std::string& tag, any user_data, int timeout = -1) noexcept {
    PackagePtr in = Package::Create(objs.size(), tag);
    for (unsigned int i = 0; i < objs.size(); i++) {
      VideoFrame vf = vframe;
      vf.roi = objs[i];
      in->data[i]->Set(std::move(vf));
    }
    return Request(session, std::move(in), std::move(user_data), timeout);
  }

  /**
   * @brief image process helper, send a inference request and wait for response
   *
   * @warning sync api, can be invoked with sync Session only. @see CreateSession(SessionDesc)
   *
   * @param session infer session
   * @param vframe image data
   * @param tag tag of request
   * @param status[out] execute status
   * @param output[out] output result
   * @param timeout timeout threshold (milliseconds), -1 for endless
   */
  bool RequestSync(Session_t session, const VideoFrame& vframe, const std::string& tag, Status* status,
                   PackagePtr response, int timeout = -1) noexcept {
    PackagePtr in = Package::Create(1, tag);
    in->data[0]->Set(vframe);
    return RequestSync(session, std::move(in), status, std::move(response), timeout);
  }

  /**
   * @brief image with object process helper, send a inference request and wait for response
   *
   * @warning sync api, can be invoked with sync Session only. @see CreateSession(SessionDesc)
   *
   * @param session infer session
   * @param vframe image data
   * @param objs objects detected in video frame (model process on objs)
   * @param tag tag of request
   * @param status[out] execute status
   * @param output[out] output result
   * @param timeout timeout threshold (milliseconds), -1 for endless
   */
  bool RequestSync(Session_t session, const VideoFrame& vframe, const std::vector<BoundingBox>& objs,
                   const std::string& tag, Status* status, PackagePtr response, int timeout = -1) noexcept {
    PackagePtr in = Package::Create(objs.size(), tag);
    for (unsigned int i = 0; i < objs.size(); ++i) {
      VideoFrame vf = vframe;
      vf.roi = objs[i];
      in->data[i]->Set(std::move(vf));
    }
    return RequestSync(session, std::move(in), status, std::move(response), timeout);
  }
};

}  // namespace video
}  // namespace infer_server

#endif  // INFER_SERVER_VIDEO_HELPER_H_
