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

#ifndef UNITEST_UTIL_FFMPEG_DEMUXER_H_
#define UNITEST_UTIL_FFMPEG_DEMUXER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavformat/avio.h>

#ifdef __cplusplus
}
#endif

#include <mutex>
#include <iostream>
#include <sstream>


inline bool CheckStatus(int e, int i_line, const char *sz_file) {
    if (e < 0) {
        std::cout << "General error " << e << " at line "
                       << i_line << " in file " << sz_file;
        return false;
    }
    return true;
}

#define FFMPEG_STATUS_CHECK(call) CheckStatus(call, __LINE__, __FILE__)

//---------------------------------------------------------------------------
//! \file ffmpeg_demuxer.h
//! \brief Provides functionality for stream demuxing
//!
//! This header file is used by sample apps to demux input video clips before decoding frames from it.
//---------------------------------------------------------------------------

/**
 * FFMPEG use FF_AV_INPUT_BUFFER_PADDING_SIZE instead of
 * FF_INPUT_BUFFER_PADDING_SIZE since from version 2.8
 * (avcodec.h/version:56.56.100)
 * */

#define FFMPEG_VERSION_2_8 AV_VERSION_INT(56, 56, 100)

/**
 * FFMPEG use AVCodecParameters instead of AVCodecContext
 * since from version 3.1(libavformat/version:57.40.100)
 **/

#define FFMPEG_VERSION_3_1 AV_VERSION_INT(57, 40, 100)


#define FFMPEG_VERSION_4_0 AV_VERSION_INT(58, 9, 100)
/**
 * FFMPEG use ID of AV_CODEC_ID_AVS2
 * since from version 4.2(libavcodec/version:58.54.100, AV_CODEC_ID_AVS2 is defined inside libavcodec)
 **/

#define FFMPEG_VERSION_4_2 AV_VERSION_INT(58, 54, 100)

/**
* @brief libavformat wrapper class. Retrieves the elementary encoded stream from the container format.
*/

/**
 * AVS2
 */
enum BSBoundaryType {
  BSPARSER_NO_BOUNDARY = 0,
  BSPARSER_BOUNDARY = 1,
  BSPARSER_BOUNDARY_NON_SLICE_NAL = 2
};

struct BSParser {
  FILE* file;
  off_t size;
};

class FFmpegDemuxer{
 private:
  AVFormatContext          *fmtc_  = nullptr;
  AVIOContext              *avioc_ = nullptr;
  AVPacket                  pkt_;
  AVPacket                  pkt_filtered_;
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
  AVBSFContext             *bsfc_  = nullptr;
#else
  AVBitStreamFilterContext *bsfc_  = nullptr;
#endif
  AVCodecID                 video_codec_;
  AVPixelFormat             chroma_format_;
  int                       video_stream_;
  int                       width_;
  int                       height_;
  int                       bit_depth_;
  int                       bpp_;
  int                       chroma_height_;
  double                    time_base_ = 0.0;
  int64_t                   user_time_scale_ = 0;
  bool                      mp4_h264_;
  bool                      mp4_hevc_;
  bool                      mp4_mpeg4_;
  uint8_t                  *data_with_header_ = nullptr;
  unsigned int              frame_count_ = 0;
  const char* fname;

 public:
  class DataProvider {
   public:
    virtual ~DataProvider() {}
    virtual int GetData(uint8_t *buf, int size) = 0;
  };

