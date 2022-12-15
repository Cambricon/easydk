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
#include <memory>
#include <string>

#include "glog/logging.h"

#include "cnedk_platform.h"
#include "cnedk_buf_surface.h"
#include "cnedk_buf_surface_util.hpp"

#include "test_base.h"

namespace cnedk {

const int g_device_id = 0;

static int CreateSurfacePool(void** surf_pool, int width, int height, CnedkBufSurfaceColorFormat color_format,
                            bool device = true) {
  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);

  CnedkBufSurfaceCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.batch_size = 1;
  create_params.width = width;
  create_params.height = height;
  create_params.color_format = color_format;
  create_params.device_id = g_device_id;

  if (is_edge_platform) {
    create_params.mem_type = CNEDK_BUF_MEM_VB_CACHED;
  } else {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  }

  if (CnedkBufPoolCreate(surf_pool, &create_params, 6) < 0) {
    LOG(ERROR) << "[EasyDK Tests] [Transform] CreateSurfacePool(): Create pool failed";
    return -1;
  }
  return 0;
}

TEST(BufSurfaceWrapper, Create) {
  void* pool = nullptr;
  {  // need destory
    CnedkBufSurface* surf;
    int width = 1920;
    int height = 1080;
    CnedkBufSurfaceColorFormat corlor_fmt = CNEDK_BUF_COLOR_FORMAT_NV12;

    EXPECT_EQ(CreateSurfacePool(&pool, width, height, corlor_fmt, true), 0);
    EXPECT_EQ(CnedkBufSurfaceCreateFromPool(&surf, pool), 0);
    cnedk::BufSurfWrapperPtr surf_wrapper = std::make_shared<BufSurfaceWrapper>(surf, false);
    EXPECT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    EXPECT_EQ(CnedkBufPoolDestroy(pool), 0);
    pool = nullptr;
  }
  {  // no need destory
    CnedkBufSurface* surf;
    int width = 1920;
    int height = 1080;
    CnedkBufSurfaceColorFormat corlor_fmt = CNEDK_BUF_COLOR_FORMAT_NV12;

    ASSERT_EQ(CreateSurfacePool(&pool, width, height, corlor_fmt, true), 0);
    ASSERT_EQ(CnedkBufSurfaceCreateFromPool(&surf, pool), 0);
    cnedk::BufSurfWrapperPtr surf_wrapper = std::make_shared<BufSurfaceWrapper>(surf, true);
    surf_wrapper.reset();
    EXPECT_EQ(CnedkBufPoolDestroy(pool), 0);
    pool = nullptr;
  }
}

