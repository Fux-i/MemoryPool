#include "CentralCache.h"

#include "PageCache.h"

namespace MemoryPoolV2
{

std::optional<std::byte*> CentralCache::Allocate(size_t memorySize, size_t blockCount)
{
	assert(memorySize % 8 == 0 && blockCount <= PageSpan::MAX_UNIT_COUNT);

	if (memorySize == 0 || blockCount == 0) return std::nullopt;

	// Request size is too large
	if (memorySize > SizeUtil::MAX_CACHED_UNIT_SIZE)
		return PageCache::GetInstance().AllocateUnit(memorySize).
			transform([this](MemorySpan memory){ return memory.GetData(); });

	const size_t index = SizeUtil::GetIndex(memorySize);
	std::byte* result = nullptr;

	AtomicFlagGuard guard(statusLists_[index]);
	try
	{
		// current cache is not enough, get from page cache
		if (freeListSizes_[index] < blockCount)
		{
			const size_t allocatedPageCount = GetAllocatedPageCount(memorySize);
			const auto ret = GetPageFromPageCache(allocatedPageCount);
			if (!ret.has_value())
				return std::nullopt;

			MemorySpan memory = ret.value();
			// use PageSpan to manage
			PageSpan pageSpan(memory, memorySize);
			size_t allocatedUnitCount = SizeUtil::MAX_UNIT_COUNT;

			// split
			for (size_t i = 0; i < blockCount; i++)
			{
				MemorySpan splitMemory = memory.SubSpan(0, memorySize);
				memory = memory.SubSpan(memorySize);
				assert((index + 1) * 8 == splitMemory.GetSize());

				*reinterpret_cast<std::byte**>(splitMemory.GetData()) = result;
				result = splitMemory.GetData();
				// allocate
				pageSpan.Allocate(splitMemory);
			}

			// add to page map
			auto startAddress = pageSpan.GetData();
			auto [_, success] = pageMaps_[index].emplace(startAddress, pageSpan);
			assert(success);

			// add the rest to free list
			allocatedUnitCount -= blockCount;
			for (size_t i = 0; i < allocatedUnitCount; i++)
			{
				MemorySpan splitMemory = memory.SubSpan(0, memorySize);
				MemorySpan restMemory = memory.SubSpan(memorySize);
				assert((index + 1) * 8 == splitMemory.GetSize());
				
				*reinterpret_cast<std::byte**>(splitMemory.GetData()) = freeLists_[index];	// todo
				freeLists_[index] = splitMemory.GetData();
				freeListSizes_[index]++;
			}
		}
		else // current cache is enough
		{
			auto& targetList = freeLists_[index];
			assert(freeListSizes_[index] >= blockCount);
			// split
			for (size_t i = 0; i < blockCount; i++)
			{
				assert(freeLists_[index] != nullptr);

				std::byte* temp = freeLists_[index];
				freeLists_[index] = *reinterpret_cast<std::byte**>(temp);
				freeListSizes_[index]--;

				RecordAllocatedMemorySpan(temp, memorySize);
				*reinterpret_cast<std::byte**>(temp) = result;
				result = temp;
			}
		}
	}
	catch (...)
	{
		throw std::runtime_error("Memory allocation failed");
		return std::nullopt;
	}

	assert(CountBlock(result) == blockCount);
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

	const size_t index = SizeUtil::GetIndex(memorySize);
	AtomicFlagGuard guard(statusLists_[index]);

	std::byte* currentSpan = memoryList;
	while (currentSpan != nullptr)
	{
		std::byte* nextSpan = *reinterpret_cast<std::byte**>(currentSpan);
		assert((index + 1) * 8 == memorySize);

		*reinterpret_cast<std::byte**>(currentSpan) = freeLists_[index];
		freeLists_[index] = currentSpan;
		freeListSizes_[index]++;

		auto it = pageMaps_[index].upper_bound(currentSpan);
		assert(it != pageMaps_.begin());
		--it;
		assert(it->second.IsInCharge(MemorySpan{currentSpan, memorySize}));

		// check if the page need to be recycled
		if (it->second.empty())
		{
			const auto pageStart = it->second.GetData();
            const auto pageEnd = pageStart + it->second.GetSize();
            assert(it->second.GetUnitSize() == memorySize);

			std::byte* current = freeLists_[index];
			std::byte* prev = nullptr;
			// traversal
			while (current != nullptr)
			{
				std::byte* next = *reinterpret_cast<std::byte**>(current);
				bool shouldRemove = false;
				const auto memoryStart = current;
				const auto memoryEnd = memoryStart + memorySize;
				if (memoryStart >= pageStart && memoryEnd <= pageEnd)
				{
					assert(it->second.IsInCharge(MemorySpan{current, memorySize}));
					shouldRemove = true;
				}
				
				if (shouldRemove)
				{
					// if current is head node
					if (prev == nullptr) freeLists_[index] = next;
					// else link previous and next node
					else *reinterpret_cast<std::byte**>(prev) = next;
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
			// change allocated memory adaptively
			nextAllocateMemoryGroupCount_[index] /= 2;
			PageCache::GetInstance().DeallocatePage(page);
		}
		currentSpan = nextSpan;
	}
}

size_t CentralCache::GetAllocatedPageCount(size_t memorySize)
{
	return SizeUtil::AlignSize(memorySize * SizeUtil::MAX_UNIT_COUNT, SizeUtil::PAGE_SIZE)
           		/ SizeUtil::PAGE_SIZE;
}

void CentralCache::RecordAllocatedMemorySpan(std::byte* memory, const size_t memorySize)
{
	const size_t index = SizeUtil::GetIndex(memorySize);
	auto it = pageMaps_[index].upper_bound(memory);
	assert(it != pageMaps_.begin());
	--it;
	it->second.Allocate(MemorySpan(memory, memorySize));
}

std::optional<MemorySpan> CentralCache::GetPageFromPageCache(size_t pageCount)
{
	return PageCache::GetInstance().AllocatePage(pageCount);
}

}