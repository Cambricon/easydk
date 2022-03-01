/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

/**
 * @file easy_encode.h
 *
 * This file contains a declaration of the EasyDecode class and involved structures.
 */

#ifndef EASYCODEC_EASY_ENCODE_H_
#define EASYCODEC_EASY_ENCODE_H_

#include <functional>
#include <memory>

#include "cxxutil/edk_attribute.h"
#include "cxxutil/exception.h"
#include "easycodec/vformat.h"

namespace edk {

/**
 * @brief Video profile enumeration.
 */
enum class VideoProfile {
  H264_BASELINE = 0,
  H264_MAIN,
  H264_HIGH,
  H264_HIGH_10,

  H265_MAIN,
  H265_MAIN_STILL,
  H265_MAIN_INTRA,
  H265_MAIN_10,
  PROFILE_MAX
};

/**
 * @brief Video codec level
 */
enum class VideoLevel {
  H264_1 = 0,
  H264_1B,
  H264_11,
  H264_12,
  H264_13,
  H264_2,
  H264_21,
  H264_22,
  H264_3,
  H264_31,
  H264_32,
  H264_4,
  H264_41,
  H264_42,
  H264_5,
  H264_51,
  H264_52,  // Only for mlu300 encoder
  H264_6,   // Only for mlu300 encoder
  H264_61,  // Only for mlu300 encoder
  H264_62,  // Only for mlu300 encoder

