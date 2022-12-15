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
#include "glog/logging.h"

#include "cnedk_platform.h"
#include "cnedk_buf_surface.h"
#include "cnedk_transform.h"

#include "test_base.h"

const size_t g_device_id = 0;

static int CreateSurfacePool(void** surf_pool, int width, int height, CnedkBufSurfaceColorFormat color_format) {
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


int TestFun(CnedkBufSurfaceColorFormat src_color, CnedkBufSurfaceColorFormat dst_color,
            int src_w, int src_h,
            int dst_w, int dst_h,
            CnedkTransformParams* param) {
  int ret = 0;
  void* src_pool;
  void* dst_pool;

  CreateSurfacePool(&src_pool, src_w, src_h, src_color);
  CreateSurfacePool(&dst_pool, dst_w, dst_h, dst_color);

  CnedkBufSurface* src_surf;
  ret = CnedkBufSurfaceCreateFromPool(&src_surf, src_pool);
  if (ret != 0) {
    LOG(ERROR) << "[EasyDK Tests] [Transform] Create src pool failed";
    return ret;
  }

  CnedkBufSurface* dst_surf;
  ret = CnedkBufSurfaceCreateFromPool(&dst_surf, dst_pool);
  if (ret != 0) {
    LOG(ERROR) << "[EasyDK Tests] [Transform] Create dst Pool failed";
    CnedkBufPoolDestroy(src_pool);
    return ret;
  }

  ret = CnedkTransform(src_surf, dst_surf, param);

  CnedkBufSurfaceDestroy(dst_surf);
  CnedkBufSurfaceDestroy(src_surf);
  CnedkBufPoolDestroy(dst_pool);
  CnedkBufPoolDestroy(src_pool);

  return ret;
}

TEST(Transform, SetGetSession) {
  EXPECT_NE(CnedkTransformGetSessionParams(nullptr), 0);
  EXPECT_NE(CnedkTransformSetSessionParams(nullptr), 0);

  CnedkTransformConfigParams param;
  EXPECT_EQ(CnedkTransformGetSessionParams(&param), 0);
  EXPECT_EQ(CnedkTransformSetSessionParams(&param), 0);
}

TEST(Transform, ColorTransorm) {
  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);
  CnedkTransformParams param;
  memset(&param, 0, sizeof(param));
  {  // input nullptr
    CnedkBufSurface src_surf;
    CnedkBufSurface dst_surf;
    EXPECT_NE(CnedkTransform(&src_surf, nullptr, &param), 0);
    EXPECT_NE(CnedkTransform(nullptr, &dst_surf, &param), 0);
    EXPECT_NE(CnedkTransform(&src_surf, &dst_surf, nullptr), 0);
    EXPECT_NE(CnedkTransform(nullptr, nullptr, nullptr), 0);
  }

  {  // not target mem type
    CnedkBufSurface* src_surf = nullptr;
    CnedkBufSurface* dst_surf = nullptr;
    CnedkBufSurfaceCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));
    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_BGR;
    create_params.mem_type = CNEDK_BUF_MEM_SYSTEM;
    create_params.device_id = g_device_id;
    EXPECT_EQ(CnedkBufSurfaceCreate(&src_surf, &create_params), 0);
    EXPECT_EQ(CnedkBufSurfaceCreate(&dst_surf, &create_params), 0);

    EXPECT_NE(CnedkTransform(src_surf, dst_surf, &param), 0);
    EXPECT_EQ(CnedkBufSurfaceDestroy(src_surf), 0);
    EXPECT_EQ(CnedkBufSurfaceDestroy(dst_surf), 0);
  }

  {  // filled number bigger than batch size
    CnedkBufSurface* src_surf = nullptr;
    CnedkBufSurface* dst_surf = nullptr;
    CnedkBufSurfaceCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));

    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.mem_type = CNEDK_BUF_MEM_DEVICE;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_BGR;
    create_params.device_id = g_device_id;

    EXPECT_EQ(CnedkBufSurfaceCreate(&src_surf, &create_params), 0);
    EXPECT_EQ(CnedkBufSurfaceCreate(&dst_surf, &create_params), 0);

    src_surf->num_filled = 4;

    EXPECT_NE(CnedkTransform(src_surf, dst_surf, &param), 0);

    src_surf->num_filled = 1;
    src_surf->surface_list[0].data_size = 0;
    EXPECT_NE(CnedkTransform(src_surf, dst_surf, &param), 0);
    EXPECT_EQ(CnedkBufSurfaceDestroy(src_surf), 0);
    EXPECT_EQ(CnedkBufSurfaceDestroy(dst_surf), 0);
  }

  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_BGR, 1920, 1080, 1920, 1080, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_BGR, 1920, 1080, 416, 416, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_BGR, 1920, 1080, 1920, 1080, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_BGR, 1920, 1080, 1280, 720, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_RGBA, 1920, 1080, 1920, 1080, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_ARGB, 1920, 1080, 1920, 1080, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_ABGR, 1920, 1080, 1920, 1080, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_BGRA, 1920, 1080, 416, 416, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_NV12, 1920, 1080, 1280, 720, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_NV21, 1920, 1080, 1280, 720, &param), 0);

  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_NV12, 1920, 1080, 1920, 1080, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_NV12, 1280, 720, 1920, 1080, &param), 0);

  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_NV21, 1920, 1080, 1920, 1080, &param), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_NV21, 1280, 720, 1920, 1080, &param), 0);
  if (is_edge_platform) {
    EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_NV21, 1280, 720, 1920, 1080, &param), 0);
  } else {
    EXPECT_NE(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_NV21, 1280, 720, 1920, 1080, &param), 0);
  }
  // keep respection
}

