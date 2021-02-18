#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>
#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include "device/mlu_context.h"
#include "easycodec/easy_decode.h"
#include "test_base.h"

std::mutex mut;
std::condition_variable cond;
bool rec = false;
const char* jpeg_file = "../../tests/data/1080p.jpg";
const char* progressive_jpeg_file = "../../tests/data/progressive_1080p.jpg";
const char* h264_file = "../../tests/data/1080p.h264";
char* test_file = NULL;
FILE* p_big_stream = NULL;

edk::EasyDecode* g_decode;
static uint8_t* g_data_buffer;

#ifndef MAX_INPUT_DATA_SIZE
#define MAX_INPUT_DATA_SIZE (25 << 20)
#endif

void frame_callback(bool* condv, std::condition_variable* cond, const edk::CnFrame& frame) {
  EXPECT_EQ(static_cast<uint32_t>(1080), frame.height);
  EXPECT_EQ(static_cast<uint32_t>(1920), frame.width);

  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
  } catch (edk::Exception& err) {
    std::cout << "set mlu env failed" << std::endl;
    g_decode->ReleaseBuffer(frame.buf_id);
    return;
  }

  std::cout << "\n#########################\n";
  std::cout << "width " << frame.width << " height " << frame.height << std::endl;
  std::cout << "stride | addr:\n";
  for (size_t idx = 0; idx < frame.n_planes; ++idx) {
    std::cout << frame.strides[idx] << " | ";
    std::cout << frame.ptrs[idx] << "\n";
  }
  std::cout << "#########################\n\n";

  if (p_big_stream == NULL) {
    p_big_stream = fopen("big.yuv", "wb");
    if (p_big_stream == NULL) {
      std::cout << "open big.yuv failed" << std::endl;
      g_decode->ReleaseBuffer(frame.buf_id);
      return;
    }
  }

  uint8_t* buffer = NULL;
  uint32_t length = frame.frame_size;
  size_t written;

  if (length == 0) {
    g_decode->ReleaseBuffer(frame.buf_id);
    return;
  }

  buffer = new uint8_t[length];
  if (buffer == NULL) {
    std::cout << ("ERROR: malloc for big buffer failed\n");
    g_decode->ReleaseBuffer(frame.buf_id);
    return;
  }

  g_decode->CopyFrameD2H(buffer, frame);

  written = fwrite(buffer, 1, length, p_big_stream);
  if (written != length) {
    printf("ERROR: big written size(%u) != data length(%u)\n", (unsigned int)written, length);
  }

  delete[] buffer;

  g_decode->ReleaseBuffer(frame.buf_id);

#if 0
  if (*condv == false) {
    std::unique_lock<std::mutex> lk(mut);
    *condv = true;
    cond->notify_one();
  }
#endif
}

void eos_callback(bool* condv, std::condition_variable* cond) {
  std::cout << "eos_callback" << std::endl;
  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
  } catch (edk::Exception& err) {
    printf("set mlu env failed\n");
    return;
  }
  if (p_big_stream) {
    fflush(p_big_stream);
    fclose(p_big_stream);
    p_big_stream = NULL;
  }

  std::unique_lock<std::mutex> lk(mut);
  *condv = true;
  cond->notify_one();
}

#if 0
void perf_callback(const edk::DecodePerfInfo& perf) {
  std::cout << "----------- Decode Performance Info -----------" << std::endl;
  std::cout << "total us: " << perf.total_us << "us" << std::endl;
  std::cout << "decode us: " << perf.decode_us << "us" << std::endl;
  std::cout << "transfer us: " << perf.transfer_us << "us" << std::endl;
  std::cout << "----------- END ------------" << std::endl;
}
#endif

bool SendData(edk::EasyDecode* decode, bool _abort = false) {
  edk::CnPacket packet;
  FILE* fid;

  if (test_file == NULL) {
    std::cout << "test_file == NULL" << std::endl;
    return false;
  }

  std::string test_path = GetExePath() + test_file;
  fid = fopen(test_path.c_str(), "rb");
  if (fid == NULL) {
    return false;
  }
  fseek(fid, 0, SEEK_END);
  int64_t file_len = ftell(fid);
  rewind(fid);
  if ((file_len == 0) || (file_len > MAX_INPUT_DATA_SIZE)) {
    fclose(fid);
    return false;
  }
  packet.length = fread(g_data_buffer, 1, MAX_INPUT_DATA_SIZE, fid);
  fclose(fid);
  packet.data = g_data_buffer;
  packet.pts = 0;
  return _abort ? decode->SendData(packet, false) : decode->SendData(packet, true);
}

