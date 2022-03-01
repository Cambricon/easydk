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

#include <libyuv.h>

#include <memory>

#include "cxxutil/log.h"
#include "easyinfer/mlu_memory_op.h"
#include "video_decoder.h"
#include "runner.h"
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// ------------------------------- EasyDecodeImpl --------------------------------------
class EasyDecodeImpl : public VideoDecoderImpl {
 public:
  EasyDecodeImpl(VideoDecoder* interface, IDecodeEventHandle* handle, int device_id)
      : VideoDecoderImpl(interface, handle, device_id) {}
  bool Init() override {
    VideoInfo& info = interface_->GetVideoInfo();
    edk::EasyDecode::Attr attr;
    attr.frame_geometry.w = info.width;
    attr.frame_geometry.h = info.height;
    p_bsfc_ = nullptr;
    if (AV_CODEC_ID_H264 == info.codec_id) {
      attr.codec_type = edk::CodecType::H264;
      p_bsfc_ = av_bitstream_filter_init("h264_mp4toannexb");
    } else if (AV_CODEC_ID_HEVC == info.codec_id) {
      attr.codec_type = edk::CodecType::H265;
      p_bsfc_ = av_bitstream_filter_init("hevc_mp4toannexb");
    } else if (AV_CODEC_ID_MJPEG == info.codec_id) {
      attr.codec_type = edk::CodecType::JPEG;
    } else {
      LOGE(SAMPLES) << "nonsupport codec id: " << info.codec_id;
      return false;
    }
    codec_ctx_ = info.codec_ctx;
    // attr.interlaced = info.progressive ? false : true;
    attr.pixel_format = edk::PixelFmt::NV12;
    attr.dev_id = device_id_;
    attr.frame_callback = std::bind(&IDecodeEventHandle::OnDecodeFrame, handle_, std::placeholders::_1);
    attr.eos_callback = std::bind(&IDecodeEventHandle::OnEos, handle_);
    attr.silent = false;
    attr.output_buffer_num = 6;
    decode_ = edk::EasyDecode::New(attr);

    return true;
  }
  bool FeedPacket(const AVPacket* packet) override {
    AVPacket* parsed_pack = const_cast<AVPacket*>(packet);
    if (p_bsfc_) {
      av_bitstream_filter_filter(p_bsfc_, codec_ctx_, NULL, reinterpret_cast<uint8_t **>(&parsed_pack->data),
                                 reinterpret_cast<int *>(&parsed_pack->size), packet->data, packet->size, 0);
    }

    edk::CnPacket pkt;
    pkt.data = parsed_pack->data;
    pkt.length = parsed_pack->size;
    pkt.pts = parsed_pack->pts;
    bool ret = decode_->FeedData(pkt);
    // free packet
    if (p_bsfc_) {
      av_freep(&parsed_pack->data);
    }
    return ret;
  }
  void FeedEos() override {
    decode_->FeedEos();
  }
  void ReleaseFrame(edk::CnFrame&& frame) override {
    decode_->ReleaseBuffer(frame.buf_id);
  }
  bool CopyFrameD2H(void *dst, const edk::CnFrame &frame) { return decode_->CopyFrameD2H(dst, frame); }
  ~EasyDecodeImpl() {
    if (p_bsfc_) {
      av_bitstream_filter_close(p_bsfc_);
      p_bsfc_ = nullptr;
    }
  }

 private:
  AVBitStreamFilterContext* p_bsfc_ = nullptr;
  AVCodecContext* codec_ctx_ = nullptr;
  std::unique_ptr<edk::EasyDecode> decode_{nullptr};
};

_Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
// ------------------------------- FFmpegDecodeImpl --------------------------------------
#define CALL_CNRT_FUNC(func, msg)                                                            \
  do {                                                                                       \
    int ret = (func);                                                                        \
    if (0 != ret) {                                                                          \
      LOGE(DECODE) << msg << " error code: " << ret;                                         \
      THROW_EXCEPTION(Exception::INTERNAL, msg " cnrt error code : " + std::to_string(ret)); \
    }                                                                                        \
  } while (0)
