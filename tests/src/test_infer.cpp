#include <gtest/gtest.h>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "device/mlu_context.h"
#include "easyinfer/easy_infer.h"
#include "easyinfer/mlu_memory_op.h"
#include "easyinfer/model_loader.h"
#include "test_base.h"

constexpr const char *gmodel_path_220 =
    "../../samples/data/models/MLU220/inceptionv3/inception-v3_int8_scale_dense_4batch_4core.cambricon";
constexpr const char *gmodel_path_270 = "../../samples/data/models/MLU270/resnet50_offline.cambricon";

TEST(Easyinfer, Shape) {
  uint32_t n = 1, c = 3, h = 124, w = 82, stride = 128;
  edk::Shape shape(n, h, w, c, stride);
  EXPECT_EQ(n, shape.n);
  EXPECT_EQ(c, shape.c);
  EXPECT_EQ(h, shape.h);
  EXPECT_EQ(w, shape.w);
  EXPECT_EQ(stride, shape.Stride());
  EXPECT_EQ(c * stride, shape.Step());
  EXPECT_EQ(h * w, shape.hw());
  EXPECT_EQ(h * w * c, shape.hwc());
  EXPECT_EQ(n * c * h * w, shape.nhwc());
  EXPECT_EQ(n * c * h * stride, shape.DataCount());
  std::cout << shape << std::endl;

  // stride should be equal to w, while set stride less than w
  n = 4, c = 1, h = 20, w = 96, stride = 64;
  shape.n = n;
  shape.c = c;
  shape.h = h;
  shape.w = w;
  shape.SetStride(stride);
  EXPECT_EQ(w, shape.Stride());

  edk::Shape another_shape(n, h, w, c, stride);
  EXPECT_TRUE(another_shape == shape);
  another_shape.c = c + 1;
  EXPECT_TRUE(another_shape != shape);
}

TEST(Easyinfer, ModelLoader) {
  std::string function_name = "subnet0";
  edk::MluContext context;
  context.SetDeviceId(0);
  context.BindDevice();
  auto version = context.GetCoreVersion();
  std::string model_path = GetExePath();
  if (version == edk::CoreVersion::MLU220) {
    std::cout << "220 model" << std::endl;
    model_path += gmodel_path_220;
  } else if (version == edk::CoreVersion::MLU270) {
    std::cout << "270 model" << std::endl;
    model_path += gmodel_path_270;
  } else {
    ASSERT_TRUE(false) << "Unsupport core version" << static_cast<int>(version);
  }
  int parallelism = 0, input_align = 0, output_align = 0;
  {
    auto model_loader = std::make_shared<edk::ModelLoader>(model_path, function_name);

    edk::DataLayout layout;
    layout.dtype = edk::DataType::FLOAT32;
    layout.order = edk::DimOrder::NHWC;
    model_loader->SetCpuInputLayout(layout, 0);
    layout.dtype = edk::DataType::FLOAT32;
    layout.order = edk::DimOrder::NCHW;
    model_loader->SetCpuOutputLayout(layout, 0);

    EXPECT_NO_THROW(model_loader->AdjustStackMemory());
    parallelism = model_loader->ModelParallelism();
    EXPECT_NE(parallelism, 0);
    input_align = model_loader->GetInputDataBatchAlignSize(0);
    EXPECT_NE(input_align, 0);
    output_align = model_loader->GetOutputDataBatchAlignSize(0);
    EXPECT_NE(output_align, 0);
    ASSERT_FALSE(model_loader->InputShapes().empty());
    EXPECT_GT(model_loader->InputShapes()[0].nhwc(), static_cast<uint32_t>(0));
    ASSERT_FALSE(model_loader->OutputShapes().empty());
    EXPECT_GT(model_loader->OutputShapes()[0].nhwc(), static_cast<uint32_t>(0));
  }
  {
    FILE *fd = fopen(model_path.c_str(), "rb");
    ASSERT_TRUE(fd) << "model path does not exist";
    fseek(fd, 0, SEEK_END);
    size_t fsize = ftell(fd);
    rewind(fd);
    char *mem_buf = new char[fsize];
    size_t size = fread(mem_buf, 1, fsize, fd);
    fclose(fd);

    if (size != fsize) {
      delete[] mem_buf;
      ASSERT_TRUE(false) << "read model file failed";
    }

    auto model_loader = std::make_shared<edk::ModelLoader>(reinterpret_cast<void *>(mem_buf), function_name.c_str());
    delete[] mem_buf;

    edk::DataLayout layout;
    layout.dtype = edk::DataType::FLOAT32;
    layout.order = edk::DimOrder::NHWC;
    model_loader->SetCpuInputLayout(layout, 0);
    layout.dtype = edk::DataType::FLOAT32;
    layout.order = edk::DimOrder::NCHW;
    model_loader->SetCpuOutputLayout(layout, 0);

    EXPECT_EQ(parallelism, model_loader->ModelParallelism());
    EXPECT_EQ(input_align, model_loader->GetInputDataBatchAlignSize(0));
    EXPECT_EQ(output_align, model_loader->GetOutputDataBatchAlignSize(0));
    EXPECT_NO_THROW(model_loader->AdjustStackMemory());
    ASSERT_FALSE(model_loader->InputShapes().empty());
    EXPECT_GT(model_loader->InputShapes()[0].nhwc(), static_cast<uint32_t>(0));
    ASSERT_FALSE(model_loader->OutputShapes().empty());
    EXPECT_GT(model_loader->OutputShapes()[0].nhwc(), static_cast<uint32_t>(0));
  }
}

