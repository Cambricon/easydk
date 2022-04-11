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
 * @file vformat.h
 *
 * This file contains a declaration of structures used in decode and encode.
 */

#ifndef EASYCODEC_VFORMAT_H_
#define EASYCODEC_VFORMAT_H_

#include <glog/logging.h>

#include <cstdint>
#include <string>

#define CN_MAXIMUM_PLANE 3

namespace edk {

/**
 * @brief Structure to describe resolution of video or image.
 */
struct Geometry {
  unsigned int w{0};  ///< width in pixel
  unsigned int h{0};  ///< height in pixel
};

/**
 * @brief Enumeration to describe image colorspace.
 */
enum class PixelFmt {
  NV12 = 0,  ///< NV12, YUV family
  NV21,      ///< NV21, YUV family
  I420,
  YV12,
  YUYV,
  UYVY,
  YVYU,
  VYUY,
  P010,
  YUV420_10BIT,
  YUV444_10BIT,
  ARGB,
  ABGR,
  BGRA,
  RGBA,
  AYUV,
  RGB565,
  RAW,  ///< No format
  BGR24,
  RGB24,
  I010,
  MONOCHROME,
  TOTAL_COUNT
};

/**
 * @brief Enumeration to describe data codec type
 * @note Type contains both video and image
 */
enum class CodecType {
  MPEG2 = 0,
  MPEG4,  ///< MPEG4 video codec standard
  H264,   ///< H.264 video codec standard
  H265,   ///< H.265 video codec standard, aka HEVC
  VP8,
  VP9,
  AVS,
  MJPEG,  ///< Motion JPEG video codec standard
  JPEG    ///< JPEG image format
};

/**
 * @brief Structure contains raw data and informations
 * @note Used as output in decode and input in encode
 */
struct CnFrame {
  /**
   * Used to release buffer in EasyDecode::ReleaseBuffer
   * when frame memory from decoder will not be used. Useless in encoder.
   */
  uint64_t buf_id{0};
  /// Presentation time stamp
  uint64_t pts{0};
  /// Frame height in pixel
  uint32_t height{0};
  /// Frame width in pixel
  uint32_t width{0};
  /// Frame data size, unit: byte
  uint64_t frame_size{0};
  /// Frame color space, @see edk::PixelFmt
  PixelFmt pformat{PixelFmt::NV12};
  /// MLU device identification, -1 means data is on cpu
  int device_id{-1};
  /// MLU channel in which memory stored
  int channel_id{0};
  /// Plane count for this frame
  uint32_t n_planes{0};
  /// Frame strides for each plane
  uint32_t strides[CN_MAXIMUM_PLANE]{0, 0, 0};
  /// Frame data pointer
  void* ptrs[CN_MAXIMUM_PLANE]{nullptr, nullptr, nullptr};
  /// get user data passed to codec, only support on Mlu300 decoder
  void* user_data{nullptr};

  uint32_t GetPlaneSize(uint32_t plane) const {
    if (plane >= CN_MAXIMUM_PLANE) {
      LOG(ERROR) << "[EasyDK VFrame] [CnFrame] Plane index (" << plane << ") out of range (" << CN_MAXIMUM_PLANE << ")";
      return 0;
    }
    uint32_t plane_size = 0;
    if (pformat == PixelFmt::NV12 || pformat == PixelFmt::NV21 || pformat == PixelFmt::I420 ||
        pformat == PixelFmt::YV12 || pformat == PixelFmt::P010) {
      plane_size = plane == 0 ? (strides[plane] * height) : (strides[plane] * (height >> 1));
    } else {
      plane_size = strides[plane] * height;
    }
    return plane_size;
  }
};

/**
 * @brief Encode bitstream slice type.
 */
enum class BitStreamSliceType { SPS_PPS, FRAME, KEY_FRAME};

/**
 * @brief Structure contains encoded data and informations
 * @note Used as output in encode and input in decode
 */
struct CnPacket {
  /**
   * Used to release buffer in EasyEncode::ReleaseBuffer
   * when memory from encoder will not be used. Useless in decoder.
   */
  uint64_t buf_id{0};
  /// Frame data pointer
  void* data{nullptr};
  /// Frame length, unit pixel
  uint64_t length{0};
  /// Presentation time stamp
  uint64_t pts{0};
  /// Video codec type, @see edk::CodecType
  CodecType codec_type{CodecType::H264};
  /// Bitstream slice type, only used in EasyEncode, @see edk::BitStreamSliceType
  BitStreamSliceType slice_type{BitStreamSliceType::FRAME};
  /// pass user data to codec, only support on Mlu300 decoder
  void* user_data{nullptr};
};

inline std::string PixelFmtStr(PixelFmt fmt) noexcept {
  switch (fmt) {
#define PIXELFMT2STR(fmt) \
  case PixelFmt::fmt:     \
    return #fmt;
    PIXELFMT2STR(NV12)
    PIXELFMT2STR(NV21)
    PIXELFMT2STR(I420)
    PIXELFMT2STR(YV12)
    PIXELFMT2STR(YUYV)
    PIXELFMT2STR(UYVY)
    PIXELFMT2STR(YVYU)
    PIXELFMT2STR(VYUY)
    PIXELFMT2STR(P010)
    PIXELFMT2STR(YUV420_10BIT)
    PIXELFMT2STR(YUV444_10BIT)
    PIXELFMT2STR(ARGB)
    PIXELFMT2STR(ABGR)
    PIXELFMT2STR(BGRA)
    PIXELFMT2STR(RGBA)
    PIXELFMT2STR(AYUV)
    PIXELFMT2STR(RGB565)
    PIXELFMT2STR(RAW)
    PIXELFMT2STR(BGR24)
    PIXELFMT2STR(RGB24)
    PIXELFMT2STR(I010)
    PIXELFMT2STR(MONOCHROME)
#undef PIXELFMT2STR
    default:
      LOG(ERROR) << "[EasyDK VFormat] [PixelFmtStr] Unsupported pixel format";
      return "INVALID";
  }
}

inline std::string CodecTypeStr(CodecType type) noexcept {
  switch (type) {
#define CODECTYPE2STR(type) \
  case CodecType::type:     \
    return #type;
    CODECTYPE2STR(MPEG2)
    CODECTYPE2STR(MPEG4)
    CODECTYPE2STR(H264)
    CODECTYPE2STR(H265)
    CODECTYPE2STR(VP8)
    CODECTYPE2STR(VP9)
    CODECTYPE2STR(AVS)
    CODECTYPE2STR(MJPEG)
    CODECTYPE2STR(JPEG)
#undef CODECTYPE2STR
    default:
      LOG(ERROR) << "[EasyDK VFormat] [CodecTypeStr] Unsupported codec type";
      return "INVALID";
  }
}

}  // namespace edk

#endif  // EASYCODEC_VFORMAT_H_

