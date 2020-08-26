#ifndef vcBatchedRenderQueue_h__
#define vcBatchedRenderQueue_h__

#include "udChunkedArray.h"

template <typename T>
struct vcBatchedRenderQueue
{
  struct Batch
  {
    uint32_t count;
    uint32_t capacity;
    T *pData;
  };

  udResult Init(uint32_t batchSizeBytes);
  udResult Deinit();

  udResult PushInstances(const T *pData, uint32_t count);
  void Clear(bool clearMemory);

  uint32_t GetBatchCount();
  const Batch* GetBatch(uint32_t index);

private:
  udChunkedArray<Batch> batches;
  uint32_t maxBatchSize;
  uint32_t currentBinIndex;
};

template <typename T>
inline udResult vcBatchedRenderQueue<T>::Init(uint32_t batchSizeBytes)
{
  udResult result = udR_Success;

  UD_ERROR_CHECK(batches.Init(32));

  maxBatchSize = batchSizeBytes;

epilogue:
  return result;
}

template <typename T>
inline udResult vcBatchedRenderQueue<T>::Deinit()
{
  Clear(true);
  batches.Deinit();

  return udR_Success;
}

template <typename T>
inline void vcBatchedRenderQueue<T>::Clear(bool clearMemory)
{
  for (size_t i = 0; i < batches.length; ++i)
  {
    vcBatchedRenderQueue::Batch *pBatch = &batches[i];
    pBatch->count = 0;

    if (clearMemory)
      udFree(pBatch->pData);
  }

  if (clearMemory)
    batches.Clear();

  currentBinIndex = 0;
}

template <typename T>
inline udResult vcBatchedRenderQueue<T>::PushInstances(const T *pData, uint32_t count)
{
  udResult result;

  // find an existing bin for pModel
  uint32_t sourceOffset = 0;
  for (; currentBinIndex < batches.length; ++currentBinIndex)
  {
    vcBatchedRenderQueue::Batch *pBatch = &batches[currentBinIndex];
    if (pBatch->count + count < pBatch->capacity)
    {
      memcpy(pBatch->pData + pBatch->count, pData + sourceOffset, sizeof(T) * count);
      pBatch->count += count;
      break;
    }
    else
    {
      // split it up
      uint32_t first = (pBatch->capacity - pBatch->count);
      memcpy(pBatch->pData + pBatch->count, pData + sourceOffset, sizeof(T) * first);
      pBatch->count += first;
      sourceOffset += first;
      count -= first;
    }
  }

  if (currentBinIndex == batches.length)
  {
    // Make a new bin
    vcBatchedRenderQueue::Batch *pBatch = nullptr;
    batches.PushBack(&pBatch);

    pBatch->pData = udAllocType(T, maxBatchSize, udAF_None);
    UD_ERROR_NULL(pBatch->pData, udR_MemoryAllocationFailure);

    pBatch->capacity = maxBatchSize;

    memcpy(pBatch->pData, pData + sourceOffset, sizeof(T) * count);
    pBatch->count = count;
  }

  result = udR_Success;
epilogue:

  return result;
}

template <typename T>
inline uint32_t vcBatchedRenderQueue<T>::GetBatchCount()
{
  return (uint32_t)batches.length;
}

template <typename T>
inline const typename vcBatchedRenderQueue<T>::Batch* vcBatchedRenderQueue<T>::GetBatch(uint32_t index)
{
  return &batches[index];
}

#endif//vcBatchedRenderQueue
