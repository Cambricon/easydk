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

#ifndef CNEDK_TRANSFORM_CNCV_HPP_
#define CNEDK_TRANSFORM_CNCV_HPP_

#include <map>
#include <memory>
#include <vector>

#include "cncv.h"

#include "cnedk_buf_surface.h"
#include "cnedk_transform.h"

namespace cnedk {

class CncvContext {
 public:
  explicit CncvContext(int dev_id) {
    device_id_ = dev_id;

#if CNRT_MAJOR_VERSION < 5
    cnrtDev_t dev;
    cnrtGetDeviceHandle(&dev, dev_id_);
    cnrtSetCurrentDevice(dev);
#else
    cnrtSetDevice(device_id_);
#endif

#if CNRT_MAJOR_VERSION < 5
    cnrtSetDeviceFlag(1);
#endif

    CnedkTransformGetSessionParams(&params_);
    cncvCreate(&handle_);
    cncvSetQueue(handle_, params_.cnrt_queue);
  }

  virtual int Process(const CnedkBufSurface& src, CnedkBufSurface* dst, CnedkTransformParams* transform_params) = 0;
  int GetDeviceId() { return device_id_; }

  virtual ~CncvContext() {
    if (handle_) cncvDestroy(handle_);
    CnedkTransformSetSessionParams(&params_);
  }

  static cncvPixelFormat GetPixFormat(CnedkBufSurfaceColorFormat format) {
    static std::map<CnedkBufSurfaceColorFormat, cncvPixelFormat> color_map{
        {CNEDK_BUF_COLOR_FORMAT_YUV420, CNCV_PIX_FMT_I420}, {CNEDK_BUF_COLOR_FORMAT_NV12, CNCV_PIX_FMT_NV12},
        {CNEDK_BUF_COLOR_FORMAT_NV21, CNCV_PIX_FMT_NV21},   {CNEDK_BUF_COLOR_FORMAT_BGR, CNCV_PIX_FMT_BGR},
        {CNEDK_BUF_COLOR_FORMAT_RGB, CNCV_PIX_FMT_RGB},     {CNEDK_BUF_COLOR_FORMAT_BGRA, CNCV_PIX_FMT_BGRA},
        {CNEDK_BUF_COLOR_FORMAT_RGBA, CNCV_PIX_FMT_RGBA},   {CNEDK_BUF_COLOR_FORMAT_ABGR, CNCV_PIX_FMT_ABGR},
        {CNEDK_BUF_COLOR_FORMAT_ARGB, CNCV_PIX_FMT_ARGB},
    };
    return color_map[format];
  }

 protected:
  int device_id_ = 0;
  cncvHandle_t handle_ = nullptr;
  CnedkTransformConfigParams params_;
};  // class CncvContext

class YuvResizeCncvCtx : public CncvContext {
 public:
  explicit YuvResizeCncvCtx(int dev_id) : CncvContext(dev_id) {}

  int Process(const CnedkBufSurface& src, CnedkBufSurface* dst, CnedkTransformParams* transform_params) override;
  ~YuvResizeCncvCtx() override {
    if (mlu_input_) cnrtFree(mlu_input_);
    if (mlu_output_) cnrtFree(mlu_output_);
    if (workspace_) cnrtFree(workspace_);
  };

 private:
  std::vector<void**> cpu_input_;
  std::vector<void**> cpu_output_;

  void** mlu_input_ = nullptr;
  void** mlu_output_ = nullptr;

  std::vector<cncvImageDescriptor> src_descs_;
  std::vector<cncvImageDescriptor> dst_descs_;
  std::vector<cncvRect> src_rois_;
  std::vector<cncvRect> dst_rois_;

  void* workspace_ = nullptr;
  size_t workspace_size_ = 0;
  size_t batch_size_ = 0;
  const int plane_number_ = 2;
};  // class YuvResizeCncvCtx

class Yuv2RgbxResizeCncvCtx : public CncvContext {
 public:
  explicit Yuv2RgbxResizeCncvCtx(int dev_id) : CncvContext(dev_id) {}

  int Process(const CnedkBufSurface& src, CnedkBufSurface* dst, CnedkTransformParams* transform_params) override;
  ~Yuv2RgbxResizeCncvCtx() override {
    if (mlu_input_) cnrtFree(mlu_input_);
    if (mlu_output_) cnrtFree(mlu_output_);
    if (workspace_) cnrtFree(workspace_);
  }

