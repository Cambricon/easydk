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

#ifndef EDK_SAMPLES_VIDEO_PARSER_H_
#define EDK_SAMPLES_VIDEO_PARSER_H_

#include <atomic>
#include <chrono>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "easycodec/easy_decode.h"
#include "easycodec/vformat.h"

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

#define FFMPEG_VERSION_3_1 AV_VERSION_INT(57, 40, 100)
#define FFMPEG_VERSION_4_2_2 AV_VERSION_INT(58, 29, 100)

namespace detail {
struct BeginWith {
  explicit BeginWith(const std::string& str) noexcept : s(str) {}
  inline bool operator()(const std::string& prefix) noexcept {
    if (s.size() < prefix.size()) return false;
    return prefix == s.substr(0, prefix.size());
  }
  std::string s;
};  // struct BeginWith

class FileSaver {
 public:
  explicit FileSaver(const char* file_name) {
    of_.open(file_name);
    if (!of_.is_open()) {
      throw std::runtime_error("open file failed");
    }
  }

  ~FileSaver() { of_.close(); }

  void Write(char* buf, size_t len) { of_.write(buf, len); }

 private:
  std::ofstream of_;
};
}  // namespace detail

inline bool IsRtsp(const std::string& url) { return detail::BeginWith(url)("rtsp://"); }

struct VideoInfo {
  AVCodecID codec_id = AV_CODEC_ID_NONE;
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
  AVCodecParameters* codecpar = nullptr;
#endif
  AVCodecContext* codec_ctx = nullptr;
  std::vector<uint8_t> extra_data{};
  int width = 0;
  int height = 0;
  int progressive = 0;
};

class IDemuxEventHandle {
 public:
  virtual bool OnParseInfo(const VideoInfo& info) = 0;
  virtual bool OnPacket(const AVPacket* frame) = 0;
  virtual void OnEos() = 0;
  virtual bool Running() = 0;
};

class VideoParser {
 public:
  explicit VideoParser(IDemuxEventHandle* handle) : handler_(handle) {}
  ~VideoParser() { Close(); }
  bool Open(const char* url, bool save_file = false);
  // -1 for error, 1 for eos
  int ParseLoop(uint32_t frame_interval);
  void Close();
  bool CheckTimeout();
  bool IsRtsp() { return is_rtsp_; }

  const VideoInfo& GetVideoInfo() const { return info_; }

 private:
  static constexpr uint32_t max_receive_timeout_{3000};

  AVFormatContext* p_format_ctx_ = nullptr;
  AVPacket packet_;
  AVDictionary* options_{nullptr};

  VideoInfo info_;
  IDemuxEventHandle* handler_;
  std::unique_ptr<detail::FileSaver> saver_{nullptr};
  std::chrono::time_point<std::chrono::steady_clock> last_receive_frame_time_{};

  uint64_t frame_index_{0};
  int32_t video_index_{0};
  std::atomic<bool> have_video_source_{false};
  bool first_frame_{true};
  bool is_rtsp_{false};
};

#endif  // EDK_SAMPLES_VIDEO_PARSER_H_
