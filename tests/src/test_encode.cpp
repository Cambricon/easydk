#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "cnrt.h"
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
              const std::string &image_path, bool mlu_data) {
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

    if (!mlu_data) {
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
      frame.device_id = -1;
    } else {
      encoder->RequestFrame(&frame);
      cnrtMemcpy(frame.ptrs[0], p_data_buffer, width * height, CNRT_MEM_TRANS_DIR_HOST2DEV);
      if (pixel_format == PixelFmt::NV21 || pixel_format == PixelFmt::NV12) {
        cnrtMemcpy(frame.ptrs[1], p_data_buffer + width * height, width * height / 2, CNRT_MEM_TRANS_DIR_HOST2DEV);
      } else {
        cnrtMemcpy(frame.ptrs[1], p_data_buffer + width * height, width * height / 4, CNRT_MEM_TRANS_DIR_HOST2DEV);
        cnrtMemcpy(frame.ptrs[2], p_data_buffer + width * height * 5 / 4, width * height / 4,
                   CNRT_MEM_TRANS_DIR_HOST2DEV);
      }
    }
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
  if (!eos) {
    ret = encoder->FeedData(frame);
  } else {
    ret = encoder->FeedEos();
  }

  if (p_data_buffer) delete[] p_data_buffer;

  return ret;
}