class FFmpegDecodeImpl : public VideoDecoderImpl {
 public:
  FFmpegDecodeImpl(VideoDecoder* interface, IDecodeEventHandle* handle, int device_id)
      : VideoDecoderImpl(interface, handle, device_id) {}
  bool Init() override {
    VideoInfo& info = interface_->GetVideoInfo();
    AVCodec *dec = avcodec_find_decoder(info.codec_id);
    if (!dec) {
      LOGE(SAMPLE) << "avcodec_find_decoder failed";
      return false;
    }
    decode_ = avcodec_alloc_context3(dec);
    if (!decode_) {
      LOGE(SAMPLE) << "Failed to do avcodec_alloc_context3";
      return false;
    }
    // av_codec_set_pkt_timebase(instance_, st->time_base);

    if (!info.extra_data.empty()) {
      decode_->extradata = info.extra_data.data();
      decode_->extradata_size = info.extra_data.size();
    }
    if (avcodec_open2(decode_, dec, NULL) < 0) {
      LOGE(SAMPLE) << "Failed to open codec";
      return false;
    }
    av_frame_ = av_frame_alloc();
    if (!av_frame_) {
      LOGE(SAMPLE) << "Could not alloc frame";
      return false;
    }
    eos_got_.store(0);
    eos_sent_.store(0);
    return true;
  }
  bool FeedPacket(const AVPacket* pkt) override {
    int got_frame = 0;
    int ret = avcodec_decode_video2(decode_, av_frame_, &got_frame, pkt);
    if (ret < 0) {
      LOGE(SAMPLE) << "avcodec_decode_video2 failed, data ptr, size:" << pkt->data << ", " << pkt->size;
      return false;
    }
    if (got_frame) {
      ProcessFrame(av_frame_);
    }
    return true;
  }
  void FeedEos() override {
    AVPacket packet;
    av_init_packet(&packet);
    packet.size = 0;
    packet.data = NULL;

    LOGI(SAMPLE) << "Sent EOS packet to decoder";
    eos_sent_.store(1);
    // flush all frames ...
    int got_frame = 0;
    do {
      avcodec_decode_video2(decode_, av_frame_, &got_frame, &packet);
      if (got_frame) ProcessFrame(av_frame_);
    } while (got_frame);

    if (handle_) {
      handle_->OnEos();
    }
    eos_got_.store(1);
  }
  void ReleaseFrame(edk::CnFrame&& frame) override {
    for (uint32_t i = 0; i < frame.n_planes; i++) {
      mem_op.FreeMlu(frame.ptrs[i]);
    }
  }
  bool CopyFrameD2H(void *dst, const edk::CnFrame &frame) override {
    return edk::EasyDecode::CopyFrameD2H(dst, frame);
  }
  ~FFmpegDecodeImpl() {
    if (av_frame_) {
      av_frame_free(&av_frame_);
    }
    if (decode_) {
      avcodec_close(decode_);
      avcodec_free_context(&decode_);
    }
  }