 private:
  std::vector<void**> cpu_input_;
  std::vector<void**> cpu_output_;

  void** mlu_input_ = nullptr;
  void** mlu_output_ = nullptr;

  std::vector<cncvImageDescriptor> src_descs_;
  std::vector<cncvImageDescriptor> dst_descs_;
  std::vector<cncvRect> src_rois_;
  std::vector<cncvRect> dst_rois_;
  bool keep_aspect_ratio_;
  uint8_t pad_value_ = 0;
  void* workspace_ = nullptr;
  size_t workspace_size_ = 0;
  size_t batch_size_ = 0;
  const int plane_number_ = 2;
};

class RgbxToYuvCncvCtx : public CncvContext {
 public:
  explicit RgbxToYuvCncvCtx(int dev_id) : CncvContext(dev_id) {}
  int Process(const CnedkBufSurface& src, CnedkBufSurface* dst, CnedkTransformParams* transform_params) override;
  ~RgbxToYuvCncvCtx() override {}

 private:
  cncvRect src_roi_;
  cncvImageDescriptor src_desc_;
  cncvImageDescriptor dst_desc_;
};  // class RgbxToYuvCncvCtx

class MeanStdCncvCtx : public CncvContext {
 public:
  explicit MeanStdCncvCtx(int dev_id) : CncvContext(dev_id) {}

  int Process(const CnedkBufSurface& src, CnedkBufSurface* dst, CnedkTransformParams* transform_params) override;
  ~MeanStdCncvCtx() override {
    if (mlu_input_) cnrtFree(mlu_input_);
    if (mlu_output_) cnrtFree(mlu_output_);
    if (workspace_) cnrtFree(workspace_);
  }

 private:
  std::vector<void**> cpu_input_;
  std::vector<void**> cpu_output_;

  void** mlu_input_ = nullptr;
  void** mlu_output_ = nullptr;

  cncvImageDescriptor src_desc_;
  cncvImageDescriptor dst_desc_;

  void* workspace_ = nullptr;
  size_t workspace_size_ = 0;
  size_t batch_size_ = 0;
  float* mean_;
  float* std_;
};

class Rgbx2YuvResizeAndConvert {
 public:
  explicit Rgbx2YuvResizeAndConvert(int dev_id) {
    rgbx_yuv_ = std::make_shared<RgbxToYuvCncvCtx>(dev_id);
    yuv_resize_ = std::make_shared<YuvResizeCncvCtx>(dev_id);
    dev_id_ = dev_id;
  }
  ~Rgbx2YuvResizeAndConvert() {
    if (src_yuv_mlu_)  cnrtFree(src_yuv_mlu_);
  }
  int Process(const CnedkBufSurface& src, CnedkBufSurface* dst, CnedkTransformParams* transform_params);
 private:
  int dev_id_;
  std::shared_ptr<RgbxToYuvCncvCtx> rgbx_yuv_;
  std::shared_ptr<YuvResizeCncvCtx> yuv_resize_;

  void* src_yuv_mlu_ = nullptr;
  uint32_t src_yuv_size_ = 0;
};

class Yuv2RgbxResizeWithMeanStdCncv {
 public:
  explicit Yuv2RgbxResizeWithMeanStdCncv(int dev_id) {
    mean_std_ = std::make_shared<MeanStdCncvCtx>(dev_id);
    resize_convert_ = std::make_shared<Yuv2RgbxResizeCncvCtx>(dev_id);
  }

  int Process(const CnedkBufSurface& src, CnedkBufSurface* dst, CnedkTransformParams* transform_params);
  ~Yuv2RgbxResizeWithMeanStdCncv() {
    if (mlu_ptr_) cnrtFree(mlu_ptr_);
  }

 private:
  std::shared_ptr<MeanStdCncvCtx> mean_std_;
  std::shared_ptr<Yuv2RgbxResizeCncvCtx> resize_convert_;

  void* mlu_ptr_ = nullptr;
  size_t mlu_size_ = 0;
};

int GetBufSurfaceFromTensor(CnedkBufSurface* src, CnedkBufSurface* dst, CnedkTransformTensorDesc* tensor_desc);
int CncvTransform(CnedkBufSurface* src, CnedkBufSurface* dst, CnedkTransformParams* transform_params);

}  // namespace cnedk

#endif  // CNEDK_TRANSFORM_CNCV_HPP_
