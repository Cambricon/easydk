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
#include <gtest/gtest.h>

#include <chrono>
#include <condition_variable>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "glog/logging.h"
#include "cnrt.h"

#include "cnedk_decode.h"

#include "ffmpeg_demuxer.h"
#include "test_base.h"

static const char *jpeg_file = "../../unitest/data/1080p.jpg";
static const char *corrupt_jpeg_file = "../../unitest/data/A_6000x3374.jpg";
static const char *h264_file = "../../unitest/data/img.h264";
static const char *h265_file = "../../unitest/data/img.hevc";

std::condition_variable cond;
std::mutex mut;
bool decode_done = false;

#ifndef MAX_INPUT_DATA_SIZE
#define MAX_INPUT_DATA_SIZE (25 << 20)
#endif

FILE* p_big_stream = NULL;

static const size_t g_device_id = 0;

static uint8_t* g_data_buffer;
static void* g_surf_pool = nullptr;

bool SendData(void* vdec, CnedkVdecType type, std::string file, bool test_crush = false) {
  if (file == "") {
    LOG(INFO) << "[EasyDK Tests] [Decode] SendData(): Send EOS";
    CnedkVdecStream stream;
    stream.bits = nullptr;
    stream.len = 0;
    stream.pts = 0;
    if (CnedkVdecSendStream(vdec, &stream, 5000) != 0) {  // send eos
      LOG(ERROR) << "[EasyDK Tests] [Decode] Send Eos Failed";
      return false;
    }
    return true;
  }

  uint64_t pts = 0;
  std::string test_path = GetExePath() + file;

  if (type == CNEDK_VDEC_TYPE_JPEG) {
    FILE* fid;
    fid = fopen(test_path.c_str(), "rb");
    if (fid == NULL) {
      LOG(ERROR) << "[EasyDK Tests] [Decode] SendData(): Open file failed";
      return false;
    }
    fseek(fid, 0, SEEK_END);
    int64_t file_len = ftell(fid);
    rewind(fid);
    if ((file_len == 0) || (file_len > MAX_INPUT_DATA_SIZE)) {
      fclose(fid);
      LOG(ERROR) << "[EasyDK Tests] [Decode] SendData(): File length is 0";
      return false;
    }

    int length = fread(g_data_buffer, 1, MAX_INPUT_DATA_SIZE, fid);
    fclose(fid);

    CnedkVdecStream stream;
    memset(&stream, 0, sizeof(stream));

    stream.bits = g_data_buffer;
    stream.len = length;
    stream.pts = pts++;
    if (CnedkVdecSendStream(vdec, &stream, 5000) < 0) {
      LOG(ERROR) << "[EasyDK Tests] [Decode] SendData(): Send data failed";
      return false;
    }
    return true;
  }

  std::unique_ptr<FFmpegDemuxer> demuxer;
  demuxer = std::unique_ptr<FFmpegDemuxer>{new FFmpegDemuxer(test_path.c_str())};
  int corrupt_time = 0;
  while (1) {
    int data_len = 0;
    if (!demuxer->ReadFrame(reinterpret_cast<uint8_t**>(&g_data_buffer), reinterpret_cast<int*>(&data_len))) {
      break;
    }

    if (test_crush == true) {
      if (corrupt_time++ == 3) test_crush = false;
      continue;
    }

    CnedkVdecStream stream;
    memset(&stream, 0, sizeof(stream));

    stream.bits = g_data_buffer;
    stream.len = data_len;
    stream.pts = pts++;
    int retry = 5;
    while ((CnedkVdecSendStream(vdec, &stream, 5000) < 0) && (retry-- > 0)) {
      std::this_thread::sleep_for(std::chrono::microseconds(500));
    }
    if (retry < 0) return false;
  }

  return true;
}