 private:
    /**
    *   @brief  Private constructor to initialize libavformat resources.
    *   @param  fmtc - Pointer to AVFormatContext allocated inside avformat_open_input()
    */
  explicit FFmpegDemuxer(AVFormatContext *fmtc, int64_t time_scale = 1000 /*Hz*/) : fmtc_(fmtc) {
    if (!fmtc_) {
      std::ostringstream err;
      err << "No AVFormatContext provided." << std::endl;
      throw std::invalid_argument(err.str());
    }

    // Initialize packet fields with default values
    // av_init_packet(&pkt_);
    pkt_.data = nullptr;
    pkt_.size = 0;
    // av_init_packet(&pkt_filtered_);
    pkt_filtered_.data = nullptr;
    pkt_filtered_.size = 0;

    // std::cout << "Media format: " << fmtc_->iformat->long_name << " (" << fmtc_->iformat->name << ")";

    FFMPEG_STATUS_CHECK(avformat_find_stream_info(fmtc_, NULL));
    video_stream_ = av_find_best_stream(fmtc_, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (video_stream_ < 0) {
        std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "Could not find stream in input file";
        std::ostringstream err;
        err << "Could not find stream." << std::endl;
        throw std::invalid_argument(err.str());
    }

#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
    video_codec_ = fmtc_->streams[video_stream_]->codecpar->codec_id;
    width_ = fmtc_->streams[video_stream_]->codecpar->width;
    height_ = fmtc_->streams[video_stream_]->codecpar->height;
    chroma_format_ = (AVPixelFormat)fmtc_->streams[video_stream_]->codecpar->format;
#else
    video_codec_ = fmtc_->streams[video_stream_]->codec->codec_id;
    width_ = fmtc_->streams[video_stream_]->codec->width;
    height_ = fmtc_->streams[video_stream_]->codec->height;
    chroma_format_ = (AVPixelFormat)fmtc_->streams[video_stream_]->codec->pix_fmt;
#endif

    AVRational rTimeBase = fmtc_->streams[video_stream_]->time_base;
    time_base_ = av_q2d(rTimeBase);
    user_time_scale_ = time_scale;

    // Set bit depth, chroma height, bits per pixel based on chroma_format_ of input
    switch (chroma_format_) {
    case AV_PIX_FMT_YUV420P10LE:
      bit_depth_ = 10;
      chroma_height_ = (height_ + 1) >> 1;
      bpp_ = 2;
      break;
    case AV_PIX_FMT_YUV420P12LE:
      bit_depth_ = 12;
      chroma_height_ = (height_ + 1) >> 1;
      bpp_ = 2;
      break;
    case AV_PIX_FMT_YUV444P10LE:
      bit_depth_ = 10;
      chroma_height_ = height_ << 1;
      bpp_ = 2;
      break;
    case AV_PIX_FMT_YUV444P12LE:
      bit_depth_ = 12;
      chroma_height_ = height_ << 1;
      bpp_ = 2;
      break;
    case AV_PIX_FMT_YUV444P:
      bit_depth_ = 8;
      chroma_height_ = height_ << 1;
      bpp_ = 1;
      break;
    case AV_PIX_FMT_YUV420P:
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:   // jpeg decoder output is subsampled to NV12 for 422/444 so treat it as 420
    case AV_PIX_FMT_YUVJ444P:   // jpeg decoder output is subsampled to NV12 for 422/444 so treat it as 420
      bit_depth_ = 8;
      chroma_height_ = (height_ + 1) >> 1;
      bpp_ = 1;
      break;
    default:
      std::cout << "ChromaFormat not recognized. Assuming 420";
      bit_depth_ = 8;
      chroma_height_ = (height_ + 1) >> 1;
      bpp_ = 1;
    }

    mp4_h264_ = video_codec_ == AV_CODEC_ID_H264 && (
            !strcmp(fmtc_->iformat->long_name, "QuickTime / MOV")
            || !strcmp(fmtc_->iformat->long_name, "FLV (Flash Video)")
            || !strcmp(fmtc_->iformat->long_name, "Matroska / WebM"));
    mp4_hevc_ = video_codec_ == AV_CODEC_ID_HEVC && (
            !strcmp(fmtc_->iformat->long_name, "QuickTime / MOV")
            || !strcmp(fmtc_->iformat->long_name, "FLV (Flash Video)")
            || !strcmp(fmtc_->iformat->long_name, "Matroska / WebM"));

    mp4_mpeg4_ = video_codec_ == AV_CODEC_ID_MPEG4 && (
            !strcmp(fmtc_->iformat->long_name, "QuickTime / MOV")
            || !strcmp(fmtc_->iformat->long_name, "FLV (Flash Video)")
            || !strcmp(fmtc_->iformat->long_name, "Matroska / WebM"));

    // Initialize bitstream filter and its required resources
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
    if (mp4_h264_) {
      const AVBitStreamFilter *bsf = av_bsf_get_by_name("h264_mp4toannexb");
      if (!bsf) {
        std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "av_bsf_get_by_name() failed";
        std::ostringstream err;
        err << "av_bsf_get_by_name() failed." << std::endl;
        throw std::invalid_argument(err.str());
      }

      int err = av_bsf_alloc(bsf, &bsfc_);
      if (err < 0) {
        std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "av_bsf_alloc() failed";
        std::ostringstream err;
        err << "av_bsf_alloc() failed." << std::endl;
        throw std::invalid_argument(err.str());
      }

      avcodec_parameters_copy(bsfc_->par_in, fmtc_->streams[video_stream_]->codecpar);

      err = av_bsf_init(bsfc_);
      if (err < 0) {
        if (bsfc_) {
          av_bsf_free(&bsfc_);
          bsfc_ = nullptr;
        }
        std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "av_bsf_init() failed";
        std::ostringstream err;
        err << "av_bsf_init() failed." << std::endl;
        throw std::invalid_argument(err.str());
      }
    }

    if (mp4_hevc_) {
      const AVBitStreamFilter *bsf = av_bsf_get_by_name("hevc_mp4toannexb");
      if (!bsf) {
        std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "av_bsf_get_by_name() failed";
        std::ostringstream err;
        err << "av_bsf_get_by_name() failed" << std::endl;
        throw std::invalid_argument(err.str());
      }

      int err = av_bsf_alloc(bsf, &bsfc_);
      if (err < 0) {
        std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "av_bsf_alloc() failed";
        std::ostringstream err;
        err << "av_bsf_alloc() failed" << std::endl;
        throw std::invalid_argument(err.str());
      }

      avcodec_parameters_copy(bsfc_->par_in, fmtc_->streams[video_stream_]->codecpar);

      err = av_bsf_init(bsfc_);
      if (err < 0) {
        if (bsfc_) {
          av_bsf_free(&bsfc_);
          bsfc_ = nullptr;
        }
        std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "av_bsf_init() failed";
        std::ostringstream err;
        err << "av_bsf_init() failed" << std::endl;
        throw std::invalid_argument(err.str());
      }
    }
#else
    if (mp4_h264_) {
      bsfc_ = av_bitstream_filter_init("h264_mp4toannexb");
      if (!bsfc_) {
        LOG(ERROR) << "FFmpeg error: " << __FILE__ << " " << __LINE__
                   << " " << "av_bitstream_filter_init() failed";
        std::ostringstream err;
        err << "av_bitstream_filter_init()" << std::endl;
        throw std::invalid_argument(err.str());
      }
    }
    if (mp4_hevc_) {
      bsfc_ = av_bitstream_filter_init("hevc_mp4toannexb");
      if (!bsfc_) {
        LOG(ERROR) << "FFmpeg error: " << __FILE__ << " " << __LINE__
                   << " " << "av_bitstream_filter_init() failed";
        std::ostringstream err;
        err << "av_bitstream_filter_init() failed" << std::endl;
        throw std::invalid_argument(err.str());
      }
    }
#endif
  }

  AVFormatContext *CreateFormatContext(DataProvider *data_provider) {
#if LIBAVFORMAT_VERSION_INT <= FFMPEG_VERSION_4_0
    av_register_all();
#endif
    avformat_network_init();

    AVFormatContext *ctx = nullptr;
    if (!(ctx = avformat_alloc_context())) {
      std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__;
      return nullptr;
    }

    uint8_t *avioc_buffer = nullptr;
    int avioc_buffer_size = 8 * 1024 * 1024;
    avioc_buffer = reinterpret_cast<uint8_t *>(av_malloc(avioc_buffer_size));
    if (!avioc_buffer) {
      std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__;
      return nullptr;
    }
    avioc_ = avio_alloc_context(avioc_buffer, avioc_buffer_size,
        0, data_provider, &ReadPacket, nullptr, nullptr);
    if (!avioc_) {
      std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__;
      return nullptr;
    }
    ctx->pb = avioc_;

    if (0 != avformat_open_input(&ctx, nullptr, nullptr, nullptr)) {
      std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                     << " " << "avformat_open_input() failed";
      return nullptr;
    }
    return ctx;
  }

  /**
  *   @brief  Allocate and return AVFormatContext*.
  *   @param  sz_file_path - Filepath pointing to input stream.
  *   @return Pointer to AVFormatContext
  */
  AVFormatContext *CreateFormatContext(const char *sz_file_path) {
    static std::mutex init_mutex;
    std::lock_guard<std::mutex> lock(init_mutex);
    AVFormatContext *ctx = nullptr;
    AVDictionary *opts = nullptr;
#if LIBAVFORMAT_VERSION_INT <= FFMPEG_VERSION_4_0
    av_register_all();
#endif
    avformat_network_init();

    if (!strncasecmp(sz_file_path, "rtsp://", strlen("rtsp://"))
        || !strncasecmp(sz_file_path, "rtmp://", strlen("rtmp://"))
        || !strncasecmp(sz_file_path, "http://", strlen("http://"))) {
      // options, this is only for weak network condition
      av_dict_set(&opts, "buffer_size", "1024000", 0);
      av_dict_set(&opts, "max_delay", "500000", 0);
      av_dict_set(&opts, "stimeout", "20000000", 0);
      av_dict_set(&opts, "rtsp_flags", "prefer_tcp", 0);
    } else {
      // options
      av_dict_set(&opts, "buffer_size", "1024000", 0);
      av_dict_set(&opts, "max_delay", "500000", 0);
    }

    if (0 != avformat_open_input(&ctx, sz_file_path, nullptr, &opts)) {
      std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                     << " " << "avformat_open_input() failed";
      return nullptr;
    }
    if (opts) {
      av_dict_free(&opts);
      opts = nullptr;
    }
    fname = sz_file_path;
    return ctx;
  }

 public:
  explicit FFmpegDemuxer(const char *sz_file_path, int64_t time_scale = 1000 /*Hz*/) :
            FFmpegDemuxer(CreateFormatContext(sz_file_path), time_scale) {}
  explicit FFmpegDemuxer(DataProvider *data_provider,  int64_t time_scale = 1000 /*Hz*/) :
            FFmpegDemuxer(CreateFormatContext(data_provider), time_scale) {avioc_ = fmtc_->pb;}
  ~FFmpegDemuxer() {
    if (!fmtc_) {
      return;
    }

    if (pkt_.data) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
      av_packet_unref(&pkt_);
#else
      av_free_packet(&pkt_);
#endif
    }

    if (pkt_filtered_.data) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
      av_packet_unref(&pkt_filtered_);
#else
      av_freep(&pkt_filtered_.data);
      av_free_packet(&pkt_filtered_);
#endif
    }