int TestTensorFun(CnedkBufSurfaceColorFormat src_color, CnedkBufSurfaceColorFormat dst_color,
                      int src_w, int src_h,
                      int dst_w, int dst_h,
                      CnedkTransformParams* param) {
  int ret = 0;
  void* src_pool;

  CreateSurfacePool(&src_pool, src_w, src_h, src_color);

  CnedkBufSurface* src_surf;
  ret = CnedkBufSurfaceCreateFromPool(&src_surf, src_pool);
  if (ret != 0) {
    LOG(ERROR) << "[EasyDK Tests] [Transform] Create src pool failed";
    return ret;
  }

  int per_type = 2;
  if (param->dst_desc->data_type == CNEDK_TRANSFORM_FLOAT32) {
    per_type = 4;
  } else if (param->dst_desc->data_type == CNEDK_TRANSFORM_FLOAT16) {
    per_type = 2;
  }

  int channel_num = 3;
  if (param->dst_desc->color_format == CNEDK_TRANSFORM_COLOR_FORMAT_ABGR ||
      param->dst_desc->color_format == CNEDK_TRANSFORM_COLOR_FORMAT_BGRA ||
      param->dst_desc->color_format == CNEDK_TRANSFORM_COLOR_FORMAT_ARGB ||
      param->dst_desc->color_format == CNEDK_TRANSFORM_COLOR_FORMAT_RGBA) {
    channel_num = 4;
  }

  CnedkBufSurfaceCreateParams create_params;
  memset(&create_params, 0, sizeof(create_params));
  create_params.batch_size = 1;
  create_params.width = dst_w;
  create_params.height = dst_h;
  create_params.size = dst_w * dst_h * channel_num * per_type;
  create_params.color_format = dst_color;
  create_params.device_id = g_device_id;

  create_params.mem_type = CNEDK_BUF_MEM_DEVICE;

  CnedkBufSurface* dst_surf = nullptr;
  ret = CnedkBufSurfaceCreate(&dst_surf, &create_params);
  if (ret != 0) {
    LOG(ERROR) << "[EasyDK Tests] [Transform] Create dst surface failed";
    CnedkBufPoolDestroy(src_pool);
    return ret;
  }

  ret = CnedkTransform(src_surf, dst_surf, param);

  CnedkBufSurfaceDestroy(dst_surf);
  CnedkBufSurfaceDestroy(src_surf);
  CnedkBufPoolDestroy(src_pool);

  return ret;
}