static int CreateSurfacePool(void** surf_pool, int width, int height, CnedkBufSurfaceColorFormat fmt) {
  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);

  CnedkBufSurfaceCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.batch_size = 1;
  create_params.width = width;
  create_params.height = height;
  create_params.color_format = fmt;
  create_params.device_id = g_device_id;

  if (is_edge_platform) {
    create_params.mem_type = CNEDK_BUF_MEM_VB_CACHED;
  } else {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  }

  if (CnedkBufPoolCreate(surf_pool, &create_params, 6) < 0) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] CreateSurfacePool(): Create pool failed";
    return -1;
  }

  return 0;
}

int GetBufSurface(CnedkBufSurface** surf, int width, int height, CnedkBufSurfaceColorFormat fmt, int timeout_ms,
                  void* user_data) {
  if (g_surf_pool) {
    if (CnedkBufSurfaceCreateFromPool(surf, g_surf_pool) < 0) {
      LOG(ERROR) << "[EasyDK Tests] [Decode] GetBufSurface(): Get BufSurface from pool failed";
      return -1;
    }
    return 0;
  } else {
    bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);

    CnedkBufSurfaceCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));
    create_params.batch_size = 1;
    create_params.device_id = g_device_id;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = fmt;

    if (is_edge_platform) {
      create_params.mem_type = CNEDK_BUF_MEM_VB_CACHED;
    } else {
      create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
    }

    if (CnedkBufSurfaceCreate(surf, &create_params) < 0) {
      LOG(ERROR) << "[EasyDK Tests] [Decode] GetBufSurface(): Create BufSurface failed";
      return -1;
    }
  }
  return 0;
}

int OnFrame(CnedkBufSurface* surf, void* user_data) {
  surf->surface_list[0].width -= surf->surface_list[0].width & 1;
  surf->surface_list[0].height -= surf->surface_list[0].height & 1;
  surf->surface_list[0].plane_params.width[0] -= surf->surface_list[0].plane_params.width[0] & 1;
  surf->surface_list[0].plane_params.height[0] -= surf->surface_list[0].plane_params.height[0] & 1;
  surf->surface_list[0].plane_params.width[1] -= surf->surface_list[0].plane_params.width[1] & 1;
  surf->surface_list[0].plane_params.height[1] -= surf->surface_list[0].plane_params.height[1] & 1;

  if (p_big_stream == NULL) {
    p_big_stream = fopen("big.yuv", "wb");
    if (p_big_stream == NULL) {
      return -1;
    }
  }

  size_t length = surf->surface_list[0].data_size;

  uint8_t* buffer = new uint8_t[length];
  if (!buffer) return -1;

  cnrtMemcpy(buffer, surf->surface_list[0].data_ptr, surf->surface_list[0].width * surf->surface_list[0].height,
             cnrtMemcpyDevToHost);
  cnrtMemcpy(buffer + surf->surface_list[0].width * surf->surface_list[0].height,
             reinterpret_cast<void*>(reinterpret_cast<uint64_t>(surf->surface_list[0].data_ptr) +
                                     surf->surface_list[0].width * surf->surface_list[0].height),
             surf->surface_list[0].width * surf->surface_list[0].height / 2, cnrtMemcpyDevToHost);

  size_t written;
  written = fwrite(buffer, 1, length, p_big_stream);
  if (written != length) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] Big written size " << written << " != data length " << length;
  }

  CnedkBufSurfaceDestroy(surf);

  delete[] buffer;
  return 0;
}

int OnEos(void* user_data) {
  LOG(INFO) << "[EasyDK Tests] [Decode] OnEos" << std::endl;
  if (p_big_stream) {
    fflush(p_big_stream);
    fclose(p_big_stream);
    p_big_stream = NULL;
  }
  decode_done = true;
  cond.notify_one();
  return 0;
}

int OnError(int err_code, void* user_data) {
  LOG(INFO) << "[EasyDK Tests] [Decode] OnError";
  return 0;
}

