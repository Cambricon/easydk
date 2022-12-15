/*************************************************************************
 * Copyright (C) [2021] by Cambricon, Inc. All rights reserved
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
#include "mps_service_impl.hpp"
#include <mutex>
#include "glog/logging.h"
#include "mps_internal/cnsample_comm.h"
#if LIBMPS_VERSION_INT >= MPS_VERSION_1_1_0
#include "cn_vb.h"
#endif

namespace cnedk {

int MpsServiceImpl::Init(const MpsServiceConfig &config) {
  // check config, TODO(gaoyujia)
  mps_config_ = config;

  // Due to the mps-sdk design that vpps must use public common-vbpool,
  //    we have to config common-VB-buffers first ...
  //
  if (mps_vout_->Config(config) != 0) {
    LOG(ERROR) << "[EasyDK] [MpsServiceImpl] Init(): Config Vout failed";
    goto err_exit;
  }
  if (mps_vin_->Config(config) != 0) {
    LOG(ERROR) << "[EasyDK] [MpsServiceImpl] Init(): Config Vin failed";
    goto err_exit;
  }
  if (mps_vdec_->Config(config) != 0) {
    LOG(ERROR) << "[EasyDK] [MpsServiceImpl] Init(): Config Vdec failed";
    goto err_exit;
  }
  if (mps_venc_->Config(config) != 0) {
    LOG(ERROR) << "[EasyDK] [MpsServiceImpl] Init(): Config Venc failed";
    goto err_exit;
  }
  cnS32_t ret;
  ret = InitSys();
  if (ret) {
    goto err_exit;
  }

  // init
  if (mps_vout_->Init() != 0) {
    LOG(ERROR) << "[EasyDK] [MpsServiceImpl] Init(): Init Vout failed";
    goto err_exit;
  }

  if (mps_vin_->Init() != 0) {
    LOG(ERROR) << "[EasyDK] [MpsServiceImpl] Init(): Init Vin failed";
    goto err_exit;
  }
  return 0;

err_exit:
  Destroy();
  return -1;
}

void MpsServiceImpl::Destroy() {
  mps_vin_->Destroy();
  mps_vout_->Destroy();
  cnsampleCommSysExit();
}

void MpsServiceImpl::OnVBInfo(cnU64_t blkSize, int blkCount) {
  if (pool_cfgs_.find(blkSize) != pool_cfgs_.end()) {
    pool_cfgs_[blkSize] += blkCount;
  } else {
    pool_cfgs_[blkSize] = blkCount;
  }
}

cnS32_t MpsServiceImpl::InitSys() {
  cnS32_t ret = CN_SUCCESS;

  // pre-allocated buffer pools, mainly for VPPS
  for (auto &it : mps_config_.vbs) {
    if (pool_cfgs_.count(it.block_size)) {
      pool_cfgs_[it.block_size] += it.block_count;
    } else {
      pool_cfgs_[it.block_size] = it.block_count;
    }
  }

  // do init
#if LIBMPS_VERSION_INT < MPS_VERSION_1_1_0
  cnrtVBConfigs_t vb_conf;
#else
  cnVbCfg_t vb_conf;
#endif
  memset(&vb_conf, 0, sizeof(vb_conf));
  vb_conf.maxPoolCnt = pool_cfgs_.size();
  int i = 0;
  for (auto &it : pool_cfgs_) {
    vb_conf.poolConfigs[i].blkSize = it.first;
    vb_conf.poolConfigs[i].blkCnt = it.second;
    VLOG(2) << "[EasyDK] [MpsServiceImpl] InitSys(): pool[" << i << "]: block size = " << it.first
            << ", block count = " << it.second;
    ++i;
  }

  ret = cnsampleCommSysInit(&vb_conf);
  if (CN_SUCCESS != ret) {
    CNSAMPLE_TRACE("system init failed with %d!\n", ret);
    LOG(ERROR) << "[EasyDK] [MpsServiceImpl] InitSys(): cnsampleCommSysInit failed, ret = " << ret;
    return CN_FAILURE;
  }
  return CN_SUCCESS;
}
};  // namespace cnedk
