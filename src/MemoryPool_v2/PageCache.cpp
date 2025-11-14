#include "PageCache.h"
#include <cstring>

namespace MemoryPoolV2
{

std::optional<MemorySpan> PageCache::AllocatePage(size_t pageCount)
{
	if (pageCount == 0)
		return std::nullopt;
	std::lock_guard guard(mutex_);
	// find first element that greater than or equal to given one
	auto it = freePageStore_.lower_bound(pageCount);

	constexpr size_t MAX_SEARCH_ITERATIONS = 1000;
	size_t			 searchCount		   = 0;

	// find valid memory
	while (it != freePageStore_.end() && searchCount < MAX_SEARCH_ITERATIONS)
	{
		searchCount++;

		// find first valid page
		if (it->second.empty())
		{
			++it;
			continue;
		}

		auto	   spanIt	= it->second.begin();
		MemorySpan freeSpan = *spanIt;
		// delete from store and map
		it->second.erase(spanIt);
		freePageMap_.erase(freeSpan.GetData());
		// slice
		size_t	   sizeToUse   = pageCount * SizeUtil::PAGE_SIZE;
		MemorySpan memoryToUse = freeSpan.SubSpan(0, sizeToUse);
		// rest free memory
		freeSpan = freeSpan.SubSpan(sizeToUse);
		if (freeSpan.GetSize())
		{
			freePageStore_[freeSpan.GetSize() / SizeUtil::PAGE_SIZE].emplace(freeSpan);
			freePageMap_.emplace(freeSpan.GetData(), freeSpan);
		}
		return memoryToUse;
	}

	// no page of this size
	size_t pageToAllocate = std::max(PAGE_ALLOCATE_COUNT, pageCount);
	return SystemAlloc(pageToAllocate)
		.transform(
			[this, pageCount](MemorySpan memory)
			{
				pageVector_.push_back(memory);
				size_t	   memoryToUse = pageCount * SizeUtil::PAGE_SIZE;
				MemorySpan result	   = memory.SubSpan(0, memoryToUse);
				// rest free memory
				MemorySpan freeSpan = memory.SubSpan(memoryToUse);
				if (freeSpan.GetSize())
				{
					freePageStore_[freeSpan.GetSize() / SizeUtil::PAGE_SIZE].emplace(freeSpan);
					freePageMap_.emplace(freeSpan.GetData(), freeSpan);
				}
				return result;
			});
}

void PageCache::DeallocatePage(MemorySpan page)
{
	assert(page.GetSize() % SizeUtil::PAGE_SIZE == 0);
	std::lock_guard guard(mutex_);

	// check previous memory spans, combine if continuous
	constexpr size_t MAX_MERGE_ITERATIONS = 100;
	size_t			 mergeCount			  = 0;
	while (!freePageMap_.empty() && mergeCount < MAX_MERGE_ITERATIONS)
	{
		// page should not be contained
		assert(!freePageMap_.contains(page.GetData()));
		// find first element that greater than given one
		auto it = freePageMap_.upper_bound(page.GetData());
		if (it != freePageMap_.begin())
		{
			--it; // check previous one
			const MemorySpan& prevSpan = it->second;
			if (prevSpan.GetData() + prevSpan.GetSize() == page.GetData())
			{
				// combine continuous ones;
				page = MemorySpan(prevSpan.GetData(), prevSpan.GetSize() + page.GetSize());
				// delete from store and map
				freePageStore_[prevSpan.GetSize() / SizeUtil::PAGE_SIZE].erase(prevSpan);
				freePageMap_.erase(it);
				mergeCount++;
			}
			else break; // not continuous
		}
		else break; // no more previous
	}

	// check memory spans after the page
	mergeCount = 0; // 重置计数器
	while (!freePageMap_.empty() && mergeCount < MAX_MERGE_ITERATIONS)
	{
		// page should not be contained
		assert(!freePageMap_.contains(page.GetData()));
		if (freePageMap_.contains(page.GetData() + page.GetSize()))
		{
			auto	   it		= freePageMap_.find(page.GetData() + page.GetSize());
			MemorySpan nextSpan = it->second;
			freePageStore_[nextSpan.GetSize() / SizeUtil::PAGE_SIZE].erase(nextSpan);
			freePageMap_.erase(it);
			page = MemorySpan(page.GetData(), page.GetSize() + nextSpan.GetSize());
			mergeCount++;
		}
		else break;
	}
	const size_t index = page.GetSize() / SizeUtil::PAGE_SIZE;
	freePageStore_[index].emplace(page);
	freePageMap_.emplace(page.GetData(), page);
}

std::optional<MemorySpan> PageCache::AllocateUnit(size_t size)
{
	auto ptr = malloc(size);
	if (ptr == nullptr)
		return std::nullopt;
	return std::make_optional<MemorySpan>(static_cast<std::byte*>(ptr), size);
}

void PageCache::DeallocateUnit(MemorySpan memoryUnit)
{
	free(memoryUnit.GetData());
}

void PageCache::Stop()
{
	std::lock_guard guard(mutex_);
	if (!isStop_)
	{
		isStop_ = true;
		for (const auto& page : pageVector_) SystemFree(page);
	}
}

#ifdef _WIN32
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#else
#error "Unsupported platform"
#endif

std::optional<MemorySpan> PageCache::SystemAlloc(const size_t pageCount)
{
	const size_t size = SizeUtil::PAGE_SIZE * pageCount;
	void*		 ptr;
#ifdef _WIN32
	ptr = VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#elif defined(__linux__) || defined(__APPLE__)
	ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
	if (ptr == nullptr)
		return std::nullopt;

	memset(ptr, 0, size);
	return std::make_optional<MemorySpan>(static_cast<std::byte*>(ptr), size);
}

void PageCache::SystemFree(MemorySpan pages)
{
#ifdef _WIN32
	VirtualFree(pages.GetData(), 0, MEM_RELEASE);
#elif defined(__linux__) || defined(__APPLE__)
	munmap(pages.GetData(), pages.GetSize());
#endif
}

} // namespace MemoryPoolV2
