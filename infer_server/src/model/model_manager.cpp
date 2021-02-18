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

#include "model.h"

#include <glog/logging.h>
#include <algorithm>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "util/env.h"

namespace infer_server {

std::unordered_map<std::string, std::shared_ptr<Model>> ModelManager::model_cache_;
std::mutex ModelManager::model_cache_mutex_;

#define RETURN_VAL_IF_FAIL(cond, msg, ret_val) \
  do {                                         \
    if (!(cond)) {                             \
      LOG(ERROR) << msg;                       \
      return ret_val;                          \
    }                                          \
  } while (0)

namespace detail {
struct BeginWith {
  explicit BeginWith(const std::string& str) noexcept : s(str) {}
  inline bool operator()(const std::string& prefix) noexcept {
    if (s.size() < prefix.size()) return false;
    return prefix == s.substr(0, prefix.size());
  }
  std::string s;
};  // struct BeginWith

inline bool IsNetFile(const std::string& url) {
  static const std::vector<std::string> protocols = {"http://", "https://", "ftp://"};
  return std::any_of(protocols.cbegin(), protocols.cend(), BeginWith(url));
}
}  // namespace detail

ModelManager* ModelManager::Instance() noexcept {
  static ModelManager m;
  return &m;
}

static inline const std::string GetModelKey(const std::string& model_path, const std::string& func_name) noexcept {
  return model_path + func_name;
};

static inline const std::string GetModelKey(const void* mem_ptr, const std::string& func_name) noexcept {
  std::ostringstream ss;
  ss << mem_ptr << func_name;
  return ss.str();
};

void ModelManager::CheckAndCleanCache() noexcept {
  if (model_cache_.size() >= GetUlongFromEnv("CNIS_MODEL_CACHE_LIMIT", 10)) {
    for (auto& p : model_cache_) {
      if (p.second.use_count() == 1) {
        model_cache_.erase(p.first);
        break;
      }
    }
  }
}

ModelPtr ModelManager::Load(const std::string& url, const std::string& func_name) noexcept {
  std::string model_path;
  // check if model file exist
  if (detail::IsNetFile(url)) {
    model_path = DownloadModel(url);
    RETURN_VAL_IF_FAIL(!model_path.empty(), "Download model file failed: " + url, nullptr);
  } else {
    model_path = url;
    std::ifstream f;
    f.open(model_path);
    RETURN_VAL_IF_FAIL(f.is_open(), "Model file not exist. Please check model path: " + model_path, nullptr);
    f.close();
  }

  std::string model_key = GetModelKey(model_path, func_name);

  std::unique_lock<std::mutex> lk(model_cache_mutex_);
  if (model_cache_.find(model_key) == model_cache_.cend()) {
    // cache not hit
    LOG(INFO) << "Load model from file: " << model_path;
    auto model = std::make_shared<Model>();
    if (!model->Init(model_path, func_name)) {
      return nullptr;
    }
    CheckAndCleanCache();
    model_cache_[model_key] = model;
    return model;
  } else {
    // cache hit
    LOG(INFO) << "Get model from cache";
    return model_cache_.at(model_key);
  }
};

ModelPtr ModelManager::Load(void* mem_ptr, const std::string& func_name) noexcept {
  // check model in cache valid
  RETURN_VAL_IF_FAIL(mem_ptr, "Invalid memory pointer, please check model cached in memory", nullptr);

  std::string model_key = GetModelKey(mem_ptr, func_name);

  std::unique_lock<std::mutex> lk(model_cache_mutex_);
  if (model_cache_.find(model_key) == model_cache_.cend()) {
    // cache not hit
    LOG(INFO) << "Load model from memory, " << mem_ptr;
    auto model = std::make_shared<Model>();
    if (!model->Init(mem_ptr, func_name)) {
      return nullptr;
    }
    CheckAndCleanCache();
    model_cache_[model_key] = model;
    return model;
  } else {
    // cache hit
    LOG(INFO) << "Get model from cache";
    return model_cache_.at(model_key);
  }
};

std::shared_ptr<Model> ModelManager::GetModel(const std::string& name) noexcept {
  std::unique_lock<std::mutex> lk(model_cache_mutex_);
  if (model_cache_.find(name) == model_cache_.cend()) {
    return nullptr;
  }
  return model_cache_.at(name);
}

int ModelManager::CacheSize() noexcept { return model_cache_.size(); }

bool ModelManager::Unload(ModelPtr model) noexcept {
  RETURN_VAL_IF_FAIL(model, "model is nullptr!", false);
  const std::string& model_key = model->GetKey();
  std::lock_guard<std::mutex> lk(model_cache_mutex_);
  if (model_cache_.find(model_key) == model_cache_.cend()) {
    LOG(WARNING) << "model not in cache";
    return false;
  } else {
    model_cache_.erase(model_key);
    return true;
  }
}

void ModelManager::ClearCache() noexcept {
  std::lock_guard<std::mutex> lk(model_cache_mutex_);
  model_cache_.clear();
}

#ifdef CNIS_HAVE_CURL
#include <curl/curl.h>
#include <cstdio>

class CurlDownloader {
 public:
  explicit CurlDownloader(const std::string& model_dir) : model_dir_(model_dir) {
    CHECK_EQ(access(model_dir_.c_str(), W_OK), 0)
        << "model directory not exist or do not have write permission: " << model_dir_;
    curl_ = curl_easy_init();
    curl_easy_setopt(curl_, CURLOPT_NOPROGRESS, false);
    curl_easy_setopt(curl_, CURLOPT_ERRORBUFFER, errbuf_);
  }

