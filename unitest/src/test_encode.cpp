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

#include <chrono>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "gtest/gtest.h"
#include "opencv2/opencv.hpp"

#include "cnrt.h"

#include "cnedk_encode.h"
#include "cnedk_platform.h"

#include "test_base.h"

static const char *test_1080p_jpg = "../../unitest/data/1080p.jpg";
static const char *test_500x500_jpg = "../../unitest/data/500x500.jpg";

const size_t device_id = 0;

class TestEncode {
 public:
  explicit TestEncode(int dev_id, uint32_t frame_w, uint32_t frame_h, CnedkVencType type,
                      CnedkBufSurfaceColorFormat color_format = CNEDK_BUF_COLOR_FORMAT_NV12);
  ~TestEncode();

  int Start();

  int SendData(CnedkBufSurface *surf, int timeout_ms);

 public:
  static int OnFrameBits_(CnedkVEncFrameBits *framebits, void *userdata) {
    TestEncode *thiz = reinterpret_cast<TestEncode *>(userdata);
    return thiz->OnFrameBits(framebits);
  }
  static int OnEos_(void *userdata) {
    TestEncode *thiz = reinterpret_cast<TestEncode *>(userdata);
    return thiz->OnEos();
  }
  static int OnError_(int errcode, void *userdata) {
    TestEncode *thiz = reinterpret_cast<TestEncode *>(userdata);
    return thiz->OnError(errcode);
  }

 private:
  int OnFrameBits(CnedkVEncFrameBits *framebits);
  int OnEos();
  int OnError(int errcode);

 private:
  bool send_done_ = false;
  void *venc_ = nullptr;
  CnedkVencCreateParams params_;

  std::condition_variable enc_cond_;
  bool eos_send_ = false;
  bool encode_done_ = false;
  std::mutex mut_;

  size_t frame_count_ = 0;

  FILE *p_output_file_ = nullptr;
};

TestEncode::TestEncode(int dev_id, uint32_t frame_w, uint32_t frame_h, CnedkVencType type,
                       CnedkBufSurfaceColorFormat color_format) {
  params_.type = type;
  params_.device_id = dev_id;
  params_.width = frame_w;
  params_.height = frame_h;

  params_.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;

  params_.frame_rate = 0;
  params_.key_interval = 0;
  params_.input_buf_num = 2;
  params_.gop_size = 30;
  params_.bitrate = 0x4000000;
  params_.OnFrameBits = TestEncode::OnFrameBits_;
  params_.OnEos = TestEncode::OnEos_;
  params_.OnError = TestEncode::OnError_;
  params_.userdata = this;
}

TestEncode::~TestEncode() {
  if (!eos_send_) {
    SendData(nullptr, 5000);
  }
  CnedkVencDestroy(venc_);
}

int TestEncode::Start() {
  int ret = CnedkVencCreate(&venc_, &params_);
  if (ret < 0) {
    LOG(ERROR) << "[EasyDK Tests] [Encode] Create encode failed";
  }
  return ret;
}

int TestEncode::SendData(CnedkBufSurface *surf, int timeout_ms) {
  if (!surf) {                                             // send eos
    if (CnedkVencSendFrame(venc_, nullptr, timeout_ms) < 0) {  // send eos
      LOG(ERROR) << "[EasyDK Tests] [Encode] Send EOS failed";
      return -1;
    }

    {
      std::unique_lock<std::mutex> lk(mut_);
      enc_cond_.wait(lk, [&]() -> bool { return encode_done_; });
    }
    eos_send_ = true;
    return 0;
  }

  if (CnedkVencSendFrame(venc_, surf, timeout_ms) < 0) {
    LOG(ERROR) << "[EasyDK Tests] [Encode] Send Frame failed";
    return -1;
  }
  return 0;
}

int TestEncode::OnEos() {
  if (p_output_file_) {
    fflush(p_output_file_);
    fclose(p_output_file_);
    p_output_file_ = NULL;
  }

  {
    std::unique_lock<std::mutex> lk(mut_);
    encode_done_ = true;
    enc_cond_.notify_all();
  }

  return 0;
}

