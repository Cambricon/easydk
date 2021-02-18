#include <gtest/gtest.h>

#include <cmath>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "device/mlu_context.h"
#include "device/version.h"
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
  edk::MluContext ctx(0);
  ctx.BindDevice();
  auto task_queue = edk::MluTaskQueue::Create();
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

TEST(Device, TimeMark) {
  using edk::Exception;
  edk::MluContext ctx(0);
  ctx.BindDevice();
  auto task_queue = edk::MluTaskQueue::Create();
  auto cnrt_queue = edk::MluTaskQueueProxy::GetCnrtQueue(task_queue);
  cnrtNotifier_t n_start, n_end;
  std::unique_ptr<edk::TimeMark> mark1, mark2;
  ASSERT_NO_THROW(mark1.reset(new edk::TimeMark));
  ASSERT_NO_THROW(mark2.reset(new edk::TimeMark));
  auto create_notifier = [](cnrtNotifier_t* notifier) {
    CALL_CNRT_FUNC(cnrtCreateNotifier(notifier), "Create notifier failed");
  };
  auto place_notifier = [](cnrtNotifier_t notifier, cnrtQueue_t queue) {
    CALL_CNRT_FUNC(cnrtPlaceNotifier(notifier, queue), "cnrtPlaceNotifier failed");
  };
  auto cal_time = [](cnrtNotifier_t start, cnrtNotifier_t end, float* dura) {
    CALL_CNRT_FUNC(cnrtNotifierDuration(start, end, dura), "Calculate elapsed time failed.");
  };
  ASSERT_NO_THROW(create_notifier(&n_start));
  ASSERT_NO_THROW(create_notifier(&n_end));

  auto q_mark1 = task_queue->PlaceMark();
  ASSERT_NO_THROW(place_notifier(n_start, cnrt_queue));
  ASSERT_NO_THROW(mark1->Mark(task_queue));
  ASSERT_NO_THROW(mark2->Mark(task_queue));
  ASSERT_NO_THROW(place_notifier(n_end, cnrt_queue));
  auto q_mark2 = task_queue->PlaceMark();
  task_queue->Sync();
  float time = edk::TimeMark::Count(*mark1, *mark2);
  EXPECT_NEAR(time, 0, 1e-4);

  ASSERT_NO_THROW(cal_time(n_start, n_end, &time));
  time /= 1000;
  EXPECT_NEAR(time, 0, 1e-4);

  ASSERT_NO_THROW(time = task_queue->Count(q_mark1, q_mark2));
  EXPECT_NEAR(time, 0, 1e-4);

  cnrtDestroyNotifier(&n_start);
  cnrtDestroyNotifier(&n_end);

  auto test_reuse = [&task_queue]() {
    int repeat_time = 1000;
    while (repeat_time--) {
      auto tmp_1 = task_queue->PlaceMark();
      auto tmp_2 = task_queue->PlaceMark();
    }
  };
  EXPECT_NO_THROW(test_reuse());
}

#ifndef EDK_VERSION_MAJOR
#error EDK_VERSION_MAJOR is not defined
#endif

#ifndef EDK_VERSION_MINOR
#error EDK_VERSION_MINOR is not defined
#endif

#ifndef EDK_VERSION_PATCH
#error EDK_VERSION_PATCH is not defined
#endif

#ifndef EDK_GET_VERSION
#error EDK_GET_VERSION is not defined
#endif

#ifndef EDK_VERSION
#error EDK_VERSION is not defined
#endif

TEST(Device, Version) {
  ASSERT_EQ(EDK_VERSION, EDK_GET_VERSION(EDK_VERSION_MAJOR, EDK_VERSION_MINOR, EDK_VERSION_PATCH));
  ASSERT_EQ(EDK_VERSION, (EDK_VERSION_MAJOR << 20) | (EDK_VERSION_MINOR << 10) | EDK_VERSION_PATCH);

  std::string version = std::to_string(EDK_VERSION_MAJOR) + "." +
                        std::to_string(EDK_VERSION_MINOR) + "." +
                        std::to_string(EDK_VERSION_PATCH);
  EXPECT_EQ(version, edk::Version());
}
