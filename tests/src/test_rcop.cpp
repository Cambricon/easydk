#include <gtest/gtest.h>
#include <opencv2/opencv.hpp>
#include <sys/time.h>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "cxxutil/log.h"
#include "device/mlu_context.h"
#include "easybang/resize_and_colorcvt.h"
#include "easyinfer/mlu_memory_op.h"
#include "internal/mlu_task_queue.h"
#include "test_base.h"

#define FULL_TEST 0

namespace {

enum Fmt {
  YUV420SP_NV21 = 0,
  YUV420SP_NV12,
  RGBA32,
  BGRA32,
  ARGB32,
  ABGR32
};

enum ColorCvtMode : uint64_t {
  YUV420SP2RGBA32_NV21 = (uint64_t)YUV420SP_NV21 << 32 | RGBA32,
  YUV420SP2RGBA32_NV12 = (uint64_t)YUV420SP_NV12 << 32 | RGBA32,

  YUV420SP2BGRA32_NV21 = (uint64_t)YUV420SP_NV21 << 32 | BGRA32,
  YUV420SP2BGRA32_NV12 = (uint64_t)YUV420SP_NV12 << 32 | BGRA32,

  YUV420SP2ARGB32_NV21 = (uint64_t)YUV420SP_NV21 << 32 | ARGB32,
  YUV420SP2ARGB32_NV12 = (uint64_t)YUV420SP_NV12 << 32 | ARGB32,