bool test_decode(edk::CodecType ctype, edk::PixelFmt pf, uint32_t frame_w, uint32_t frame_h,
                 std::function<void(const edk::CnFrame&)> frame_cb, bool _abort = false,
                 bool progressive_mode = false) {
  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
  } catch (edk::Exception& err) {
    std::cout << "set mlu env failed" << std::endl;
    return false;
  }

  if (ctype == edk::CodecType::H264) {
    test_file = const_cast<char*>(h264_file);
  } else if (ctype == edk::CodecType::JPEG) {
    if (progressive_mode) {
      test_file = const_cast<char*>(progressive_jpeg_file);
    } else {
      test_file = const_cast<char*>(jpeg_file);
    }
  } else {
    std::cout << "unknown codec type" << std::endl;
    return false;
  }

  rec = false;
  edk::EasyDecode::Attr attr;
  attr.frame_geometry.w = 1920;
  attr.frame_geometry.h = 1080;
  attr.codec_type = ctype;
  attr.pixel_format = pf;
  attr.frame_callback = frame_cb;
  if (frame_cb) {
    attr.eos_callback = std::bind(eos_callback, &rec, &cond);
  }
  attr.silent = false;
  std::unique_ptr<edk::EasyDecode> decode = nullptr;
  try {
    bool ret;
    decode = edk::EasyDecode::New(attr);
    auto _attr = decode->GetAttr();
    EXPECT_EQ(_attr.codec_type, ctype);
    EXPECT_EQ(_attr.pixel_format, pf);
    EXPECT_EQ(_attr.frame_geometry.w, attr.frame_geometry.w);
    EXPECT_EQ(_attr.frame_geometry.h, attr.frame_geometry.h);
    g_decode = decode.get();
    decode->Pause();
    EXPECT_EQ(decode->GetStatus(), edk::EasyDecode::Status::PAUSED);
    decode->Resume();
    EXPECT_EQ(decode->GetStatus(), edk::EasyDecode::Status::RUNNING);
    ret = SendData(g_decode, _abort);
    if (!ret) {
      std::cout << "Send Data failed" << std::endl;
      return false;
    }
    if (_abort) {
      decode->AbortDecoder();
    } else {
      std::unique_lock<std::mutex> lk(mut);
      if (nullptr != frame_cb) {
        // substream is not open but main stream callback is set,
        // wait for main stream receive is ok.
        cond.wait(lk, []() -> bool { return rec; });
        EXPECT_EQ(decode->GetStatus(), edk::EasyDecode::Status::EOS);
        if (ctype == edk::CodecType::H264) {
          EXPECT_EQ(1, decode->GetMinimumOutputBufferCount());
        }
      }
    }

    g_decode = nullptr;
  } catch (edk::Exception& err) {
    std::cout << err.what() << std::endl;
    if (nullptr != decode) {
      g_decode = nullptr;
    }
    return false;
  }
  return true;
}

TEST(Codec, DecodeH264) {
  bool ret = false;
  g_data_buffer = new uint8_t[MAX_INPUT_DATA_SIZE];

  ret = test_decode(edk::CodecType::H264, edk::PixelFmt::NV21, 1920, 1080,
                    std::bind(frame_callback, &rec, &cond, std::placeholders::_1));
  EXPECT_TRUE(ret);

  ret = test_decode(edk::CodecType::H264, edk::PixelFmt::NV12, 1920, 1080,
                    std::bind(frame_callback, &rec, &cond, std::placeholders::_1));
  EXPECT_TRUE(ret);

  ret = test_decode(edk::CodecType::H264, edk::PixelFmt::I420, 1920, 1080,
                    std::bind(frame_callback, &rec, &cond, std::placeholders::_1));
  EXPECT_TRUE(ret);
  // test wait EOS in destruct
  ret = test_decode(edk::CodecType::H264, edk::PixelFmt::NV12, 1920, 1080, nullptr);
  EXPECT_TRUE(ret);

  // test abort decode
  ret = test_decode(edk::CodecType::H264, edk::PixelFmt::NV21, 1920, 1080, nullptr, true);
  EXPECT_TRUE(ret);
  delete[] g_data_buffer;
}

TEST(Codec, DecodeJpeg) {
  bool ret = false;
  g_data_buffer = new uint8_t[MAX_INPUT_DATA_SIZE];

  ret = test_decode(edk::CodecType::JPEG, edk::PixelFmt::NV21, 1920, 1080,
                    std::bind(frame_callback, &rec, &cond, std::placeholders::_1));
  EXPECT_TRUE(ret);
  ret = test_decode(edk::CodecType::JPEG, edk::PixelFmt::NV12, 1920, 1080,
                    std::bind(frame_callback, &rec, &cond, std::placeholders::_1));
  EXPECT_TRUE(ret);

#ifdef ENABLE_TURBOJPEG
  // test progressive jpeg
  ret = test_decode(edk::CodecType::JPEG, edk::PixelFmt::NV12, 1920, 1080,
                    std::bind(frame_callback, &rec, &cond, std::placeholders::_1), false, true);
  EXPECT_TRUE(ret);
#endif

  // test abort decode
  ret = test_decode(edk::CodecType::JPEG, edk::PixelFmt::NV21, 1920, 1080, nullptr, true);
  EXPECT_TRUE(ret);
  delete[] g_data_buffer;
}

TEST(Codec, DecodeNoFrame) {
  {
    edk::EasyDecode::Attr attr;
    attr.frame_geometry.w = 1920;
    attr.frame_geometry.h = 1080;
    attr.codec_type = edk::CodecType::H264;
    attr.pixel_format = edk::PixelFmt::NV21;
    attr.frame_callback = nullptr;
    attr.eos_callback = nullptr;
    attr.silent = false;
    std::unique_ptr<edk::EasyDecode> decode = nullptr;
    decode = edk::EasyDecode::New(attr);
  }
  {
    edk::EasyDecode::Attr attr;
    attr.frame_geometry.w = 1920;
    attr.frame_geometry.h = 1080;
    attr.codec_type = edk::CodecType::JPEG;
    attr.pixel_format = edk::PixelFmt::NV21;
    attr.frame_callback = nullptr;
    attr.eos_callback = nullptr;
    attr.silent = false;
    std::unique_ptr<edk::EasyDecode> decode = nullptr;
    decode = edk::EasyDecode::New(attr);
  }
}