    if (bsfc_) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
      av_bsf_free(&bsfc_);
#else
      av_bitstream_filter_close(bsfc_);
#endif
      bsfc_ = nullptr;
    }

    avformat_close_input(&fmtc_);
    fmtc_ = nullptr;

    if (avioc_) {
      av_freep(&avioc_->buffer);
      av_freep(&avioc_);
      avioc_ = nullptr;
    }

    if (data_with_header_) {
      av_free(data_with_header_);
      data_with_header_ = nullptr;
    }
  }
  AVCodecID GetVideoCodec() {
    return video_codec_;
  }
  AVPixelFormat GetChromaFormat() {
    return chroma_format_;
  }
  int GetWidth() {
    return width_;
  }
  int GetHeight() {
    return height_;
  }
  int GetBitDepth() {
    return bit_depth_;
  }
  int GetFrameSize() {
    return width_ * (height_ + chroma_height_) * bpp_;
  }
  bool ReadFrame(uint8_t **video, int *video_bytes_num, int64_t *pts = nullptr) {
    if (!fmtc_) {
      return false;
    }
    *video_bytes_num = 0;

    if (pkt_.data) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
      av_packet_unref(&pkt_);
#else
      av_free_packet(&pkt_);
#endif
    }

    if (mp4_h264_ || mp4_hevc_) {
      if (pkt_filtered_.data && pkt_filtered_.size > 0) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
        av_packet_unref(&pkt_filtered_);
#else
        av_freep(&pkt_filtered_.data);
        av_free_packet(&pkt_filtered_);
#endif
      }
    }

    int e = 0;
    while ((e = av_read_frame(fmtc_, &pkt_)) >= 0 && pkt_.stream_index != video_stream_) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
      av_packet_unref(&pkt_);
