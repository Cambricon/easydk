#include <gtest/gtest.h>
#include <iostream>

#include "../src/easytrack/matrix.h"

TEST(CxxUtil, Matrix) {
  edk::Matrix m_1, m_answer;
  m_1 = {{1, 2}, {3, 4}};
  EXPECT_EQ(m_1.Cols(), 2);
  EXPECT_EQ(m_1.Rows(), 2);
  EXPECT_TRUE(m_1.Square());
  EXPECT_FALSE(m_1.Empty());

  edk::Matrix m_2(2, 2);
  m_2.Resize(3, 4);
  EXPECT_EQ(m_2.Rows(), 3);
  EXPECT_EQ(m_2.Cols(), 4);
  m_2.Resize(2, 2);
  m_2.Fill(1);

  /*
   *  m1          m2
   * | 1 2 |     | 1 1 |
   * | 3 4 |     | 1 1 |
   */

  EXPECT_TRUE(m_1 != m_2);

  // add
  m_answer = {{2, 3}, {4, 5}};
  EXPECT_EQ(m_1 + m_2, m_answer);

  // minus
  m_answer = {{0, -1}, {-2, -3}};
  EXPECT_EQ(m_2 - m_1, m_answer);

  // times
  m_answer = {{3, 3}, {7, 7}};
  EXPECT_EQ(m_1 * m_2, m_answer);
  m_answer = {{4, 6}, {4, 6}};
  EXPECT_EQ(m_2 * m_1, m_answer);

  // trans
  m_answer = {{1, 3}, {2, 4}};
  EXPECT_EQ(m_1.Trans(), m_answer);

  // inv
  m_answer = {{1, 0}, {0, 1}};
  EXPECT_EQ(m_1.Inv() * m_1, m_answer);

  m_1.Show();
  m_2.Show();

  m_answer = {{3, 3}, {7, 7}};
  m_1 *= m_2;
  EXPECT_EQ(m_1, m_answer);

  m_answer = {{4, 4}, {8, 8}};
  m_2 += m_1;
  EXPECT_EQ(m_2, m_answer);

  m_answer = {{-1, -1}, {-1, -1}};
  m_1 -= m_2;
  EXPECT_EQ(m_1, m_answer);
}