  YUV420SP2ABGR32_NV21 = (uint64_t)YUV420SP_NV21 << 32 | ABGR32,
  YUV420SP2ABGR32_NV12 = (uint64_t)YUV420SP_NV12 << 32 | ABGR32
};

struct ImgInfo {
  std::vector<uint8_t> data;
  uint32_t w = 0, h = 0;
  Fmt fmt = YUV420SP_NV21;
  cv::Rect roi = {0, 0, 0, 0};
};

std::ostream& operator<<(std::ostream& os, const ImgInfo &img_info) {
  os << "========================" << std::endl;
  os << "Info head:" << std::endl;
  os << "width:" << img_info.w << std::endl;
  os << "height:" << img_info.h << std::endl;
  os << "format:" << static_cast<int>(img_info.fmt) << std::endl;
  os << "roi(x, y, w, h): " << img_info.roi.x << ", "
                            << img_info.roi.y << ", "
                            << img_info.roi.width << ", "
                            << img_info.roi.height << std::endl;
  os << "========================" << std::endl;

  os.write(reinterpret_cast<const char*>(img_info.data.data()), img_info.data.size() * sizeof(uint8_t));

  return os;
}

ImgInfo GenerateRandomImage(uint32_t w, uint32_t h, Fmt fmt) {
  size_t data_len = 0;
  switch (fmt) {
    case YUV420SP_NV12:
    case YUV420SP_NV21:
      if (h % 2) {
        EXPECT_TRUE(false) << "yuv420sp: height must be a positive even number.";
      } else {
        data_len = h * w * 3 / 2;
      }
      break;
    default:
      EXPECT_TRUE(false);
  }

  std::default_random_engine random_engine(time(NULL));
  std::uniform_int_distribution<uint8_t> rand_func(0, 255);

  ImgInfo img_info;
  img_info.w = w;
  img_info.h = h;
  img_info.fmt = fmt;
  if (0 == img_info.roi.width || 0 == img_info.roi.height) {
    img_info.roi.x = 0;
    img_info.roi.y = 0;
    img_info.roi.width = w;
    img_info.roi.height = h;
  }
  img_info.data.reserve(data_len);

  for (size_t index = 0; index < data_len; ++index)
    img_info.data.emplace_back(rand_func(random_engine));

  return img_info;
}

struct OperatorParams {
  ColorCvtMode cvt_mode;
  uint32_t dst_w = 0, dst_h = 0;
  uint32_t core_number;  // use how many mlu core
  bool keep_aspect_ratio = false;
};

std::ostream& operator<<(std::ostream &os, const OperatorParams &op_params) {
  os << "color space convert mode:" << static_cast<int>(op_params.cvt_mode) << std::endl;
  os << "dst width, dst height:" << op_params.dst_w << ", " << op_params.dst_h << std::endl;
  os << "core number:" << op_params.core_number << std::endl;
  os << "keep aspect ratio:" << op_params.keep_aspect_ratio << std::endl;
  return os;
}

static const std::map<ColorCvtMode, edk::MluResizeConvertOp::ColorMode> gmode_map = {
  {YUV420SP2RGBA32_NV21, edk::MluResizeConvertOp::ColorMode::YUV2RGBA_NV21},
  {YUV420SP2RGBA32_NV12, edk::MluResizeConvertOp::ColorMode::YUV2RGBA_NV12},
  {YUV420SP2BGRA32_NV21, edk::MluResizeConvertOp::ColorMode::YUV2BGRA_NV21},
  {YUV420SP2BGRA32_NV12, edk::MluResizeConvertOp::ColorMode::YUV2BGRA_NV12},
  {YUV420SP2ARGB32_NV21, edk::MluResizeConvertOp::ColorMode::YUV2ARGB_NV21},
  {YUV420SP2ARGB32_NV12, edk::MluResizeConvertOp::ColorMode::YUV2ARGB_NV12},
  {YUV420SP2ABGR32_NV21, edk::MluResizeConvertOp::ColorMode::YUV2ABGR_NV21},
  {YUV420SP2ABGR32_NV12, edk::MluResizeConvertOp::ColorMode::YUV2ABGR_NV12}
};

// resize, colorspace convert, crop
bool MluResizeAndCvt(const std::vector<ImgInfo> &src_imgs,
                     std::vector<ImgInfo> *pdst_imgs,
                     const OperatorParams &params) {
  if (gmode_map.find(params.cvt_mode) == gmode_map.end()) {
    LOGE(TEST) << "Unkonw color space convert mode." << static_cast<uint64_t>(params.cvt_mode);
    return false;
  }

  const int batchsize = static_cast<int>(src_imgs.size());

  if (batchsize == 0) return false;

  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();

  edk::MluMemoryOp mem_op;
  edk::MluResizeConvertOp op;
  edk::MluResizeConvertOp::Attr attr;
  attr.dst_h = params.dst_h;
  attr.dst_w = params.dst_w;
  attr.data_mode = edk::MluResizeConvertOp::DataMode::UINT8ToUINT8;
  attr.color_mode = gmode_map.at(params.cvt_mode);
  attr.core_version = context.GetCoreVersion();  // mlu270 or mlu220
  attr.core_number = params.core_number;
  attr.keep_aspect_ratio = params.keep_aspect_ratio;
  attr.batch_size = batchsize;
  if (!op.Init(attr)) {
    LOGE(TEST) << "Init mlu resize convert op failed.";
    return false;
  }

  auto mlu_queue = edk::MluTaskQueue::Create();
  op.SetMluQueue(mlu_queue);

  std::vector<void*> mlu_ptrs;
  for (const auto &img : src_imgs) {
    size_t y_plane_len = img.w * img.h * sizeof(uint8_t);
    size_t uv_plane_len = img.w * img.h / 2 * sizeof(uint8_t);
    void *y_plane_mlu = mem_op.AllocMlu(img.w * img.h);
    void *uv_plane_mlu = mem_op.AllocMlu(img.w * img.h / 2);
    void *y_plane_cpu = static_cast<void*>(const_cast<uint8_t*>(img.data.data()));
    void *uv_plane_cpu = static_cast<void*>(const_cast<uint8_t*>(img.data.data()) + y_plane_len);
    EXPECT_NO_THROW(mem_op.MemcpyH2D(y_plane_mlu, y_plane_cpu, y_plane_len));
    EXPECT_NO_THROW(mem_op.MemcpyH2D(uv_plane_mlu, uv_plane_cpu, uv_plane_len));

    mlu_ptrs.push_back(y_plane_mlu);  // save for deallocate
    mlu_ptrs.push_back(uv_plane_mlu);  // save for deallocate

    edk::MluResizeConvertOp::InputData input_data;
    input_data.src_w = img.w;
    input_data.src_h = img.h;
    input_data.src_stride = img.w;
    input_data.crop_x = img.roi.x;
    input_data.crop_y = img.roi.y;
    input_data.crop_w = img.roi.width;
    input_data.crop_h = img.roi.height;
    input_data.planes[0] = y_plane_mlu;
    input_data.planes[1] = uv_plane_mlu;

    op.BatchingUp(input_data);
  }

  size_t output_size = params.dst_w * params.dst_h * 4 * batchsize * sizeof(uint8_t);
  void *output_mlu = nullptr;
  EXPECT_NO_THROW(output_mlu = mem_op.AllocMlu(output_size));
  EXPECT_EQ(CNRT_RET_SUCCESS, cnrtMemset(output_mlu, 0, output_size));  // keep aspect ratio: pad value not verified.
  if (!op.SyncOneOutput(output_mlu)) {
    LOGE(TEST) << "invoke kernel failed.";
    return false;
  }

  uint8_t *output_cpu = new (std::nothrow) uint8_t[output_size];
  EXPECT_TRUE(output_cpu) << "alloc memory on cpu failed.";

  EXPECT_NO_THROW(mem_op.MemcpyD2H(static_cast<void *>(output_cpu), output_mlu, output_size));

  pdst_imgs->clear();
  pdst_imgs->reserve(batchsize);
  size_t batch_offset = params.dst_w * params.dst_h * 4;
  // unbatch
  for (int bidx = 0; bidx < batchsize; ++bidx) {
    ImgInfo img;
    img.w = params.dst_w;
    img.h = params.dst_h;
    img.fmt = static_cast<Fmt>(params.cvt_mode & (((uint64_t)1 << 33) - 1));
    uint8_t *begin = output_cpu + bidx * batch_offset;
    uint8_t *end = begin + batch_offset;
    img.data.clear();
    img.data.assign(begin, end);
    pdst_imgs->push_back(img);
  }

  op.Destroy();
  delete[] output_cpu;
  mem_op.FreeMlu(output_mlu);
  for (auto it : mlu_ptrs) mem_op.FreeMlu(it);

  return true;
}

bool CpuResizeAndCvt(const std::vector<ImgInfo> &src_imgs,
                     std::vector<ImgInfo> *pdst_imgs,
                     const OperatorParams &params) {
  pdst_imgs->clear();
  int batchsize = static_cast<int>(src_imgs.size());
  pdst_imgs->resize(batchsize);
  for (int bidx = 0; bidx < batchsize; ++bidx) {
    const ImgInfo &src_img = src_imgs[bidx];
    ImgInfo *dst_img = &(*pdst_imgs)[bidx];

    dst_img->fmt = static_cast<Fmt>(params.cvt_mode & (((uint64_t)1 << 33) - 1));
    dst_img->w = params.dst_w;
    dst_img->h = params.dst_h;

    cv::Mat src_mat(static_cast<int>(src_img.h * 3 / 2),
                    static_cast<int>(src_img.w), CV_8UC1,
                    static_cast<void*>(const_cast<uint8_t*>(src_img.data.data())));
    cv::Mat cvt_mat;
    cv::Mat chn_a(static_cast<int>(src_img.h), static_cast<int>(src_img.w), CV_8UC1, cv::Scalar(0));
    // colorspace convert
    switch (params.cvt_mode) {
      case YUV420SP2RGBA32_NV21: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2RGB_NV21);
        cv::merge(std::vector<cv::Mat>({chn3, chn_a}), cvt_mat);
        break;
      }
      case YUV420SP2RGBA32_NV12: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2RGB_NV12);
        cv::merge(std::vector<cv::Mat>({chn3, chn_a}), cvt_mat);
        break;
      }
      case YUV420SP2BGRA32_NV21: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2BGR_NV21);
        cv::merge(std::vector<cv::Mat>({chn3, chn_a}), cvt_mat);
        break;
      }
      case YUV420SP2BGRA32_NV12: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2BGR_NV12);
        cv::merge(std::vector<cv::Mat>({chn3, chn_a}), cvt_mat);
        break;
      }
      case YUV420SP2ARGB32_NV21: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2RGB_NV21);
        cv::merge(std::vector<cv::Mat>({chn_a, chn3}), cvt_mat);
        break;
      }
      case YUV420SP2ARGB32_NV12: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2RGB_NV12);
        cv::merge(std::vector<cv::Mat>({chn_a, chn3}), cvt_mat);
        break;
      }
      case YUV420SP2ABGR32_NV21: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2BGR_NV21);
        cv::merge(std::vector<cv::Mat>({chn_a, chn3}), cvt_mat);
        break;
      }
      case YUV420SP2ABGR32_NV12: {
        cv::Mat chn3;
        cv::cvtColor(src_mat, chn3, cv::COLOR_YUV2BGR_NV12);
        cv::merge(std::vector<cv::Mat>({chn_a, chn3}), cvt_mat);
        break;
      }
      default:
        EXPECT_TRUE(false) << "It's not supposed to be here.";
        break;
    }

    // roi
    cv::Mat roi_mat = cvt_mat(src_img.roi);

    // resize
    const double scaling_factors = std::min(1.0 * params.dst_w / roi_mat.cols, 1.0 * params.dst_h / roi_mat.rows);
    cv::Size dst_size;
    dst_size.width = std::round(params.keep_aspect_ratio ? scaling_factors * roi_mat.cols : params.dst_w);
    dst_size.height = std::round(params.keep_aspect_ratio ? scaling_factors * roi_mat.rows : params.dst_h);
    dst_size.width = std::min(params.dst_w, static_cast<uint32_t>(dst_size.width));
    dst_size.height = std::min(params.dst_h, static_cast<uint32_t>(dst_size.height));

    cv::Mat resized_mat;
    cv::resize(roi_mat, resized_mat, dst_size, 0, 0, cv::INTER_LINEAR);

    // addpad
    if (static_cast<uint32_t>(resized_mat.cols) != params.dst_w ||
        static_cast<uint32_t>(resized_mat.rows) != params.dst_h) {
      cv::Mat t(params.dst_h, params.dst_w, CV_8UC4, cv::Scalar(0, 0, 0, 0));
      resized_mat.copyTo(t(cv::Rect((params.dst_w - resized_mat.cols) / 2,  // floor
                                    (params.dst_h - resized_mat.rows) / 2,  // floor
                                    resized_mat.cols, resized_mat.rows)));
      resized_mat = t;
    }

    size_t output_len = dst_img->w * dst_img->h * 4;
    dst_img->data.assign(resized_mat.data, resized_mat.data + output_len);
  }

  return true;
}

