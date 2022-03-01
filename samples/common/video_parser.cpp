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

#include "video_parser.h"

#include <atomic>
#include <chrono>
#include <thread>

#include "cxxutil/log.h"
#include "easycodec/easy_decode.h"

#ifdef __cplusplus
extern "C" {
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#ifdef __cplusplus
}
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace detail {
static int InterruptCallBack(void* ctx) {
  VideoParser* parser = reinterpret_cast<VideoParser*>(ctx);
  if (parser->CheckTimeout()) {
    LOGD(SAMPLES) << "[RTSP] Get interrupt and timeout";
    return 1;
  }
  return 0;
}
}  // namespace detail

bool VideoParser::CheckTimeout() {
  std::chrono::duration<float, std::milli> dura = std::chrono::steady_clock::now() - last_receive_frame_time_;
  if (dura.count() > max_receive_timeout_) {
    return true;
  }
  return false;
}

bool VideoParser::Open(const char *url, bool save_file) {
  static struct _InitFFmpeg {
    _InitFFmpeg() {
      // init ffmpeg
      avcodec_register_all();
      av_register_all();
      avformat_network_init();
    }
  } _init_ffmpeg;

  is_rtsp_ = ::IsRtsp(url);
  if (have_video_source_.load()) return false;
  // format context
  p_format_ctx_ = avformat_alloc_context();
  if (!p_format_ctx_) return false;
  if (is_rtsp_) {
    AVIOInterruptCB intrpt_callback = {detail::InterruptCallBack, this};
    p_format_ctx_->interrupt_callback = intrpt_callback;
    last_receive_frame_time_ = std::chrono::steady_clock::now();
    // options
    av_dict_set(&options_, "buffer_size", "1024000", 0);
    av_dict_set(&options_, "max_delay", "500000", 0);
    av_dict_set(&options_, "stimeout", "20000000", 0);
    av_dict_set(&options_, "rtsp_flags", "prefer_tcp", 0);
  } else {
    av_dict_set(&options_, "buffer_size", "1024000", 0);
    av_dict_set(&options_, "stimeout", "200000", 0);
  }
  // open input
  int ret_code = avformat_open_input(&p_format_ctx_, url, NULL, &options_);
  if (0 != ret_code) {
    LOGE(SAMPLES) << "couldn't open input stream: " << url;
    return false;
  }
  // find video stream information
  ret_code = avformat_find_stream_info(p_format_ctx_, NULL);
  if (ret_code < 0) {
    LOGE(SAMPLES) << "couldn't find stream information.";
    return false;
  }
  video_index_ = -1;
  AVStream *vstream = nullptr;
  for (uint32_t iloop = 0; iloop < p_format_ctx_->nb_streams; iloop++) {
    vstream = p_format_ctx_->streams[iloop];
  #if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
    if (vstream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
  #else
    if (vstream->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
  #endif
      video_index_ = iloop;
      break;
    }
  }
  if (video_index_ == -1) {
    LOGE(SAMPLES) << "didn't find a video stream.";
    return false;
  }

  info_.width = vstream->codec->width;
  info_.height = vstream->codec->height;

  // Get codec id, check progressive
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
  auto codec_id = vstream->codecpar->codec_id;
  int field_order = vstream->codecpar->field_order;
#else
  auto codec_id = vstream->codec->codec_id;
  int field_order = vstream->codec->field_order;
#endif
  info_.codec_id = codec_id;
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
  info_.codecpar = p_format_ctx_->streams[video_index_]->codecpar;
#endif
  info_.codec_ctx = p_format_ctx_->streams[video_index_]->codec;
  /*
   * At this moment, if the demuxer does not set this value (avctx->field_order == UNKNOWN),
   * the input stream will be assumed as progressive one.
   */
  switch (field_order) {
    case AV_FIELD_TT:
    case AV_FIELD_BB:
    case AV_FIELD_TB:
    case AV_FIELD_BT:
      info_.progressive = 0;
      break;
    case AV_FIELD_PROGRESSIVE:  // fall through
    default:
      info_.progressive = 1;
      break;
  }

  // get extra data
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
  uint8_t* extradata = vstream->codecpar->extradata;
  int extradata_size = vstream->codecpar->extradata_size;
#else
  uint8_t* extradata = vstream->codec->extradata;
  int extradata_size = vstream->codec->extradata_size;
#endif
  info_.extra_data = std::vector<uint8_t>(extradata, extradata + extradata_size);

  LOGI(SAMPLES) << p_format_ctx_->iformat->name;
  if (strstr(p_format_ctx_->iformat->name, "mp4") || strstr(p_format_ctx_->iformat->name, "flv") ||
      strstr(p_format_ctx_->iformat->name, "matroska") || strstr(p_format_ctx_->iformat->name, "h264") ||
      strstr(p_format_ctx_->iformat->name, "rtsp")) {
    if (AV_CODEC_ID_H264 == codec_id) {
      // info_.codec_type = edk::CodecType::H264;
      if (save_file) saver_.reset(new detail::FileSaver("out.h264"));
    } else if (AV_CODEC_ID_HEVC == codec_id) {
      // info_.codec_type = edk::CodecType::H265;
      if (save_file) saver_.reset(new detail::FileSaver("out.h265"));
    } else {
      LOGE(SAMPLES) << "nonsupport codec id.";
      return false;
    }
  }
  have_video_source_.store(true);
  first_frame_ = true;

  if (handler_ && !handler_->OnParseInfo(info_)) {
      return false;
  }
  return true;
}

void VideoParser::Close() {
  if (!have_video_source_.load()) return;
  LOGI(SAMPLES) << "Close ffmpeg resources";
  if (p_format_ctx_) {
    avformat_close_input(&p_format_ctx_);
    avformat_free_context(p_format_ctx_);
    av_dict_free(&options_);
    p_format_ctx_ = nullptr;
    options_ = nullptr;
  }
  have_video_source_.store(false);
  frame_index_ = 0;
  saver_.reset();
}

int VideoParser::ParseLoop(uint32_t frame_interval) {
  auto now_time = std::chrono::steady_clock::now();
  auto last_time = std::chrono::steady_clock::now();
  std::chrono::duration<double, std::milli> dura;

  while (handler_->Running()) {
    if (!have_video_source_.load()) {
      LOGE(SAMPLES) << "video source have not been init";
      return -1;
    }

    if (av_read_frame(p_format_ctx_, &packet_) < 0) {
      // EOS
      handler_->OnEos();
      return 1;
    }

    // update receive frame time
    last_receive_frame_time_ = std::chrono::steady_clock::now();

    // skip unmatched stream
    if (packet_.stream_index != video_index_) {
      av_packet_unref(&packet_);
      continue;
    }

    // filter non-key-frame in head
    if (first_frame_) {
      LOGI(SAMPLES) << "check first frame";
      if (packet_.flags & AV_PKT_FLAG_KEY) {
        first_frame_ = false;
      } else {
        LOGW(SAMPLES) << "skip first not-key-frame";
        av_packet_unref(&packet_);
        continue;
      }
    }

    // parse data from packet
    auto vstream = p_format_ctx_->streams[video_index_];
    // find pts information
    if (AV_NOPTS_VALUE == packet_.pts) {
      LOGI(SAMPLES) << "Didn't find pts informations, use ordered numbers instead. ";
      packet_.pts = frame_index_++;
    } else {
      packet_.pts = av_rescale_q(packet_.pts, vstream->time_base, {1, 90000});
    }

    if (saver_) {
      saver_->Write(reinterpret_cast<char *>(packet_.data), packet_.size);
    }

    if (!handler_->OnPacket(&packet_)) return -1;

    av_packet_unref(&packet_);

    // frame rate control
    if (frame_interval) {
      now_time = std::chrono::steady_clock::now();
      dura = now_time - last_time;
      if (frame_interval > dura.count()) {
        std::this_thread::sleep_for(std::chrono::duration<double, std::milli>(frame_interval - dura.count()));
      }
      last_time = std::chrono::steady_clock::now();
    }
  }  // while (true)

  return 1;
}
