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
#include <unordered_map>

#include "cnrt.h"
#include "cxxutil/log.h"
#include "cxxutil/noncopy.h"
#include "decoder.h"
#include "easycodec/easy_decode.h"

#ifdef ENABLE_TURBOJPEG
#include "cxxutil/threadsafe_queue.h"
extern "C" {
#include "libyuv.h"
#include "turbojpeg.h"
}
#endif

#define ALIGN(size, alignment) (((u32_t)(size) + (alignment)-1) & ~((alignment)-1))

#define CALL_CNRT_FUNC(func, msg)                                                            \
  do {                                                                                       \
    int ret = (func);                                                                        \
    if (0 != ret) {                                                                          \
      LOGE(DECODE) << msg << " error code: " << ret;                                         \
      THROW_EXCEPTION(Exception::INTERNAL, msg " cnrt error code : " + std::to_string(ret)); \
    }                                                                                        \
  } while (0)

namespace edk {

namespace detail {
int CheckProgressiveMode(uint8_t* data, uint64_t length) {
  static constexpr uint16_t kJPEG_HEADER = 0xFFD8;
  uint64_t i = 0;
  uint16_t header = (data[i] << 8) | data[i + 1];
  if (header != kJPEG_HEADER) {
    return -1;
  }
  i = i + 2;  // jump jpeg header
  while (i < length) {
    uint16_t seg_header = (data[i] << 8) | data[i + 1];
    if (seg_header == 0xffc2 || seg_header == 0xffca) {
      return 1;
    }
    uint16_t step = (data[i + 2] << 8) | data[i + 3];
    i += 2;     // jump seg header
    i += step;  // jump whole seg
  }
  return 0;
}

#ifdef ENABLE_TURBOJPEG
static bool BGRToNV21(uint8_t* src, uint8_t* dst_y, int dst_y_stride, uint8_t* dst_uv, int dst_uv_stride, int width,
               int height) {
  int i420_stride_y = width;
  int i420_stride_u = width / 2;
  int i420_stride_v = i420_stride_u;
  uint8_t* i420 = new uint8_t[width * height * 3 / 2];
  // clang-format off
  libyuv::RGB24ToI420(src, width * 3,
                      i420, i420_stride_y,
                      i420 + width * height, i420_stride_u,
                      i420 + width * height * 5 / 4, i420_stride_v,
                      width, height);

  libyuv::I420ToNV21(i420, i420_stride_y,
                     i420 + width * height, i420_stride_u,
                     i420 + width * height * 5 / 4, i420_stride_v,
                     dst_y, dst_y_stride,
                     dst_uv, dst_uv_stride,
                     width, height);
  // clang-format on
  delete[] i420;
  return true;
}

static bool BGRToNV12(uint8_t* src, uint8_t* dst_y, int dst_y_stride, uint8_t* dst_vu, int dst_vu_stride, int width,
               int height) {
  int i420_stride_y = width;
  int i420_stride_u = width / 2;
  int i420_stride_v = i420_stride_u;
  uint8_t* i420 = new uint8_t[width * height * 3 / 2];
  // clang-format off
  libyuv::RGB24ToI420(src, width * 3,
                      i420, i420_stride_y,
                      i420 + width * height, i420_stride_u,
                      i420 + width * height * 5 / 4, i420_stride_v,
                      width, height);


  libyuv::I420ToNV12(i420, i420_stride_y,
                     i420 + width * height, i420_stride_u,
                     i420 + width * height * 5 / 4, i420_stride_v,
                     dst_y, dst_y_stride,
                     dst_vu, dst_vu_stride,
                     width, height);
  // clang-format on
  delete[] i420;
  return true;
}
#endif

}  // namespace detail

#ifdef ENABLE_TURBOJPEG
class ProgressiveJpegDecoder : public Decoder, public NonCopyable {
 public:
  explicit ProgressiveJpegDecoder(const EasyDecode::Attr& attr);
  ~ProgressiveJpegDecoder();
  bool FeedData(const CnPacket& packet) override;
  bool FeedEos() override;
  void AbortDecoder() override {
    LOGW(DECODE) << "Abort Decoder do nothing";
  };
  bool ReleaseBuffer(uint64_t buf_id) override;

