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

#include <glog/logging.h>
// #include <libyuv.h>

#include <algorithm>
#include <memory>
#include <string>

#include "cnrt.h"

#include "video_decoder.h"
#include "stream_runner.h"

#include "util/utils.hpp"

#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// ------------------------------- cnDecodeImpl --------------------------------------
class EasyDecodeImpl : public VideoDecoderImpl {
 public:
  EasyDecodeImpl(VideoDecoder* interface, IDecodeEventHandle* handle, int device_id)
      : VideoDecoderImpl(interface, handle, device_id) {}
  bool Init() override {
    cnrtSetDevice(device_id_);

    VideoInfo& info = interface_->GetVideoInfo();
    p_bsfc_ = nullptr;

    codec_ctx_ = info.codec_ctx;

    CnedkVdecCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id  = device_id_;

    create_params.max_width = info.width;
    create_params.max_height = info.height;
    create_params.frame_buf_num = 12;  // for CE3226
    create_params.surf_timeout_ms = 5000;
    create_params.userdata = this;
    create_params.GetBufSurf = GetBufSurface_;
    create_params.OnFrame = OnFrame_;
    create_params.OnEos = OnDecodeEos_;
    create_params.OnError = OnError_;

    if (AV_CODEC_ID_H264 == info.codec_id) {
      create_params.type = CNEDK_VDEC_TYPE_H264;
      p_bsfc_ = av_bitstream_filter_init("h264_mp4toannexb");
    } else if (AV_CODEC_ID_HEVC == info.codec_id) {
      create_params.type = CNEDK_VDEC_TYPE_H265;
      p_bsfc_ = av_bitstream_filter_init("hevc_mp4toannexb");
    } else if (AV_CODEC_ID_MJPEG == info.codec_id) {
      create_params.type = CNEDK_VDEC_TYPE_JPEG;
    } else {
      LOG(ERROR) << "[EasyDK Samples] [EasyDecodeImpl] Init(): Unsupported codec id: " << info.codec_id;
      return false;
    }

    if (IsEdgePlatform(device_id_)) {
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV21;
    } else {
      create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
    }

    if (CreateSurfacePool(&surf_pool_, create_params.max_width, create_params.max_height) < 0) {
      LOG(ERROR) << "[EasyDK Samples] [EasyDecodeImpl] Init(): Create BufSurface pool failed ";
      return false;
    }

    VLOG(1) << "[EasyDK Samples] [EasyDecodeImpl] Init(): surf_pool:" << surf_pool_;

    if (CnedkVdecCreate(&vdec_, &create_params) < 0) {
      LOG(ERROR) << "[EasyDK Samples] [EasyDecodeImpl] Init(): Create decoder failed";
      return false;
    }

    return true;
  }

