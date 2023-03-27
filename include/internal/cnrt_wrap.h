#ifndef EDK_INTERNAL_CNRT_WRAP_H_
#define EDK_INTERNAL_CNRT_WRAP_H_

#include <cnrt.h>

// cnrt changed api name at v5.0.0
namespace cnrt {
#if CNRT_MAJOR_VERSION < 5
// cnrtQueue_t
inline cnrtRet_t QueueCreate(cnrtQueue_t *pqueue) { return cnrtCreateQueue(pqueue); }
inline cnrtRet_t QueueDestroy(cnrtQueue_t queue) { return cnrtDestroyQueue(queue); }
inline cnrtRet_t QueueSync(cnrtQueue_t queue) { return cnrtSyncQueue(queue); }
// cnrtNotifier_t
inline cnrtRet_t NotifierCreate(cnrtNotifier_t *pn) { return cnrtCreateNotifier(pn); }
inline cnrtRet_t NotifierDestroy(cnrtNotifier_t n) { return cnrtDestroyNotifier(&n); }
inline cnrtRet_t NotifierDuration(cnrtNotifier_t start, cnrtNotifier_t end, float* dura) {
  return cnrtNotifierDuration(start, end, dura);
}
inline cnrtRet_t TransDataOrder(void* src, cnrtDataType dtype, void* dst,
                                size_t dim_num, int* dim_vals, int* dim_order) {
  return cnrtTransDataOrder(src, dtype, dst, dim_num, dim_vals, dim_order);
}
inline cnrtRet_t TransOrderAndCast(void* src, cnrtDataType src_dtype,
                                   void* dst, cnrtDataType dst_dtype,
                                   cnrtQuantizedParam_t param,
                                   size_t dim_num, int* dim_vals, int* dim_order) {
  return cnrtTransOrderAndCast(src, src_dtype, dst, dst_dtype, param, dim_num, dim_vals, dim_order);
}
#else
// cnrtQueue_t
inline cnrtRet_t QueueCreate(cnrtQueue_t *pqueue) { return cnrtQueueCreate(pqueue); }
inline cnrtRet_t QueueDestroy(cnrtQueue_t queue) { return cnrtQueueDestroy(queue); }
inline cnrtRet_t QueueSync(cnrtQueue_t queue) { return cnrtQueueSync(queue); }
// cnrtNotifier_t
inline cnrtRet_t NotifierCreate( cnrtNotifier_t *pn) { return cnrtNotifierCreate(pn); }
inline cnrtRet_t NotifierDestroy(cnrtNotifier_t n) { return cnrtNotifierDestroy(n); }
inline cnrtRet_t NotifierDuration(cnrtNotifier_t start, cnrtNotifier_t end, float* dura) {
  return cnrtNotifierElapsedTime(start, end, dura);
}
template<typename T>
inline void TransDataOrderImpl(void* src_data, void* dst_data, int* dim_vals, int* dim_order) {
  T* src = reinterpret_cast<T*>(src_data);
  T* dst = reinterpret_cast<T*>(dst_data);
  // dim_num 4
  int xi[4];
  memset(xi, 0, sizeof(xi));
  int dst_last_one_dim = dim_vals[dim_order[3]];
  int dst_last_two_dim = dim_vals[dim_order[2]] * dst_last_one_dim;
  int dst_last_three_dim = dim_vals[dim_order[1]] * dst_last_two_dim;
  int src_idx = 0;
  int dst_idx = 0;
  while (xi[0] < dim_vals[0]) {
    xi[1] = 0;
    while (xi[1] < dim_vals[1]) {
      xi[2] = 0;
      while (xi[2] < dim_vals[2]) {
        xi[3] = 0;
        while (xi[3] < dim_vals[3]) {
          dst_idx = xi[dim_order[0]] * dst_last_three_dim +
                    xi[dim_order[1]] * dst_last_two_dim +
                    xi[dim_order[2]] * dst_last_one_dim +
                    xi[dim_order[3]];
          dst[dst_idx] = src[src_idx];
          xi[3]++;
          src_idx++;
        }
        xi[2]++;
      }
      xi[1]++;
    }
    xi[0]++;
  }
}

inline cnrtRet_t TransDataOrder(void* src, cnrtDataType dtype, void* dst,
                                size_t dim_num, int* dim_vals, int* dim_order) {
  if (dim_num != 4) {
    return cnrtErrorUnknown;
  }
  switch (dtype) {
    case cnrtFloat32:
      TransDataOrderImpl<float>(src, dst, dim_vals, dim_order);
      break;
    case cnrtFloat16:
      TransDataOrderImpl<uint16_t>(src, dst, dim_vals, dim_order);
      break;
    case cnrtUInt8:
      TransDataOrderImpl<uint8_t>(src, dst, dim_vals, dim_order);
      break;
    case cnrtInt32:
      TransDataOrderImpl<int32_t>(src, dst, dim_vals, dim_order);
      break;
    case cnrtInt16:
      TransDataOrderImpl<int16_t>(src, dst, dim_vals, dim_order);
      break;
    default:
      break;
  }
  return CNRT_RET_SUCCESS;
}

inline cnrtRet_t TransOrderAndCast(void* src, cnrtDataType src_dtype,
                                   void* dst, cnrtDataType dst_dtype,
                                   cnrtQuantizedParam_t param,
                                   size_t dim_num, int* dim_vals, int* dim_order) {
  if (dim_num != 4) {
    return cnrtErrorUnknown;
  }
  int size = 1;
  for (unsigned i = 0; i < dim_num; i++) {
    size *= dim_vals[i];
  }
  int dtype_size = 0;
  switch (src_dtype) {
    case cnrtFloat32:
      dtype_size = 4;
      break;
    case cnrtFloat16:
      dtype_size = 2;
      break;
    case cnrtUInt8:
      dtype_size = 1;
      break;
    case cnrtInt32:
      dtype_size = 4;
      break;
    case cnrtInt16:
      dtype_size = 2;
      break;
    default:
      break;
  }
  void* dst_tmp = malloc(size * dtype_size);
  if (!dst_tmp) return cnrtErrorUnknown;
  TransDataOrder(src, src_dtype, dst_tmp, dim_num, dim_vals, dim_order);

  cnrtRet_t ret =  cnrtCastDataType(dst_tmp, src_dtype, dst, dst_dtype, size, param);
  free(dst_tmp);
  return ret;
}
#endif

inline cnrtRet_t PlaceNotifier(cnrtNotifier_t notifier, cnrtQueue_t queue) {
  return cnrtPlaceNotifier(notifier, queue);
}
inline cnrtRet_t CastDataType(void* src, cnrtDataType src_dtype,
                              void* dst, cnrtDataType dst_dtype,
                              size_t size, cnrtQuantizedParam_t param) {
  return cnrtCastDataType(src, src_dtype, dst, dst_dtype, size, param);
}

}  // namespace cnrt

#endif  // EDK_INTERNAL_CNRT_WRAP_H_