  H265_MAIN_1,
  H265_HIGH_1,
  H265_MAIN_2,
  H265_HIGH_2,
  H265_MAIN_21,
  H265_HIGH_21,
  H265_MAIN_3,
  H265_HIGH_3,
  H265_MAIN_31,
  H265_HIGH_31,
  H265_MAIN_4,
  H265_HIGH_4,
  H265_MAIN_41,
  H265_HIGH_41,
  H265_MAIN_5,
  H265_HIGH_5,
  H265_MAIN_51,
  H265_HIGH_51,
  H265_MAIN_52,
  H265_HIGH_52,
  H265_MAIN_6,
  H265_HIGH_6,
  H265_MAIN_61,
  H265_HIGH_61,
  H265_MAIN_62,
  H265_HIGH_62,
  AUTO_SELECT,  // Only for mlu300 encoder
  LEVEL_MAX
};

/// ----------------------------Mlu200------------------------
/**
 * @brief Rate control for Mlu200 parameters
 */
struct RateControlMlu200 {
  /// Using variable bit rate or constant bit rate
  bool vbr{false};
  /// The interval of ISLICE.
  uint32_t gop{0};
  /// The numerator of encode frame rate of the venc channel
  uint32_t frame_rate_num{0};
  /// The denominator of encode frame rate of the venc channel
  uint32_t frame_rate_den{0};
  /// Average bitrate in unit of kpbs, for cbr only.
  uint32_t bit_rate{0};
  /// The max bitrate in unit of kbps, for vbr only .
  uint32_t max_bit_rate{0};
  /// The max qp, range [min_qp, 51]
  uint32_t max_qp{0};
  /// The min qp, range [0, max_qp]
  uint32_t min_qp{0};
};

/*
 * @brief cncodec GOP type, see developer guide
 */
enum class GopTypeMlu200 {
  BIDIRECTIONAL,
  LOW_DELAY,
  PYRAMID,
  TYPE_MAX
};

/**
 * @brief for Mlu200 encoding parameters
 */
struct EncodeAttrMlu200 {
  /// Quality factor for jpeg encoder.
  uint32_t jpeg_qfactor = 50;
  /// Profile for video encoder.
  VideoProfile profile = VideoProfile::H264_MAIN;
  /// Video encode level
  VideoLevel level = VideoLevel::H264_41;
  /// The rate control parameters
  RateControlMlu200 rate_control;
  /// B frame number in gop when profile is above main, default 0
  uint32_t b_frame_num = 0;
  /// GOP type, @see edk::GopTypeMlu200
  GopTypeMlu200 gop_type = GopTypeMlu200::BIDIRECTIONAL;
  /// insert SPS/PPS before IDR,1, insert, 0 not
  bool insert_spspps_when_idr = true;
};

/// ----------------------------Mlu300------------------------
enum class EncodePreset {
  VERY_FAST,
  FAST,
  MEDIUM,
  SLOW,
  VERY_SLOW
};

enum class EncodeTune {
  DEFAULT,
  HIGH_QUALITY,
  LOW_LATENCY,
  LOW_LATENCY_HIGH_QUALITY,
  FAST_DECODE
};

/*
 * @brief cncodec_v3 GOP type, see developer guide.
 */
enum class GopTypeMlu300 {
  BFRAME_REF_DISABLED,
  BFRAME_REF_EACH,
  BFRAME_REF_MIDDLE,
  BFRAME_REF_HIERARCHICAL,
  TYPE_MAX
};

/**
 * @brief Rate control mode for Mlu300 enumeration.
 *
 * For more details, see cncodec_v3 developer guide.
 */
enum class RateControlModeMlu300 {
  CBR,
  VBR,
  CVBR,
  CRF,
  FIXEDQP,
  MODE_MAX
};

/**
 * @brief Rate control for Mlu300 parameters
 */
struct RateControlMlu300 {
  /// Rate control mode
  RateControlModeMlu300 rc_mode{RateControlModeMlu300::MODE_MAX};
  /// The target bit rate in bits/s range [10000, max_bitrate_in_level]
  uint32_t bit_rate{0};
  /// The rate control window, range [0, 300], 0 means calculating automatic
  int32_t rc_windows{-1};
  /// The lookahead depth in two pass modes, range 0 or [4, 40], for cbr, vbr, cvbr and crf. For crf must not be 0.
  int32_t lookahead_depth{-1};
  /// The max qp, range [min_qp, 51], for cvbr only. Not recommend greater than 45.
  int32_t max_qp{-1};
  /// The min qp, range [0, max_qp], for cvbr only. Not recommend lower than 10.
  int32_t min_qp{-1};
  /// The target quality range [0, 51], for crf only, the higher the value, the lower the encoding quality.
  int32_t target_quality{-1};
  /// The const qp for i frame, range [0, 51], for fixedqp only.
  int32_t const_qp_i{-1};
  /// The const qp for p frame, range [0, 51], for fixedqp only.
  int32_t const_qp_p{-1};
  /// The const qp for b frame, range [0, 51], for fixedqp only.
  int32_t const_qp_b{-1};
  /// The initial QP, range [-1, 51], for cbr, vbr, cvbr and crf. -1 means calculating automatic.
  int32_t initial_qp{-2};
};

/**
 * @brief for Mlu300 encoding parameters
 */
struct EncodeAttrMlu300 {
  /// preset mode
  EncodePreset preset = EncodePreset::VERY_FAST;
  /// tune mode
  EncodeTune tune = EncodeTune::LOW_LATENCY;

  /// max width and max height
  Geometry max_geometry;
  /// Quality factor for jpeg encoder, range [1-100].
  uint32_t jpeg_qfactor = 50;
  /// Profile for video encoder.
  VideoProfile profile = VideoProfile::PROFILE_MAX;
  /// Video encode level
  VideoLevel level = VideoLevel::LEVEL_MAX;
  /// The interval of ISLICE.
  uint32_t gop_size{0};
  /// The numerator of encode frame rate.
  uint32_t frame_rate_num{30};
  /// The denominator of encode frame rate.
  uint32_t frame_rate_den{1};
  /// The rate control parameters
  RateControlMlu300 rate_control;
  /// Specifies the number of B frames between two P frames, e.g., 4 means 3 B frames and 1 P frame.
  int32_t frame_interval_p = -1;
  /// GOP type, @see edk::GopTypeMlu300
  GopTypeMlu300 gop_type = GopTypeMlu300::TYPE_MAX;
  /// insert SPS/PPS before IDR,1, insert, 0 not. -1, use preset value.
  int insert_spspps_when_idr = -1;
};


/**
 * @brief Encode packet callback function type
 * @param CnPacket Packet containing encoded frame information
 */
using EncodePacketCallback = std::function<void(const CnPacket&)>;

/// Encode EOS callback function type
using EncodeEosCallback = std::function<void()>;

class Encoder;

/**
 * @brief Easy encoder class, provide a fast and easy API to encode on MLU platform.
 */
class EasyEncode {
 public:
  /**
   * @brief params for creating EasyEncode
   */
  struct Attr {
    /// The maximum resolution that this encoder can handle.
    Geometry frame_geometry;