  int CreateSurfacePool(void** surf_pool, int width, int height) {
    CnedkBufSurfaceCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));
    create_params.batch_size = 1;
    create_params.width = width;
    create_params.height = height;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
    create_params.device_id = device_id_;

    if (IsEdgePlatform(device_id_)) {
      create_params.mem_type = CNEDK_BUF_MEM_VB_CACHED;
    } else {
      create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
    }

    if (CnedkBufPoolCreate(surf_pool, &create_params, 16) < 0) {
      LOG(ERROR) << "[EasyDK Samples] [EasyDecodeImpl] CreateSurfacePool(): Create pool failed";
      return -1;
    }

    return 0;
  }

  bool FeedPacket(const AVPacket* packet) override {
    AVPacket* parsed_pack = const_cast<AVPacket*>(packet);
    int e = 0;
    if (p_bsfc_) {
      e = av_bitstream_filter_filter(p_bsfc_, codec_ctx_, NULL, reinterpret_cast<uint8_t **>(&parsed_pack->data),
                                     reinterpret_cast<int *>(&parsed_pack->size), packet->data, packet->size, 0);
    }


    CnedkVdecStream stream;
    memset(&stream, 0, sizeof(stream));

    stream.bits = parsed_pack->data;
    stream.len = parsed_pack->size;
    stream.pts = parsed_pack->pts;
    bool ret = true;
    if (CnedkVdecSendStream(vdec_, &stream, 5000) != 0) {
      LOG(ERROR) << "EasyDK Samples] [EasyDecodeImpl] FeedPacket(): Send stream failed";
      ret = false;
    }
    // free packet
    if (e >= 0 && p_bsfc_) {
      av_freep(&parsed_pack->data);
    }
    return ret;
  }
  void FeedEos() override {
    CnedkVdecStream stream;
    stream.bits = nullptr;
    stream.len = 0;
    stream.pts = 0;
    CnedkVdecSendStream(vdec_, &stream, 5000);
  }
  void ReleaseFrame(CnedkBufSurface* surf) override {
    CnedkBufSurfaceDestroy(surf);
  }

  void Destroy() {
    if (p_bsfc_) {
      av_bitstream_filter_close(p_bsfc_);
      p_bsfc_ = nullptr;
    }
    if (vdec_) {
      CnedkVdecDestroy(vdec_);
      vdec_ = nullptr;
    }

    if (surf_pool_) {
      CnedkBufPoolDestroy(surf_pool_);
      surf_pool_ = nullptr;
    }
  }

  static int GetBufSurface_(CnedkBufSurface **surf,
                            int width, int height, CnedkBufSurfaceColorFormat fmt,
                            int timeout_ms, void*userdata) {
    EasyDecodeImpl *thiz =  reinterpret_cast<EasyDecodeImpl*>(userdata);
    return thiz->GetBufSurface(surf, width, height, fmt, timeout_ms);
  }

  static int OnFrame_(CnedkBufSurface *surf, void *userdata) {
    EasyDecodeImpl *thiz =  reinterpret_cast<EasyDecodeImpl*>(userdata);
    return thiz->OnFrame(surf);
  }

  static int OnDecodeEos_(void *userdata) {
    EasyDecodeImpl *thiz = reinterpret_cast<EasyDecodeImpl*>(userdata);
    return thiz->OnDecodeEos();
  }

  static int OnError_(int errCode, void *userdata) {
    EasyDecodeImpl *thiz = reinterpret_cast<EasyDecodeImpl*>(userdata);
    return thiz->OnError(errCode);
  }

  int GetBufSurface(CnedkBufSurface **surf,
                    int width, int height, CnedkBufSurfaceColorFormat fmt,
                    int timeout_ms) {
    int count = timeout_ms + 1;
    int retry_cnt = 1;
    while (1) {
      int ret = CnedkBufSurfaceCreateFromPool(surf, surf_pool_);
      if (ret == 0) {
        return 0;
      }
      count -= retry_cnt;
      VLOG(3) << "EasyDK Samples] [EasyDecodeImpl] GetBufSurface(): retry, remaining times: " << count;
      if (count <= 0) {
        LOG(ERROR) << "EasyDK Samples] [EasyDecodeImpl] GetBufSurface(): Maximum number of attempts reached: "
                   << timeout_ms;
        return -1;
      }

      usleep(1000 * retry_cnt);
      retry_cnt = std::min(retry_cnt * 2, 10);
    }
    return 0;
  }

  int OnFrame(CnedkBufSurface *surf) {
    handle_->OnDecodeFrame(surf);
    return 0;
  }

  int OnDecodeEos() {
    handle_->OnDecodeEos();
    return 0;
  }

  int OnError(int errCode) {
    return 0;
  }

  ~EasyDecodeImpl() {
    EasyDecodeImpl::Destroy();
  }

 private:
  AVBitStreamFilterContext *p_bsfc_  = nullptr;
  AVCodecContext* codec_ctx_ = nullptr;
  void* vdec_{nullptr};
  void* surf_pool_ = nullptr;
};

// _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wdeprecated-declarations\"")
// // ------------------------------- FFmpegDecodeImpl --------------------------------------
// class FFmpegDecodeImpl : public VideoDecoderImpl {
//  public:
//   FFmpegDecodeImpl(VideoDecoder* interface, IDecodeEventHandle* handle, int device_id)
//       : VideoDecoderImpl(interface, handle, device_id) {}
//   bool Init() override {
//     VideoInfo& info = interface_->GetVideoInfo();
//     AVCodec *dec = avcodec_find_decoder(info.codec_id);
//     if (!dec) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegDecodeImpl] avcodec_find_decoder failed";
//       return false;
//     }
//     decode_ = avcodec_alloc_context3(dec);
//     if (!decode_) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegDecodeImpl] avcodec_alloc_context3 failed";
//       return false;
//     }
//     // av_codec_set_pkt_timebase(instance_, st->time_base);