bool test_EasyEncode(const char *input_file, uint32_t w, uint32_t h, PixelFmt pixel_format, CodecType codec_type,
                     bool mlu_data, int rc_mode, int preset = 0/*only for mlu370*/, int tune = 2/*only for mlu370*/,
                     bool mismatch_config = false, uint32_t max_mb_per_slice = 0, bool _abort = false) {
  printf("\nTesting encode %s image to %s\n", pf_str(pixel_format), cc_str(codec_type));

  p_output_file = NULL;
  frame_count = 0;
  frames_output = 0;
  std::string input_path = GetExePath() + input_file;

  is_eos = false;
  edk::CoreVersion core_version;
  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
    core_version = context.GetCoreVersion();
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
  attr.silent = false;
  if (core_version == edk::CoreVersion::MLU370) {
    attr.attr_mlu300.preset = static_cast<edk::EncodePreset>(preset);
    attr.attr_mlu300.tune = static_cast<edk::EncodeTune>(tune);
    attr.attr_mlu300.gop_size = 20;
    attr.attr_mlu300.frame_rate_num = 30;
    attr.attr_mlu300.frame_rate_den = 1;
    attr.attr_mlu300.rate_control.rc_mode = static_cast<edk::RateControlModeMlu300>(rc_mode);
    attr.attr_mlu300.rate_control.bit_rate = 1024 * 1024;
    attr.attr_mlu300.jpeg_qfactor = 50;
    switch (codec_type) {
      case CodecType::H264:
        if (mismatch_config) {
          attr.attr_mlu300.profile = VideoProfile::H265_MAIN;
          attr.attr_mlu300.level = VideoLevel::H265_HIGH_41;
        } else {
          attr.attr_mlu300.profile = VideoProfile::H264_MAIN;
          attr.attr_mlu300.level = VideoLevel::H264_41;
        }
        break;
      case CodecType::H265:
        if (mismatch_config) {
          attr.attr_mlu300.profile = VideoProfile::H264_MAIN;
          attr.attr_mlu300.level = VideoLevel::H264_41;
        } else {
          attr.attr_mlu300.profile = VideoProfile::H265_MAIN;
          attr.attr_mlu300.level = VideoLevel::H265_HIGH_41;
        }
        break;
      default:
        break;
    }
  } else if (core_version == edk::CoreVersion::MLU270 || core_version == edk::CoreVersion::MLU220) {
    attr.attr_mlu200.rate_control.vbr = rc_mode;
    attr.attr_mlu200.rate_control.gop = 20;
    attr.attr_mlu200.rate_control.frame_rate_num = 30;
    attr.attr_mlu200.rate_control.frame_rate_den = 1;
    attr.attr_mlu200.rate_control.bit_rate = 1024;
    attr.attr_mlu200.rate_control.max_bit_rate = 2048;
    attr.attr_mlu200.jpeg_qfactor = 50;
    switch (codec_type) {
      case CodecType::H264:
        if (mismatch_config) {
          attr.attr_mlu200.profile = VideoProfile::H265_MAIN;
          attr.attr_mlu200.level = VideoLevel::H265_HIGH_41;
        } else {
          attr.attr_mlu200.profile = VideoProfile::H264_MAIN;
          attr.attr_mlu200.level = VideoLevel::H264_41;
        }
        break;
      case CodecType::H265:
        if (mismatch_config) {
          attr.attr_mlu200.profile = VideoProfile::H264_MAIN;
          attr.attr_mlu200.level = VideoLevel::H264_41;
        } else {
          attr.attr_mlu200.profile = VideoProfile::H265_MAIN;
          attr.attr_mlu200.level = VideoLevel::H265_HIGH_41;
        }
        break;
      default:
        break;
    }
  } else {
    THROW_EXCEPTION(edk::Exception::INTERNAL, "Not supported core version");
    return false;
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
        ret = SendData(g_encoder, pixel_format, codec_type, end, input_path, mlu_data);
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

TEST(Codec, EncodeVideo) {
  std::vector<bool> mlu_data_vec = {false, true};
  edk::MluContext context;
  edk::CoreVersion core_version = context.GetCoreVersion();

  for (auto mlu_data : mlu_data_vec) {
    bool ret = false;
    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H264, mlu_data, false);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::H264, mlu_data, false);
    EXPECT_TRUE(ret);

    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H265, mlu_data, true);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::H265, mlu_data, false);
    EXPECT_TRUE(ret);

    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::I420, CodecType::H264, mlu_data, true);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::I420, CodecType::H265, mlu_data, true);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::H264, mlu_data, false);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV21, CodecType::H264, mlu_data, false);
    EXPECT_TRUE(ret);

    // test mismatch profile and level
    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::H264, mlu_data, true, 0, 2, true);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H265, mlu_data, false, 0, 2, true);
    EXPECT_TRUE(ret);

    // test abort encoder
    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::H264, mlu_data, false, 0, 2,
                          false, true);
    EXPECT_TRUE(ret);

    if (core_version == edk::CoreVersion::MLU370) {
      std::vector<int> rc_mode_vec = {0, 1, 2, 3, 4};
      for (auto &rc_mode : rc_mode_vec) {
        ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H264, mlu_data, rc_mode);
        EXPECT_TRUE(ret);
      }
      std::vector<int> preset_vec = {0, 1, 2, 3, 4};
      std::vector<int> tune_vec = {0, 1, 2, 3, 4};
      for (auto &preset : preset_vec) {
        for (auto &tune : tune_vec) {
          ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::H264, mlu_data, 0, preset, tune);
          EXPECT_TRUE(ret);
          ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::H265, mlu_data, 0, preset, tune);
          EXPECT_TRUE(ret);
        }
      }
    }
  }
}

TEST(Codec, EncodeJpeg) {
  std::vector<bool> mlu_data_vec = {false, true};
  for (auto mlu_data : mlu_data_vec) {
    bool ret = false;
    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV21, CodecType::JPEG, mlu_data, false);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_1080P_JPG, 1920, 1080, PixelFmt::NV12, CodecType::JPEG, mlu_data, false);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV21, CodecType::JPEG, mlu_data, false);
    EXPECT_TRUE(ret);
    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::JPEG, mlu_data, false);
    EXPECT_TRUE(ret);

    // test abort encoder
    ret = test_EasyEncode(TEST_500x500_JPG, 500, 500, PixelFmt::NV12, CodecType::JPEG, mlu_data, false, 0, 2,
                          false, true);
    EXPECT_TRUE(ret);
  }
}

TEST(Codec, EncodeNoFrame) {
  {
    edk::MluContext context;
    edk::CoreVersion core_version = context.GetCoreVersion();
    edk::EasyEncode::Attr attr;
    // TODO(gaoyujia) : fixed on cncodec_v3 version 0.9
    if (core_version != edk::CoreVersion::MLU370) {
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
