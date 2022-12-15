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

#ifndef MPS_SERVICE_HPP_
#define MPS_SERVICE_HPP_

#include <map>
#include <memory>
#include <mutex>
#include <tuple>
#include <utility>
#include <vector>

#include "cn_comm_vdec.h"  // for cnvdecStream_t
#include "cn_comm_venc.h"  // for cnvdecStream_t

#include "cnedk_buf_surface.h"
#include "cnedk_encode.h"

namespace cnedk {

//
// vpps grp management
//
constexpr int kVppsVoutBase = 0;
constexpr int KVppsVinBase = 4;
constexpr int KVppsVencBase = 8;
constexpr int KVppsAppBase = 12;

struct VinParam {
  int sensor_type;
  int mipi_dev;
  int bus_id;
  int sns_clk_id;
  int out_width;
  int out_height;
  explicit VinParam(int sensor_type = 0, int mipi_dev = 0, int bus_id = 0, int sns_clk_id = 0,
                    int out_width = 0, int out_height = 0)
      : sensor_type(sensor_type),
        mipi_dev(mipi_dev),
        bus_id(bus_id),
        sns_clk_id(sns_clk_id),
        out_width(out_width),
        out_height(out_height) {}
};

struct VBInfo {
  int block_size = 0;
  int block_count = 0;
  explicit VBInfo(int block_size = 0, int block_count = 0) : block_size(block_size), block_count(block_count) {}
};

struct MpsServiceConfig {
  struct {
    bool enable = false;
    cnEnPixelFormat_t input_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_UV;
    int max_input_width = 1920;
    int max_input_height = 1080;
  } vout;
  std::map<int, VinParam> vins;
  std::vector<VBInfo> vbs;
  int codec_id_start = 0;
};

class IVBInfo {
 public:
  virtual ~IVBInfo() {}
  virtual void OnVBInfo(cnU64_t blkSize, int blkCount) = 0;
};

class IVDecResult {
 public:
  virtual ~IVDecResult() {}
  virtual void OnFrame(void *handle, const cnVideoFrameInfo_t *info) = 0;
  virtual void OnEos() = 0;
  virtual void OnError(cnS32_t errcode) = 0;
};

struct VEncFrameBits {
  unsigned char *bits = nullptr;
  int len = 0;
  uint64_t pts = -1;
  CnedkVencPakageType pkt_type;
};
class IVEncResult {
 public:
  virtual ~IVEncResult() {}
  virtual void OnFrameBits(VEncFrameBits *framebits) = 0;
  virtual void OnEos() = 0;
  virtual void OnError(cnS32_t errcode) = 0;
};

struct Point {
  int x;
  int y;
  explicit Point(int x = 0, int y = 0) : x(x), y(y) {}
};

struct Bbox {
  int x, y, w, h;
  explicit Bbox(int x = 0, int y = 0, int w = 0, int h = 0) : x(x), y(y), w(w), h(h) {}
  bool IsDefault() { return (x == 0 && y == 0 && w == 0 && h == 0); }
};

class NonCopyable {
 public:
  NonCopyable() = default;
  virtual ~NonCopyable() = default;

 private:
  NonCopyable(const NonCopyable &) = delete;
  NonCopyable &operator=(const NonCopyable &) = delete;
  NonCopyable(NonCopyable &&) = delete;
  NonCopyable &operator=(NonCopyable &&) = delete;
};

struct VencCreateParam {
  int gop_size;
  double frame_rate;
  cnEnPixelFormat_t pixel_format;
  cnEnPayloadType_t type;
  uint32_t width;
  uint32_t height;
  uint32_t bitrate;  // kbps
};

class MpsServiceImpl;
class MpsService {
 public:
  static MpsService &Instance() {
    static MpsService mps;
    return mps;
  }

  int Init(const MpsServiceConfig &config);
  void Destroy();

  // vout (vpps + vo)
  //
  int GetVoutSize(int *w, int *h);
  int VoutSendFrame(cnVideoFrameInfo_t *info);

  // vins (vi + vpps or vi only)
  int GetVinSize(int sensor_id, int *w, int *h);
  int VinCaptureFrame(int sensor_id, cnVideoFrameInfo_t *info, int timeout_ms);
  int VinCaptureFrameRelease(int sensor, cnVideoFrameInfo_t *info);

  // vdecs
  void *CreateVDec(IVDecResult *result, cnEnPayloadType_t type, int max_width, int max_height, int buf_num = 12,
                   cnEnPixelFormat_t pix_fmt = PIXEL_FORMAT_YUV420_8BIT_SEMI_VU);
  int DestroyVDec(void *handle);
  int VDecSendStream(void *handle, const cnvdecStream_t *pst_stream, cnS32_t milli_sec);
  int VDecReleaseFrame(void *handle, const cnVideoFrameInfo_t *info);

  // vencs
  void *CreateVEnc(IVEncResult *result, VencCreateParam *params);
  int DestroyVEnc(void *handle);
  int VEncSendFrame(void *handle, const cnVideoFrameInfo_t *pst_frame, cnS32_t milli_sec);

  // vgu resize-convert,
  // TODO(gaoyujia): add constraints
  int VguScaleCsc(const cnVideoFrameInfo_t *src, cnVideoFrameInfo_t *dst);

  //
  // draw bboxes, etc
  int OsdDrawBboxes(const cnVideoFrameInfo_t *info, const std::vector<std::tuple<Bbox, cnU32_t, cnU32_t>> &bboxes);
  int OsdFillBboxes(const cnVideoFrameInfo_t *info, const std::vector<std::pair<Bbox, cnU32_t>> &bboxes);
  int OsdPutText(const cnVideoFrameInfo_t *info, const std::vector<std::tuple<Bbox, void *, cnU32_t, cnU32_t>> &texts);

 private:
  MpsService();
  ~MpsService();

  MpsService(const MpsService &) = delete;
  MpsService(MpsService &&) = delete;
  MpsService &operator=(const MpsService &) = delete;
  MpsService &operator=(MpsService &&) = delete;

 private:
  std::unique_ptr<MpsServiceImpl> impl_;
};

Bbox KeepAspectRatio(int src_w, int src_h, int dst_w, int dst_h);
// TODO(gaoyujia)
void TestVoutService();

}  // namespace cnedk

#endif  // MPS_SERVICE_HPP_