//     if (!info.extra_data.empty()) {
//       decode_->extradata_size = info.extra_data.size();
//       uint8_t* extradata = reinterpret_cast<uint8_t*>(malloc(decode_->extradata_size));
//       memcpy(extradata, info.extra_data.data(), decode_->extradata_size);
//       decode_->extradata = extradata;
//     }
//     if (avcodec_open2(decode_, dec, NULL) < 0) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegDecodeImpl] Failed to open codec";
//       return false;
//     }
//     av_frame_ = av_frame_alloc();
//     if (!av_frame_) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegDecodeImpl] Could not alloc frame";
//       return false;
//     }
//     eos_got_.store(0);
//     eos_sent_.store(0);
//     return true;
//   }
//   bool FeedPacket(const AVPacket* pkt) override {
//     int got_frame = 0;
//     int ret = avcodec_decode_video2(decode_, av_frame_, &got_frame, pkt);
//     if (ret < 0) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegDecodeImpl] avcodec_decode_video2 failed, data ptr, size:"
//                  << pkt->data << ", " << pkt->size;
//       return false;
//     }
//     if (got_frame) {
//       ProcessFrame(av_frame_);
//     }
//     return true;
//   }
//   void FeedEos() override {
//     AVPacket packet;
//     av_init_packet(&packet);
//     packet.size = 0;
//     packet.data = NULL;

//     LOG(INFO) << "[EasyDK Samples] [FFmpegDecodeImpl] Sent EOS packet to decoder";
//     eos_sent_.store(1);
//     // flush all frames ...
//     int got_frame = 0;
//     do {
//       avcodec_decode_video2(decode_, av_frame_, &got_frame, &packet);
//       if (got_frame) ProcessFrame(av_frame_);
//     } while (got_frame);

//     if (handle_) {
//       handle_->OnEos();
//     }
//     eos_got_.store(1);
//   }
//   void ReleaseFrame(edk::CnFrame&& frame) override {
//     for (uint32_t i = 0; i < frame.n_planes; i++) {
//       mem_op.FreeMlu(frame.ptrs[i]);
//     }
//   }
//   bool CopyFrameD2H(void *dst, const edk::CnFrame &frame) override {
//     return edk::EasyDecode::CopyFrameD2H(dst, frame);
//   }
//   void Destroy() {
//     if (av_frame_) {
//       av_frame_free(&av_frame_);
//       av_frame_ = nullptr;
//     }
//     if (decode_) {
//       avcodec_close(decode_);
//       avcodec_free_context(&decode_);
//       decode_ = nullptr;
//     }
//   }

//   ~FFmpegDecodeImpl() {
//     FFmpegDecodeImpl::Destroy();
//   }

//  private:
//   bool ProcessFrame(AVFrame* frame) {
//     edk::CnFrame cn_frame;
// #if LIBAVFORMAT_VERSION_INT <= FFMPEG_VERSION_3_1
//     cn_frame.pts = frame->pkt_pts;
// #else
//     cn_frame.pts = frame->pts;
// #endif
//     cn_frame.width = frame->width;
//     cn_frame.height = frame->height;
//     cn_frame.pformat = edk::PixelFmt::NV12;
//     cn_frame.n_planes = 2;
//     cn_frame.strides[0] = frame->linesize[0];
//     cn_frame.strides[1] = frame->linesize[0];
//     uint32_t plane_size[2];
//     plane_size[0] = cn_frame.height * cn_frame.strides[0];
//     plane_size[1] = cn_frame.height * cn_frame.strides[1] / 2;
//     cn_frame.frame_size = plane_size[0] + plane_size[1];
//     uint8_t* cpu_plane[2];
//     std::unique_ptr<uint8_t[]> dst_y(new uint8_t[cn_frame.frame_size]);
//     cpu_plane[0] = dst_y.get();
//     cpu_plane[1] = dst_y.get() + plane_size[0];
//     switch (decode_->pix_fmt) {
//       case AV_PIX_FMT_YUV420P:
//       case AV_PIX_FMT_YUVJ420P: {
//         libyuv::I420ToNV12(static_cast<uint8_t *>(frame->data[0]), frame->linesize[0],
//                            static_cast<uint8_t *>(frame->data[1]), frame->linesize[1],
//                            static_cast<uint8_t *>(frame->data[2]), frame->linesize[2], cpu_plane[0],
//                            cn_frame.strides[0], cpu_plane[1], cn_frame.strides[1], cn_frame.width, cn_frame.height);
//         break;
//       }
//       case AV_PIX_FMT_YUYV422: {
//         int tmp_stride = (frame->width + 1) / 2 * 2;
//         int tmp_height = (frame->height + 1) / 2 * 2;
//         std::unique_ptr<uint8_t[]> tmp_i420_y(new uint8_t[tmp_stride * tmp_height]);
//         std::unique_ptr<uint8_t[]> tmp_i420_u(new uint8_t[tmp_stride * tmp_height / 4]);
//         std::unique_ptr<uint8_t[]> tmp_i420_v(new uint8_t[tmp_stride * tmp_height / 4]);