#else
      av_free_packet(&pkt_);
#endif
    }

    if (e < 0) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
      av_packet_unref(&pkt_);
#else
      av_free_packet(&pkt_);
#endif
      return false;
    }

    if (mp4_h264_ || mp4_hevc_) {
      if (bsfc_) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
        e = av_bsf_send_packet(bsfc_, &pkt_);
        if (e < 0) {
          std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                    << " " << "Packet submission for filtering failed";
          return false;
        }

        e = av_bsf_receive_packet(bsfc_, &pkt_filtered_);
        if (e < 0) {
          std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__
                    << " " << "Filter packet receive failed";
          return false;
        }
#else
        e = av_bitstream_filter_filter(bsfc_, fmtc_->streams[video_stream_]->codec, nullptr,
                                      &pkt_filtered_.data, &pkt_filtered_.size,
                                      pkt_.data, pkt_.size, 0);
        if (e < 0) {
          LOG(WARNING) << "FFmpeg error: " << __FILE__ << " " << __LINE__
                       << " " << "Filter packet receive failed";
          pkt_filtered_.data = nullptr;
          pkt_filtered_.size = 0;
          return false;
        }

#endif
      }
      *video = pkt_filtered_.data;
      *video_bytes_num = pkt_filtered_.size;
      if (pts) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
        *pts = (int64_t) (pkt_filtered_.pts * user_time_scale_ * time_base_);