 private:
  bool ProcessFrame(AVFrame* frame) {
    edk::CnFrame cn_frame;
#if LIBAVFORMAT_VERSION_INT <= FFMPEG_VERSION_3_1
    cn_frame.pts = frame->pkt_pts;
#else
    cn_frame.pts = frame->pts;
#endif
    cn_frame.width = frame->width;
    cn_frame.height = frame->height;
    cn_frame.pformat = edk::PixelFmt::NV12;
    cn_frame.n_planes = 2;
    cn_frame.strides[0] = frame->linesize[0];
    cn_frame.strides[1] = frame->linesize[0];
    uint32_t plane_size[2];
    plane_size[0] = cn_frame.height * cn_frame.strides[0];
    plane_size[1] = cn_frame.height * cn_frame.strides[1] / 2;
    cn_frame.frame_size = plane_size[0] + plane_size[1];
    uint8_t* cpu_plane[2];
    std::unique_ptr<uint8_t[]> dst_y(new uint8_t[cn_frame.frame_size]);
    cpu_plane[0] = dst_y.get();
    cpu_plane[1] = dst_y.get() + plane_size[0];
    switch (decode_->pix_fmt) {
      case AV_PIX_FMT_YUV420P:
      case AV_PIX_FMT_YUVJ420P: {
        libyuv::I420ToNV12(static_cast<uint8_t *>(frame->data[0]), frame->linesize[0],
                           static_cast<uint8_t *>(frame->data[1]), frame->linesize[1],
                           static_cast<uint8_t *>(frame->data[2]), frame->linesize[2], cpu_plane[0],
                           cn_frame.strides[0], cpu_plane[1], cn_frame.strides[1], cn_frame.width, cn_frame.height);
        break;
      }
      case AV_PIX_FMT_YUYV422: {
        int tmp_stride = (frame->width + 1) / 2 * 2;
        int tmp_height = (frame->height + 1) / 2 * 2;
        std::unique_ptr<uint8_t[]> tmp_i420_y(new uint8_t[tmp_stride * tmp_height]);
        std::unique_ptr<uint8_t[]> tmp_i420_u(new uint8_t[tmp_stride * tmp_height / 4]);
        std::unique_ptr<uint8_t[]> tmp_i420_v(new uint8_t[tmp_stride * tmp_height / 4]);

        libyuv::YUY2ToI420(static_cast<uint8_t *>(frame->data[0]), frame->linesize[0], tmp_i420_y.get(),
                           tmp_stride, tmp_i420_u.get(), tmp_stride / 2, tmp_i420_v.get(), tmp_stride / 2,
                           frame->width, frame->height);

        libyuv::I420ToNV12(tmp_i420_y.get(), tmp_stride, tmp_i420_u.get(), tmp_stride/2, tmp_i420_v.get(), tmp_stride/2,
            cpu_plane[0], cn_frame.strides[0], cpu_plane[1], cn_frame.strides[1], cn_frame.width, cn_frame.height);
        break;
      }
      default: {
        LOGE(SAMPLE) << "FFmpegDecode ProcessFrame() Unsupported pixel format: " << decode_->pix_fmt;
        return false;
      }
    }

    cn_frame.device_id = device_id_;
    for (unsigned i = 0; i < cn_frame.n_planes; i++) {
      cn_frame.ptrs[i] = mem_op.AllocMlu(plane_size[i]);
      mem_op.MemcpyH2D(cn_frame.ptrs[i], cpu_plane[i], plane_size[i]);
    }

    // Send cn_frame to handle
    if (handle_) {
      handle_->OnDecodeFrame(cn_frame);
    }
    return true;
  }

  edk::MluMemoryOp mem_op;
  AVCodecContext* decode_{nullptr};
  AVFrame *av_frame_ = nullptr;
  std::atomic<int> eos_got_{0};
  std::atomic<int> eos_sent_{0};
};
_Pragma("GCC diagnostic pop")

// ------------------------------- FFmpegMluDecodeImpl --------------------------------------
#if LIBAVFORMAT_VERSION_INT == FFMPEG_VERSION_4_2_2
#define FFMPEG_MLU_OUTPUT_ON_MLU
// comment the following line to output decoding results on mlu
#undef FFMPEG_MLU_OUTPUT_ON_MLU