//         libyuv::YUY2ToI420(static_cast<uint8_t *>(frame->data[0]), frame->linesize[0], tmp_i420_y.get(),
//                            tmp_stride, tmp_i420_u.get(), tmp_stride / 2, tmp_i420_v.get(), tmp_stride / 2,
//                            frame->width, frame->height);

//         libyuv::I420ToNV12(tmp_i420_y.get(), tmp_stride, tmp_i420_u.get(), tmp_stride/2, tmp_i420_v.get(),
//             tmp_stride/2, cpu_plane[0], cn_frame.strides[0], cpu_plane[1], cn_frame.strides[1], cn_frame.width,
//             cn_frame.height);
//         break;
//       }
//       default: {
//         LOG(ERROR) << "[EasyDK Samples] [FFmpegDecodeImpl] ProcessFrame() Unsupported pixel format: "
//                    << decode_->pix_fmt;
//         return false;
//       }
//     }

//     cn_frame.device_id = device_id_;
//     for (unsigned i = 0; i < cn_frame.n_planes; i++) {
//       cn_frame.ptrs[i] = mem_op.AllocMlu(plane_size[i]);
//       mem_op.MemcpyH2D(cn_frame.ptrs[i], cpu_plane[i], plane_size[i]);
//     }

//     // Send cn_frame to handle
//     if (handle_) {
//       handle_->OnDecodeFrame(cn_frame);
//     }
//     return true;
//   }

//   edk::MluMemoryOp mem_op;
//   AVCodecContext* decode_{nullptr};
//   AVFrame *av_frame_ = nullptr;
//   std::atomic<int> eos_got_{0};
//   std::atomic<int> eos_sent_{0};
// };
// _Pragma("GCC diagnostic pop")

// ------------------------------- FFmpegMluDecodeImpl --------------------------------------
// #if LIBAVFORMAT_VERSION_INT == FFMPEG_VERSION_4_2_2
// #define FFMPEG_MLU_OUTPUT_ON_MLU
// // comment the following line to output decoding results on mlu
// #undef FFMPEG_MLU_OUTPUT_ON_MLU

// #ifdef FFMPEG_MLU_OUTPUT_ON_MLU
// static enum AVPixelFormat hw_pix_fmt;
// static enum AVPixelFormat get_hw_format(AVCodecContext *ctx, const enum AVPixelFormat *pix_fmts) {
//   const enum AVPixelFormat *format;
//   for (format = pix_fmts; *format != -1; format++) {
//     if (*format == hw_pix_fmt) return *format;
//   }
//   LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] Failed to get HW surface format. ";
//   return AV_PIX_FMT_NONE;
// }
// #endif
// class FFmpegMluDecodeImpl : public VideoDecoderImpl {
//  public:
//   FFmpegMluDecodeImpl(VideoDecoder* interface, IDecodeEventHandle* handle, int device_id)
//       : VideoDecoderImpl(interface, handle, device_id) {}
//   bool Init() override {
//     VideoInfo& info = interface_->GetVideoInfo();
//     AVCodec *dec;
//     switch (info.codec_id) {
//       case AV_CODEC_ID_H264:
//         dec = avcodec_find_decoder_by_name("h264_mludec");
//         break;
//       case AV_CODEC_ID_HEVC:
//         dec = avcodec_find_decoder_by_name("hevc_mludec");
//         break;
//       case AV_CODEC_ID_VP8:
//         dec = avcodec_find_decoder_by_name("vp8_mludec");
//         break;
//       case AV_CODEC_ID_VP9:
//         dec = avcodec_find_decoder_by_name("vp9_mludec");
//         break;
//       case AV_CODEC_ID_MJPEG:
//         dec = avcodec_find_decoder_by_name("mjpeg_mludec");
//         break;
//       default:
//         dec = avcodec_find_decoder(info.codec_id);
//         break;
//     }

