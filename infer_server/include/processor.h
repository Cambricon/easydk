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

#ifndef INFER_SERVER_PROCESSOR_H_
#define INFER_SERVER_PROCESSOR_H_

#include <memory>
#include <vector>

#include "buffer.h"
#include "infer_server.h"
#include "shape.h"

namespace infer_server {

// -------------------- Predictor --------------------
/**
 * @brief a structure storing input and output data of inference
 */
struct ModelIO {
  /// input / output data
  std::vector<Buffer> buffers;
  /// shape of input / output
  std::vector<Shape> shapes;
};

struct PredictorPrivate;
/**
 * @brief Predictor processor
 */
class Predictor : public ProcessorForkable<Predictor> {
 public:
  /**
   * @brief Construct a new Predictor object
   */
  Predictor() noexcept;

  /**
   * @brief Destroy the Predictor object
   */
  ~Predictor();

  /**
   * @brief Perform predict
   *
   * @param data processed data
   * @retval Status::SUCCESS Succeeded
   * @retval Status::INVALID_PARAM Predictor get uncontinuous data
   * @retval Status::WRONG_TYPE Predictor get data of wrong type (bad_any_cast)
   * @retval Status::ERROR_BACKEND Predict failed
   */
  Status Process(PackagePtr data) noexcept override;

  /**
   * @brief Initialize predictor
   *
   * @retval Status::SUCCESS Init succeeded
   * @retval Status::INVALID_PARAM Predictor donot have enough params or get wrong params, @see BaseObject::SetParam
   * @retval Status::WRONG_TYPE Predictor get params of wrong type (bad_any_cast)
   */
  Status Init() noexcept override;

 private:
  PredictorPrivate* priv_;
};  // class Predictor
// -------------------- Predictor END --------------------

// -------------------- PreprocessorHost --------------------
struct PreprocessorHostPrivate;
/**
 * @brief PreprocessorHost processor
 */
class PreprocessorHost : public ProcessorForkable<PreprocessorHost> {
 public:
  /**
   * @brief Preprocess function on single data, set by user
   */
  using ProcessFunction = std::function<bool(ModelIO*, const InferData&, const ModelInfo&)>;

  /**
   * @brief Construct a new PreprocessorHost object
   */
  PreprocessorHost() noexcept;

  /**
   * @brief Destroy the PreprocessorHost object
   */
  ~PreprocessorHost();

  /**
   * @brief Perform preprocess on host
   *
   * @param data processed data
   * @retval Status::SUCCESS Succeeded
   * @retval Status::ERROR_BACKEND Preprocess failed in transform layout or process_function set by user
   */
  Status Process(PackagePtr data) noexcept override;

  /**
   * @brief Initialize PreprocessorHost
   *
   * @retval Status::SUCCESS Init succeeded
   * @retval Status::INVALID_PARAM Preprocessor donot have enough params or get wrong params, @see BaseObject::SetParam
   * @retval Status::WRONG_TYPE Preprocessor get params of wrong type (bad_any_cast)
   */
  Status Init() noexcept override;

 private:
  PreprocessorHostPrivate* priv_;
};  // class PreprocessorHost
// -------------------- PreprocessorHost END --------------------

// -------------------- Postprocessor --------------------
struct PostprocessorPrivate;
/**
 * @brief Postprocessor processor
 */
class Postprocessor : public ProcessorForkable<Postprocessor> {
 public:
  /**
   * @brief Postprocess function on single data, set by user
   */
  using ProcessFunction = std::function<bool(InferData*, const ModelIO&, const ModelInfo&)>;

  /**
   * @brief Construct a new Postprocessor object
   */
  Postprocessor() noexcept;

  /**
   * @brief Destroy the Postprocessor object
   */
  ~Postprocessor();

  /**
   * @brief Perform postprocess
   *
   * @param data processed data
   * @retval Status::SUCCESS Succeeded
   * @retval Status::INVALID_PARAM Postprocessor get uncontinuous data
   * @retval Status::WRONG_TYPE Postprocessor get data of wrong type (bad_any_cast)
   * @retval Status::ERROR_BACKEND Postprocess failed in transform layout or process_function set by user
   */
  Status Process(PackagePtr data) noexcept override;

  /**
   * @brief Initialize PostprocessorHost
   *
   * @retval Status::SUCCESS Init succeeded
   * @retval Status::INVALID_PARAM Postprocessor donot have enough params or get wrong params, @see BaseObject::SetParam
   * @retval Status::WRONG_TYPE Postprocessor get params of wrong type (bad_any_cast)
   */
  Status Init() noexcept override;

 private:
  PostprocessorPrivate* priv_;
};  // class PreprocessorHost
// -------------------- PostprocessorHost END --------------------

}  // namespace infer_server

#endif  // INFER_SERVER_PROCESSOR_H_
