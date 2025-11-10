#pragma once

#include <array>
#include <list>
#include <optional>

#include "Common.h"

namespace MemoryPoolV2
{

class ThreadCache
{
  public:
	static ThreadCache& GetInstance()
	{
		thread_local ThreadCache instance;
		return instance;
	}

	/**
	 * Allocate memory from thread cache
	 * @param memorySize: the size of memory to allocate
	 * @return: the pointer to the allocated memory
	 */
	[[nodiscard("Allocated memory should not be discarded!")]]
	std::optional<void*> Allocate(size_t memorySize);

	/**
	 * Deallocate memory from thread cache
	 * @param ptr start point of the allocated memory
	 * @param memorySize the size of the memory to deallocate
	 */
	void Deallocate(void* ptr, size_t memorySize);

  private:
	/**
	 * Fetch from central cache
	 * @param memorySize the size of memory to allocate
	 * @return  (optional)
	 */
	std::optional<std::byte*> FetchFromCentralCache(size_t memorySize);

	size_t ComputeAllocateCount(size_t size);

	std::array<std::byte*, SizeUtil::CACHE_LIST_SIZE> freeList_{};
	std::array<size_t, SizeUtil::CACHE_LIST_SIZE>	  freeListSize_{};
	std::array<size_t, SizeUtil::CACHE_LIST_SIZE>	  nextAllocateCount_{};

  public:
	static constexpr size_t MAX_FREE_BYTES_PER_LIST = 1 << 21; // 2MB
};

} // namespace MemoryPoolV2