bool CompareData(const ImgInfo &cpu_data, const ImgInfo &mlu_data,
                 ColorCvtMode color_mode) {
  EXPECT_EQ(cpu_data.fmt, mlu_data.fmt);
  EXPECT_EQ(cpu_data.w, mlu_data.w);
  EXPECT_EQ(cpu_data.h, mlu_data.h);

  bool ret = true;
  float thres = 0.02;
  float diff = 0.0;
  float mae = 0.0;
  float mse = 0.0;
  float ma = 0.0;
  float ms = 0.0;
  int height = cpu_data.h;
  int width = cpu_data.w;

  for (int i = 0; i < height; i++) {
    for (int j = 0; j < width; j++) {
      for (int k = 0; k < 4; k++) {
        diff = static_cast<float>(mlu_data.data[i * width * 4 + j * 4 + k]) -
               static_cast<float>(cpu_data.data[i * width * 4 + j * 4 + k]);

        ma += static_cast<float>(cpu_data.data[i * width * 4 + j * 4 + k]);
        ms += static_cast<float>(cpu_data.data[i * width * 4 + j * 4 + k]) *
              static_cast<float>(cpu_data.data[i * width * 4 + j * 4 + k]);

        mae += std::abs(diff);
        mse += diff * diff;
      }
    }
  }

  mae /= ma;
  mse = std::sqrt(mse);
  ms = std::sqrt(ms);
  mse /= ms;

  if (mae > thres || mse > thres) {
    ret = false;
    std::cout << "COMPARE DATA FAILED! "
              << "mae:" << mae << " mse:" << mse << std::endl;
  } else {
    ret = true;
  }

  return ret;
}

