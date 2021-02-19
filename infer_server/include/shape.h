/*************************************************************************
 * Copyright (C) [2019] by Cambricon, Inc. All rights reserved
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

#ifndef INFER_SERVER_SHAPE_H_
#define INFER_SERVER_SHAPE_H_

#include <iostream>
#include <vector>

namespace infer_server {

/**
 * @brief Shape to describe inference model input and output data
 * @warning No matter how data is placed in memory, dim values always keep in order of NHWC
 */
class Shape {
 public:
  /// stored value type
  using value_type = int;

  /**
   * @brief Construct a new Shape object
   */
  Shape() = default;

  /**
   * @brief Construct a new Shape object from shape vector
   *
   * @param v vector stored shape value
   */
  explicit Shape(const std::vector<value_type>& v) noexcept { data_ = v; }

  Shape(const Shape&) = default;
  Shape& operator=(const Shape&) = default;
  Shape(Shape&&) = default;
  Shape& operator=(Shape&&) = default;

  /**
   * @brief Get value of nth dimension
   *
   * @param offset serial number of dimension
   * @return value_type shape value
   */
  value_type operator[](int offset) const noexcept { return data_[offset]; }

  /**
   * @brief Get value of nth dimension
   *
   * @param offset serial number of dimension
   * @return value_type reference to shape value
   */
  value_type& operator[](int offset) noexcept { return data_[offset]; }

  /**
   * @brief Returns the dimension size of Shape
   *
   * @return size_t The dimension size of Shape
   */
  size_t Size() const noexcept { return data_.size(); };

  /**
   * @brief Returns whether Shape is empty
   *
   * @retval true if the Shape doesn't have any value
   * @retval false otherwise
   */
  bool Empty() const noexcept { return data_.empty(); };

  /**
   * @brief Get vectorized shape value
   *
   * @return std::vector<value_type> vectorized shape value
   */
  std::vector<value_type> Vectorize() const noexcept { return data_; }

  /**
   * @brief Get batchsize
   *
   * @return value_type batch size
   */
  value_type BatchSize() const noexcept { return data_[0]; }

  /**
   * @brief Get n value
   *
   * @note Only work on Shape of 4 dimension, will return 0 if size() != 4.
   * @return value_type n or 0
   */
  value_type GetN() const noexcept {
    if (Size() == 4) {
      return data_[0];
    }
    return 0;
  }

  /**
   * @brief Get height value
   *
   * @note Only work on Shape of 4 dimension, will return 0 if size() != 4.
   * @return value_type height or 0
   */
  value_type GetH() const noexcept {
    if (Size() == 4) {
      return data_[1];
    }
    return 0;
  }

  /**
   * @brief Get width value
   *
   * @note Only work on Shape of 4 dimension, will return 0 if size() != 4.
   * @return value_type width or 0
   */
  value_type GetW() const noexcept {
    if (Size() == 4) {
      return data_[2];
    }
    return 0;
  }

  /**
   * @brief Get channel value
   *
   * @note Only work on Shape of 4 dimension, will return 0 if size() != 4.
   * @return value_type channel or 0
   */
  value_type GetC() const noexcept {
    if (Size() == 4) {
      return data_[3];
    }
    return 0;
  }

  /**
   * @brief Get total data count / batch size
   *
   * @return Data count
   */
  int64_t DataCount() const noexcept {
    int64_t cnt = 1;
    for (size_t i = 1; i < data_.size(); ++i) {
      cnt *= data_[i];
    }
    return cnt;
  }

  /**
   * @brief Get total data count
   *
   * @return Total data count
   */
  int64_t BatchDataCount() const noexcept {
    int64_t cnt = 1;
    for (size_t i = 0; i < data_.size(); ++i) {
      cnt *= data_[i];
    }
    return cnt;
  }

  /**
   * @brief Print shape
   *
   * @param os Output stream
   * @param shape Shape to be printed
   * @return Output stream
   */
  friend std::ostream& operator<<(std::ostream& os, const Shape& shape) {
    os << "Shape (";
    for (size_t i = 0; i < shape.Size() - 1; ++i) {
      os << shape[i] << ", ";
    }
    if (shape.Size() > 0) os << shape[shape.Size() - 1];
    os << ")";
    return os;
  }

  /**
   * @brief Judge whether two shapes are equal
   *
   * @param other another Shape
   * @retval true if two shapes are equal
   * @retval false otherwise
   */
  bool operator==(const Shape& other) const noexcept {
    if (Size() != other.Size()) return false;
    for (size_t i = 0; i < Size(); ++i) {
      if (data_[i] != other[i]) return false;
    }
    return true;
  }

  /**
   * @brief Judge whether two shapes are not equal
   *
   * @param other another Shape
   * @retval true if two shapes are not equal
   * @retval false otherwise
   */
  bool operator!=(const Shape& other) const noexcept { return !(*this == other); }

 private:
  std::vector<value_type> data_;
};  // class Shape

}  // namespace infer_server

#endif  // INFER_SERVER_SHAPE_H_
