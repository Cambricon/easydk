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

#ifndef INFER_SERVER_BASE_OBJECT_H_
#define INFER_SERVER_BASE_OBJECT_H_

#include <map>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "util/any.h"

namespace infer_server {

/**
 * @brief Params object base class
 */
class BaseObject {
 public:
  /**
   * @brief Set param
   *
   * @tparam T Param type
   * @param param_name Unique param name
   * @param param Param value
   */
  template <typename T>
  void SetParams(const std::string& param_name, T&& param) {
    params_[param_name] = std::forward<T>(param);
  }

  /**
   * @brief Set params
   *
   * @tparam T Param type
   * @tparam Args Param_name:param values' type
   * @param param_name Unique param name
   * @param param Param value
   * @param args Param_name:param values
   */
  template <typename T, typename... Args>
  void SetParams(const std::string& param_name, T&& param, Args&&... args) {
    params_[param_name] = std::forward<T>(param);
    SetParams(std::forward<Args>(args)...);
  }

  /**
   * @brief Get param
   *
   * @tparam T Param type
   * @param param_name Param name
   * @return T Specified param
   */
  template <typename T>
  auto GetParam(const std::string& param_name) const -> typename std::remove_reference<T>::type {
    return any_cast<typename std::remove_reference<T>::type>(params_.at(param_name));
  }

  /**
   * @brief Pop param
   *
   * @tparam T Param type
   * @param param_name Param name
   * @return T&& Specified param
   */
  template <typename T>
  T PopParam(const std::string& param_name) {
    T tmp = any_cast<typename std::remove_reference<T>::type>(params_.at(param_name));
    params_.erase(param_name);
    return tmp;
  }

  /**
   * @brief Check if object has specified param
   *
   * @param param_name Param name
   * @retval true Have specified param
   * @retval false Donot have specified param
   */
  bool HaveParam(const std::string& param_name) noexcept { return params_.find(param_name) != params_.cend(); }

  /**
   * @brief Get name of stored params
   *
   * @return std::vector<std::string> Name of stored params
   */
  std::vector<std::string> GetParamNames() noexcept {
    std::vector<std::string> names;
    for (auto& p : params_) {
      names.emplace_back(p.first);
    }
    return names;
  }

  /**
   * @brief Copy params form another object
   *
   * @param other another object
   */
  void CopyParamsFrom(const BaseObject& other) noexcept { params_ = other.params_; }

  /**
   * @brief Destroy the BaseObject object
   */
  virtual ~BaseObject() = default;

 protected:
  std::map<std::string, any> params_;
};

}  // namespace infer_server

#endif  // INFER_SERVER_BASE_OBJECT_H_
