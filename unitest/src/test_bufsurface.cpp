/*************************************************************************
 * Copyright (C) [2022] by Cambricon, Inc. All rights reserved
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

#include <algorithm>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

#include "cnrt.h"

#include "cnedk_buf_surface.h"
#include "cnedk_platform.h"

#include "test_base.h"

static const size_t device_id = 0;

static std::vector<CnedkBufSurfaceColorFormat> g_fmts {
    CNEDK_BUF_COLOR_FORMAT_GRAY8,
    CNEDK_BUF_COLOR_FORMAT_GRAY8,
    CNEDK_BUF_COLOR_FORMAT_YUV420,
    CNEDK_BUF_COLOR_FORMAT_NV12,
    CNEDK_BUF_COLOR_FORMAT_NV21,
    CNEDK_BUF_COLOR_FORMAT_ARGB,
    CNEDK_BUF_COLOR_FORMAT_ABGR,
    CNEDK_BUF_COLOR_FORMAT_RGB,
    CNEDK_BUF_COLOR_FORMAT_BGR,
    CNEDK_BUF_COLOR_FORMAT_BGRA,
    CNEDK_BUF_COLOR_FORMAT_RGBA,
    CNEDK_BUF_COLOR_FORMAT_ARGB1555,
};

TEST(BufSurface, PoolCreateDestory) {
  {  // create destory failed
    EXPECT_NE(CnedkBufPoolCreate(nullptr, nullptr, 6), 0);
    EXPECT_NE(CnedkBufSurfaceCreate(nullptr, nullptr), 0);
    EXPECT_NE(CnedkBufSurfaceDestroy(nullptr), 0);
    EXPECT_NE(CnedkBufPoolDestroy(nullptr), 0);
    // int invailed_pool = 1;
    // EXPECT_NE(CnedkBufPoolDestroy(&invailed_pool), 0);

    // CnedkBufSurface surf;
    // EXPECT_NE(CnedkBufSurfaceDestroy(&surf), 0);
    void* pool = nullptr;
    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 4;
    memset(&create_params, 0, sizeof(create_params));
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.mem_type = CnedkBufSurfaceMemType(-1);
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_INVALID;
    create_params.device_id = 0;
    create_params.batch_size = 4;

    EXPECT_NE(CnedkBufPoolCreate(&pool, &create_params, 6), 0);

    // create_params.mem_type = CNEDK_BUF_MEM_SYSTEM;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
    create_params.device_id = -1;
    EXPECT_NE(CnedkBufPoolCreate(&pool, &create_params, 6), 0);
  }

  {
    void* pool = nullptr;
    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 4;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id = device_id;
    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_ARGB;
    create_params.mem_type = CNEDK_BUF_MEM_DEFAULT;

    EXPECT_EQ(CnedkBufPoolCreate(&pool, &create_params, 1), 0);
    ASSERT_NE(CnedkBufSurfaceCreateFromPool(nullptr, pool), 0);
    CnedkBufSurface* surf;
    ASSERT_EQ(CnedkBufSurfaceCreateFromPool(&surf, pool), 0);
    CnedkBufSurface* surf_1;
    ASSERT_NE(CnedkBufSurfaceCreateFromPool(&surf_1, pool), 0);
    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    ASSERT_EQ(CnedkBufPoolDestroy(pool), 0);
  }

  for (auto& fmt : g_fmts) {
    bool is_edge_platform = cnedk::IsEdgePlatform(device_id);

    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 4;
    memset(&create_params, 0, sizeof(create_params));
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = fmt;
    create_params.device_id = device_id;
    create_params.batch_size = 4;
    if (is_edge_platform) {
      create_params.mem_type = CNEDK_BUF_MEM_UNIFIED;
    } else {
      create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
    }

    void* surface_pool;

    ASSERT_EQ(CnedkBufPoolCreate(&surface_pool, &create_params, 6), 0);
    ASSERT_TRUE(surface_pool);

    CnedkBufSurface* surf;
    ASSERT_EQ(CnedkBufSurfaceCreateFromPool(&surf, surface_pool), 0);

    if (is_edge_platform) {
      ASSERT_EQ(surf->mem_type, CNEDK_BUF_MEM_UNIFIED);
    } else {
      ASSERT_EQ(surf->mem_type, CNEDK_BUF_MEM_DEVICE);
    }

    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    ASSERT_EQ(CnedkBufPoolDestroy(surface_pool), 0);
  }
}

TEST(BufSurface, CreateDestory) {
  {
    CnedkBufSurface* surf = nullptr;
    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 4;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id = device_id;
    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_ARGB;
    create_params.mem_type = CNEDK_BUF_MEM_SYSTEM;
    ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    surf = nullptr;

    create_params.batch_size = 0;
    ASSERT_NE(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    ASSERT_NE(CnedkBufSurfaceDestroy(surf), 0);
    surf = nullptr;
    create_params.batch_size = 1;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_INVALID;
    ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    surf = nullptr;
    create_params.size = 1920 * 1080 * 3;
    ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    surf = nullptr;

    bool is_edge_platform = cnedk::IsEdgePlatform(device_id);
    if (is_edge_platform) {
      create_params.mem_type = CNEDK_BUF_MEM_UNIFIED;
    } else {
      create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
    }
    ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    surf = nullptr;

    create_params.mem_type = CNEDK_BUF_MEM_DEFAULT;
    ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
    surf = nullptr;
  }

  {
    CnedkBufSurface* surf = nullptr;
    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 4;
    memset(&create_params, 0, sizeof(create_params));
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.mem_type = CnedkBufSurfaceMemType(-1);
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_INVALID;
    create_params.device_id = 0;
    create_params.batch_size = 4;
    ASSERT_NE(CnedkBufSurfaceCreate(&surf, &create_params), 0);

    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_NV12;
    create_params.device_id = 0;
    ASSERT_NE(CnedkBufSurfaceCreate(&surf, &create_params), 0);
  }

  for (auto& fmt : g_fmts) {
    bool is_edge_platform = cnedk::IsEdgePlatform(device_id);

    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 4;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id = device_id;
    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = fmt;
    if (is_edge_platform) {
      create_params.mem_type = CNEDK_BUF_MEM_UNIFIED;
    } else {
      create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
    }

    CnedkBufSurface* surf;

    ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
    ASSERT_TRUE(surf);

    if (is_edge_platform) {
      ASSERT_EQ(surf->mem_type, CNEDK_BUF_MEM_UNIFIED);
    } else {
      ASSERT_EQ(surf->mem_type, CNEDK_BUF_MEM_DEVICE);
    }

    ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
  }
}

TEST(BufSurface, SyncCpu) {
  bool is_edge_platform = cnedk::IsEdgePlatform(device_id);

  CnedkBufSurfaceCreateParams create_params;
  create_params.batch_size = 1;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = device_id;
  create_params.batch_size = 1;
  create_params.width = 1920;
  create_params.height = 1080;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_ARGB;
  if (is_edge_platform) {
    create_params.mem_type = CNEDK_BUF_MEM_UNIFIED_CACHED;
  } else {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  }

  CnedkBufSurface* surf;

  ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
  ASSERT_TRUE(surf);

  if (is_edge_platform) {
    ASSERT_EQ(CnedkBufSurfaceSyncForCpu(surf, 0, 0), 0);
  } else {
    ASSERT_EQ(CnedkBufSurfaceSyncForCpu(surf, 0, 0), -1);
  }
  ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
}

TEST(BufSurface, SyncDevice) {
  bool is_edge_platform = cnedk::IsEdgePlatform(device_id);

  CnedkBufSurfaceCreateParams create_params;
  create_params.batch_size = 1;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = device_id;
  create_params.batch_size = 1;
  create_params.width = 1920;
  create_params.height = 1080;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_ARGB;
  if (is_edge_platform) {
    create_params.mem_type = CNEDK_BUF_MEM_UNIFIED_CACHED;
  } else {
    return;
  }
  CnedkBufSurface* surf;

  ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
  ASSERT_TRUE(surf);

  ASSERT_EQ(CnedkBufSurfaceSyncForDevice(surf, 0, 0), 0);
}

TEST(BufSurface, Copy) {
  {  // copy failed
    ASSERT_NE(CnedkBufSurfaceCopy(nullptr, nullptr), 0);

    CnedkBufSurface* src_surf;  // create src
    CnedkBufSurface* dst_surf;  // create src
    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 1;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id = device_id;
    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_BGR;
    create_params.mem_type = CNEDK_BUF_MEM_SYSTEM;

    ASSERT_EQ(CnedkBufSurfaceCreate(&src_surf, &create_params), 0);
    create_params.batch_size = 4;
    ASSERT_EQ(CnedkBufSurfaceCreate(&dst_surf, &create_params), 0);
    ASSERT_NE(CnedkBufSurfaceCopy(src_surf, dst_surf), 0);  // copy content
    ASSERT_EQ(CnedkBufSurfaceDestroy(src_surf), 0);
    ASSERT_EQ(CnedkBufSurfaceDestroy(dst_surf), 0);
  }

  const uint64_t pts = 1000;

  bool is_edge_platform = cnedk::IsEdgePlatform(device_id);

  CnedkBufSurfaceCreateParams create_params;
  create_params.batch_size = 1;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = device_id;
  create_params.batch_size = 1;
  create_params.width = 1920;
  create_params.height = 1080;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_BGR;
  if (is_edge_platform) {
    create_params.mem_type = CNEDK_BUF_MEM_UNIFIED_CACHED;
  } else {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  }

  CnedkBufSurface* surf;  // create src
  ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
  ASSERT_TRUE(surf);
  surf->pts = pts;

  CnedkBufSurface* dst_surf;  // create dst
  ASSERT_EQ(CnedkBufSurfaceCreate(&dst_surf, &create_params), 0);

  ASSERT_EQ(CnedkBufSurfaceCopy(surf, dst_surf), 0);  // copy content
  ASSERT_EQ(dst_surf->pts, pts);
  ASSERT_NE(dst_surf->surface_list[0].data_ptr, surf->surface_list[0].data_ptr);
  int sum_src, sum_dst;
  if (is_edge_platform) {
    ASSERT_EQ(CnedkBufSurfaceSyncForCpu(surf, 0, 0), 0);
    ASSERT_EQ(CnedkBufSurfaceSyncForCpu(dst_surf, 0, 0), 0);
    std::vector<uint8_t> v(reinterpret_cast<uint8_t*>(surf->surface_list[0].mapped_data_ptr),
        reinterpret_cast<uint8_t*>(reinterpret_cast<uint64_t>(surf->surface_list[0].mapped_data_ptr) +
                                                              surf->surface_list[0].data_size));
    std::vector<uint8_t> dst_v(reinterpret_cast<uint8_t*>(dst_surf->surface_list[0].mapped_data_ptr),
        reinterpret_cast<uint8_t*>(reinterpret_cast<uint64_t>(dst_surf->surface_list[0].mapped_data_ptr) +
                                                              dst_surf->surface_list[0].data_size));
    sum_src = std::accumulate(v.begin(), v.end(), 0);
    sum_dst = std::accumulate(dst_v.begin(), dst_v.end(), 0);
  } else {
    uint8_t* cpu_data = new uint8_t[surf->surface_list[0].data_size];
    ASSERT_EQ(cnrtMemcpy(cpu_data, surf->surface_list[0].data_ptr, surf->surface_list[0].data_size,
              cnrtMemcpyDevToHost), 0);
    std::vector<uint8_t> v(cpu_data, cpu_data + surf->surface_list[0].data_size);
    sum_src = std::accumulate(v.begin(), v.end(), 0);
    delete[] cpu_data;

    uint8_t* dst_cpu_data = new uint8_t[dst_surf->surface_list[0].data_size];
    ASSERT_EQ(cnrtMemcpy(dst_cpu_data, dst_surf->surface_list[0].data_ptr, dst_surf->surface_list[0].data_size,
              cnrtMemcpyDevToHost), 0);
    std::vector<uint8_t> dst_v(dst_cpu_data, dst_cpu_data + dst_surf->surface_list[0].data_size);
    sum_dst = std::accumulate(dst_v.begin(), dst_v.end(), 0);
    delete[] dst_cpu_data;
  }
  ASSERT_EQ(sum_src, sum_dst);

  ASSERT_EQ(CnedkBufSurfaceDestroy(surf), 0);
  ASSERT_EQ(CnedkBufSurfaceDestroy(dst_surf), 0);
}

TEST(BufSurface, Memset) {
  {
    CnedkBufSurface temp_surf;
    EXPECT_NE(CnedkBufSurfaceMemSet(nullptr, 0, 0, 0), 0);
    EXPECT_NE(CnedkBufSurfaceMemSet(&temp_surf, -2, 0, 0), 0);
    EXPECT_NE(CnedkBufSurfaceMemSet(&temp_surf, 0, -2, 0), 0);
  }
  {
    CnedkBufSurface* temp_surf;
    CnedkBufSurfaceCreateParams create_params;
    create_params.batch_size = 1;
    memset(&create_params, 0, sizeof(create_params));
    create_params.device_id = device_id;
    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_ARGB;
    create_params.mem_type = CNEDK_BUF_MEM_SYSTEM;
    ASSERT_EQ(CnedkBufSurfaceCreate(&temp_surf, &create_params), 0);
    ASSERT_EQ(CnedkBufSurfaceMemSet(temp_surf, 0, 0, 0), 0);
    // std::vector<int> des_v;
    // des_v.resize(1920 * 1080 * 4, 0);
    // ret = v.size() == dst_v.size() ? std::equal(v.begin(), v.end(), dst_v.begin()) : false;
  }

  bool is_edge_platform = cnedk::IsEdgePlatform(device_id);

  CnedkBufSurfaceCreateParams create_params;
  create_params.batch_size = 1;
  memset(&create_params, 0, sizeof(create_params));
  create_params.device_id = device_id;
  create_params.batch_size = 1;
  create_params.width = 1920;
  create_params.height = 1080;
  create_params.color_format = CNEDK_BUF_COLOR_FORMAT_ARGB;
  if (is_edge_platform) {
    create_params.mem_type = CNEDK_BUF_MEM_UNIFIED_CACHED;
  } else {
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
  }
  CnedkBufSurface* surf;

  ASSERT_EQ(CnedkBufSurfaceCreate(&surf, &create_params), 0);
  ASSERT_TRUE(surf);

  uint8_t val = 0;
  ASSERT_EQ(CnedkBufSurfaceMemSet(surf, 0, 0, val), 0);

  if (is_edge_platform) {
    ASSERT_EQ(CnedkBufSurfaceSyncForCpu(surf, 0, 0), 0);
    std::vector<uint8_t> v(reinterpret_cast<uint8_t*>(surf->surface_list[0].mapped_data_ptr),
        reinterpret_cast<uint8_t*>(reinterpret_cast<uint64_t>(surf->surface_list[0].mapped_data_ptr) +
                                                              surf->surface_list[0].data_size));
    bool equal = std::all_of(v.begin(), v.end(), [=](uint8_t i) { return i == val; });
    ASSERT_TRUE(equal);
  } else {
    uint8_t* cpu_data = new uint8_t[surf->surface_list[0].data_size];
    ASSERT_EQ(cnrtMemcpy(cpu_data, surf->surface_list[0].data_ptr, surf->surface_list[0].data_size,
              cnrtMemcpyDevToHost), 0);
    std::vector<uint8_t> v(cpu_data, cpu_data + surf->surface_list[0].data_size);
    bool equal = std::all_of(v.begin(), v.end(), [=](uint8_t i) { return i == val; });
    ASSERT_TRUE(equal);
    delete[] cpu_data;
  }

  val = 1;
  ASSERT_EQ(CnedkBufSurfaceMemSet(surf, -1, -1, val), 0);

  if (is_edge_platform) {
    ASSERT_EQ(CnedkBufSurfaceSyncForCpu(surf, 0, 0), 0);
    std::vector<uint8_t> v(reinterpret_cast<uint8_t*>(surf->surface_list[0].mapped_data_ptr),
        reinterpret_cast<uint8_t*>(reinterpret_cast<uint64_t>(surf->surface_list[0].mapped_data_ptr) +
                                                              surf->surface_list[0].data_size));
    bool equal = std::all_of(v.begin(), v.end(), [=](uint8_t i) { return i == val; });
    ASSERT_TRUE(equal);
  } else {
    // Batch size is 1
    uint8_t* cpu_data = new uint8_t[surf->surface_list[0].data_size];
    ASSERT_EQ(cnrtMemcpy(cpu_data, surf->surface_list[0].data_ptr, surf->surface_list[0].data_size,
              cnrtMemcpyDevToHost), 0);
    std::vector<uint8_t> v(cpu_data, cpu_data + surf->surface_list[0].data_size);
    bool equal = std::all_of(v.begin(), v.end(), [=](uint8_t i) { return i == val; });
    ASSERT_TRUE(equal);
    delete[] cpu_data;
  }
}
