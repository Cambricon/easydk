#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "../../src/easycodec/format_info.h"
#include "device/mlu_context.h"
#include "easycodec/easy_encode.h"
#include "test_base.h"

using edk::CodecType;
using edk::PixelFmt;
using edk::VideoLevel;
using edk::VideoProfile;

static std::mutex enc_mutex;
static std::condition_variable enc_cond;
static bool is_eos = false;

static edk::EasyEncode *g_encoder = nullptr;
static edk::PixelFmt input_pixel_format = PixelFmt::NV12;
static FILE *p_output_file;
static uint32_t frame_count = 0;

#define VIDEO_ENCODE_FRAME_COUNT 30

#define TEST_1080P_JPG "../../tests/data/1080p.jpg"
#define TEST_500x500_JPG "../../tests/data/500x500.jpg"

static const char *pf_str(const PixelFmt &fmt) {
  switch (fmt) {
    case PixelFmt::NV21:
      return "NV21";
    case PixelFmt::NV12:
      return "NV12";
    case PixelFmt::I420:
      return "I420";
    default:
      return "UnknownType";
  }
}

static const char *cc_str(const CodecType &mode) {
  switch (mode) {
    case CodecType::MPEG4:
      return "MPEG4";
    case CodecType::H264:
      return "H264";
    case CodecType::H265:
      return "H265";
    case CodecType::JPEG:
      return "JPEG";
    case CodecType::MJPEG:
      return "MJPEG";
    default:
      return "UnknownType";
  }
}

static int frames_output = 0;

#define ALIGN(w, a) ((w + a - 1) & ~(a - 1))

static bool cvt_bgr_to_yuv420sp(const cv::Mat &bgr_image, uint32_t alignment, edk::PixelFmt pixel_fmt,
                                uint8_t *yuv_2planes_data) {
  cv::Mat yuv_i420_image;
  uint32_t width, height, stride;
  uint8_t *src_y, *src_u, *src_v, *dst_y, *dst_u, *dst_v;

  cv::cvtColor(bgr_image, yuv_i420_image, cv::COLOR_BGR2YUV_I420);

  width = bgr_image.cols;
  height = bgr_image.rows;
  if (alignment > 0)
    stride = ALIGN(width, alignment);
  else
    stride = width;

  uint32_t y_len = width * height;
  src_y = yuv_i420_image.data;
  src_u = yuv_i420_image.data + y_len;
  src_v = yuv_i420_image.data + y_len * 5 / 4;
  dst_y = yuv_2planes_data;
  dst_u = yuv_2planes_data + stride * height;
  dst_v = yuv_2planes_data + stride * height * 5 / 4;

  for (uint32_t i = 0; i < height; i++) {
    // y data
    memcpy(dst_y + i * stride, src_y + i * width, width);
    // uv data
    if (i % 2 == 0) {
      if (pixel_fmt == edk::PixelFmt::I420) {
        memcpy(dst_u + i * stride / 4, src_u + i * width / 4, width / 2);
        memcpy(dst_v + i * stride / 4, src_v + i * width / 4, width / 2);
        continue;
      }
      for (uint32_t j = 0; j < width / 2; j++) {
        if (pixel_fmt == edk::PixelFmt::NV21) {
          *(dst_u + i * stride / 2 + 2 * j) = *(src_v + i * width / 4 + j);
          *(dst_u + i * stride / 2 + 2 * j + 1) = *(src_u + i * width / 4 + j);
        } else {
          *(dst_u + i * stride / 2 + 2 * j) = *(src_u + i * width / 4 + j);
          *(dst_u + i * stride / 2 + 2 * j + 1) = *(src_v + i * width / 4 + j);
        }
      }
    }
  }

  return true;
}

void eos_callback() {
  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
  } catch (edk::Exception &err) {
    printf("set mlu env failed\n");
    return;
  }
  printf("eos_callback()\n");
  if (p_output_file) {
    fflush(p_output_file);
    fclose(p_output_file);
    p_output_file = NULL;
  }
  frames_output = 0;
  if (g_encoder->GetAttr().codec_type != CodecType::JPEG) {
    printf("encode video pass\n");
  } else {
    printf("encode jpeg pass\n");
  }
  std::unique_lock<std::mutex> lk(enc_mutex);
  is_eos = true;
  enc_cond.notify_all();
}

void packet_callback(const edk::CnPacket &packet) {
  char *output_file = NULL;
  char str[256] = {0};
  size_t written;

  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
  } catch (edk::Exception &err) {
    g_encoder->ReleaseBuffer(packet.buf_id);
    printf("set mlu env failed\n");
    return;
  }

  if (packet.codec_type == CodecType::JPEG) {
    snprintf(str, sizeof(str), "./encoded_%s_%02d.jpg", pf_str(input_pixel_format), frames_output);
    output_file = str;
  } else if (packet.codec_type == CodecType::H264) {
    snprintf(str, sizeof(str), "./encoded_%s_%lu.h264", pf_str(input_pixel_format), packet.length);
    output_file = str;
  } else if (packet.codec_type == CodecType::H265) {
    snprintf(str, sizeof(str), "./encoded_%s_%lu.h265", pf_str(input_pixel_format), packet.length);
    output_file = str;
  } else {
    printf("ERROR: unknown output codec type <%d>\n", static_cast<int>(packet.codec_type));
  }

  if (p_output_file == NULL) p_output_file = fopen(output_file, "wb");
  if (p_output_file == NULL) {
    printf("ERROR: open output file failed\n");
  }

  frames_output++;
  uint32_t length = packet.length;

  written = fwrite(packet.data, 1, length, p_output_file);
  if (written != length) {
    printf("ERROR: written size(%u) != data length(%u)\n", (unsigned int)written, length);
  }
  g_encoder->ReleaseBuffer(packet.buf_id);
}

