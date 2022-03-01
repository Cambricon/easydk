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

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

#include "cxxutil/log.h"
#include "easyinfer/mlu_memory_op.h"
#include "encoder.h"

#ifdef ENABLE_MLU300_CODEC

using std::to_string;
#define ALIGN(size, alignment) (((uint32_t)(size) + (alignment)-1) & ~((alignment)-1))

#include <cncodec_v3_common.h>
#include <cncodec_v3_enc.h>
#include <cnrt.h>

namespace edk {

// 2 MiB
static constexpr uint32_t g_buffer_size = 0x200000;

static cncodecEncProfile_t ProfileCast(VideoProfile prof) {
  switch (prof) {
    case VideoProfile::H264_BASELINE:
      return CNCODEC_ENC_PROFILE_H264_BASELINE;
    case VideoProfile::H264_MAIN:
      return CNCODEC_ENC_PROFILE_H264_MAIN;
    case VideoProfile::H264_HIGH:
      return CNCODEC_ENC_PROFILE_H264_HIGH;
    case VideoProfile::H264_HIGH_10:
      return CNCODEC_ENC_PROFILE_H264_HIGH_10;
    case VideoProfile::H265_MAIN:
      return CNCODEC_ENC_PROFILE_HEVC_MAIN;
    case VideoProfile::H265_MAIN_STILL:
      return CNCODEC_ENC_PROFILE_HEVC_MAIN_STILL;
    case VideoProfile::H265_MAIN_10:
      return CNCODEC_ENC_PROFILE_HEVC_MAIN_10;
    default:
      return CNCODEC_ENC_PROFILE_MAX;
  }
}

static cncodecEncLevel_t LevelCast(VideoLevel level) {
  switch (level) {
    case VideoLevel::AUTO_SELECT:
      return CNCODEC_ENC_LEVEL_AUTO_SELECT;
    case VideoLevel::H264_1:
      return CNCODEC_ENC_LEVEL_H264_1;
    case VideoLevel::H264_1B:
      return CNCODEC_ENC_LEVEL_H264_1B;
    case VideoLevel::H264_11:
      return CNCODEC_ENC_LEVEL_H264_11;
    case VideoLevel::H264_12:
      return CNCODEC_ENC_LEVEL_H264_12;
    case VideoLevel::H264_13:
      return CNCODEC_ENC_LEVEL_H264_13;
    case VideoLevel::H264_2:
      return CNCODEC_ENC_LEVEL_H264_2;
    case VideoLevel::H264_21:
      return CNCODEC_ENC_LEVEL_H264_21;
    case VideoLevel::H264_22:
      return CNCODEC_ENC_LEVEL_H264_22;
    case VideoLevel::H264_3:
      return CNCODEC_ENC_LEVEL_H264_3;
    case VideoLevel::H264_31:
      return CNCODEC_ENC_LEVEL_H264_31;
    case VideoLevel::H264_32:
      return CNCODEC_ENC_LEVEL_H264_32;
    case VideoLevel::H264_4:
      return CNCODEC_ENC_LEVEL_H264_4;
    case VideoLevel::H264_41:
      return CNCODEC_ENC_LEVEL_H264_41;
    case VideoLevel::H264_42:
      return CNCODEC_ENC_LEVEL_H264_42;
    case VideoLevel::H264_5:
      return CNCODEC_ENC_LEVEL_H264_5;
    case VideoLevel::H264_51:
      return CNCODEC_ENC_LEVEL_H264_51;
    case VideoLevel::H264_52:
      return CNCODEC_ENC_LEVEL_H264_52;
    case VideoLevel::H264_6:
      return CNCODEC_ENC_LEVEL_H264_6;
    case VideoLevel::H264_61:
      return CNCODEC_ENC_LEVEL_H264_61;
    case VideoLevel::H264_62:
      return CNCODEC_ENC_LEVEL_H264_62;
    case VideoLevel::H265_MAIN_1:
    case VideoLevel::H265_HIGH_1:
      return CNCODEC_ENC_LEVEL_HEVC_1;
    case VideoLevel::H265_MAIN_2:
    case VideoLevel::H265_HIGH_2:
      return CNCODEC_ENC_LEVEL_HEVC_2;
    case VideoLevel::H265_MAIN_21:
    case VideoLevel::H265_HIGH_21:
      return CNCODEC_ENC_LEVEL_HEVC_21;
    case VideoLevel::H265_MAIN_3:
    case VideoLevel::H265_HIGH_3:
      return CNCODEC_ENC_LEVEL_HEVC_3;
    case VideoLevel::H265_MAIN_31:
    case VideoLevel::H265_HIGH_31:
      return CNCODEC_ENC_LEVEL_HEVC_31;
    case VideoLevel::H265_MAIN_4:
    case VideoLevel::H265_HIGH_4:
      return CNCODEC_ENC_LEVEL_HEVC_4;
    case VideoLevel::H265_MAIN_41:
    case VideoLevel::H265_HIGH_41:
      return CNCODEC_ENC_LEVEL_HEVC_41;
    case VideoLevel::H265_MAIN_5:
    case VideoLevel::H265_HIGH_5:
      return CNCODEC_ENC_LEVEL_HEVC_5;
    case VideoLevel::H265_MAIN_51:
    case VideoLevel::H265_HIGH_51:
      return CNCODEC_ENC_LEVEL_HEVC_51;
    case VideoLevel::H265_MAIN_52:
    case VideoLevel::H265_HIGH_52:
      return CNCODEC_ENC_LEVEL_HEVC_52;
    case VideoLevel::H265_MAIN_6:
    case VideoLevel::H265_HIGH_6:
      return CNCODEC_ENC_LEVEL_HEVC_6;
    case VideoLevel::H265_MAIN_61:
    case VideoLevel::H265_HIGH_61:
      return CNCODEC_ENC_LEVEL_HEVC_61;
    case VideoLevel::H265_MAIN_62:
    case VideoLevel::H265_HIGH_62:
      return CNCODEC_ENC_LEVEL_HEVC_62;
    default:
      return CNCODEC_ENC_LEVEL_MAX;
  }
}

static cncodecEncBframeRefMode_t GopTypeCast(GopTypeMlu300 type) {
  switch (type) {
    case GopTypeMlu300::BFRAME_REF_DISABLED:
      return CNCODEC_ENC_BFRAME_REF_MODE_DISABLED;
    case GopTypeMlu300::BFRAME_REF_EACH:
      return CNCODEC_ENC_BFRAME_REF_MODE_EACH;
    case GopTypeMlu300::BFRAME_REF_MIDDLE:
      return CNCODEC_ENC_BFRAME_REF_MODE_MIDDLE;
    case GopTypeMlu300::BFRAME_REF_HIERARCHICAL:
      return CNCODEC_ENC_BFRAME_REF_MODE_HIERARCHICAL;
    default:
      return CNCODEC_ENC_BFRAME_REF_MODE_MAX;
  }
}

static void PrintCreateAttr(cncodecEncParam_t *p_attr, cncodecEncPreset_t preset) {
  printf("%-32s%s\n", "param", "value");
  printf("-------------------------------------\n");
  switch (preset) {
    case CNCODEC_ENC_PRESET_VERY_FAST:
      printf("%-32s%s\n", "preset", "VERY FAST");
      break;
    case CNCODEC_ENC_PRESET_FAST:
      printf("%-32s%s\n", "preset", "FAST");
      break;
    case CNCODEC_ENC_PRESET_MEDIUM:
      printf("%-32s%s\n", "preset", "MEDIUM");
      break;
    case CNCODEC_ENC_PRESET_SLOW:
      printf("%-32s%s\n", "preset", "SLOW");
      break;
    case CNCODEC_ENC_PRESET_VERY_SLOW:
      printf("%-32s%s\n", "preset", "VERY SLOW");
      break;
    default:
      break;
  }
  printf("-------------------------------------\n");
  printf("%-32s%u\n", "Codectype", p_attr->coding_attr.codec_attr.codec);
  printf("%-32s%u\n", "PixelFmt", p_attr->pixel_format);
  printf("%-32s%u\n", "DeviceID", p_attr->device_id);
  printf("%-32s%u\n", "MemoryAllocType", p_attr->input_buf_source);
  printf("%-32s%u\n", "Width", p_attr->pic_width);
  printf("%-32s%u\n", "Height", p_attr->pic_height);
  printf("%-32s%u\n", "MaxWidth", p_attr->max_width);
  printf("%-32s%u\n", "MaxHeight", p_attr->max_height);
  printf("%-32s%u\n", "FrameRateNum", p_attr->frame_rate_num);
  printf("%-32s%u\n", "FrameRateDen", p_attr->frame_rate_den);
  printf("%-32s%u\n", "RateCtrlMode", p_attr->coding_attr.rc_attr.rc_mode);
  printf("%-32s%u\n", "BitRate", p_attr->coding_attr.rc_attr.target_bitrate);
  if (p_attr->coding_attr.codec_attr.codec == CNCODEC_H264) {
    printf("%-32s%u\n", "GopType", p_attr->coding_attr.codec_attr.h264_attr.b_frame_ref_mode);
    printf("%-32s%u\n", "GopSize", p_attr->coding_attr.codec_attr.h264_attr.idr_period);
  } else if (p_attr->coding_attr.codec_attr.codec == CNCODEC_HEVC) {
    printf("%-32s%u\n", "GopType", p_attr->coding_attr.codec_attr.hevc_attr.b_frame_ref_mode);
    printf("%-32s%u\n", "GopSize", p_attr->coding_attr.codec_attr.hevc_attr.idr_period);
  }
  printf("%-32s%u\n", "InputBufferNumber", p_attr->input_buf_num);
  printf("%-32s%u\n", "OutputStreamBufSize", p_attr->stream_buf_size);
}

class Mlu300Encoder : public Encoder{
 public:
  explicit Mlu300Encoder(const EasyEncode::Attr& attr);
  ~Mlu300Encoder();
  void AbortEncoder() override;
  bool FeedData(const CnFrame& frame) override;
  bool FeedEos() override;
  bool RequestFrame(CnFrame* frame) override;
  bool ReleaseBuffer(uint64_t buf_id) override;