bool TestFunc(const std::vector<ImgInfo> &src_imgs, const OperatorParams &op_params) {
  bool ret = true;

  int batchsize = static_cast<int>(src_imgs.size());

  std::vector<ImgInfo> dst_imgs_mlu;
  std::vector<ImgInfo> dst_imgs_cpu;

  ret = MluResizeAndCvt(src_imgs, &dst_imgs_mlu, op_params);

  ret = ret && CpuResizeAndCvt(src_imgs, &dst_imgs_cpu, op_params);

  for (int bidx = 0; bidx < batchsize; ++bidx)
    ret = ret && CompareData(dst_imgs_cpu[bidx], dst_imgs_mlu[bidx], op_params.cvt_mode);

  if (!ret) {
    // dump
    timeval tval;
    gettimeofday(&tval, nullptr);
    const std::string dir_name = "test_rcop_input_" +
                                 std::to_string(tval.tv_sec) + "s_" + std::to_string(tval.tv_usec) + "us";
    if (0 == access(dir_name.c_str(), F_OK) || 0 == mkdir(dir_name.c_str(), 0777)) {
      std::ofstream ofs(dir_name + "/op_params.txt");
      ofs << op_params << std::endl;
      ofs.close();
      for (int bidx = 0; bidx < batchsize; ++bidx) {
        ofs.open(dir_name + "/input_data_" + std::to_string(bidx) + ".txt");
        ofs << src_imgs[bidx] << std::endl;
        ofs.close();
      }
    } else {
      LOGE(TEST) << "Dump input data failed. Check directory permissions.";
    }
  }

  return ret;
}

