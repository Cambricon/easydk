#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <memory>
#include <string>

#include "fixture.h"
#include "infer_server.h"

namespace infer_server {

std::map<std::string, int> g_param_set{{"number1", 1}, {"number2", 2}, {"number4", 4}};

class TestProcessor : public ProcessorForkable<TestProcessor> {
 public:
  TestProcessor() noexcept : ProcessorForkable("TestProcessor") { std::cout << "[TestProcessor] Construct\n"; }

  ~TestProcessor() { std::cout << "[TestProcessor] Destruct\n"; }

  Status Process(PackagePtr data) noexcept override {
    std::cout << "[TestProcessor] Process\n";
    if (!initialized_) return Status::ERROR_BACKEND;
    for (auto& it : g_param_set) {
      if (!HaveParam(it.first)) return Status::INVALID_PARAM;
      if (GetParam<int>(it.first) != it.second) return Status::INVALID_PARAM;
    }
    return Status::SUCCESS;
  }

  Status Init() noexcept override {
    std::cout << "[TestProcessor] Init\n";
    initialized_ = true;
    return Status::SUCCESS;
  }

 private:
  bool initialized_{false};
};

TEST_F(InferServerTestAPI, Processor) {
  TestProcessor processor;
  for (auto& it : g_param_set) {
    processor.SetParams(it.first, it.second);
  }
  ASSERT_EQ(processor.Init(), Status::SUCCESS);
  ASSERT_EQ(processor.Process({}), Status::SUCCESS);
  auto fork = processor.Fork();
  ASSERT_TRUE(fork);
  ASSERT_NE(fork.get(), &processor);
  ASSERT_EQ(fork->Process({}), Status::SUCCESS);

  // two processor should have independent params
  processor.PopParam<int>(g_param_set.begin()->first);
  EXPECT_TRUE(fork->HaveParam(g_param_set.begin()->first));
}

}  // namespace infer_server
