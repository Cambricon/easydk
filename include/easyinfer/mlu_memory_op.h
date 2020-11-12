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

/**
 * @file mlu_memory_op.h
 *
 * This file contains a declaration of the MluMemoryOp class.
 */

#ifndef EASYINFER_MLU_MEMORY_OP_H_
#define EASYINFER_MLU_MEMORY_OP_H_

#include <memory>
#include "cxxutil/edk_attribute.h"
#include "cxxutil/exception.h"
#include "easyinfer/model_loader.h"

namespace edk {

/**
 * @brief MluMemoryOp is a MLU memory helper class.
 * @note It provides a easy way to manage memory on MLU.
 */
class MluMemoryOp {
 public:
  /**
   * @brief Construct a new Mlu Memory Op object
   */
  MluMemoryOp();

  /**
   * @brief Set ModelLoader
   *
   * @deprecated
   * @note model loader is used for manage model's input and output memory easily,
   *       do not need to set if this feature is not used.
   * @param model Model loader
   * @see edk::ModelLoader
   */
  attribute_deprecated void SetLoader(std::shared_ptr<ModelLoader> ploader);

  /**
   * @brief Set ModelLoader
   *
   * @note model loader is used for manage model's input and output memory easily,
   *       do not need to set if this feature is not used.
   * @param model Model loader
   * @see edk::ModelLoader
   */
  void SetModel(std::shared_ptr<ModelLoader> model);

  /**
   * @brief Get model loader
   *
   * @deprecated
   * @return Model loader
   */
  attribute_deprecated std::shared_ptr<ModelLoader> Loader() const;

  /**
   * @brief Get model loader
   *
   * @return Model loader
   */
  std::shared_ptr<ModelLoader> Model() const;

  /**
   * @brief Alloc memory on CPU for model input
   * @deprecated use AllocCpuInput(void) instead
   *
   * @note Input data shape is described by input_shapes from ModelLoader
   * @attention Ensure SetModel has been called once
   * @param batch_size batch size
   * @return Alloced CPU memory
   */
  attribute_deprecated void **AllocCpuInput(uint32_t batch_size) const;

  /**
   * @brief Alloc memory on CPU for model input
   *
   * @note Input data shape is described by input_shapes from ModelLoader
   * @attention Ensure SetModel has been called once
   * @return Alloced CPU memory
   */
  void **AllocCpuInput() const;

  /**
   * @brief Alloc memory on CPU for model output
   * @deprecated use AllocCpuOutput(void) instead
   *
   * @note Output data shape is described by output_shapes from ModelLoader
   * @attention Ensure SetModel has been called once
   * @param batch_size batch size
   * @return Alloced CPU memory
   */
  attribute_deprecated void **AllocCpuOutput(uint32_t batch_size) const;

  /**
   * @brief Alloc memory on CPU for model output
   *
   * @note Output data shape is described by output_shapes from ModelLoader
   * @attention Ensure SetModel has been called once
   * @return Alloced CPU memory
   */
  void **AllocCpuOutput() const;

  /**
   * @brief Alloc memory on MLU according to nBytes.
   * @deprecated use AllocMlu(size_t) instead
   *
   * @param nBytes Alloced memory size in bytes
   * @param batch_size Batch size
   * @return Alloced MLU memory
   */
  attribute_deprecated void *AllocMlu(size_t nBytes, uint32_t batch_size) const;

  /**
   * @brief Alloc memory on MLU according to nBytes.
   *
   * @param nBytes Alloced memory size in bytes
   * @return Alloced MLU memory
   */
  void *AllocMlu(size_t nBytes) const;

  /**
   * @brief Alloc memory on MLU for model input
   * @deprecated use AllocMluInput(void) instead
   *
   * @note Input data shape is described by input_data_descs from ModelLoader
   * @attention Ensure SetModel has been called once
   * @param batch_size Batch size
   * @return Alloced MLU memory
   */
  attribute_deprecated void **AllocMluInput(uint32_t batch_size) const;

  /**
   * @brief Alloc memory on MLU for model input
   *
   * @note Input data shape is described by input_data_descs from ModelLoader
   * @attention Ensure SetModel has been called once
   * @return Alloced MLU memory
   */
  void **AllocMluInput() const;

  /**
   * @brief Alloc memory on MLU for model output
   * @deprecated use AllocMluOutput(void) instead
   *
   * @note Input data shape is described by output_data_descs from ModelLoader
   * @attention Ensure SetModel has been called once
   * @param batch_size Batch size
   * @return Alloced MLU memory
   */
  attribute_deprecated void **AllocMluOutput(uint32_t batch_size) const;

  /**
   * @brief Alloc memory on MLU for model output
   *
   * @note Input data shape is described by output_data_descs from ModelLoader
   * @attention Ensure SetModel has been called once
   * @return Alloced MLU memory
   */
  void **AllocMluOutput() const;

