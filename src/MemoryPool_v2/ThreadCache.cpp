#include "ThreadCache.h"

#include "CentralCache.h"

namespace MemoryPoolV2
{

std::optional<void*> ThreadCache::Allocate(size_t memorySize)
{
	if (memorySize == 0) return std::nullopt;

	memorySize = SizeUtil::AlignSize(memorySize);
	if (memorySize > SizeUtil::MAX_CACHED_UNIT_SIZE)
		return FetchFromCentralCache(memorySize).and_then([](std::byte* memory)
			{ return std::optional<void*>(memory); });

	const size_t index = SizeUtil::GetIndex(memorySize);
	if (freeList_[index] != nullptr)
	{
		std::byte* result = freeList_[index];
		freeList_[index] = GetNextBlock(result);
		freeListSize_[index]--;
		return result;
	}
	return FetchFromCentralCache(memorySize).and_then([](std::byte* memory)
			{ return std::optional<void*>(memory); });
}

void ThreadCache::Deallocate(void* ptr, size_t memorySize)
{
	if (memorySize == 0 || ptr == nullptr) return;

	memorySize = SizeUtil::AlignSize(memorySize);
	if (memorySize > SizeUtil::MAX_CACHED_UNIT_SIZE)
	{
		CentralCache::GetInstance().Deallocate(static_cast<std::byte*>(ptr), memorySize);
		return;
	}

	const size_t index = SizeUtil::GetIndex(memorySize);
	*static_cast<std::byte**>(ptr) = freeList_[index];
	freeList_[index] = static_cast<std::byte*>(ptr);
	freeListSize_[index]++;

	// check if the memory needs to be recycled
	if (freeListSize_[index] * memorySize > MAX_FREE_BYTES_PER_LIST)
	{
		// recycle half memory
		const size_t freeBlockSize = freeListSize_[index] / 2;

		std::byte* blockToFree = freeList_[index];
		std::byte* lastNodeToRemove = blockToFree;

		for (size_t i = 0; i < freeBlockSize - 1; i++)
		{
			assert(lastNodeToRemove != nullptr);
			if (GetNextBlock(lastNodeToRemove) == nullptr)
			{
				// severe problem, return
				assert(false && "Free list is shorter than expected!");
				return;
			}
			lastNodeToRemove = GetNextBlock(lastNodeToRemove);
		}

		std::byte* newHead = GetNextBlock(lastNodeToRemove);
		GetNextBlock(lastNodeToRemove) = nullptr;
		freeList_[index] = newHead;
		freeListSize_[index] -= freeBlockSize;

		assert(CountBlock(freeList_[index]) == freeListSize_[index]);
		assert(CountBlock(blockToFree) == freeBlockSize);

		// Return half memory to CentralCache
		CentralCache::GetInstance().Deallocate(blockToFree, memorySize);
		
		// Adaptive strategy: halve next allocate count (fast response to memory pressure)
		nextAllocateCount_[index] = std::max(nextAllocateCount_[index] / 2, size_t{4});
	}
}

std::optional<std::byte*> ThreadCache::FetchFromCentralCache(size_t memorySize)
{
	size_t blockCount = ComputeAllocateCount(memorySize);
	return CentralCache::GetInstance().Allocate(memorySize, blockCount)
		.transform([this, memorySize, blockCount](std::byte* memoryList)
		{
			const size_t index = SizeUtil::GetIndex(memorySize);
			std::byte* listEnd = memoryList;
			size_t listLength = 1;
			while (GetNextBlock(listEnd) != nullptr)
			{
				listEnd = GetNextBlock(listEnd);
				listLength++;
			}
			assert(listLength == blockCount);
			GetNextBlock(listEnd) = freeList_[index];
			freeList_[index] = GetNextBlock(memoryList);
			freeListSize_[index] += blockCount - 1;

			return memoryList;
		});
}

size_t ThreadCache::ComputeAllocateCount(size_t size)
{
	size_t index = SizeUtil::GetIndex(size);
	if (index >= SizeUtil::CACHE_LIST_SIZE) return 1;

	// At least allocate 4 blocks
	const size_t result = std::max(nextAllocateCount_[index], size_t{4});
	
	// Calculate next allocate count: double it (slow start strategy)
	size_t nextCount = result * 2;
	
	// Limit 1: don't exceed max list capacity (reserve half for buffer)
	// Example: for 16KB blocks, max = 256KB / 16KB / 2 = 8 blocks
	nextCount = std::min(nextCount, MAX_FREE_BYTES_PER_LIST / size / 2);
	
	// Limit 2: reasonable upper bound for allocation count
	nextCount = std::min(nextCount, SizeUtil::MAX_UNIT_COUNT);
	
	// Update for next allocation
	nextAllocateCount_[index] = nextCount;
	
	return result;
}

}