bool Create(void** vdec, CnedkVdecType type, uint32_t frame_w, uint32_t frame_h, CnedkBufSurfaceColorFormat fmt) {
  cnrtSetDevice(g_device_id);
  CnedkVdecCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = g_device_id;

  create_params.max_width = frame_w;
  create_params.max_height = frame_h;
  create_params.frame_buf_num = 33;  // for CE3226
  create_params.surf_timeout_ms = 5000;
  create_params.userdata = nullptr;
  create_params.GetBufSurf = GetBufSurface;
  create_params.OnFrame = OnFrame;
  create_params.OnEos = OnEos;
  create_params.OnError = OnError;
  create_params.type = type;
  create_params.color_format = fmt;

  int ret = CnedkVdecCreate(vdec, &create_params);
  if (ret) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] Create decoder failed";
    return false;
  }
  LOG(INFO) << "[EasyDK Tests] [Decode] Create decoder done";
  return true;
}

int TestDecode(std::string file, CnedkVdecType type, uint32_t frame_w, uint32_t frame_h,
               CnedkBufSurfaceColorFormat fmt = CNEDK_BUF_COLOR_FORMAT_NV21, bool test_crush = false) {
  void* decode;
  int ret = 0;
  decode_done = false;
  std::unique_ptr<uint8_t[]> buffer = std::unique_ptr<uint8_t[]>{new uint8_t[MAX_INPUT_DATA_SIZE]};
  g_data_buffer = buffer.get();
  if (!g_data_buffer) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] malloc cpu data failed";
    return -1;
  }

  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);

  if (is_edge_platform) {
    if (CreateSurfacePool(&g_surf_pool, frame_w, frame_h, fmt) < 0) {
      LOG(ERROR) << "[EasyDK Tests] [Decode] Create surface pool failed";
      return -1;
    }
  }

  if (!Create(&decode, type, frame_w, frame_h, fmt)) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] Create decode failed";
    if (is_edge_platform) {
      CnedkBufPoolDestroy(g_surf_pool);
      g_surf_pool = nullptr;
    }
    return -1;
  }

  if (!SendData(decode, type, file)) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] Send Data failed";
    ret = -1;
  }

  if (!SendData(decode, type, "")) {  // send eos
    LOG(ERROR) << "[EasyDK Tests] [Decode] Send Eos failed";
    ret = -1;
  }

  {
    std::unique_lock<std::mutex> lk(mut);
    cond.wait(lk, []() -> bool { return decode_done; });
  }
  if (CnedkVdecDestroy(decode) < 0) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] Destory decode failed";
    ret = -1;
  }

  if (is_edge_platform) {
    CnedkBufPoolDestroy(g_surf_pool);
    g_surf_pool = nullptr;
  }

  return ret;
}

TEST(Decode, Jpeg) {
  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);
  EXPECT_EQ(TestDecode(jpeg_file, CNEDK_VDEC_TYPE_JPEG, 1920, 1080), 0);
  if (!is_edge_platform) {
    EXPECT_NE(TestDecode(corrupt_jpeg_file, CNEDK_VDEC_TYPE_JPEG, 1920, 1080), 0);
  }
}

TEST(Decode, SendError) {
  void* decode;
  bool ret;
  decode_done = false;
  int frame_w = 1920;
  int frame_h = 1080;
  std::string file = h265_file;
  CnedkVdecType type = CNEDK_VDEC_TYPE_H265;
  CnedkBufSurfaceColorFormat fmt = CNEDK_BUF_COLOR_FORMAT_NV21;

  std::unique_ptr<uint8_t[]> buffer = std::unique_ptr<uint8_t[]>{new uint8_t[MAX_INPUT_DATA_SIZE]};
  g_data_buffer = buffer.get();
  if (!g_data_buffer) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] malloc cpu data failed";
    return;
  }

  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);

  if (is_edge_platform) {
    int res = CreateSurfacePool(&g_surf_pool, frame_w, frame_h, fmt);
    EXPECT_EQ(res, 0);
    if (res != 0) {
      LOG(ERROR) << "[EasyDK Tests] [Decode] Create surface pool failed";
      return;
    }
  }

  ret = Create(&decode, type, frame_w, frame_h, fmt);
  EXPECT_TRUE(ret);
  if (!ret) {
    LOG(ERROR) << "[EasyDK Tests] [Decode] Create decode failed";
    if (is_edge_platform) {
      CnedkBufPoolDestroy(g_surf_pool);
      g_surf_pool = nullptr;
    }
    return;
  }
  CnedkVdecStream stream;
  EXPECT_NE(CnedkVdecSendStream(nullptr, &stream, 5000), 0);

  EXPECT_TRUE(SendData(decode, type, file));

  EXPECT_TRUE(SendData(decode, type, ""));

  EXPECT_FALSE(SendData(decode, type, file));  // error send

  EXPECT_TRUE(SendData(decode, type, ""));  // send eos error
  {
    std::unique_lock<std::mutex> lk(mut);
    cond.wait(lk, []() -> bool { return decode_done; });
  }
  EXPECT_EQ(CnedkVdecDestroy(decode), 0);

  if (is_edge_platform) {
    CnedkBufPoolDestroy(g_surf_pool);
    g_surf_pool = nullptr;
  }
}