#ifdef FFMPEG_MLU_OUTPUT_ON_MLU
static enum AVPixelFormat hw_pix_fmt;
static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
  const enum AVPixelFormat *format;
  for (format = pix_fmts; *format != -1; format++) {
    if (*format == hw_pix_fmt) return *format;
  }
  fprintf(stderr, "Failed to get HW surface format.\n");
  return AV_PIX_FMT_NONE;
}
#endif
class FFmpegMluDecodeImpl : public VideoDecoderImpl {
 public:
  FFmpegMluDecodeImpl(VideoDecoder* interface, IDecodeEventHandle* handle, int device_id)
      : VideoDecoderImpl(interface, handle, device_id) {}
  bool Init() override {
    VideoInfo& info = interface_->GetVideoInfo();
    AVCodec *dec;
    switch (info.codec_id) {
      case AV_CODEC_ID_H264:
        dec = avcodec_find_decoder_by_name("h264_mludec");
        break;
      case AV_CODEC_ID_HEVC:
        dec = avcodec_find_decoder_by_name("hevc_mludec");
        break;
      case AV_CODEC_ID_VP8:
        dec = avcodec_find_decoder_by_name("vp8_mludec");
        break;
      case AV_CODEC_ID_VP9:
        dec = avcodec_find_decoder_by_name("vp9_mludec");
        break;
      case AV_CODEC_ID_MJPEG:
        dec = avcodec_find_decoder_by_name("mjpeg_mludec");
        break;
      default:
        dec = avcodec_find_decoder(info.codec_id);
        break;
    }

    if (!dec) {
      LOGE(SAMPLE) << "avcodec_find_decoder failed";
      return false;
    }
#ifdef FFMPEG_MLU_OUTPUT_ON_MLU
    AVHWDeviceType dev_type = av_hwdevice_find_type_by_name("mlu");
    for (int i = 0;; i++) {
      const AVCodecHWConfig *config = avcodec_get_hw_config(dec, i);
      if (!config) {
        LOGE(SAMPLE)<< "Decoder " << dec->name << " doesn't support device type "
                    << av_hwdevice_get_type_name(dev_type) << std::endl;
        return -1;
      }
      if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == dev_type) {
        hw_pix_fmt = config->pix_fmt;
        break;
      }
    }
#endif
    decode_ = avcodec_alloc_context3(dec);
    if (!decode_) {
      LOGE(SAMPLE) << "Failed to do avcodec_alloc_context3";
      return false;
    }
    // av_codec_set_pkt_timebase(instance_, st->time_base);

    if (avcodec_parameters_to_context(decode_, info.codecpar) != 0) {
        LOGE(SAMPLE) << "Copy codec context failed";
        return false;
    }

    AVDictionary *decoder_opts = nullptr;
    av_dict_set_int(&decoder_opts, "device_id", device_id_, 0);

#ifdef FFMPEG_MLU_OUTPUT_ON_MLU
    decode_->get_format = get_hw_format;

    char dev_idx_des[4] = {'\0'};
    AVBufferRef *hw_device_ctx = nullptr;
    snprintf(dev_idx_des, sizeof(device_id_), "%d", device_id_);
    if (av_hwdevice_ctx_create(&hw_device_ctx, dev_type, dev_idx_des, NULL, 0) < 0) {
      LOGE(SAMPLE) << "Failed to create specified HW device.";
      return false;
    }
    decode_->hw_device_ctx = av_buffer_ref(hw_device_ctx);
#endif