  std::string Download(const std::string& url) noexcept {
    // get name of model file
    std::string file_path = model_dir_ + url.substr(url.find_last_of('/'));

    if (access(file_path.c_str(), F_OK) == 0) {
      LOG(INFO) << "model exists in specified directory, skip download";
      return file_path;
    } else {
      FileHandle f(file_path, "wb");
      FILE* file = f.GetFile();

      curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());
      curl_easy_setopt(curl_, CURLOPT_WRITEDATA, file);
      LOG(INFO) << "url: " << url;
      LOG(INFO) << "file: " << file_path;

      auto re = curl_easy_perform(curl_);
      if (re != CURLE_OK) {
        LOG(ERROR) << "Download model error, error_code: " << re;
        size_t len = strlen(errbuf_);
        if (len) {
          LOG(ERROR) << "extra message from cURL: " << errbuf_;
        } else {
          LOG(ERROR) << "extra message from cURL" << curl_easy_strerror(re);
        }
        LOG(ERROR) << "model url: " << url;
        return {};
      }
      return file_path;
    }
  }

  ~CurlDownloader() {
    if (curl_) {
      curl_easy_cleanup(curl_);
      curl_ = nullptr;
    }
  }

 private:
  class FileHandle {
   public:
    explicit FileHandle(const std::string& fpath, const char* mode) { file_ = fopen(fpath.c_str(), mode); }
    FILE* GetFile() { return file_; }
    ~FileHandle() { fclose(file_); }

   private:
    FileHandle(const FileHandle&) = delete;
    const FileHandle& operator=(const FileHandle&) = delete;
    FILE* file_ = nullptr;
  };  // class FileHandle

  std::string model_dir_;
  CURL* curl_ = nullptr;
  char errbuf_[CURL_ERROR_SIZE];
};  // class CurlDownloader

std::string ModelManager::DownloadModel(const std::string& url) noexcept {
  thread_local CurlDownloader downloader(model_dir_);
  return downloader.Download(url);
}
#else

std::string ModelManager::DownloadModel(const std::string& url) noexcept {
  LOG(ERROR) << "Build without curl, download model from net is not supported";
  return {};
}

#endif  // CNIS_WITH_CURL

}  // namespace infer_server
