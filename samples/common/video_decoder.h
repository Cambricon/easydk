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

#ifndef EDK_SAMPLES_FFMPEG_DECODER_H_
#define EDK_SAMPLES_FFMPEG_DECODER_H_

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#if LIBAVFORMAT_VERSION_INT == FFMPEG_VERSION_4_2_2
#include <libavutil/hwcontext.h>
#endif
#ifdef __cplusplus
}
#endif

#include <utility>

#include "video_parser.h"

class IDecodeEventHandle {
 public:
  virtual void OnDecodeFrame(const edk::CnFrame& frame) = 0;
  virtual void OnEos() = 0;
};

class StreamRunner;
class VideoDecoder;

class VideoDecoderImpl {
 public:
  explicit VideoDecoderImpl(VideoDecoder* interface, IDecodeEventHandle* handle, int device_id)
      : interface_(interface), handle_(handle), device_id_(device_id) {}
  virtual ~VideoDecoderImpl() = default;
  virtual bool Init() = 0;
  virtual bool FeedPacket(const AVPacket* pkt) = 0;
  virtual void FeedEos() = 0;
  virtual void ReleaseFrame(edk::CnFrame&& frame) = 0;
  virtual bool CopyFrameD2H(void *dst, const edk::CnFrame &frame) = 0;

 protected:
  VideoDecoder* interface_;
  IDecodeEventHandle* handle_;
  int device_id_;
};

class VideoDecoder : public IDemuxEventHandle {
 public:
  enum DecoderType {
    EASY_DECODE,
    FFMPEG,
    FFMPEG_MLU
  };
  VideoDecoder(StreamRunner* runner, DecoderType type, int device_id);
  bool OnParseInfo(const VideoInfo& info) override;
  bool OnPacket(const AVPacket* packet) override;
  void OnEos() override;
  bool Running() override;
  void SendEos();
  VideoInfo& GetVideoInfo() { return info_; }
  bool CopyFrameD2H(void *dst, const edk::CnFrame &frame) { return impl_->CopyFrameD2H(dst, frame); }
  void ReleaseFrame(edk::CnFrame&& frame) { impl_->ReleaseFrame(std::forward<edk::CnFrame>(frame)); }

 private:
  bool InitEasyDecode();
  bool InitFFmpegDecode();

  VideoInfo info_;
  const StreamRunner* runner_{nullptr};
  VideoDecoderImpl* impl_{nullptr};
  bool send_eos_{false};
};

#endif  // EDK_SAMPLES_VIDEO_PARSER_H_