    if (avcodec_open2(decode_, dec,  &decoder_opts) < 0) {
      LOGE(SAMPLE) << "Failed to open codec";
      return false;
    }
    av_frame_ = av_frame_alloc();
    if (!av_frame_) {
      LOGE(SAMPLE) << "Could not alloc frame";
      return false;
    }
    eos_got_.store(0);
    eos_sent_.store(0);
    return true;
  }
  bool FeedPacket(const AVPacket* pkt) override {
    int ret = avcodec_send_packet(decode_, pkt);
    if (ret < 0) {
      LOGE(SAMPLE) << "avcodec_send_packet failed, data ptr, size:" << pkt->data << ", " << pkt->size;
      return false;
    }
    ret = avcodec_receive_frame(decode_, av_frame_);
    if (ret >= 0) {
      ProcessFrame(av_frame_);
    }
    return true;
  }
  void FeedEos() override {
    AVPacket packet;
    av_init_packet(&packet);
    packet.size = 0;
    packet.data = NULL;

    LOGI(SAMPLE) << "Sent EOS packet to decoder";
    eos_sent_.store(1);
    avcodec_send_packet(decode_, &packet);
    // flush all frames ...
    int ret = 0;
    do {
      ret = avcodec_receive_frame(decode_, av_frame_);
      if (ret >= 0) ProcessFrame(av_frame_);
    } while (ret >= 0);

    if (handle_) {
      handle_->OnEos();
    }
    eos_got_.store(1);
  }
  void ReleaseFrame(edk::CnFrame&& frame) override {
    for (uint32_t i = 0; i < frame.n_planes; i++) {
      mem_op.FreeMlu(frame.ptrs[i]);
    }
  }
  bool CopyFrameD2H(void *dst, const edk::CnFrame &frame) override {
    return edk::EasyDecode::CopyFrameD2H(dst, frame);
  }
  ~FFmpegMluDecodeImpl() {
    if (av_frame_) {
      av_frame_free(&av_frame_);
    }
    if (decode_) {
      avcodec_close(decode_);
      avcodec_free_context(&decode_);
    }
  }

 private:
  bool ProcessFrame(AVFrame* frame) {
    edk::CnFrame cn_frame;
    cn_frame.pts = frame->pts;
    cn_frame.width = frame->width;
    cn_frame.height = frame->height;
    switch (decode_->sw_pix_fmt) {
      case AV_PIX_FMT_NV12:
        cn_frame.pformat = edk::PixelFmt::NV12;
        break;
      case AV_PIX_FMT_NV21:
        cn_frame.pformat = edk::PixelFmt::NV21;
        break;
      default:
        LOGE(SAMPLE) << "FFmpegMluDecode ProcessFrame() Unsupported pixel format: " << decode_->sw_pix_fmt;
        return false;
    }
    cn_frame.n_planes = 2;
    cn_frame.strides[0] = frame->linesize[0];
    cn_frame.strides[1] = frame->linesize[1];
    uint32_t plane_size[2];
    plane_size[0] = cn_frame.height * cn_frame.strides[0];
    plane_size[1] = cn_frame.height * cn_frame.strides[1] / 2;
    cn_frame.frame_size = plane_size[0] + plane_size[1];
    cn_frame.device_id = device_id_;
    for (unsigned i = 0; i < cn_frame.n_planes; i++) {
      cn_frame.ptrs[i] = mem_op.AllocMlu(plane_size[i]);
#ifdef FFMPEG_MLU_OUTPUT_ON_MLU
      // cn_frame.ptrs[i] = frame->data[i];  // For now, it is not possible to use the mlu data without d2d copy
      mem_op.MemcpyD2D(cn_frame.ptrs[i], frame->data[i], plane_size[i]);
#else
      mem_op.MemcpyH2D(cn_frame.ptrs[i], frame->data[i], plane_size[i]);
#endif
    }

    // Send cn_frame to handle
    if (handle_) {
      handle_->OnDecodeFrame(cn_frame);
    }
    return true;
  }

  edk::MluMemoryOp mem_op;
  AVCodecContext* decode_{nullptr};
  AVFrame *av_frame_ = nullptr;
#ifdef FFMPEG_MLU_OUTPUT_ON_MLU
  AVPixelFormat hw_pix_fmt_;
#endif
  std::atomic<int> eos_got_{0};
  std::atomic<int> eos_sent_{0};
};
#endif

VideoDecoder::VideoDecoder(StreamRunner* runner, DecoderType type, int device_id) : runner_(runner) {
  switch (type) {
    case EASY_DECODE:
      impl_ = new EasyDecodeImpl(this, runner, device_id);
      break;
    case FFMPEG:
      impl_ = new FFmpegDecodeImpl(this, runner, device_id);
      break;
#if LIBAVFORMAT_VERSION_INT == FFMPEG_VERSION_4_2_2
    case FFMPEG_MLU:
      impl_ = new FFmpegMluDecodeImpl(this, runner, device_id);
      break;
#endif
    default:
      LOGF(SAMPLE) << "unsupported decoder type";
  }
}

bool VideoDecoder::OnParseInfo(const VideoInfo& info) {
  info_ = info;
  return impl_->Init();
}

bool VideoDecoder::OnPacket(const AVPacket* packet) {
  return impl_->FeedPacket(packet);
}

void VideoDecoder::OnEos() { LOGI(SAMPLE) << "Get EOS"; }

bool VideoDecoder::Running() {
  return runner_->Running();
}

void VideoDecoder::SendEos() {
  if (!send_eos_) {
    impl_->FeedEos();
    send_eos_ = true;
  }
}