// params: src_size, dst_size, src_fmt, dst_fmt, core_number, keep_aspect_ratio
class ResizeConvertTestParam
    : public testing::TestWithParam<std::tuple<cv::Size, cv::Size, Fmt, Fmt, int, bool>> {};

TEST_P(ResizeConvertTestParam, Execute) {
  auto params = GetParam();
  cv::Size src_size = std::get<0>(params);
  cv::Size dst_size = std::get<1>(params);
  Fmt src_fmt = std::get<2>(params);
  Fmt dst_fmt = std::get<3>(params);
  int core_number = std::get<4>(params);
  bool keep_aspect_ratio = std::get<5>(params);

  // generate src imgs
  ImgInfo src_img = GenerateRandomImage(src_size.width, src_size.height, src_fmt);

  OperatorParams op_params;
  op_params.dst_w = dst_size.width;
  op_params.dst_h = dst_size.height;
  op_params.keep_aspect_ratio = keep_aspect_ratio;
  op_params.core_number = core_number;
  op_params.cvt_mode = static_cast<ColorCvtMode>((uint64_t)src_fmt << 32 | dst_fmt);

  EXPECT_TRUE(TestFunc({src_img}, op_params));
}

// params: src_sizes, dst_size, src_fmt, dst_fmt, core_number, keep_aspect_ratio
class ResizeConvertCoreSplitTestParam
    : public testing::TestWithParam<std::tuple<std::vector<cv::Size>, cv::Size, Fmt, Fmt, int, bool>> {};

TEST_P(ResizeConvertCoreSplitTestParam, Execute) {
  auto params = GetParam();
  std::vector<cv::Size> src_sizes = std::get<0>(params);
  cv::Size dst_size = std::get<1>(params);
  Fmt src_fmt = std::get<2>(params);
  Fmt dst_fmt = std::get<3>(params);
  int core_number = std::get<4>(params);
  bool keep_aspect_ratio = std::get<5>(params);
  int batchsize = static_cast<int>(src_sizes.size());

  // generate src imgs
  std::vector<ImgInfo> src_imgs;
  src_imgs.reserve(batchsize);
  for (const auto &src_size : src_sizes)
    src_imgs.emplace_back(GenerateRandomImage(src_size.width, src_size.height, src_fmt));

  OperatorParams op_params;
  op_params.dst_w = dst_size.width;
  op_params.dst_h = dst_size.height;
  op_params.keep_aspect_ratio = keep_aspect_ratio;
  op_params.core_number = core_number;
  op_params.cvt_mode = static_cast<ColorCvtMode>((uint64_t)src_fmt << 32 | dst_fmt);

  EXPECT_TRUE(TestFunc(src_imgs, op_params));
}

// params: src_size, src_rois, dst_size, src_fmt, dst_fmt, core_number, keep_aspect_ratio
class ResizeConvertRoiCoreSplitTestParam
    : public testing::TestWithParam<std::tuple<cv::Size, std::vector<cv::Rect>, cv::Size, Fmt, Fmt, int, bool>> {};