int TestEncode::OnFrameBits(CnedkVEncFrameBits *framebits) {
  char *output_file = NULL;
  char str[256] = {0};

  size_t length = framebits->len;

  if (params_.type == CNEDK_VENC_TYPE_JPEG) {
    snprintf(str, sizeof(str), "./encoded_%d_%d_%02lu.jpg", params_.width, params_.height, frame_count_);
    output_file = str;
  } else if (params_.type == CNEDK_VENC_TYPE_H264) {
    snprintf(str, sizeof(str), "./encoded_%d_%d_%lu.h264", params_.width, params_.height, length);
    output_file = str;
  } else if (params_.type == CNEDK_VENC_TYPE_H265) {
    snprintf(str, sizeof(str), "./encoded_%d_%d_%lu.h265", params_.width, params_.height, length);
    output_file = str;
  } else {
    LOG(ERROR) << "[EasyDK Tests] [Encode] Unsupported output codec type: " << params_.type;
  }

  if (p_output_file_ == NULL) p_output_file_ = fopen(output_file, "wb");
  if (p_output_file_ == NULL) {
    LOG(ERROR) << "[EasyDK Tests] [Encode] Open output file failed";
    return -1;
  }

  size_t written;
  written = fwrite(framebits->bits, 1, length, p_output_file_);
  if (written != length) {
    LOG(ERROR) << "[EasyDK Tests] [Encode] Written size " << written << " != data length " << length;
    return -1;
  }

  return 0;
}

int TestEncode::OnError(int err_code) { return -1; }