  /**
   * @brief Free input memory on CPU.
   *
   * @attention Ensure SetModel has been called once
   * @param ptr CPU memory pointer
   */
  void FreeCpuInput(void **ptr) const;

  /**
   * @brief Free output memory on CPU.
   *
   * @attention Ensure SetModel has been called once
   * @param ptr CPU memory pointer
   */
  void FreeCpuOutput(void **ptr) const;

  /**
   * @brief Free memory array on MLU
   *
   * @deprecated
   * @param ptr Memory array on MLU
   * @param mem_num Memory number, usually input_num or output_num got from ModelLoader
   */
  attribute_deprecated void FreeArrayMlu(void **ptr, uint32_t mem_num) const;

  /**
   * @brief Free input memory on MLU
   *
   * @param ptr Memory array on MLU
   * @attention Ensure SetModel has been called once
   */
  void FreeMluInput(void **ptr) const;

  /**
   * @brief Free output memory on MLU
   *
   * @param ptr Memory array on MLU
   * @attention Ensure SetModel has been called once
   */
  void FreeMluOutput(void **ptr) const;

  /**
   * @brief Free memory on MLU
   *
   * @param ptr MLU memory pointer
   */
  void FreeMlu(void *ptr) const;

  /**
   * @brief Copy model input data, from host(CPU) to device(MLU)
   * @deprecated use MemcpyInputH2D(void**, void**) instead
   *
   * @attention Ensure SetModel has been called once
   * @param mlu_dst Copy destination, memory on MLU
   * @param cpu_src Copy source, data on CPU
   * @param batch_size Batch size
   */
  attribute_deprecated void MemcpyInputH2D(void **mlu_dst, void **cpu_src, uint32_t batch_size) const;

  /**
   * @brief Copy model input data, from host(CPU) to device(MLU)
   *
   * @attention Ensure SetModel has been called once
   * @param mlu_dst Copy destination, memory on MLU
   * @param cpu_src Copy source, data on CPU
   */
  void MemcpyInputH2D(void **mlu_dst, void **cpu_src) const;

  /**
   * @brief Copy model output data, from device to host
   * @deprecated use MemcpyOutputD2H(void**, void**) instead
   *
   * @attention Ensure SetModel has been called once
   * @param cpu_dst Copy destination, memory on CPU
   * @param mlu_src Copy source, data on MLU
   * @param batch_size Batch size
   */
  attribute_deprecated void MemcpyOutputD2H(void **cpu_dst, void **mlu_src, uint32_t batch_size) const;

  /**
   * @brief Copy model output data, from device to host
   *
   * @attention Ensure SetModel has been called once
   * @param cpu_dst Copy destination, memory on CPU
   * @param mlu_src Copy source, data on MLU
   */
  void MemcpyOutputD2H(void **cpu_dst, void **mlu_src) const;

  /**
   * @brief Copy data from host to device
   * @deprecated use MemcpyH2D(void**, void**, size_t) instead
   *
   * @param mlu_dst Copy destination, memory on MLU
   * @param cpu_src Copy source, data on CPU
   * @param nBytes Memory size in bytes
   * @param batch_size Batch size
   */
  attribute_deprecated void MemcpyH2D(void *mlu_dst, void *cpu_src, size_t nBytes, uint32_t batch_size) const;

  /**
   * @brief Copy data from host to device
   *
   * @param mlu_dst Copy destination, memory on MLU
   * @param cpu_src Copy source, data on CPU
   * @param nBytes Memory size in bytes
   */
  void MemcpyH2D(void *mlu_dst, void *cpu_src, size_t nBytes) const;

  /**
   * @brief Copy data from device to host
   * @deprecated use MemcpyD2H(void**, void**, size_t) instead
   *
   * @param cpu_dst Copy destination, memory on CPU
   * @param mlu_src Copy source, data on MLU
   * @param nBytes Memory size in bytes
   * @param batch_size Batch size
   */
  attribute_deprecated void MemcpyD2H(void *cpu_dst, void *mlu_src, size_t nBytes, uint32_t batch_size) const;

  /**
   * @brief Copy data from device to host
   *
   * @param cpu_dst Copy destination, memory on CPU
   * @param mlu_src Copy source, data on MLU
   * @param nBytes Memory size in bytes
   */
  void MemcpyD2H(void *cpu_dst, void *mlu_src, size_t nBytes) const;

  /**
   * @brief Copy data from device to device
   *
   * @param mlu_dst Copy destination, memory on MLU
   * @param mlu_src Copy source, data on MLU
   * @param nBytes Memory size in bytes
   */
  void MemcpyD2D(void *mlu_dst, void *mlu_src, size_t nBytes) const;

 private:
  std::shared_ptr<ModelLoader> model_;
};  // class MluMemoryOp

}  // namespace edk

#endif  // EASYINFER_MLU_MEMORY_OP_H_