TEST_P(ResizeConvertRoiCoreSplitTestParam, Execute) {
  auto params = GetParam();
  cv::Size src_size = std::get<0>(params);
  std::vector<cv::Rect> src_rois = std::get<1>(params);
  cv::Size dst_size = std::get<2>(params);
  Fmt src_fmt = std::get<3>(params);
  Fmt dst_fmt = std::get<4>(params);
  int core_number = std::get<5>(params);
  bool keep_aspect_ratio = std::get<6>(params);
  int batchsize = static_cast<int>(src_rois.size());

  // generate src imgs
  std::vector<ImgInfo> src_imgs(batchsize, GenerateRandomImage(src_size.width, src_size.height, src_fmt));
  src_imgs.reserve(batchsize);
  for (int bidx = 0; bidx < batchsize; ++bidx) {
    src_imgs[bidx].roi = src_rois[bidx];
  }

  OperatorParams op_params;
  op_params.dst_w = dst_size.width;
  op_params.dst_h = dst_size.height;
  op_params.keep_aspect_ratio = keep_aspect_ratio;
  op_params.core_number = core_number;
  op_params.cvt_mode = static_cast<ColorCvtMode>((uint64_t)src_fmt << 32 | dst_fmt);

  EXPECT_TRUE(TestFunc(src_imgs, op_params));
}

INSTANTIATE_TEST_CASE_P(Bang, ResizeConvertTestParam,
  testing::Combine(
    testing::Values(
#if FULL_TEST
                    cv::Size(176, 144),     // QCIF
                    cv::Size(704, 288),     // HALF D1
                    cv::Size(704, 576),     // 4CIF
                    cv::Size(1408, 1152),   // 16CIF
                    cv::Size(960, 576),     // WD1
                    cv::Size(960, 540),     // 540
                    cv::Size(2048, 1080),
                    cv::Size(4096, 2160),
                    cv::Size(7680, 4320),
#endif
                    cv::Size(352, 288),     // CIF
                    cv::Size(1280, 720),    // 720
                    cv::Size(1920, 1080),   // 1080
                    cv::Size(3840, 2160)),
    // neural net work input size
    testing::Values(
#if FULL_TEST
                    cv::Size(32, 32),
                    cv::Size(32, 64),
                    cv::Size(64, 64),
                    cv::Size(64, 128),
                    cv::Size(608, 608),
                    cv::Size(1280, 720),
                    cv::Size(1920, 1080),
                    cv::Size(1920, 1088),
#endif
                    cv::Size(128, 128),
                    cv::Size(224, 224),
                    cv::Size(300, 300),
                    cv::Size(416, 416)),
    // src pixel format (only yuv support)
    testing::Values(YUV420SP_NV12),
    // dst pixel format (only 4 channel fmt support)
    testing::Values(RGBA32),
    // core number
    testing::Values(1),
    // keep aspect ratio
    testing::Values(false, true))
);

INSTANTIATE_TEST_CASE_P(Bang, ResizeConvertCoreSplitTestParam,
  testing::Combine(
    testing::Values(std::vector<cv::Size>({cv::Size(352, 288)}),
                    std::vector<cv::Size>({cv::Size(352, 288), cv::Size(1280, 720)}),
                    std::vector<cv::Size>({cv::Size(352, 288), cv::Size(1920, 1080),
                                            cv::Size(1280, 720), cv::Size(3840, 2160)}),
                    std::vector<cv::Size>({cv::Size(1920, 1080), cv::Size(1920, 1080),
                                            cv::Size(1920, 1080), cv::Size(1920, 1080)})),
    // neural net work input size
    testing::Values(
#if FULL_TEST
                    cv::Size(32, 32),
                    cv::Size(32, 64),
                    cv::Size(64, 64),
                    cv::Size(64, 128),
                    cv::Size(608, 608),
                    cv::Size(1280, 720),
                    cv::Size(1920, 1080),
                    cv::Size(1920, 1088),
#endif
                    cv::Size(224, 224),
                    cv::Size(300, 300),
                    cv::Size(416, 416)),
    // src pixel format (only yuv support)
    testing::Values(YUV420SP_NV12),
    // dst pixel format (only 4 channel fmt support)
    testing::Values(RGBA32),
    // core number
    testing::Values(4, 16),
    // keep aspect ratio
    testing::Values(false, true))
);

