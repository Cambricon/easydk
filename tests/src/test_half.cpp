#include <gtest/gtest.h>
#include <sstream>

#include "../../src/easybang/resize_and_convert/half.hpp"

TEST(EasyBang_Half, operator) {
  half h1 = 100.0f;
  half h2(200.0f);

  half ret;

  ret = h1 + h2;

  EXPECT_TRUE(ret == half(300.0f));

  ret = h1 * h2;

  EXPECT_TRUE(ret == half(20000.0f));

  ret = h2 - h1;

  EXPECT_EQ(static_cast<int>(ret), 100);

  ret = h2 / h1;

  EXPECT_TRUE(ret == half(2));

  ret -= 1;

  EXPECT_TRUE(ret == half(1));

  ret += 1;

  EXPECT_TRUE(ret == half(2));

  EXPECT_TRUE(ret >= half(1));

  EXPECT_TRUE(ret <= half(3));

  ret = 1 + ret;

  EXPECT_EQ(static_cast<int>(ret), 3);

  float tf = static_cast<float>(ret);

  EXPECT_EQ(tf, 3.0f);

  double td = static_cast<double>(ret);

  EXPECT_EQ(td, 3.0);

  std::stringstream ss;
  ss << ret;
  ss >> ret;

  ret = -ret;

  EXPECT_TRUE((float)ret == -3.0f);

  ret *= half(-1);

  EXPECT_TRUE(ret == half(3.0));

  ret /= half(1.0f);

  EXPECT_TRUE((float)ret == 3.0f);

  EXPECT_TRUE(ret > half(1.0f));

  EXPECT_TRUE(ret < half(4.0f));

  EXPECT_TRUE(ret != half(4.0f));
}