    /**
     * @brief Input pixel format
     * @note h264/h265 support NV21/NV12/I420/RGBA/BGRA/ARGB/ABGR
     *       jpeg support NV21/NV12
     */
    PixelFmt pixel_format;

    /**
     * @brief output codec type
     * @note support h264/h265/jpeg
     */
    CodecType codec_type = CodecType::H264;

    /// parameters for Mlu200.
    EncodeAttrMlu200 attr_mlu200;

    /// parameters for Mlu300.
    EncodeAttrMlu300 attr_mlu300;

    /// Whether to print encoder attribute
    bool silent = false;

    /// Callback for receive packet
    EncodePacketCallback packet_callback = NULL;

    /// Callback for receive eos
    EncodeEosCallback eos_callback = NULL;

    /// Identification to specify device on which create encoder
    int dev_id = 0;
  };

  /**
   * @brief Create encoder by attr. Throw a Exception while error encountered.
   * @param attr Encoder attribute description
   * @return Pointer to new encoder instance
   */
  static std::unique_ptr<EasyEncode> New(const Attr& attr);

  /**
   * @brief Abort encoder instance at once
   * @note aborted encoder instance cannot be used any more
   */
  void AbortEncoder();

  /**
   * @brief Get the encoder instance attribute
   * @return Encoder attribute
   */
  Attr GetAttr() const;

  /**
   * @brief Destroy the Easy Encode object
   */
  ~EasyEncode();

  /**
   * @brief Requests frame from encoder.
   *
   * @param frame Outputs the frame with mlu data. The mlu data is from encoder input buffers.
   *
   * @note If FeedData with mlu frame data, must use this function to get mlu data ``ptrs``
   *
   * @return Returns true if request data succeed, otherwise returns false.
   */
  bool RequestFrame(CnFrame* frame);

  /**
   * @brief Send data to encoder, block when STATUS is pause.
   *        An Exception is thrown when send data failed.
   *
   * @param frame The frame data, will be sent to encoder.
   *              Sets device_id to negative if data is on cpu. If data is on mlu, sets device_id to device id.
   *
   * @note If data is on mlu, must get mlu frame data by RequestFrame function to get encoder input buffer.
   *
   * @return Returns true if feed data succeed, otherwise returns false.
   */
  bool FeedData(const CnFrame& frame) noexcept(false);

  /**
   * @brief Send EOS to encoder, block when STATUS is pause.
   *        An Exception is thrown when send EOS failed.
   *
   * @return Returns true if feed eos succeed, otherwise returns false.
   */
  bool FeedEos() noexcept(false);

  /**
   * @brief send frame to encoder.
   * @param frame frame data, will be sent to encoder
   * @param eos default false
   *
   * @deprecated use EasyEncode::FeedData(const CnFrame&) and EasyEncode::FeedEos() instead.
   *
   * @return Returns false if send data failed, otherwise returns true.
   */
  bool SendDataCPU(const CnFrame& frame, bool eos = false);

  /**
   * @brief Release encoder buffer.
   * @note Release encoder buffer each time received packet while packet content will not be used,
   *       otherwise encoder may be blocked.
   * @param buf_id Codec buffer id.
   */
  void ReleaseBuffer(uint64_t buf_id);

 private:
  explicit EasyEncode(const Attr& attr);
  EasyEncode(const EasyEncode&) = delete;
  const EasyEncode& operator=(const EasyEncode&) = delete;

  Encoder* handler_{nullptr};
};  // class EasyEncode

}  // namespace edk

#endif  // EASYCODEC_EASY_ENCODE_H_