//     if (!dec) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] avcodec_find_decoder failed";
//       return false;
//     }
// #ifdef FFMPEG_MLU_OUTPUT_ON_MLU
//     AVHWDeviceType dev_type = av_hwdevice_find_type_by_name("mlu");
//     for (int i = 0;; i++) {
//       const AVCodecHWConfig *config = avcodec_get_hw_config(dec, i);
//       if (!config) {
//         LOG(ERROR)<< "[EasyDK Samples] [FFmpegMluDecodeImpl] Decoder " << dec->name
//                   << " doesn't support device type " << av_hwdevice_get_type_name(dev_type);
//         return -1;
//       }
//       if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX && config->device_type == dev_type) {
//         hw_pix_fmt = config->pix_fmt;
//         break;
//       }
//     }
// #endif
//     decode_ = avcodec_alloc_context3(dec);
//     if (!decode_) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] Failed to do avcodec_alloc_context3";
//       return false;
//     }
//     // av_codec_set_pkt_timebase(instance_, st->time_base);

//     if (avcodec_parameters_to_context(decode_, info.codecpar) != 0) {
//         LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] Copy codec context failed";
//         return false;
//     }

//     AVDictionary *decoder_opts = nullptr;
//     av_dict_set_int(&decoder_opts, "device_id", device_id_, 0);

// #ifdef FFMPEG_MLU_OUTPUT_ON_MLU
//     decode_->get_format = get_hw_format;

//     char dev_idx_des[4] = {'\0'};
//     AVBufferRef *hw_device_ctx = nullptr;
//     snprintf(dev_idx_des, sizeof(device_id_), "%d", device_id_);
//     if (av_hwdevice_ctx_create(&hw_device_ctx, dev_type, dev_idx_des, NULL, 0) < 0) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] Failed to create specified HW device.";
//       return false;
//     }
//     decode_->hw_device_ctx = av_buffer_ref(hw_device_ctx);
// #endif

//     if (avcodec_open2(decode_, dec,  &decoder_opts) < 0) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] Failed to open codec";
//       return false;
//     }
//     av_frame_ = av_frame_alloc();
//     if (!av_frame_) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] Could not alloc frame";
//       return false;
//     }
//     eos_got_.store(0);
//     eos_sent_.store(0);
//     return true;
//   }
//   bool FeedPacket(const AVPacket* pkt) override {
//     int ret = avcodec_send_packet(decode_, pkt);
//     if (ret < 0) {
//       LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] avcodec_send_packet failed, data ptr, size:"
//                  << pkt->data << ", " << pkt->size;
//       return false;
//     }
//     ret = avcodec_receive_frame(decode_, av_frame_);
//     if (ret >= 0) {
//       ProcessFrame(av_frame_);
//     }
//     return true;
//   }
//   void FeedEos() override {
//     AVPacket packet;
//     av_init_packet(&packet);
//     packet.size = 0;
//     packet.data = NULL;

//     LOG(INFO) << "[EasyDK Samples] [FFmpegMluDecodeImpl] Sent EOS packet to decoder";
//     eos_sent_.store(1);
//     avcodec_send_packet(decode_, &packet);
//     // flush all frames ...
//     int ret = 0;
//     do {
//       ret = avcodec_receive_frame(decode_, av_frame_);
//       if (ret >= 0) ProcessFrame(av_frame_);
//     } while (ret >= 0);

//     if (handle_) {
//       handle_->OnEos();
//     }
//     eos_got_.store(1);
//   }
//   void ReleaseFrame(edk::CnFrame&& frame) override {
//     for (uint32_t i = 0; i < frame.n_planes; i++) {
//       mem_op.FreeMlu(frame.ptrs[i]);
//     }
//   }
//   bool CopyFrameD2H(void *dst, const edk::CnFrame &frame) override {
//     return edk::EasyDecode::CopyFrameD2H(dst, frame);
//   }
//   void Destroy() {
//     if (av_frame_) {
//       av_frame_free(&av_frame_);
//       av_frame_ = nullptr;
//     }
//     if (decode_) {
//       avcodec_close(decode_);
//       avcodec_free_context(&decode_);
//       decode_ = nullptr;
//     }
//   }
//   ~FFmpegMluDecodeImpl() {
//     FFmpegMluDecodeImpl::Destroy();
//   }