TEST(Easyinfer, MluMemoryOp) {
  int times = 100;
  void *mlu_ptr = nullptr, *mlu_dst = nullptr;
  edk::MluMemoryOp mem_op;
  char *str = new char[20 * 100 * 16];
  char *str_out = new char[20 * 100 * 16];
  while (--times) {
    try {
      size_t kStrSize = 317 * times;
      snprintf(str, kStrSize, "test MluMemoryOp, s: %lu", kStrSize);
      void *in = reinterpret_cast<void *>(str);
      void *out = reinterpret_cast<void *>(str_out);
      edk::MluContext context;
      context.SetDeviceId(0);
      context.BindDevice();
      mlu_ptr = mem_op.AllocMlu(kStrSize);
      mlu_dst = mem_op.AllocMlu(kStrSize);
      mem_op.MemcpyH2D(mlu_ptr, in, kStrSize);
      mem_op.MemcpyD2D(mlu_dst, mlu_ptr, kStrSize);
      mem_op.MemcpyD2H(out, mlu_dst, kStrSize);
      mem_op.FreeMlu(mlu_ptr);
      mem_op.FreeMlu(mlu_dst);
      EXPECT_STREQ(str, str_out);
    } catch (edk::Exception &err) {
      EXPECT_TRUE(false) << err.what();
      if (mlu_dst) mem_op.FreeMlu(mlu_dst);
      if (mlu_ptr) mem_op.FreeMlu(mlu_ptr);
    }
  }
  delete[] str;
  delete[] str_out;
}

TEST(Easyinfer, Infer) {
  std::string function_name = "subnet0";
  try {
    edk::MluContext context;
    context.SetDeviceId(0);
    context.BindDevice();
    auto version = context.GetCoreVersion();
    std::string model_path = GetExePath();
    if (version == edk::CoreVersion::MLU220) {
      model_path += gmodel_path_220;
      std::cout << "220 model" << std::endl;
    } else if (version == edk::CoreVersion::MLU270) {
      model_path += gmodel_path_270;
      std::cout << "270 model" << std::endl;
    } else {
      ASSERT_TRUE(false) << "Unsupport core version" << static_cast<int>(version);
    }
    auto model_loader = std::make_shared<edk::ModelLoader>(model_path, function_name);

    edk::MluMemoryOp mem_op;
    mem_op.SetModel(model_loader);
    EXPECT_EQ(mem_op.Model(), model_loader);
    edk::EasyInfer infer;
    infer.Init(model_loader, 0);
    EXPECT_EQ(infer.Model(), model_loader);
    EXPECT_NE(infer.GetMluQueue().get(), nullptr);

    void **mlu_input = mem_op.AllocMluInput();
    void **mlu_output = mem_op.AllocMluOutput();
    void **cpu_output = mem_op.AllocCpuOutput();
    void **cpu_input = mem_op.AllocCpuInput();

    float hw_time = 0;
    mem_op.MemcpyInputH2D(mlu_input, cpu_input);
    infer.Run(mlu_input, mlu_output, &hw_time);
    EXPECT_GT(hw_time, 0);
    EXPECT_LT(hw_time, 100);
    mem_op.MemcpyOutputD2H(cpu_output, mlu_output);

    mem_op.FreeMluInput(mlu_input);
    mem_op.FreeMluOutput(mlu_output);
    mem_op.FreeCpuOutput(cpu_output);
    mem_op.FreeCpuInput(cpu_input);
  } catch (edk::Exception &err) {
    EXPECT_TRUE(false) << err.what();
  }
}
