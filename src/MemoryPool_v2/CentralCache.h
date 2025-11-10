#pragma once
#include <array>
#include <atomic>
#include <list>
#include <map>
#include <optional>

#include "Common.h"
#include "PageSpan.h"

namespace MemoryPoolV2
{

class CentralCache
{
  public:
	static CentralCache& GetInstance()
	{
		static CentralCache instance;
		return instance;
	}

	std::optional<std::byte*> Allocate(size_t memorySize, size_t blockCount);

	void Deallocate(std::byte* memoryList, size_t memorySize);

  private:
	static constexpr size_t PAGE_SPAN = 8;

	size_t GetAllocatedPageCount(size_t memorySize);

	void RecordAllocatedMemorySpan(std::byte* memory, size_t memorySize);

	std::optional<MemorySpan> GetPageFromPageCache(size_t pageCount);

	std::array<std::byte*, SizeUtil::CACHE_LIST_SIZE>		freeLists_{};
	std::array<size_t, SizeUtil::CACHE_LIST_SIZE>			freeListSizes_{};
	std::array<std::atomic_flag, SizeUtil::CACHE_LIST_SIZE> statusLists_{};

	// record allocated memory
	std::array<std::map<std::byte*, PageSpan>, SizeUtil::CACHE_LIST_SIZE> pageMaps_{};

	// dynamic memory allocation strategy: allocate memory in groups
	// 1 group = ThreadCache::MAX_FREE_BYTES_PER_LIST
	// start with 1 group, increase by 1 each time, halve on recycle
	std::array<size_t, SizeUtil::CACHE_LIST_SIZE> nextAllocateMemoryGroupCount_{};
};

} // namespace MemoryPoolV2