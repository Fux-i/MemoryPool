#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <vector>

#include "Common.h"

namespace MemoryPoolV2
{

class PageCache
{
  public:
	static PageCache& GetInstance()
	{
		static PageCache instance;
		return instance;
	}

	/**
	 * Allocate pages from page store to central cache
	 * @param pageCount number of pages to allocate
	 * @return optional memory span
	 */
	std::optional<MemorySpan> AllocatePage(size_t pageCount);

	/**
	 * Recycle memory from central cache
	 * @param page memory span
	 */
	void DeallocatePage(MemorySpan page);

	/**
	 * Request a large block memory
	 * @param size memory size
	 * @return optional memory span
	 */
	std::optional<MemorySpan> AllocateUnit(size_t size);

	/**
	 * Deallocate memory unit to system
	 * @param memoryUnit memory span
	 */
	void DeallocateUnit(MemorySpan memoryUnit);

	// Stop working
	void Stop();

  private:
	// hide constructor
	PageCache() = default;

	/**
	 * System allocate memory
	 * @param pageCount memory page count
	 * @return optional memory span
	 */
	std::optional<MemorySpan> SystemAlloc(size_t pageCount);

	/**
	 * System free memory
	 * @param pages memory span
	 */
	void SystemFree(MemorySpan pages);

	static constexpr size_t PAGE_ALLOCATE_COUNT = 2048;

	std::map<size_t, std::set<MemorySpan>>
									 freePageStore_; // [page count - memory span] map, for allocate
	std::map<std::byte*, MemorySpan> freePageMap_;	 // [ptr = memory span] map, for deallocate
	std::vector<MemorySpan>			 pageVector_;
	bool							 isStop_ = false;
	std::mutex						 mutex_;
};

} // namespace MemoryPoolV2