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

#ifndef SAMPLE_DECODE_HPP_
#define SAMPLE_DECODE_HPP_

#include <memory>
#include <string>

#include "cnedk_decode.h"

#include "ffmpeg_demuxer.h"
#include "framerate_contrller.h"

#include "easy_module.hpp"


class SampleDecode : public EasyModule {
 public:
  SampleDecode(std::string name, int parallelism, int device_id, std::string filename, int stream_id,
               int frame_rate = 30) : EasyModule(name, parallelism) {
    filename_ = filename;
    stream_id_ = stream_id;
    dev_id_ = device_id;
    if (frame_rate > 0) {
      frame_rate_ = frame_rate;
    }
  }

  ~SampleDecode();

  int Open() override;

  int Close() override;

  int Process(std::shared_ptr<EdkFrame> frame) override;

  static int GetBufSurface_(CnedkBufSurface **surf, int width, int height, CnedkBufSurfaceColorFormat fmt,
                              int timeout_ms, void *userdata) {
    SampleDecode *thiz = reinterpret_cast<SampleDecode *>(userdata);
    return thiz->GetBufSurface(surf, width, height, fmt, timeout_ms);
  }
  static int OnFrame_(CnedkBufSurface *surf, void *userdata) {
    SampleDecode *thiz = reinterpret_cast<SampleDecode *>(userdata);
    return thiz->OnFrame(surf);
  }
  static int OnEos_(void *userdata) {
    SampleDecode *thiz = reinterpret_cast<SampleDecode *>(userdata);
    return thiz->OnEos();
  }
  static int OnError_(int errcode, void *userdata) {
    SampleDecode *thiz = reinterpret_cast<SampleDecode *>(userdata);
    return thiz->OnError(errcode);
  }

 private:
  int CreateSurfacePool(void** surf_pool, int width, int height);
  int GetBufSurface(CnedkBufSurface **surf, int width, int height, CnedkBufSurfaceColorFormat fmt, int timeout_ms);
  int OnFrame(CnedkBufSurface *surf);
  int OnEos();
  int OnError(int errcode);

 private:
  int stream_id_;
  std::string filename_;
  uint64_t frame_count_ = 0;

 private:
  CnedkVdecCreateParams params_;
  std::unique_ptr<FFmpegDemuxer> demuxer_;
  std::unique_ptr<FrController> fr_controller_;
  int width_;
  int height_;
  void* vdec_ = nullptr;
  bool eos_send_ = false;
  void* surf_pool_ = nullptr;
  int dev_id_ = 0;
  int frame_rate_ = 30;

  uint8_t* data_buffer_ = nullptr;
};

#endif
