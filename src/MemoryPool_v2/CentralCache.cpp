#include "CentralCache.h"

#include "PageCache.h"
#include "ThreadCache.h"

namespace MemoryPoolV2
{

std::optional<std::byte*> CentralCache::Allocate(size_t memorySize, size_t blockCount)
{
	// Note: memorySize should already be a size class from ThreadCache
	if (memorySize == 0 || blockCount == 0)
		return std::nullopt;

	// Request size is too large
	if (memorySize > SizeUtil::MAX_CACHED_UNIT_SIZE)
		return PageCache::GetInstance()
			.AllocateUnit(memorySize)
			.transform([](MemorySpan memory) { return memory.GetData(); });

	const size_t index	= SizeUtil::GetIndex(memorySize);
	std::byte*	 result = nullptr;

	AtomicFlagGuard guard(statusLists_[index]);
	try
	{
		// current cache is not enough, get from page cache
		if (freeListSizes_[index] < blockCount)
		{
			const size_t allocatedPageCount = GetAllocatedPageCount(memorySize);
			const auto	 ret = GetPageFromPageCache(allocatedPageCount);
			if (!ret.has_value())
				return std::nullopt;

			MemorySpan memory = ret.value();
			// use PageSpan to manage
			PageSpan pageSpan(memory, memorySize);
			// Calculate actual memory span count based on allocated memory size
			size_t allocatedSpanCount = memory.GetSize() / memorySize;

			// split and make a list
			for (size_t i = 0; i < blockCount; i++)
			{
				MemorySpan splitMemory = memory.SubSpan(0, memorySize);
				memory				   = memory.SubSpan(memorySize);
				assert(splitMemory.GetSize() == memorySize);

				GetNextBlock(splitMemory.GetData()) = result;
				result = splitMemory.GetData();
				// allocate
				pageSpan.Allocate(splitMemory);
			}

			// add to page map
			auto startAddress = pageSpan.GetData();
			auto [_, success] = pageMaps_[index].emplace(startAddress, pageSpan);
			assert(success);

			// add the rest to free list
			allocatedSpanCount -= blockCount;
			for (size_t i = 0; i < allocatedSpanCount; i++)
			{
				MemorySpan splitMemory = memory.SubSpan(0, memorySize);
				memory				   = memory.SubSpan(memorySize);
				assert(splitMemory.GetSize() == memorySize);

				GetNextBlock(splitMemory.GetData()) = freeLists_[index];
				freeLists_[index]					= splitMemory.GetData();
				freeListSizes_[index]++;
			}
		}
		else // current cache is enough
		{
			assert(freeListSizes_[index] >= blockCount);
			// split
			for (size_t i = 0; i < blockCount; i++)
			{
				assert(freeLists_[index] != nullptr);

				std::byte* temp	  = freeLists_[index];
				freeLists_[index] = GetNextBlock(temp);
				freeListSizes_[index]--;

				RecordAllocatedMemorySpan(temp, memorySize);
				GetNextBlock(temp) = result;
				result			   = temp;
			}
		}
	}
	catch (...)
	{
		throw std::runtime_error("Memory allocation failed");
		return std::nullopt;
	}

	// assert(CountBlock(result) == blockCount);
	return std::make_optional<std::byte*>(result);
}

void CentralCache::Deallocate(std::byte* memoryList, size_t memorySize)
{
	assert(memoryList != nullptr);

	// large block of memory use system
	if (memorySize > SizeUtil::MAX_CACHED_UNIT_SIZE)
	{
		PageCache::GetInstance().DeallocateUnit(MemorySpan(memoryList, memorySize));
		return;
	}

	const size_t	index = SizeUtil::GetIndex(memorySize);
	AtomicFlagGuard guard(statusLists_[index]);

	std::byte* currentSpan = memoryList;
	while (currentSpan != nullptr)
	{
		std::byte* nextSpan = GetNextBlock(currentSpan);
		assert(SizeUtil::SIZE_CLASSES[index] == memorySize);

		GetNextBlock(currentSpan) = freeLists_[index];
		freeLists_[index]		  = currentSpan;
		freeListSizes_[index]++;

		auto it = pageMaps_[index].upper_bound(currentSpan);
		assert(it != pageMaps_[index].begin());
		--it;
		assert(it->second.IsInCharge(MemorySpan{currentSpan, memorySize}));

		// check if the page need to be recycled
		if (it->second.CanBeRecycled())
		{
			const auto pageStart = it->second.GetData();
			const auto pageEnd	 = pageStart + it->second.GetSize();
			assert(it->second.GetUnitSize() == memorySize);

			std::byte* current = freeLists_[index];
			std::byte* prev	   = nullptr;
			// traversal
			while (current != nullptr)
			{
				std::byte* next			= GetNextBlock(current);
				bool	   shouldRemove = false;
				const auto memoryStart	= current;
				const auto memoryEnd	= memoryStart + memorySize;
				if (memoryStart >= pageStart && memoryEnd <= pageEnd)
				{
					assert(it->second.IsInCharge(MemorySpan{current, memorySize}));
					shouldRemove = true;
				}

				if (shouldRemove)
				{
					// if current is head node
					if (prev == nullptr)
						freeLists_[index] = next;
					// else link previous and next node
					else
						GetNextBlock(prev) = next;
					freeListSizes_[index]--;
				}
				else
				{
					prev = current;
				}
				current = next;
			}
			const MemorySpan page = it->second.GetMemorySpan();
			pageMaps_[index].erase(it);

			// Adaptive strategy: halve on recycle (fast response to memory pressure)
			nextAllocateMemoryGroupCount_[index] =
				std::max(nextAllocateMemoryGroupCount_[index] / 2, size_t{1});

			PageCache::GetInstance().DeallocatePage(page);
		}
		currentSpan = nextSpan;
	}
}

size_t CentralCache::GetAllocatedPageCount(size_t memorySize)
{
	// Dynamic group allocation strategy
	const size_t index	= SizeUtil::GetIndex(memorySize);
	size_t		 result = nextAllocateMemoryGroupCount_[index];

	// At least allocate 1 group
	result = std::max(result, size_t{1});

	// Update: next time allocate one more group (slow start strategy)
	nextAllocateMemoryGroupCount_[index] = result + 1;

	// Calculate page count: 1 group = ThreadCache::MAX_FREE_BYTES_PER_LIST
	return SizeUtil::AlignSize(result * ThreadCache::MAX_FREE_BYTES_PER_LIST, SizeUtil::PAGE_SIZE) /
		   SizeUtil::PAGE_SIZE;
}

void CentralCache::RecordAllocatedMemorySpan(std::byte* memory, const size_t memorySize)
{
	const size_t index = SizeUtil::GetIndex(memorySize);
	auto		 it	   = pageMaps_[index].upper_bound(memory);
	assert(it != pageMaps_[index].begin());
	--it;
	it->second.Allocate(MemorySpan(memory, memorySize));
}

std::optional<MemorySpan> CentralCache::GetPageFromPageCache(size_t pageCount)
{
	return PageCache::GetInstance().AllocatePage(pageCount);
}

} // namespace MemoryPoolV2