TEST(Decode, H264) {
  EXPECT_EQ(TestDecode(h264_file, CNEDK_VDEC_TYPE_H264, 1920, 1080), 0);
  EXPECT_EQ(TestDecode(h264_file, CNEDK_VDEC_TYPE_H264, 1920, 1080,
                       CNEDK_BUF_COLOR_FORMAT_NV21, true), 0);
}

TEST(Decode, H265) {
  bool is_cloud_platform = cnedk::IsCloudPlatform(g_device_id);

  EXPECT_EQ(TestDecode(h265_file, CNEDK_VDEC_TYPE_H265, 1280, 720), 0);
  if (is_cloud_platform) {
    EXPECT_EQ(TestDecode(h265_file, CNEDK_VDEC_TYPE_H265, 1280, 720,
                          CNEDK_BUF_COLOR_FORMAT_NV12, true), 0);
  }
  EXPECT_EQ(TestDecode(h265_file, CNEDK_VDEC_TYPE_H265, 3840, 2160), 0);

  if (is_cloud_platform) {
    EXPECT_EQ(TestDecode(h265_file, CNEDK_VDEC_TYPE_H265, 3840, 2160, CNEDK_BUF_COLOR_FORMAT_NV12), 0);
  }
}

TEST(Decode, CreatDestory) {
  void* vdec = nullptr;
  EXPECT_NE(CnedkVdecCreate(&vdec, nullptr), 0);

  cnrtSetDevice(g_device_id);
  CnedkVdecCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = g_device_id;

  create_params.max_width = 1920;
  create_params.max_height = 1080;
  create_params.frame_buf_num = 18;  // for CE3226
  create_params.surf_timeout_ms = 5000;
  create_params.userdata = nullptr;
  create_params.GetBufSurf = GetBufSurface;
  create_params.OnFrame = OnFrame;
  create_params.OnEos = OnEos;
  create_params.OnError = OnError;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV21;

  EXPECT_NE(CnedkVdecCreate(nullptr, &create_params), 0);

  create_params.type = CNEDK_VDEC_TYPE_INVALID;
  // invalid type
  EXPECT_NE(CnedkVdecCreate(&vdec, &create_params), 0);

  create_params.type = CNEDK_VDEC_TYPE_H264;
  create_params.device_id = -1;
  EXPECT_NE(CnedkVdecCreate(&vdec, &create_params), 0);

  create_params.device_id = g_device_id;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_BGR;
  EXPECT_NE(CnedkVdecCreate(&vdec, &create_params), 0);

  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV21;
  create_params.OnEos = nullptr;
  EXPECT_NE(CnedkVdecCreate(&vdec, &create_params), 0);

  create_params.OnEos = OnEos;
  create_params.GetBufSurf = nullptr;
  EXPECT_NE(CnedkVdecCreate(&vdec, &create_params), 0);

  EXPECT_NE(CnedkVdecDestroy(vdec), 0);
}