 private:
  std::unordered_map<uint64_t, void*> memory_pool_map_;
  ThreadSafeQueue<uint64_t> memory_ids_;
  tjhandle tjinstance_;
  uint8_t* yuv_cpu_data_ = nullptr;
  uint8_t* bgr_cpu_data_ = nullptr;
};  // class ProgressiveJpegDecoder

ProgressiveJpegDecoder::ProgressiveJpegDecoder(const EasyDecode::Attr& attr) : Decoder(attr) {
  if (attr.pixel_format != PixelFmt::NV12 && attr.pixel_format != PixelFmt::NV21) {
    THROW_EXCEPTION(Exception::UNSUPPORTED, "Unsupported output pixel format.");
  }
  uint64_t size = 0;
  uint32_t plane_num = 2;
  const size_t stride = ALIGN(attr.frame_geometry.w, 128);
  for (uint32_t j = 0; j < plane_num; ++j) {
    uint32_t plane_size = j == 0 ? stride * attr.frame_geometry.h : (stride * (attr.frame_geometry.h >> 1));
    size += plane_size;
  }
  for (size_t i = 0; i < attr.output_buffer_num; ++i) {
    void* mlu_ptr = nullptr;
    CALL_CNRT_FUNC(cnrtMalloc(reinterpret_cast<void**>(&mlu_ptr), size), "Malloc decode output buffer failed");
    memory_pool_map_[attr.output_buffer_num + i] = mlu_ptr;
    memory_ids_.Push(attr.output_buffer_num + i);
  }
  yuv_cpu_data_ = new uint8_t[stride * attr.frame_geometry.h * 3 / 2];
  bgr_cpu_data_ = new uint8_t[attr.frame_geometry.w * attr.frame_geometry.h * 3];
  tjinstance_ = tjInitDecompress();
  if (!tjinstance_) {
    THROW_EXCEPTION(Exception::INIT_FAILED, "Create tj handler failed.");
  }
}

ProgressiveJpegDecoder::~ProgressiveJpegDecoder() {
  for (auto& iter : memory_pool_map_) {
    cnrtFree(iter.second);
  }
  if (tjinstance_) {
    tjDestroy(tjinstance_);
  }
  if (yuv_cpu_data_) {
    delete[] yuv_cpu_data_;
  }
  if (bgr_cpu_data_) {
    delete[] bgr_cpu_data_;
  }
}

bool ProgressiveJpegDecoder::FeedData(const CnPacket& packet) {
  int jpegSubsamp, width, height;
  tjDecompressHeader2(tjinstance_, reinterpret_cast<uint8_t*>(packet.data), packet.length, &width, &height,
                      &jpegSubsamp);
  tjDecompress2(tjinstance_, reinterpret_cast<uint8_t*>(packet.data), packet.length, bgr_cpu_data_, width,
                0 /*pitch*/, height, TJPF_RGB, TJFLAG_FASTDCT);
  int y_stride = ALIGN(width, 128);
  int uv_stride = ALIGN(width, 128);
  uint64_t data_length = height * y_stride * 3 / 2;
  if (attr_.pixel_format == PixelFmt::NV21) {
    detail::BGRToNV21(bgr_cpu_data_, yuv_cpu_data_, y_stride, yuv_cpu_data_ + height * y_stride, uv_stride, width,
                      height);
  } else if (attr_.pixel_format == PixelFmt::NV12) {
    detail::BGRToNV12(bgr_cpu_data_, yuv_cpu_data_, y_stride, yuv_cpu_data_ + height * y_stride, uv_stride, width,
                      height);
  } else {
    LOGF(DECODE) << "Not support output pixel format " << std::to_string(static_cast<int>(attr_.pixel_format));
    return false;
  }

  size_t buf_id;
  memory_ids_.TryPop(buf_id);  // get one available buffer
  void* mlu_ptr = memory_pool_map_[buf_id];
  CALL_CNRT_FUNC(cnrtMemcpy(mlu_ptr, yuv_cpu_data_, data_length, CNRT_MEM_TRANS_DIR_HOST2DEV), "Memcpy failed");

  // 2. config CnFrame for user callback.
  CnFrame finfo;
  finfo.pts = packet.pts;
  finfo.device_id = attr_.dev_id;
  finfo.buf_id = buf_id;
  finfo.width = width;
  finfo.height = height;
  finfo.n_planes = 2;
  finfo.frame_size = height * y_stride * 3 / 2;
  finfo.strides[0] = y_stride;
  finfo.strides[1] = uv_stride;
  finfo.ptrs[0] = mlu_ptr;
  finfo.ptrs[1] = reinterpret_cast<void*>(reinterpret_cast<uint8_t*>(mlu_ptr) + height * y_stride);
  finfo.pformat = attr_.pixel_format;

  LOGT(DECODE) << "Frame: width " << finfo.width << " height " << finfo.height << " planes " << finfo.n_planes
                << " frame size " << finfo.frame_size;
  if (NULL != attr_.frame_callback) {
    attr_.frame_callback(finfo);
  }
  return true;
}

bool ProgressiveJpegDecoder::FeedEos() {
  if (attr_.eos_callback) {
    attr_.eos_callback();
  }
  return true;
}

bool ProgressiveJpegDecoder::ReleaseBuffer(uint64_t buf_id) {
  if (memory_pool_map_.find(buf_id) != memory_pool_map_.end()) {
    memory_ids_.Push(buf_id);
    return true;
  }
  return false;
}

Decoder* CreateProgressiveJpegDecoder(const EasyDecode::Attr& attr) {
  return new ProgressiveJpegDecoder(attr);
}

#else

Decoder* CreateProgressiveJpegDecoder(const EasyDecode::Attr& attr) {
  LOGE(DECODE) << "Create progressive jpeg decoder failed, please set compile option WITH_TURBOJPEG to ON.";
  return nullptr;
}

#endif

}  // namespace edk