INSTANTIATE_TEST_CASE_P(Bang, ResizeConvertRoiCoreSplitTestParam,
  testing::Combine(
    testing::Values(cv::Size(1920, 1080)),
    // rois
    testing::Values(std::vector<cv::Rect>({cv::Rect(737, 247, 783, 717)}),
                    std::vector<cv::Rect>({cv::Rect(100, 100, 100, 100),
                                           cv::Rect(737, 247, 783, 717)}),
                    std::vector<cv::Rect>({cv::Rect(138, 142, 373, 377),
                                           cv::Rect(737, 247, 783, 717),
                                           cv::Rect(178, 179, 999, 373),
                                           cv::Rect(737, 247, 17, 17)})),
    // neural net work input size
    testing::Values(
#if FULL_TEST
                    cv::Size(32, 32),
                    cv::Size(32, 64),
                    cv::Size(64, 64),
                    cv::Size(64, 128),
                    cv::Size(608, 608),
                    cv::Size(1280, 720),
                    cv::Size(1920, 1080),
                    cv::Size(1920, 1088),
                    cv::Size(224, 224),
                    cv::Size(300, 300),
#endif
                    cv::Size(416, 416)),
    // src pixel format (only yuv support)
    testing::Values(YUV420SP_NV12),
    // dst pixel format (only 4 channel fmt support)
    testing::Values(RGBA32),
    // core number
    testing::Values(4),
    // keep aspect ratio
    testing::Values(false, true))
);

TEST(Bang, RCOpRunAfterCoreDump) {
  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();

  edk::MluMemoryOp mem_op;
  edk::MluResizeConvertOp op;
  edk::MluResizeConvertOp::Attr attr;
  attr.dst_h = 512;
  attr.dst_w = 512;
  attr.core_version = context.GetCoreVersion();  // mlu270 or mlu220
  attr.core_number = 4;
  attr.keep_aspect_ratio = true;
  attr.batch_size = 1;
  ASSERT_TRUE(op.Init(attr));

  size_t output_size = attr.dst_w * attr.dst_h * 4 * sizeof(uint8_t);
  void* output_mlu = nullptr;

  /** make a mistake first **/
  edk::MluResizeConvertOp::InputData input_data;
  input_data.src_w = 100;
  input_data.src_h = 100;
  input_data.src_stride = 100;
  ASSERT_NO_THROW(input_data.planes[0] = mem_op.AllocMlu(size_t(1) * input_data.src_w * input_data.src_h));
  ASSERT_NO_THROW(input_data.planes[1] = mem_op.AllocMlu(size_t(1) * input_data.src_w * input_data.src_h / 2));
  ASSERT_NO_THROW(output_mlu = mem_op.AllocMlu(output_size / 2));  // make a DMA WRITE FAILED.
  ASSERT_NO_THROW(op.BatchingUp(input_data));
  ASSERT_FALSE(op.SyncOneOutput(output_mlu));

  /** run normally **/
  mem_op.FreeMlu(output_mlu);
  ASSERT_NO_THROW(output_mlu = mem_op.AllocMlu(output_size));
  ASSERT_NO_THROW(op.BatchingUp(input_data));
  EXPECT_TRUE(op.SyncOneOutput(output_mlu));

  op.Destroy();
  mem_op.FreeMlu(input_data.planes[0]);
  mem_op.FreeMlu(input_data.planes[1]);
  mem_op.FreeMlu(output_mlu);
}

TEST(Bang, RCOpBatchNotFull) {
  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();

  edk::MluMemoryOp mem_op;
  edk::MluResizeConvertOp op;
  edk::MluResizeConvertOp::Attr attr;
  attr.dst_h = 512;
  attr.dst_w = 512;
  attr.core_version = context.GetCoreVersion();
  attr.core_number = 4;
  attr.keep_aspect_ratio = true;
  attr.batch_size = 16;
  ASSERT_TRUE(op.Init(attr));

  size_t output_size = attr.batch_size * attr.dst_w * attr.dst_h * 4 * sizeof(uint8_t);
  void* output_mlu = nullptr;

  // only one data in batchs
  edk::MluResizeConvertOp::InputData input_data;
  input_data.src_w = 1920;
  input_data.src_h = 1080;
  input_data.src_stride = 1920;
  ASSERT_NO_THROW(input_data.planes[0] = mem_op.AllocMlu(size_t(1) * input_data.src_w * input_data.src_h));
  ASSERT_NO_THROW(input_data.planes[1] = mem_op.AllocMlu(size_t(1) * input_data.src_w * input_data.src_h / 2));
  ASSERT_NO_THROW(output_mlu = mem_op.AllocMlu(output_size));
  ASSERT_NO_THROW(op.BatchingUp(input_data));
  EXPECT_TRUE(op.SyncOneOutput(output_mlu));

  op.Destroy();
  mem_op.FreeMlu(output_mlu);
  mem_op.FreeMlu(input_data.planes[0]);
  mem_op.FreeMlu(input_data.planes[1]);
}

}  // namespace