//  private:
//   bool ProcessFrame(AVFrame* frame) {
//     edk::CnFrame cn_frame;
//     cn_frame.pts = frame->pts;
//     cn_frame.width = frame->width;
//     cn_frame.height = frame->height;
//     switch (decode_->sw_pix_fmt) {
//       case AV_PIX_FMT_NV12:
//         cn_frame.pformat = edk::PixelFmt::NV12;
//         break;
//       case AV_PIX_FMT_NV21:
//         cn_frame.pformat = edk::PixelFmt::NV21;
//         break;
//       default:
//         LOG(ERROR) << "[EasyDK Samples] [FFmpegMluDecodeImpl] ProcessFrame() Unsupported pixel format: "
//                      << decode_->sw_pix_fmt;
//         return false;
//     }
//     cn_frame.n_planes = 2;
//     cn_frame.strides[0] = frame->linesize[0];
//     cn_frame.strides[1] = frame->linesize[1];
//     uint32_t plane_size[2];
//     plane_size[0] = cn_frame.height * cn_frame.strides[0];
//     plane_size[1] = cn_frame.height * cn_frame.strides[1] / 2;
//     cn_frame.frame_size = plane_size[0] + plane_size[1];
//     cn_frame.device_id = device_id_;
//     for (unsigned i = 0; i < cn_frame.n_planes; i++) {
//       cn_frame.ptrs[i] = mem_op.AllocMlu(plane_size[i]);
// #ifdef FFMPEG_MLU_OUTPUT_ON_MLU
//       // cn_frame.ptrs[i] = frame->data[i];  // For now, it is not possible to use the mlu data without d2d copy
//       mem_op.MemcpyD2D(cn_frame.ptrs[i], frame->data[i], plane_size[i]);
// #else
//       mem_op.MemcpyH2D(cn_frame.ptrs[i], frame->data[i], plane_size[i]);
// #endif
//     }

//     // Send cn_frame to handle
//     if (handle_) {
//       handle_->OnDecodeFrame(cn_frame);
//     }
//     return true;
//   }

//   edk::MluMemoryOp mem_op;
//   AVCodecContext* decode_{nullptr};
//   AVFrame *av_frame_ = nullptr;
// #ifdef FFMPEG_MLU_OUTPUT_ON_MLU
//   AVPixelFormat hw_pix_fmt_;
// #endif
//   std::atomic<int> eos_got_{0};
//   std::atomic<int> eos_sent_{0};
// };
// #endif

VideoDecoder::VideoDecoder(StreamRunner* runner, DecoderType type, int device_id) : runner_(runner) {
  switch (type) {
    case EASY_DECODE:
      impl_ = new EasyDecodeImpl(this, runner, device_id);
      break;
//     case FFMPEG:
//       impl_ = new FFmpegDecodeImpl(this, runner, device_id);
//       break;
// #if LIBAVFORMAT_VERSION_INT == FFMPEG_VERSION_4_2_2
//     case FFMPEG_MLU:
//       impl_ = new FFmpegMluDecodeImpl(this, runner, device_id);
//       break;
// #endif
    default:
      LOG(FATAL) << "[EasyDK Samples] [VideoDecoder] Unsupported decoder type";
  }
}

VideoDecoder::~VideoDecoder() {
  if (impl_) delete impl_;
}

bool VideoDecoder::OnParseInfo(const VideoInfo& info) {
  info_ = info;
  return impl_->Init();
}

bool VideoDecoder::OnPacket(const AVPacket* packet) {
  return impl_->FeedPacket(packet);
}

void VideoDecoder::OnEos() {
  if (send_eos_ == false) {
    LOG(INFO) << "[EasyDK Samples] [VideoDecoder] OnEos(): Feed EOS";
    impl_->FeedEos();
    send_eos_ = true;
  }
}

bool VideoDecoder::Running() {
  return runner_->Running();
}

void VideoDecoder::Destroy() {
  impl_->Destroy();
}