  void ReceivePacket(void *packet);
  void ReceiveEvent(cncodecEventType_t type);
  void ReceiveEOS();

 private:
  void InitEncode(const EasyEncode::Attr &attr);
  void CopyFrame(cncodecFrame_t *dst, const CnFrame &input);

  void EventTaskRunner();
  bool send_eos_ = false;

  std::queue<cncodecEventType_t> event_queue_;
  std::mutex event_mtx_;
  std::condition_variable event_cond_;
  std::thread event_loop_;

  cncodecEncParam_t codec_params_;

  cncodecHandle_t handle_ = 0;
  uint64_t packet_cnt_ = 0;
  bool got_eos_ = false;
  std::mutex eos_mutex_;
  std::condition_variable eos_cond_;

  std::mutex list_mtx_;
  std::list<cncodecFrame_t> input_list_;

  std::atomic<bool> first_frame_{true};
  CnPacket head_pkg_;
};

static int32_t EventHandler(cncodecEventType_t type, void *user_data, void *package);

Mlu300Encoder::Mlu300Encoder(const EasyEncode::Attr &attr) : Encoder(attr) {
  struct __ShowCodecVersion {
    __ShowCodecVersion() {
      uint32_t major, minor, patch;
      int ret = cncodecGetLibVersion(&major, &minor, &patch);
      if (CNCODEC_SUCCESS == ret) {
        LOGI(DECODE) << "CNCodec Version: " << major << "." << minor << "." << patch;
      } else {
        LOGW(DECODE) << "Get CNCodec version failed.";
      }
    }
  };
  static __ShowCodecVersion show_version;

  try {
    InitEncode(attr);
  }catch (...) {
    throw;
  }
  event_loop_ = std::thread(&Mlu300Encoder::EventTaskRunner, this);
}

void Mlu300Encoder::InitEncode(const EasyEncode::Attr &attr) {
  // create params
  switch (attr.codec_type) {
    case CodecType::H264:
      codec_params_.coding_attr.codec_attr.codec = CNCODEC_H264;
      codec_params_.input_buf_num = 0;
      break;
    case CodecType::H265:
      codec_params_.coding_attr.codec_attr.codec = CNCODEC_HEVC;
      codec_params_.input_buf_num = 0;
      break;
    case CodecType::JPEG:
    case CodecType::MJPEG:
      codec_params_.coding_attr.codec_attr.codec = CNCODEC_JPEG;
      codec_params_.input_buf_num = 6;
      break;
    default: {
      THROW_EXCEPTION(Exception::INIT_FAILED, "codec type not supported yet, codec_type:"
          + to_string(static_cast<int>(attr.codec_type)));
    }
  }

  codec_params_.device_id = attr.dev_id;
  codec_params_.run_mode = CNCODEC_RUN_MODE_ASYNC;
  codec_params_.pic_width = attr.frame_geometry.w;
  codec_params_.pic_height = attr.frame_geometry.h;
  codec_params_.max_width = attr.attr_mlu300.max_geometry.w > attr.frame_geometry.w ?
      attr.attr_mlu300.max_geometry.w : attr.frame_geometry.w;
  codec_params_.max_height = attr.attr_mlu300.max_geometry.h > attr.frame_geometry.h ?
      attr.attr_mlu300.max_geometry.h : attr.frame_geometry.h;
  codec_params_.color_space = CNCODEC_COLOR_SPACE_BT_709;
  codec_params_.input_stride_align = 64;
  codec_params_.input_buf_source = CNCODEC_BUF_SOURCE_LIB;
  codec_params_.stream_buf_size = 0;
  codec_params_.user_context = reinterpret_cast<void *>(this);

  switch (attr.pixel_format) {
    case PixelFmt::NV12:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_NV12;
      break;
    case PixelFmt::NV21:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_NV21;
      break;
    case PixelFmt::I420:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_I420;
      break;
    case PixelFmt::YUYV:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_YUYV;
      break;
    case PixelFmt::UYVY:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_UYVY;
      break;
    case PixelFmt::RGBA:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_RGBA;
      break;
    case PixelFmt::BGRA:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_BGRA;
      break;
    case PixelFmt::ARGB:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_ARGB;
      break;
    case PixelFmt::P010:
      codec_params_.pixel_format = CNCODEC_PIX_FMT_P010;
      break;
    default: {
      THROW_EXCEPTION(Exception::INIT_FAILED, "codec pixel format not supported yet, pixel format:"
          + to_string(static_cast<int>(attr.pixel_format)));
    }
  }

  cncodecEncPreset_t preset = CNCODEC_ENC_PRESET_VERY_FAST;
  cncodecEncTuningInfo_t tune = CNCODEC_ENC_TUNING_INFO_DEFAULT;
  if (codec_params_.coding_attr.codec_attr.codec == CNCODEC_H264 ||
      codec_params_.coding_attr.codec_attr.codec == CNCODEC_HEVC) {
    switch (attr.attr_mlu300.preset) {
      case EncodePreset::VERY_FAST:
        preset = CNCODEC_ENC_PRESET_VERY_FAST;
        break;
      case EncodePreset::FAST:
        preset = CNCODEC_ENC_PRESET_FAST;
        break;
      case EncodePreset::MEDIUM:
        preset = CNCODEC_ENC_PRESET_MEDIUM;
        break;
      case EncodePreset::SLOW:
        preset = CNCODEC_ENC_PRESET_SLOW;
        break;
      case EncodePreset::VERY_SLOW:
        preset = CNCODEC_ENC_PRESET_VERY_SLOW;
        break;
      default:
        THROW_EXCEPTION(Exception::INIT_FAILED, "preset mode not supported yet, preset:"
            + to_string(static_cast<int>(attr.attr_mlu300.preset)));
    }
    switch (attr.attr_mlu300.tune) {
      case EncodeTune::DEFAULT:
        tune = CNCODEC_ENC_TUNING_INFO_DEFAULT;
        break;
      case EncodeTune::HIGH_QUALITY:
        tune = CNCODEC_ENC_TUNING_INFO_HIGH_QUALITY;
        break;
      case EncodeTune::LOW_LATENCY:
        tune = CNCODEC_ENC_TUNING_INFO_LOW_LATENCY;
        break;
      case EncodeTune::LOW_LATENCY_HIGH_QUALITY:
        tune = CNCODEC_ENC_TUNING_INFO_LOW_LATENCY_HIGH_QUALITY;
        break;
      case EncodeTune::FAST_DECODE:
        tune = CNCODEC_ENC_TUNING_INFO_FAST_DECODE;
        break;
      default:
        THROW_EXCEPTION(Exception::INIT_FAILED, "tune mode not supported yet, tune:"
            + to_string(static_cast<int>(attr.attr_mlu300.tune)));
    }

    cncodecEncPresetAndTuneConfig_t preset_config;
    // get preset config
    auto ret = cncodecEncGetPresetConfig(codec_params_.coding_attr.codec_attr.codec, preset, tune, &preset_config);
    if (ret != CNCODEC_SUCCESS) {
      THROW_EXCEPTION(Exception::INIT_FAILED, "Get preset config failed");
    }

    codec_params_.frame_rate_num = attr.attr_mlu300.frame_rate_num > 0 ? attr.attr_mlu300.frame_rate_num : 30;
    codec_params_.frame_rate_den = attr.attr_mlu300.frame_rate_den > 0 ? attr.attr_mlu300.frame_rate_den: 1;

    codec_params_.coding_attr = preset_config.coding_attr;

    if (attr.attr_mlu300.profile < VideoProfile::PROFILE_MAX) {
      if (codec_params_.coding_attr.codec_attr.codec == CNCODEC_H264 &&
          attr.attr_mlu300.profile <= VideoProfile::H264_HIGH_10) {
        codec_params_.coding_attr.profile = ProfileCast(attr.attr_mlu300.profile);
      } else if (codec_params_.coding_attr.codec_attr.codec == CNCODEC_HEVC &&
          attr.attr_mlu300.profile >= VideoProfile::H265_MAIN) {
        codec_params_.coding_attr.profile = ProfileCast(attr.attr_mlu300.profile);
      } else {
        LOGW(ENCODE) << "Invalid profile, using profile: "
                    << to_string(static_cast<int>(codec_params_.coding_attr.profile));
      }
    }
    if (attr.attr_mlu300.level < VideoLevel::LEVEL_MAX) {
      if (codec_params_.coding_attr.codec_attr.codec == CNCODEC_H264 &&
          (attr.attr_mlu300.level <= VideoLevel::H264_62 ||
          attr.attr_mlu300.level == VideoLevel::AUTO_SELECT)) {
        codec_params_.coding_attr.level = LevelCast(attr.attr_mlu300.level);
      } else if (codec_params_.coding_attr.codec_attr.codec == CNCODEC_HEVC &&
          (attr.attr_mlu300.level >= VideoLevel::H265_MAIN_1 ||
          attr.attr_mlu300.level == VideoLevel::AUTO_SELECT)) {
        codec_params_.coding_attr.level = LevelCast(attr.attr_mlu300.level);
      } else {
        LOGW(ENCODE) << "Invalid level, using level: "
                    << to_string(static_cast<int>(codec_params_.coding_attr.level));
      }
    }
    if (attr.attr_mlu300.gop_size > 0) {
      codec_params_.coding_attr.gop_size = attr.attr_mlu300.gop_size;
    }
    if (attr.attr_mlu300.frame_interval_p > -1) {
      codec_params_.coding_attr.frame_interval_p = attr.attr_mlu300.frame_interval_p;
    }

    // rc
    switch (attr.attr_mlu300.rate_control.rc_mode) {
      case RateControlModeMlu300::VBR:
        codec_params_.coding_attr.rc_attr.rc_mode = CNCODEC_ENC_RATE_CTRL_VBR;
        break;
      case RateControlModeMlu300::CBR:
        codec_params_.coding_attr.rc_attr.rc_mode = CNCODEC_ENC_RATE_CTRL_CBR;
        break;
      case RateControlModeMlu300::CVBR:
        codec_params_.coding_attr.rc_attr.rc_mode = CNCODEC_ENC_RATE_CTRL_CVBR;
        break;
      case RateControlModeMlu300::CRF:
        codec_params_.coding_attr.rc_attr.rc_mode = CNCODEC_ENC_RATE_CTRL_CRF;
        break;
      case RateControlModeMlu300::FIXEDQP:
        codec_params_.coding_attr.rc_attr.rc_mode = CNCODEC_ENC_RATE_CTRL_FIXEDQP;
        break;
      default:
        break;
    }
    if (attr.attr_mlu300.rate_control.bit_rate > 0) {
      codec_params_.coding_attr.rc_attr.target_bitrate = attr.attr_mlu300.rate_control.bit_rate;
    }
    if (attr.attr_mlu300.rate_control.rc_windows > -1) {
      codec_params_.coding_attr.rc_attr.rc_windows = attr.attr_mlu300.rate_control.rc_windows;
    }
    if (attr.attr_mlu300.rate_control.lookahead_depth > -1) {
      if (codec_params_.coding_attr.rc_attr.rc_mode == CNCODEC_ENC_RATE_CTRL_FIXEDQP) {
        codec_params_.coding_attr.rc_attr.lookahead_depth = 0;
      }
      codec_params_.coding_attr.rc_attr.lookahead_depth = attr.attr_mlu300.rate_control.lookahead_depth;
    }
    if (attr.attr_mlu300.rate_control.initial_qp > -2) {
      codec_params_.coding_attr.rc_attr.initial_qp = attr.attr_mlu300.rate_control.initial_qp;
    }
    switch (codec_params_.coding_attr.rc_attr.rc_mode) {
      case CNCODEC_ENC_RATE_CTRL_CVBR:
        if (attr.attr_mlu300.rate_control.max_qp > -1) {
          codec_params_.coding_attr.rc_attr.max_qp = attr.attr_mlu300.rate_control.max_qp;
        }
        if (attr.attr_mlu300.rate_control.min_qp > -1) {
          codec_params_.coding_attr.rc_attr.min_qp = attr.attr_mlu300.rate_control.min_qp;
        }
        break;
      case CNCODEC_ENC_RATE_CTRL_CRF:
        if (attr.attr_mlu300.rate_control.target_quality > -1) {
          codec_params_.coding_attr.rc_attr.target_quality = attr.attr_mlu300.rate_control.target_quality;
        }
        if (codec_params_.coding_attr.rc_attr.lookahead_depth < 4) {
          codec_params_.coding_attr.rc_attr.lookahead_depth = 4;
        }
        break;
      case CNCODEC_ENC_RATE_CTRL_FIXEDQP:
        if (attr.attr_mlu300.rate_control.const_qp_i > -1) {
          codec_params_.coding_attr.rc_attr.const_qp_i = attr.attr_mlu300.rate_control.const_qp_i;
        }
        if (attr.attr_mlu300.rate_control.const_qp_p > -1) {
          codec_params_.coding_attr.rc_attr.const_qp_p = attr.attr_mlu300.rate_control.const_qp_p;
        }
        if (attr.attr_mlu300.rate_control.const_qp_b > -1) {
          codec_params_.coding_attr.rc_attr.const_qp_b = attr.attr_mlu300.rate_control.const_qp_b;
        }
      default:
        break;
    }
    if (attr.attr_mlu300.frame_interval_p > -1) {
      codec_params_.coding_attr.frame_interval_p = attr.attr_mlu300.frame_interval_p;
    }
    if (codec_params_.coding_attr.codec_attr.codec == CNCODEC_H264) {
      if (attr.attr_mlu300.gop_size > 0)
        codec_params_.coding_attr.codec_attr.h264_attr.idr_period = attr.attr_mlu300.gop_size;
      if (attr.attr_mlu300.insert_spspps_when_idr > -1)
        codec_params_.coding_attr.codec_attr.h264_attr.enable_repeat_sps_pps = attr.attr_mlu300.insert_spspps_when_idr;
      if (attr.attr_mlu300.gop_type < GopTypeMlu300::TYPE_MAX)
        codec_params_.coding_attr.codec_attr.h264_attr.b_frame_ref_mode = GopTypeCast(attr.attr_mlu300.gop_type);
    } else if (codec_params_.coding_attr.codec_attr.codec == CNCODEC_HEVC) {
      if (attr.attr_mlu300.gop_size > 0)
        codec_params_.coding_attr.codec_attr.hevc_attr.idr_period = attr.attr_mlu300.gop_size;
      if (attr.attr_mlu300.insert_spspps_when_idr > -1)
        codec_params_.coding_attr.codec_attr.hevc_attr.enable_repeat_sps_pps = attr.attr_mlu300.insert_spspps_when_idr;
      if (attr.attr_mlu300.gop_type < GopTypeMlu300::TYPE_MAX)
        codec_params_.coding_attr.codec_attr.hevc_attr.b_frame_ref_mode = GopTypeCast(attr.attr_mlu300.gop_type);
    }
  }
  memset(&codec_params_.pp_attr, 0, sizeof(cncodecPpAttr_t));
  memset(&codec_params_.pp_attr.scale, 0, sizeof(cncodecScaleAttr_t));
  memset(&codec_params_.pp_attr.crop, 0, sizeof(cncodecCropAttr_t));

  if (!attr.silent) {
    PrintCreateAttr(&codec_params_, preset);
  }

  int ecode = cncodecEncCreate(&handle_, EventHandler, &codec_params_);
  if (CNCODEC_SUCCESS != ecode) {
    handle_ = 0;
    THROW_EXCEPTION(Exception::INIT_FAILED, "Initialize video encoder failed. cncodec error code: " + to_string(ecode));
  }
  LOGI(ENCODE) << "Init video encoder succeeded";
}

Mlu300Encoder::~Mlu300Encoder() {
  try {
    std::unique_lock<std::mutex> eos_lk(eos_mutex_);
    if (!got_eos_) {
      if (!send_eos_ && handle_) {
        eos_lk.unlock();
        LOGI(ENCODE) << "Send EOS in destruct";
        Mlu300Encoder::FeedEos();
      } else {
        if (!handle_) got_eos_ = true;
      }
    }

    if (!eos_lk.owns_lock()) {
      eos_lk.lock();
    }

    if (!got_eos_) {
      LOGI(ENCODE) << "Wait EOS in destruct";
      eos_cond_.wait(eos_lk, [this]() -> bool { return got_eos_; });
    }

    event_cond_.notify_all();
    if (event_loop_.joinable()) {
      event_loop_.join();
    }
    {
      std::lock_guard<std::mutex> lk(list_mtx_);
      input_list_.clear();
    }
    // destroy encoder
    if (handle_) {
      int ecode = cncodecEncDestroy(handle_);
      if (CNCODEC_SUCCESS != ecode) {
        LOGE(ENCODE) << "Destroy encoder failed. Error code: " << ecode;
      }
    }
  } catch (std::system_error &e) {
    LOGE(ENCODE) << e.what();
  } catch (Exception &e) {
    LOGE(ENCODE) << e.what();
  }
}

bool Mlu300Encoder::ReleaseBuffer(uint64_t buf_id) {
  LOGD(ENCODE) << "Release buffer, " << reinterpret_cast<void *>(buf_id);
  if (buf_id) delete[] reinterpret_cast<uint8_t *>(buf_id);
  return true;
}

void Mlu300Encoder::ReceivePacket(void *_packet) {
  LOGT(ENCODE) << "Encode receive packet " << _packet;
  // packet callback
  if (attr_.packet_callback) {
    CnPacket cn_packet;
    cncodecStream_t *stream = reinterpret_cast<cncodecStream_t *>(_packet);
    LOGT(ENCODE) << "ReceivePacket size=" << stream->data_len << ", pts=" << stream->pts
                 << ", type=" << stream->stream_type;
    cncodecStreamType_t stream_type = stream->stream_type;
    if (stream_type == CNCODEC_H264_NALU_TYPE_SPS_PPS || stream_type == CNCODEC_HEVC_NALU_TYPE_VPS_SPS_PPS) {
      cn_packet.slice_type = BitStreamSliceType::SPS_PPS;
    } else if (stream_type == CNCODEC_NALU_TYPE_IDR || stream_type == CNCODEC_NALU_TYPE_I) {
      cn_packet.slice_type = BitStreamSliceType::KEY_FRAME;
    } else if (stream_type == CNCODEC_NALU_TYPE_EOS) {
      return;
    } else {
      cn_packet.slice_type = BitStreamSliceType::FRAME;
      ++packet_cnt_;
    }
    cn_packet.data = new uint8_t[stream->data_len];
    auto ret = cnrtMemcpy(cn_packet.data, reinterpret_cast<void *>(stream->mem_addr + stream->data_offset),
                          stream->data_len, CNRT_MEM_TRANS_DIR_DEV2HOST);
    if (ret != CNRT_RET_SUCCESS) {
      LOGE(ENCODE) << "Copy bitstream failed, DEV2HOST";
      AbortEncoder();
      --packet_cnt_;
      return;
    }
    cn_packet.length = stream->data_len;
    cn_packet.pts = stream->pts;
    cn_packet.codec_type = attr_.codec_type;
    cn_packet.buf_id = reinterpret_cast<uint64_t>(cn_packet.data);
    if (first_frame_ && cn_packet.slice_type == BitStreamSliceType::SPS_PPS) {
      head_pkg_ = cn_packet;
    } else if (first_frame_ && (cn_packet.slice_type == BitStreamSliceType::FRAME ||
                                cn_packet.slice_type == BitStreamSliceType::KEY_FRAME)) {
      first_frame_ = false;

      if (head_pkg_.buf_id) { attr_.packet_callback(head_pkg_); }

      attr_.packet_callback(cn_packet);
    } else {
      attr_.packet_callback(cn_packet);
    }
  }
}

void Mlu300Encoder::ReceiveEOS() {
  // eos callback
  LOGI(ENCODE) << "Encode receive EOS";

  if (attr_.eos_callback) {
    attr_.eos_callback();
  }

  std::lock_guard<std::mutex> lk(eos_mutex_);
  got_eos_ = true;
  eos_cond_.notify_one();
}


void Mlu300Encoder::CopyFrame(cncodecFrame_t *dst, const CnFrame &input) {
  uint32_t frame_size = 0, uv_size = 0;
  if (input.strides[0] == 0) {
    frame_size = input.width * input.height;
  } else {
    frame_size = input.strides[0] * input.height;
  }
  if (input.strides[1] == 0) {
    uv_size = input.width * input.height >> 1;
  } else {
    uv_size = input.strides[1] * input.height >> 1;
  }

  if (input.frame_size > 0) {
    MluMemoryOp mem_op;
    // cnrtRet_t cnrt_ecode = CNRT_RET_SUCCESS;
    switch (attr_.pixel_format) {
      case PixelFmt::NV12:
      case PixelFmt::NV21: {
        LOGT(ENCODE) << "Copy frame luminance";
        mem_op.MemcpyH2D(reinterpret_cast<void *>(dst->plane[0].dev_addr), input.ptrs[0], frame_size);
        LOGT(ENCODE) << "Copy frame chroma";
        mem_op.MemcpyH2D(reinterpret_cast<void *>(dst->plane[1].dev_addr), input.ptrs[1], uv_size);
        break;
      }
      case PixelFmt::I420: {
        LOGT(ENCODE) << "Copy frame luminance";
        mem_op.MemcpyH2D(reinterpret_cast<void *>(dst->plane[0].dev_addr), input.ptrs[0], frame_size);
        LOGT(ENCODE) << "Copy frame chroma 0";
        mem_op.MemcpyH2D(reinterpret_cast<void *>(dst->plane[1].dev_addr), input.ptrs[1], uv_size >> 1);
        LOGT(ENCODE) << "Copy frame chroma 1";
        mem_op.MemcpyH2D(reinterpret_cast<void *>(dst->plane[2].dev_addr), input.ptrs[2], uv_size >> 1);
        break;
      }
      case PixelFmt::ARGB:
      case PixelFmt::ABGR:
      case PixelFmt::RGBA:
      case PixelFmt::BGRA:
        LOGT(ENCODE) << "Copy frame RGB family";
        mem_op.MemcpyH2D(reinterpret_cast<void *>(dst->plane[0].dev_addr), input.ptrs[0], frame_size << 2);
        break;
      default:
        THROW_EXCEPTION(Exception::UNSUPPORTED, "Unsupported pixel format");
        break;
    }
  }
}

bool Mlu300Encoder::RequestFrame(CnFrame* frame) {
  cncodecFrame_t input;
  memset(&input, 0, sizeof(cncodecFrame_t));
  input.width = attr_.frame_geometry.w;
  input.height = attr_.frame_geometry.h;
  input.pixel_format = codec_params_.pixel_format;
  int ecode = cncodecEncWaitAvailInputBuf(handle_, &input, 10000);
  if (CNCODEC_ERROR_TIMEOUT == ecode) {
    LOGE(ENCODE) << "cncodecEncWaitAvailInputBuf timeout";
    return false;
  } else if (CNCODEC_SUCCESS != ecode) {
    LOGE(ENCODE) << "cncodecEncWaitAvailInputBuf failed. Error code: " + to_string(ecode);
    return false;
  }

  uint32_t stride = ALIGN(attr_.frame_geometry.w, codec_params_.input_stride_align);
  memset(frame, 0, sizeof(CnFrame));
  frame->width = input.width > 0 ? input.width : attr_.frame_geometry.w;
  frame->height = input.height > 0 ? input.height : attr_.frame_geometry.h;
  if (attr_.pixel_format == PixelFmt::BGRA || attr_.pixel_format == PixelFmt::RGBA ||
      attr_.pixel_format == PixelFmt::ARGB || attr_.pixel_format == PixelFmt::ABGR) {
    frame->ptrs[0] = reinterpret_cast<uint8_t *>(input.plane[0].dev_addr);
    frame->strides[0] = input.plane[0].stride >= attr_.frame_geometry.w * 4 ? input.plane[0].stride : stride * 4;
    frame->n_planes = 1;
  }
  if (attr_.pixel_format == PixelFmt::NV12 || attr_.pixel_format == PixelFmt::NV21) {
    frame->ptrs[0] = reinterpret_cast<uint8_t *>(input.plane[0].dev_addr);
    frame->strides[0] = input.plane[0].stride >= attr_.frame_geometry.w ? input.plane[0].stride : stride;
    frame->ptrs[1] = reinterpret_cast<uint8_t *>(input.plane[1].dev_addr);
    frame->strides[1] = input.plane[1].stride >= attr_.frame_geometry.w ? input.plane[1].stride : stride;
    frame->n_planes = 2;
  }
  if (attr_.pixel_format == PixelFmt::I420) {
    frame->ptrs[0] = reinterpret_cast<uint8_t *>(input.plane[0].dev_addr);
    frame->strides[0] = input.plane[0].stride >= attr_.frame_geometry.w ? input.plane[0].stride : stride;
    frame->ptrs[1] = reinterpret_cast<uint8_t *>(input.plane[1].dev_addr);
    frame->strides[1] = input.plane[1].stride >= attr_.frame_geometry.w / 2 ? input.plane[1].stride : stride / 2;
    frame->ptrs[2] = reinterpret_cast<uint8_t *>(input.plane[2].dev_addr);
    frame->strides[2] = input.plane[2].stride >= attr_.frame_geometry.w / 2 ? input.plane[2].stride : stride / 2;
    frame->n_planes = 3;
  }
  frame->pformat = attr_.pixel_format;
  frame->device_id = attr_.dev_id;

  std::lock_guard<std::mutex> lk(list_mtx_);
  input_list_.push_back(input);
  return true;
}

bool Mlu300Encoder::FeedEos() {
  if (send_eos_) {
    LOGW(ENCODE) << "EOS had been feed, won't feed again";
    return false;
  }

  LOGI(ENCODE) << "Thread id: " << std::this_thread::get_id() << ", Feed EOS data";
  auto ecode = cncodecEncSetEos(handle_);

  if (CNCODEC_ERROR_TIMEOUT == ecode) {
    LOGE(ENCODE) << "Feed EOS timeout";
    return false;
  } else if (CNCODEC_SUCCESS != ecode) {
    THROW_EXCEPTION(Exception::INTERNAL, "Encode feed EOS failed. cncodec error code: " + to_string(ecode));
  }

  send_eos_ = true;
  return true;
}

bool Mlu300Encoder::FeedData(const CnFrame &frame) {
  if (send_eos_) {
    LOGW(ENCODE) << "EOS had been sent, won't feed data or EOS";
    return false;
  }
  cncodecFrame_t input;
  memset(&input, 0, sizeof(cncodecFrame_t));
  int ecode = CNCODEC_SUCCESS;
  if (frame.device_id < 0) {
    input.width = frame.width;
    input.height = frame.height;
    input.pixel_format = codec_params_.pixel_format;
    ecode = cncodecEncWaitAvailInputBuf(handle_, &input, 10000);
    if (CNCODEC_ERROR_TIMEOUT == ecode) {
      LOGE(ENCODE) << "cncodecEncWaitAvailInputBuf timeout";
      return false;
    } else if (CNCODEC_SUCCESS != ecode) {
      LOGE(ENCODE) << "cncodecEncWaitAvailInputBuf failed. Error code: " + to_string(ecode);
      return false;
    }

    // copy data to codec
    CopyFrame(&input, frame);
  } else {
    std::lock_guard<std::mutex> lk(list_mtx_);
    if (input_list_.empty()) {
      LOGE(ENCODE) << "Request memory from encoder if data is from device.";
      return false;
    }
    auto enc_input = std::find_if(input_list_.begin(), input_list_.end(),
                                  [frame, this](const cncodecFrame_t &enc_input) {
      return ((attr_.pixel_format == PixelFmt::I420 &&
                frame.ptrs[0] == reinterpret_cast<uint8_t *>(enc_input.plane[0].dev_addr) &&
                frame.ptrs[1] == reinterpret_cast<uint8_t *>(enc_input.plane[1].dev_addr) &&
                frame.ptrs[2] == reinterpret_cast<uint8_t *>(enc_input.plane[2].dev_addr)) ||
              ((attr_.pixel_format == PixelFmt::NV12 || attr_.pixel_format == PixelFmt::NV21) &&
                frame.ptrs[0] == reinterpret_cast<uint8_t *>(enc_input.plane[0].dev_addr) &&
                frame.ptrs[1] == reinterpret_cast<uint8_t *>(enc_input.plane[1].dev_addr)) ||
              ((attr_.pixel_format == PixelFmt::BGRA || attr_.pixel_format == PixelFmt::ARGB ||
                attr_.pixel_format == PixelFmt::RGBA || attr_.pixel_format == PixelFmt::ABGR) &&
                frame.ptrs[0] == reinterpret_cast<uint8_t *>(enc_input.plane[0].dev_addr)));
    });
    if (enc_input != input_list_.end()) {
      input = *enc_input;
      input_list_.erase(enc_input);
    } else {
      LOGE(ENCODE) << "Memory is not requested from encoder on device";
      return false;
    }
  }
  cncodecEncPicAttr_t frame_attr;
  memset(&frame_attr, 0, sizeof(cncodecEncPicAttr_t));
  if (attr_.codec_type == CodecType::JPEG || attr_.codec_type == CodecType::MJPEG) {
    frame_attr.jpg_pic_attr.jpeg_param.quality = attr_.attr_mlu300.jpeg_qfactor;
  }
  input.pts = frame.pts;
  input.priv_data = reinterpret_cast<u64_t>(frame.user_data);
  LOGT(ENCODE) << "Feed frame info data: " << frame.ptrs[0] << " length: " << frame.frame_size
               << " pts: " << frame.pts;
  ecode = cncodecEncSendFrame(handle_, &input, &frame_attr, 10000);
  if (CNCODEC_ERROR_TIMEOUT == ecode) {
    LOGE(ENCODE) << "cncodecEncSendFrame timeout";
  } else if (CNCODEC_SUCCESS != ecode) {
    THROW_EXCEPTION(Exception::INTERNAL, "cncodecEncSendFrame failed. cncodec error code: " + to_string(ecode));
  }
  return true;
}

void Mlu300Encoder::AbortEncoder() {
  LOGW(ENCODE) << "Abort encoder";
  if (handle_) {
    int codec_ret = cncodecEncDestroy(handle_);
    if (CNCODEC_SUCCESS != codec_ret) {
      LOGE(ENCODE) << "Call cncodecEncDestroy failed, ret = " << codec_ret;
    }
    handle_ = 0;

    if (attr_.eos_callback) {
      attr_.eos_callback();
    }
    std::unique_lock<std::mutex> eos_lk(eos_mutex_);
    got_eos_ = true;
    eos_cond_.notify_one();
  } else {
    LOGE(ENCODE) << "Won't do abort, since cnencode handler has not been initialized";
  }
}

void Mlu300Encoder::ReceiveEvent(cncodecEventType_t type) {
  std::lock_guard<std::mutex> lock(event_mtx_);
  event_queue_.push(type);
  event_cond_.notify_one();
}

void Mlu300Encoder::EventTaskRunner() {
  std::unique_lock<std::mutex> lock(event_mtx_);
  while (!event_queue_.empty() || !got_eos_) {
    event_cond_.wait(lock, [this] { return !event_queue_.empty() || got_eos_; });

    if (event_queue_.empty()) {
      // notified by eos
      continue;
    }

    cncodecEventType_t type = event_queue_.front();
    event_queue_.pop();
    lock.unlock();

    switch (type) {
      case CNCODEC_EVENT_OUT_OF_MEMORY:
        LOGE(DECODE) << "Out of memory error thrown from cncodec";
        AbortEncoder();
        break;
      case CNCODEC_EVENT_STREAM_CORRUPT:
        LOGW(ENCODE) << "Stream corrupt, discard frame";
        break;
      case CNCODEC_EVENT_STREAM_NOT_SUPPORTED:
        LOGE(ENCODE) << "Out of memory error thrown from cncodec";
        AbortEncoder();
        break;
      case CNCODEC_EVENT_BUFFER_OVERFLOW:
        LOGW(ENCODE) << "buffer overflow thrown from cncodec, buffer number is not enough";
        break;
      case CNCODEC_EVENT_FATAL_ERROR:
        LOGE(ENCODE) << "fatal error throw from cncodec";
        AbortEncoder();
        break;
      default:
        LOGE(ENCODE) << "Unknown event type";
        AbortEncoder();
        break;
    }
    lock.lock();
  }
}

static int32_t EventHandler(cncodecEventType_t type, void *user_data, void *package) {
  auto handler = reinterpret_cast<Mlu300Encoder*>(user_data);
  if (handler != nullptr) {
    switch (type) {
      case CNCODEC_EVENT_NEW_FRAME:
        handler->ReceivePacket(package);
        break;
      case CNCODEC_EVENT_EOS:
        handler->ReceiveEOS();
        break;
      default:
        handler->ReceiveEvent(type);
        break;
    }
  }
  return 0;
}

Encoder* CreateMlu300Encoder(const EasyEncode::Attr& attr) {
  return new Mlu300Encoder(attr);
}
}  // namespace edk
#else

namespace edk {

Encoder* CreateMlu300Encoder(const EasyEncode::Attr& attr) {
  LOGE(DECODE) << "Create mlu300 encoder failed, please install cncodec_v3.";
  return nullptr;
}

}  // namespace edk
#endif