#define ALIGN(w, a) ((w + a - 1) & ~(a - 1))
static bool CvtBgrToYuv420sp(const cv::Mat &bgr_image, uint32_t alignment, CnedkBufSurface *surf) {
  cv::Mat yuv_i420_image;
  uint32_t width, height, stride;
  uint8_t *src_y, *src_u, *src_v, *dst_y, *dst_u;
  // uint8_t *dst_v;

  cv::cvtColor(bgr_image, yuv_i420_image, cv::COLOR_BGR2YUV_I420);

  width = bgr_image.cols;
  height = bgr_image.rows;
  if (alignment > 0)
    stride = ALIGN(width, alignment);
  else
    stride = width;

  uint32_t y_len = stride * height;
  src_y = yuv_i420_image.data;
  src_u = yuv_i420_image.data + y_len;
  src_v = yuv_i420_image.data + y_len * 5 / 4;

  if (surf->mem_type == CNEDK_BUF_MEM_VB_CACHED || surf->mem_type == CNEDK_BUF_MEM_VB) {
    dst_y = reinterpret_cast<uint8_t *>(surf->surface_list[0].mapped_data_ptr);
    dst_u = reinterpret_cast<uint8_t *>(reinterpret_cast<uint64_t>(surf->surface_list[0].mapped_data_ptr) + y_len);
  } else {
    dst_y = reinterpret_cast<uint8_t *>(surf->surface_list[0].data_ptr);
    dst_u = reinterpret_cast<uint8_t *>(reinterpret_cast<uint64_t>(surf->surface_list[0].data_ptr) + y_len);
  }
  for (uint32_t i = 0; i < height; i++) {
    // y data
    memcpy(dst_y + i * stride, src_y + i * width, width);
    // uv data
    if (i % 2 == 0) {
      for (uint32_t j = 0; j < width / 2; j++) {
        if (surf->surface_list->color_format == CNEDK_BUF_COLOR_FORMAT_NV21) {
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

int ProcessEncode(std::string file, CnedkVencType type, CnedkBufSurfaceMemType mem_type,
                  CnedkBufSurfaceColorFormat fmt, uint32_t frame_w, uint32_t frame_h) {
  int ret;
  bool is_edge_platform = cnedk::IsEdgePlatform(device_id);

  if (is_edge_platform) {
    if (fmt == CNEDK_BUF_COLOR_FORMAT_BGR) {
      LOG(ERROR) << "[EasyDK Tests] [Encode] Unsupported color format: " << fmt;
      return 0;
    }
    if (mem_type == CNEDK_BUF_MEM_SYSTEM) {
      LOG(ERROR) << "[EasyDK Tests] [Encode] Unsupported mem type: " << fmt;
      return 0;
    }
  }

  TestEncode encode(device_id, frame_w, frame_h, type, fmt);

  ret = encode.Start();
  if (ret < 0) {
    LOG(ERROR) << "[EasyDK Tests] [Encode] Start encode failed";
    return -1;
  }

  // read data
  std::string test_path = GetExePath() + file;
  cv::Mat mat = cv::imread(test_path);

  // create bufsurface
  CnedkBufSurface *surf = nullptr;
  void *surf_pool = nullptr;

  if (is_edge_platform) {
    CnedkBufSurfaceCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));
    create_params.batch_size = 1;
    create_params.width = mat.cols;
    create_params.height = mat.rows;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
    create_params.device_id = device_id;
    create_params.mem_type = CNEDK_BUF_MEM_VB_CACHED;
    if (CnedkBufPoolCreate(&surf_pool, &create_params, 6) < 0) {
      LOG(ERROR) << "[EasyDK Tests] [Encode] Create pool failed";
      return -1;
    }

    if (CnedkBufSurfaceCreateFromPool(&surf, surf_pool) < 0) {
      LOG(ERROR) << "[EasyDK Tests] [Encode] Get BufSurface failed";
      return -1;
    }
  } else {
    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 1;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id = device_id;
    create_params.batch_size = 1;
    create_params.width = mat.cols;
    create_params.height = mat.rows;
    create_params.color_format = fmt;
    create_params.mem_type = mem_type;

    if (CnedkBufSurfaceCreate(&surf, &create_params) < 0) {
      return -1;
    }
  }

  if (mem_type == CNEDK_BUF_MEM_SYSTEM) {
    if (surf->surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_BGR) {
      memcpy(surf->surface_list[0].data_ptr, mat.data, mat.cols * mat.rows * 3);
    } else if (surf->surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_NV12) {
      CvtBgrToYuv420sp(mat, 0, surf);
    }
  } else {
    if (surf->surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_BGR) {
      cnrtMemcpy(surf->surface_list[0].data_ptr, mat.data, mat.cols * mat.rows * 3, cnrtMemcpyHostToDev);
    } else if (surf->surface_list[0].color_format == CNEDK_BUF_COLOR_FORMAT_NV12) {
      if (is_edge_platform) {
        CvtBgrToYuv420sp(mat, 0, surf);
      } else {
        CnedkBufSurfaceCreateParams create_params;
        create_params.batch_size = 1;
        memset(&create_params, 0, sizeof(create_params));
        create_params.device_id = device_id;
        create_params.batch_size = 1;
        create_params.width = mat.cols;
        create_params.height = mat.rows;
        create_params.color_format = fmt;
        create_params.mem_type = CNEDK_BUF_MEM_SYSTEM;

        CnedkBufSurface *cpu_surf;
        if (CnedkBufSurfaceCreate(&cpu_surf, &create_params) < 0) {
          LOG(ERROR) << "[EasyDK Tests] [Encode] Create BufSurface failed";
          return -1;
        }
        CvtBgrToYuv420sp(mat, 0, cpu_surf);
        CnedkBufSurfaceCopy(cpu_surf, surf);
        CnedkBufSurfaceDestroy(cpu_surf);
      }
    }
  }

  CnedkBufSurfaceSyncForDevice(surf, 0, 0);

  EXPECT_EQ(encode.SendData(surf, 5000), 0);

  EXPECT_EQ(encode.SendData(nullptr, 5000), 0);

  if (surf) {
    EXPECT_EQ(CnedkBufSurfaceDestroy(surf), 0);
  }

  if (surf_pool) {
    EXPECT_EQ(CnedkBufPoolDestroy(surf_pool), 0);
  }
  return 0;
}

TEST(Encode, H264) {
  CnedkPlatformInfo platform_info;
  CnedkPlatformGetInfo(device_id, &platform_info);
  std::string platform_name(platform_info.name);
  std::vector<std::string> file_name_ext_vec;
  if (platform_name.rfind("MLU5", 0) == 0) {
    return;
  }

  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_BGR,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_BGR,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_NV12,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_NV12,
                          1920, 1080), 0);

  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_BGR,
                          500, 500), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_BGR,
                          500, 500), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_NV12,
                          500, 500), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_NV12,
                          500, 500), 0);

  EXPECT_EQ(ProcessEncode(test_500x500_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_BGR,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_500x500_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_BGR,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_500x500_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_NV12,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_500x500_jpg, CNEDK_VENC_TYPE_H264, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_NV12,
                          1920, 1080), 0);
}