TEST(BufSurfaceWrapper, Function) {
  void* pool = nullptr;
  CnedkBufSurface* surf;
  uint32_t width = 1920;
  uint32_t height = 1080;
  CnedkBufSurfaceColorFormat corlor_fmt = CNEDK_BUF_COLOR_FORMAT_NV12;

  ASSERT_EQ(CreateSurfacePool(&pool, width, height, corlor_fmt, true), 0);
  ASSERT_EQ(CnedkBufSurfaceCreateFromPool(&surf, pool), 0);

  surf->is_contiguous = true;
  cnedk::BufSurfWrapperPtr surf_wrapper = std::make_shared<BufSurfaceWrapper>(surf, true);
  EXPECT_EQ(surf_wrapper->GetColorFormat(), corlor_fmt);
  EXPECT_EQ(surf_wrapper->GetDeviceId(), g_device_id);
  EXPECT_EQ(surf_wrapper->GetHeight(), height);
  EXPECT_EQ(surf_wrapper->GetWidth(), width);
  EXPECT_EQ(surf_wrapper->GetBufSurface(), surf);
  EXPECT_EQ(surf_wrapper->GetNumFilled(), uint32_t(0));
  EXPECT_EQ(surf_wrapper->GetStride(0), width);
  EXPECT_EQ(surf_wrapper->GetStride(-1), uint32_t(0));
  EXPECT_EQ(surf_wrapper->GetPlaneNum(), uint32_t(2));

  EXPECT_EQ(surf_wrapper->GetPlaneNum(), uint32_t(2));
  EXPECT_EQ(surf_wrapper->GetPts(), surf->pts);
  EXPECT_EQ(surf_wrapper->GetPlaneBytes(0), uint32_t(width * 1088));
  EXPECT_EQ(surf_wrapper->GetPlaneBytes(-1), uint32_t(0));

  uint64_t target_pts = 0x1000;
  surf_wrapper->SetPts(target_pts);
  EXPECT_EQ(surf_wrapper->GetPts(), target_pts);

  uint8_t *data = static_cast<uint8_t*>(surf_wrapper->GetHostData(0, 0));
  EXPECT_NE(data, nullptr);

  data = static_cast<uint8_t*>(surf_wrapper->GetHostData(0, 0));
  EXPECT_NE(data, nullptr);

  *(data + 1) = 100;
  surf_wrapper->SyncHostToDevice(-1, -1);

  surf_wrapper->SyncHostToDevice(0, 0);
  surf_wrapper->GetMappedData(0, 0);

  CnedkBufSurface* temp_surf = surf_wrapper->BufSurfaceChown();
  EXPECT_EQ(temp_surf, surf);
  surf_wrapper->SetPts(surf->pts);
  EXPECT_EQ(surf_wrapper->GetPts(), surf->pts);

  surf_wrapper.reset();
  CnedkBufSurfaceDestroy(temp_surf);
  EXPECT_EQ(CnedkBufPoolDestroy(pool), 0);
  pool = nullptr;
}

TEST(PlatformJudge, PlatformJudge) {
  EXPECT_NE(IsEdgePlatform(g_device_id), IsCloudPlatform(g_device_id));

  CnedkPlatformInfo platform_info;
  CnedkPlatformGetInfo(g_device_id, &platform_info);

  std::string platform_name(platform_info.name);

  EXPECT_NE(IsEdgePlatform(platform_name), IsCloudPlatform(platform_name));

  platform_name = "unknow_name";
  EXPECT_EQ(IsCloudPlatform(platform_name), false);
  EXPECT_EQ(IsEdgePlatform(platform_name), false);

  int dev_id = -1;
  EXPECT_EQ(IsCloudPlatform(dev_id), false);
  EXPECT_EQ(IsEdgePlatform(dev_id), false);
}


TEST(BufPool, BufPool) {
  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);

  CnedkBufSurfaceCreateParams create_params;
  create_params.batch_size = 4;
  memset(&create_params, 0, sizeof(create_params));
  create_params.width = 1920;
  create_params.height = 1080;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
  create_params.device_id = -1;
  create_params.batch_size = 4;
  if (is_edge_platform) {
    create_params.mem_type = CNEDK_BUF_MEM_UNIFIED;
  } else {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  }
  cnedk::BufPool pool;
  cnedk::BufSurfWrapperPtr surf_wrapper = nullptr;
  surf_wrapper = pool.GetBufSurfaceWrapper();
  ASSERT_EQ(surf_wrapper, nullptr);

  ASSERT_NE(pool.CreatePool(&create_params, 1), 0);
  create_params.device_id = g_device_id;
  ASSERT_EQ(pool.CreatePool(&create_params, 1), 0);

  surf_wrapper = pool.GetBufSurfaceWrapper();
  ASSERT_NE(surf_wrapper, nullptr);
  ASSERT_EQ(surf_wrapper->GetWidth(), uint32_t(1920));
  cnedk::BufSurfWrapperPtr temp_wrapper = nullptr;
  temp_wrapper = pool.GetBufSurfaceWrapper();
  ASSERT_EQ(temp_wrapper, nullptr);

  surf_wrapper.reset();
  pool.DestroyPool(10);
  pool.DestroyPool(10);
  temp_wrapper = pool.GetBufSurfaceWrapper();
  ASSERT_EQ(temp_wrapper, nullptr);
}
}  // end namespace cnedk