TEST(Transform, MeanStd) {
  CnedkTransformParams params;
  memset(&params, 0, sizeof(params));

  CnedkTransformMeanStdParams mean_std_params;
  float mean[3] = {127.5, 127.5, 127.5};
  float std[3] = {127.5, 127.5, 127.5};

  params.transform_flag |= CNEDK_TRANSFORM_MEAN_STD;
  for (uint32_t c_i = 0; c_i < 3; c_i++) {
    mean_std_params.mean[c_i] = mean[c_i];
    mean_std_params.std[c_i] = std[c_i];
  }
  params.mean_std_params = &mean_std_params;

  // configure dst_desc
  CnedkTransformTensorDesc dst_desc;
  dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_BGR;
  dst_desc.data_type = CNEDK_TRANSFORM_FLOAT16;
  dst_desc.shape.n = 1;
  dst_desc.shape.c = 3;
  dst_desc.shape.h = 416;
  dst_desc.shape.w = 416;
  params.dst_desc = &dst_desc;

  EXPECT_EQ(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 416, 416, &params),
            0);

  params.mean_std_params = nullptr;
  EXPECT_NE(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 416, 416, &params),
            0);

  params.mean_std_params = &mean_std_params;
  dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_BGRA;
  EXPECT_EQ(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 416, 416, &params),
            0);

  dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_ABGR;
  dst_desc.data_type = CNEDK_TRANSFORM_FLOAT32;
  EXPECT_EQ(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 416, 416, &params),
            0);
  EXPECT_EQ(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 1280, 720, &params),
            0);
  EXPECT_EQ(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1280, 720, 1920, 1080, &params),
            0);
  EXPECT_NE(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_BGRA, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 416, 416, &params),
            0);

  dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_BGR;
  EXPECT_EQ(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 1920, 1080, &params),
            0);
  params.mean_std_params = nullptr;
  EXPECT_NE(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 1920, 1080, &params),
            0);

  dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_NUM;
  EXPECT_NE(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 416, 416, &params),
            0);

  params.transform_flag = CNEDK_TRANSFORM_CROP_SRC;
  EXPECT_NE(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1920, 416, 416, &params),
            0);

  {  // src is tensor
    CnedkBufSurfaceCreateParams create_params;
    memset(&create_params, 0, sizeof(create_params));
    create_params.batch_size = 1;
    create_params.width = 1920;
    create_params.height = 1080;
    create_params.size = 1920 * 1080 * 3 / 2;
    create_params.color_format = CNEDK_BUF_COLOR_FORMAT_TENSOR;
    create_params.device_id = g_device_id;

    CnedkBufSurface* src_surf = nullptr;
    CnedkBufSurface* dst_surf = nullptr;
    CnedkBufSurfaceCreate(&src_surf, &create_params);
    CnedkBufSurfaceCreate(&dst_surf, &create_params);
    EXPECT_NE(CnedkTransform(src_surf, dst_surf, &params), 0);
    CnedkBufSurfaceDestroy(src_surf);
    CnedkBufSurfaceDestroy(dst_surf);
  }
}


TEST(Transform, Tensor) {
  CnedkTransformParams params;
  memset(&params, 0, sizeof(params));

  // configure dst_desc
  CnedkTransformTensorDesc dst_desc;
  dst_desc.color_format = CNEDK_TRANSFORM_COLOR_FORMAT_BGR;
  dst_desc.data_type = CNEDK_TRANSFORM_UINT8;
  dst_desc.shape.n = 1;
  dst_desc.shape.c = 3;
  dst_desc.shape.h = 416;
  dst_desc.shape.w = 416;
  params.dst_desc = &dst_desc;

  EXPECT_EQ(TestTensorFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_TENSOR, 1920, 1080, 416, 416, &params),
            0);
}

TEST(Transform, KeepAspectRatio) {
  bool is_edge_platform = cnedk::IsEdgePlatform(g_device_id);

  CnedkTransformParams params;
  memset(&params, 0, sizeof(params));

  CnedkTransformRect src_rect;
  src_rect.left = 0.1;
  src_rect.top = 0.1;
  src_rect.height = 0.2;
  src_rect.width = 0.2;

  CnedkTransformRect dst_rect;
  dst_rect.left = 0.1;
  dst_rect.top = 0.1;
  dst_rect.height = 0.2;
  dst_rect.width = 0.2;

  params.transform_flag |= CNEDK_TRANSFORM_CROP_SRC;
  params.src_rect = &src_rect;

  params.transform_flag |= CNEDK_TRANSFORM_CROP_DST;
  params.dst_rect = &dst_rect;

  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_BGR, 1920, 1080, 224, 224, &params), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_BGR, 1920, 1080, 1920, 1080, &params), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_BGR, 1920, 1080, 416, 416, &params), 0);

  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_NV12, 1920, 1080, 416, 416, &params), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV12, CNEDK_BUF_COLOR_FORMAT_NV12, 1280, 720, 1920, 1080, &params), 0);

  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_NV21, 1920, 1080, 416, 416, &params), 0);

  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_NV12, 1920, 1080, 416, 416, &params), 0);
  EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_BGR, CNEDK_BUF_COLOR_FORMAT_NV12, 1920, 1080, 1920, 1080, &params), 0);

  if (is_edge_platform) {  // 3226 vgu support nv12 to nv21, cncv not support
    EXPECT_EQ(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_NV12, 1920, 1080, 224, 224, &params), 0);
  } else {
    EXPECT_NE(TestFun(CNEDK_BUF_COLOR_FORMAT_NV21, CNEDK_BUF_COLOR_FORMAT_NV12, 1920, 1080, 224, 224, &params), 0);
  }
}
