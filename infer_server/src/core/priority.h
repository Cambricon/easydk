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

#ifndef INFER_SERVER_CORE_PRIORITY_H_
#define INFER_SERVER_CORE_PRIORITY_H_

namespace infer_server {

class Priority {
 public:
  constexpr explicit Priority(int base) noexcept : major_(ShiftMajor(BaseToMajor(base))) {}
  constexpr int64_t Get(int64_t bias) const noexcept { return major_ + bias; }

  // limit major priority in { p | p = x << 56, x in [0, 90] }
  constexpr static int BaseToMajor(int base) noexcept { return 10 * (base < 0 ? 0 : (base < 9 ? base : 9)); }
  constexpr static int64_t ShiftMajor(int m) noexcept { return static_cast<int64_t>(m) << 56; }
  constexpr static int64_t Offset(int64_t priority, int offset) noexcept { return priority + ShiftMajor(offset); }
  constexpr static int64_t Next(int64_t priority) noexcept { return Offset(priority, 1); }

  constexpr bool operator<(const Priority& other) const noexcept { return major_ < other.major_; }
  constexpr bool operator>(const Priority& other) const noexcept { return major_ > other.major_; }
  constexpr bool operator==(const Priority& other) const noexcept { return major_ == other.major_; }

 private:
  int64_t major_{0};
};

}  // namespace infer_server

#endif  // INFER_SERVER_CORE_PRIORITY_H_
