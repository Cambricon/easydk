#include <gtest/gtest.h>

#include <cmath>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "device/mlu_context.h"
#include "internal/mlu_task_queue.h"
#include "test_base.h"

bool err_occured = false;

bool test_context(int dev_id, int channel_id, bool multi_thread) {
  try {
    edk::MluContext context;
    context.SetDeviceId(dev_id);
    context.SetChannelId(channel_id);
    context.BindDevice();
    if (dev_id != context.DeviceId() || channel_id != context.ChannelId()) {
      THROW_EXCEPTION(edk::Exception::INTERNAL, "unmatched params (device id or channel id)");
    }
  } catch (edk::Exception &err) {
    if (multi_thread) {
      std::cout << "set context failed:\nchannel_id " + std::to_string(channel_id) << std::endl;
      err_occured = true;
      return false;
    } else {
      std::cout << err.what() << std::endl;
      return false;
    }
  }
  return true;
}

TEST(Device, MluContext) {
  edk::MluContext ctx;
  ASSERT_GT(ctx.GetDeviceNum(), 0u) << "Cannot find any device";
  ASSERT_TRUE(ctx.CheckDeviceId(0)) << "Cannot find device 0";
  ASSERT_FALSE(ctx.CheckDeviceId(99));
  ASSERT_TRUE(test_context(0, 0, false));
  ASSERT_TRUE(test_context(0, 3, false));
  ASSERT_FALSE(test_context(99, 0, false));
  ASSERT_FALSE(test_context(0, 4, false));
  ASSERT_FALSE(test_context(0, 100, false));
  std::vector<std::thread> threads;
  for (int i = 0; i < 100; ++i) {
    threads.push_back(std::thread(&test_context, 0, i % 4, true));
  }
  for (auto &it : threads) {
    it.join();
  }
  ASSERT_FALSE(err_occured);
}

TEST(Device, MluTaskQueue) {
  auto task_queue = edk::CreateTaskQueue();
  ASSERT_TRUE(task_queue);
  EXPECT_TRUE(edk::MluTaskQueueProxy::GetCnrtQueue(task_queue));
  task_queue = edk::MluTaskQueue::Create();
  ASSERT_TRUE(task_queue);
  EXPECT_TRUE(edk::MluTaskQueueProxy::GetCnrtQueue(task_queue));

  cnrtQueue_t queue;
  cnrtRet_t ret = cnrtCreateQueue(&queue);
  ASSERT_EQ(ret, CNRT_RET_SUCCESS) << "Create cnrtQueue failed.";
  task_queue = edk::MluTaskQueueProxy::Wrap(queue);
  ASSERT_TRUE(task_queue);
  EXPECT_EQ(queue, edk::MluTaskQueueProxy::GetCnrtQueue(task_queue));

  cnrtQueue_t queue2;
  ret = cnrtCreateQueue(&queue2);
  ASSERT_EQ(ret, CNRT_RET_SUCCESS) << "Create cnrtQueue failed.";
  edk::MluTaskQueueProxy::SetCnrtQueue(task_queue, queue2);
  EXPECT_EQ(queue2, edk::MluTaskQueueProxy::GetCnrtQueue(task_queue));
}
