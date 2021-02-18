#include <gtest/gtest.h>

#include "cxxutil/log.h"

int main(int argc, char *argv[]) {
  edk::log::InitLogging(true, true);
  ::testing::InitGoogleTest(&argc, argv);
  int ret = RUN_ALL_TESTS();
  edk::log::ShutdownLogging();
  return ret;
}