bool SendData(edk::EasyEncode *encoder, PixelFmt pixel_format, CodecType codec_type, bool end,
              const std::string &image_path) {
  edk::CnFrame frame;
  uint8_t *p_data_buffer = NULL;
  cv::Mat cv_image;
  int width, height;
  unsigned int input_length;

  cv_image = cv::imread(image_path);
  if (cv_image.empty()) {
    std::cerr << "Invalid image, image path" << image_path << std::endl;
    return false;
  }

  width = cv_image.cols;
  height = cv_image.rows;
  uint32_t align = 0;

  if (pixel_format == PixelFmt::NV21 || pixel_format == PixelFmt::NV12 || pixel_format == PixelFmt::I420) {
    input_length = width * height * 3 / 2;
    p_data_buffer = new (std::nothrow) uint8_t[input_length];
    if (p_data_buffer == NULL) {
      printf("ERROR: malloc buffer for input file failed\n");
      return false;
    }

    cvt_bgr_to_yuv420sp(cv_image, align, pixel_format, p_data_buffer);

    frame.pformat = pixel_format;
    frame.ptrs[0] = reinterpret_cast<void *>(p_data_buffer);
    frame.ptrs[1] = reinterpret_cast<void *>(p_data_buffer + width * height);
    frame.n_planes = 2;
    if (pixel_format == PixelFmt::I420) {
      frame.n_planes = 3;
      frame.ptrs[2] = reinterpret_cast<void *>(p_data_buffer + width * height * 5 / 4);
    }
    frame.frame_size = input_length;
    frame.width = width;
    frame.height = height;
    frame.pts = frame_count++;
  } else {
    printf("ERROR: Input pixel format(%d) invalid\n", static_cast<int>(pixel_format));
    return false;
  }

  input_pixel_format = pixel_format;

  bool ret = true;
  bool eos = false;
  if (end) {
    printf("Set EOS flag to encoder\n");
    eos = true;
  }
  ret = encoder->SendDataCPU(frame, eos);

  if (p_data_buffer) delete[] p_data_buffer;

  return ret;
}

bool test_EasyEncode(const char *input_file, uint32_t w, uint32_t h, PixelFmt pixel_format, CodecType codec_type,
                     bool vbr, bool mismatch_config = false, uint32_t max_mb_per_slice = 0, bool _abort = false) {
  printf("\nTesting encode %s image to %s\n", pf_str(pixel_format), cc_str(codec_type));

  p_output_file = NULL;
  frame_count = 0;
  frames_output = 0;
  std::string input_path = GetExePath() + input_file;

  is_eos = false;

  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
  } catch (edk::Exception &err) {
    printf("set mlu env failed\n");
    return false;
  }

  edk::EasyEncode::Attr attr;
  attr.dev_id = 0;
  attr.frame_geometry.w = w;
  attr.frame_geometry.h = h;
  attr.codec_type = codec_type;
  attr.pixel_format = pixel_format;
  attr.packet_callback = packet_callback;
  attr.eos_callback = eos_callback;
  attr.input_buffer_num = 4;
  attr.output_buffer_num = 4;
  attr.rate_control.vbr = vbr;
  attr.rate_control.gop = 20;
  attr.rate_control.frame_rate_num = 30;
  attr.rate_control.frame_rate_den = 1;
  attr.rate_control.bit_rate = 1024;
  attr.rate_control.max_bit_rate = 2048;
  attr.silent = false;
  attr.jpeg_qfactor = 50;
  // attr.ir_count = 5;
  switch (codec_type) {
    case CodecType::H264:
      if (mismatch_config) {
        attr.profile = VideoProfile::H265_MAIN;
        attr.level = VideoLevel::H265_HIGH_41;
      } else {
        attr.profile = VideoProfile::H264_MAIN;
        attr.level = VideoLevel::H264_41;
      }
      break;
    case CodecType::H265:
      if (mismatch_config) {
        attr.profile = VideoProfile::H264_MAIN;
        attr.level = VideoLevel::H264_41;
      } else {
        attr.profile = VideoProfile::H265_MAIN;
        attr.level = VideoLevel::H265_HIGH_41;
      }
      break;
    default:
      break;
  }

  std::unique_ptr<edk::EasyEncode> encoder;
  try {
    bool ret = false;
    encoder = edk::EasyEncode::New(attr);
    if (!encoder) THROW_EXCEPTION(edk::Exception::INTERNAL, "Create EasyEncode failed");
    g_encoder = encoder.get();
    if (codec_type == CodecType::H264 || codec_type == CodecType::H265 || codec_type == CodecType::JPEG) {
      // encode multi frames for video encoder
      for (int i = 0; i < VIDEO_ENCODE_FRAME_COUNT; i++) {
        bool end = i < (VIDEO_ENCODE_FRAME_COUNT - 1) || _abort ? false : true;
        ret = SendData(g_encoder, pixel_format, codec_type, end, input_path);
        if (!ret) {
          break;
        }
      }
    } else {
      THROW_EXCEPTION(edk::Exception::INVALID_ARG, "Unsupport format");
    }
    if (!ret) {
      THROW_EXCEPTION(edk::Exception::INTERNAL, "Send data failed");
    }

    if (_abort) {
      g_encoder->AbortEncoder();
    }
    std::unique_lock<std::mutex> lk(enc_mutex);
    if (!is_eos) {
      enc_cond.wait(lk, [] { return is_eos; });
    }
  } catch (edk::Exception &err) {
    std::cerr << err.what() << std::endl;
    return false;
  }

  return true;
}