TEST(Encode, H265) {
  CnedkPlatformInfo platform_info;
  CnedkPlatformGetInfo(device_id, &platform_info);
  std::string platform_name(platform_info.name);
  std::vector<std::string> file_name_ext_vec;
  if (platform_name.rfind("MLU5", 0) == 0) {
    return;
  }

  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H265, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_BGR,
                          500, 500), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H265, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_BGR,
                          500, 500), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H265, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_NV12,
                          500, 500), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_H265, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_NV12,
                          500, 500), 0);
}

TEST(Encode, Jpeg) {
  EXPECT_EQ(ProcessEncode(test_500x500_jpg, CNEDK_VENC_TYPE_JPEG, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_BGR,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_500x500_jpg, CNEDK_VENC_TYPE_JPEG, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_BGR,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_JPEG, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_NV12,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_JPEG, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_NV12,
                          1920, 1080), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_JPEG, CNEDK_BUF_MEM_SYSTEM, CNEDK_BUF_COLOR_FORMAT_NV12,
                          500, 500), 0);
  EXPECT_EQ(ProcessEncode(test_1080p_jpg, CNEDK_VENC_TYPE_JPEG, CNEDK_BUF_MEM_DEVICE, CNEDK_BUF_COLOR_FORMAT_NV12,
                          500, 500), 0);
}

TEST(Encode, CreateDestory) {
  bool is_edge_platform = cnedk::IsEdgePlatform(device_id);
  void* venc = nullptr;
  EXPECT_NE(CnedkVencCreate(&venc, nullptr), 0);

  CnedkVencCreateParams params;
  params.type = CNEDK_VENC_TYPE_H264;
  params.device_id = device_id;
  params.width = 1920;
  params.height = 1080;
  params.color_format = CNEDK_BUF_COLOR_FORMAT_NV21;
  params.frame_rate = 0;
  params.key_interval = 0;
  params.input_buf_num = 2;
  params.gop_size = 30;
  params.bitrate = 0x4000000;
  params.OnFrameBits = TestEncode::OnFrameBits_;
  params.OnEos = TestEncode::OnEos_;
  params.OnError = TestEncode::OnError_;
  params.userdata = nullptr;

  EXPECT_NE(CnedkVencCreate(nullptr, &params), 0);

  params.type = CNEDK_VENC_TYPE_INVALID;
  EXPECT_NE(CnedkVencCreate(&venc, &params), 0);

  params.type = CNEDK_VENC_TYPE_H264;
  params.OnEos = nullptr;
  EXPECT_NE(CnedkVencCreate(&venc, &params), 0);

  EXPECT_NE(CnedkVencDestroy(venc), 0);

  params.OnEos = TestEncode::OnEos_;
  params.type = CNEDK_VENC_TYPE_NUM;
  EXPECT_NE(CnedkVencCreate(&venc, &params), 0);
  EXPECT_NE(CnedkVencDestroy(venc), 0);

  params.device_id = -1;
  params.type = CNEDK_VENC_TYPE_H264;
  EXPECT_NE(CnedkVencCreate(&venc, &params), 0);
  EXPECT_NE(CnedkVencDestroy(venc), 0);

  if (!is_edge_platform) {
    params.device_id = 0;
    params.color_format = CNEDK_BUF_COLOR_FORMAT_INVALID;
    EXPECT_NE(CnedkVencCreate(&venc, &params), 0);
    EXPECT_NE(CnedkVencDestroy(venc), 0);
  }

  EXPECT_NE(CnedkVencSendFrame(nullptr, nullptr, 0), 0);
}