#else
        *pts = (int64_t)(pkt_.pts * user_time_scale_ * time_base_);
#endif
      }
    } else {
      if (mp4_mpeg4_ && (frame_count_ == 0)) {
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
        int extraDataSize = fmtc_->streams[video_stream_]->codecpar->extradata_size;
#else
        int extraDataSize = fmtc_->streams[video_stream_]->codec->extradata_size;
#endif
        if (extraDataSize > 0) {
          // extradata contains start codes 00 00 01. Subtract its size
          data_with_header_ = reinterpret_cast<uint8_t *>
                                (av_malloc(extraDataSize + pkt_.size - 3*sizeof(uint8_t)));
          if (!data_with_header_) {
            std::cout << "FFmpeg error: " << __FILE__ << " " << __LINE__;
            return false;
          }
#if LIBAVFORMAT_VERSION_INT >= FFMPEG_VERSION_3_1
          memcpy(data_with_header_, fmtc_->streams[video_stream_]->codecpar->extradata, extraDataSize);

#else
          memcpy(data_with_header_, fmtc_->streams[video_stream_]->codec->extradata, extraDataSize);

#endif
          memcpy(data_with_header_+extraDataSize, pkt_.data+3, pkt_.size - 3*sizeof(uint8_t));
          *video = data_with_header_;
          *video_bytes_num = extraDataSize + pkt_.size - 3*sizeof(uint8_t);
        }
      } else {
        *video = pkt_.data;
        *video_bytes_num = pkt_.size;
      }

      if (pts) {
        *pts = (int64_t)(pkt_.pts * user_time_scale_ * time_base_);
      }
    }

    frame_count_++;

    return true;
}

  static int ReadPacket(void *opaque, uint8_t *buf, int size) {
    return (reinterpret_cast<DataProvider *>(opaque))->GetData(buf, size);
  }

  AVCodecID CodecId() {
#if LIBAVCODEC_VERSION_INT < FFMPEG_VERSION_4_2
    if (video_codec_ == AV_CODEC_ID_CAVS)
      std::cout << "FFmpeg Warning: Codec type is avs or avs2, Please upgrade FFmpeg to 4.2 or above";
#endif
    return video_codec_;
  }
};
#if 0
inline cncodecType_t FFmpeg2CNCodecId(AVCodecID id) {
  switch (id) {
    case AV_CODEC_ID_MPEG1VIDEO : return CNCODEC_MPEG1;
    case AV_CODEC_ID_MPEG2VIDEO : return CNCODEC_MPEG2;
    case AV_CODEC_ID_MPEG4      : return CNCODEC_MPEG4;
    case AV_CODEC_ID_VC1        : return CNCODEC_VC1;
    case AV_CODEC_ID_H264       : return CNCODEC_H264;
    case AV_CODEC_ID_HEVC       : return CNCODEC_HEVC;
    case AV_CODEC_ID_VP8        : return CNCODEC_VP8;
    case AV_CODEC_ID_VP9        : return CNCODEC_VP9;
    case AV_CODEC_ID_MJPEG      : return CNCODEC_JPEG;
#if LIBAVCODEC_VERSION_INT >= FFMPEG_VERSION_4_2
    case AV_CODEC_ID_AVS2       : return CNCODEC_AVS2;
#endif
    default                     : return CNCODEC_H264;
  }
}
#endif
#endif  //  UNITEST_UTIL_FFMPEG_DEMUXER_H_