bool test_PixelFmt(PixelFmt pixel_format, cncodecPixelFormat res) {
  if (res == edk::FormatInfo::GetFormatInfo(pixel_format)->cncodec_fmt)
    return true;
  else
    return false;
}

bool test_CodecType(CodecType codec_type, cncodecType res) {
  if (res == CodecTypeCast(codec_type))
    return true;
  else
    return false;
}

TEST(Codec, EncodeVideo) {
  bool ret = false;
  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H264, true);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::H264, false);
  EXPECT_TRUE(ret);

  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H265, true);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::H265, false);
  EXPECT_TRUE(ret);

  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::I420, CodecType::H264, true);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::I420, CodecType::H265, true);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::H264, false);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV21, CodecType::H264, false);
  EXPECT_TRUE(ret);

  // test mismatch profile and level
  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::H264, true, true);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H265, false, true);
  EXPECT_TRUE(ret);

  // test abort encoder
  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::H264, false, false, true);
  EXPECT_TRUE(ret);
}

TEST(Codec, EncodeJpeg) {
  bool ret = false;
  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::JPEG, false);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::JPEG, false);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV21, CodecType::JPEG, false);
  EXPECT_TRUE(ret);
  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::JPEG, false);
  EXPECT_TRUE(ret);

  // test abort encoder
  ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::JPEG, false, false, true);
  EXPECT_TRUE(ret);
}

TEST(Codec, EncodeNoFrame) {
  {
    edk::EasyEncode::Attr attr;
    attr.frame_geometry.w = 1920;
    attr.frame_geometry.h = 1080;
    attr.codec_type = edk::CodecType::H264;
    attr.pixel_format = edk::PixelFmt::NV21;
    attr.packet_callback = nullptr;
    attr.eos_callback = nullptr;
    attr.silent = false;
    std::unique_ptr<edk::EasyEncode> encode = nullptr;
    encode = edk::EasyEncode::New(attr);
  }
  {
    edk::EasyEncode::Attr attr;
    attr.frame_geometry.w = 1920;
    attr.frame_geometry.h = 1080;
    attr.codec_type = edk::CodecType::JPEG;
    attr.pixel_format = edk::PixelFmt::NV21;
    attr.packet_callback = nullptr;
    attr.eos_callback = nullptr;
    attr.silent = false;
    std::unique_ptr<edk::EasyEncode> encode = nullptr;
    encode = edk::EasyEncode::New(attr);
  }
}

TEST(Codec, InfoFormat) {
  EXPECT_TRUE(test_CodecType(CodecType::MPEG4, CNCODEC_MPEG4));
  EXPECT_TRUE(test_CodecType(CodecType::VP8, CNCODEC_VP8));
  EXPECT_TRUE(test_CodecType(CodecType::VP9, CNCODEC_VP9));
  EXPECT_TRUE(test_CodecType(CodecType::AVS, CNCODEC_AVS));
  EXPECT_TRUE(test_CodecType(CodecType::JPEG, CNCODEC_JPEG));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::YV12, CNCODEC_PIX_FMT_YV12));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::YUYV, CNCODEC_PIX_FMT_YUYV));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::UYVY, CNCODEC_PIX_FMT_UYVY));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::YVYU, CNCODEC_PIX_FMT_YVYU));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::VYUY, CNCODEC_PIX_FMT_VYUY));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::P010, CNCODEC_PIX_FMT_P010));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::YUV420_10BIT, CNCODEC_PIX_FMT_YUV420_10BIT));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::YUV444_10BIT, CNCODEC_PIX_FMT_YUV444_10BIT));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::ARGB, CNCODEC_PIX_FMT_ARGB));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::ABGR, CNCODEC_PIX_FMT_ABGR));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::BGRA, CNCODEC_PIX_FMT_BGRA));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::RGBA, CNCODEC_PIX_FMT_RGBA));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::AYUV, CNCODEC_PIX_FMT_AYUV));
  EXPECT_TRUE(test_PixelFmt(PixelFmt::RGB565, CNCODEC_PIX_FMT_RGB565));
}
