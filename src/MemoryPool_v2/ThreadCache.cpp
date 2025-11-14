#include "ThreadCache.h"

#include "CentralCache.h"

namespace MemoryPoolV2
{

std::optional<void*> ThreadCache::Allocate(size_t memorySize)
{
	if (memorySize == 0)
		return std::nullopt;

	// Round up to the nearest size class
	memorySize = SizeUtil::GetSizeClass(memorySize);
	if (memorySize > SizeUtil::MAX_CACHED_UNIT_SIZE)
		return FetchFromCentralCache(memorySize)
			.and_then([](std::byte* memory) { return std::optional<void*>(memory); });

	const size_t index = SizeUtil::GetIndex(memorySize);
	if (freeList_[index] != nullptr)
	{
		std::byte* result = freeList_[index];
		freeList_[index]  = GetNextBlock(result);
		freeListSize_[index]--;
		return result;
	}
	return FetchFromCentralCache(memorySize)
		.and_then([](std::byte* memory) { return std::optional<void*>(memory); });
}

void ThreadCache::Deallocate(void* ptr, size_t memorySize)
{
	if (memorySize == 0 || ptr == nullptr)
		return;

	// Round up to the nearest size class
	memorySize = SizeUtil::GetSizeClass(memorySize);
	if (memorySize > SizeUtil::MAX_CACHED_UNIT_SIZE)
	{
		CentralCache::GetInstance().Deallocate(static_cast<std::byte*>(ptr), memorySize);
		return;
	}

	const size_t index			   = SizeUtil::GetIndex(memorySize);
	*static_cast<std::byte**>(ptr) = freeList_[index];
	freeList_[index]			   = static_cast<std::byte*>(ptr);
	freeListSize_[index]++;

	// check if the memory needs to be recycled
	if (freeListSize_[index] * memorySize > MAX_FREE_BYTES_PER_LIST)
	{
		// recycle half memory
		const size_t freeBlockSize = freeListSize_[index] / 2;

		std::byte* blockToFree		= freeList_[index];
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

		std::byte* newHead			   = GetNextBlock(lastNodeToRemove);
		GetNextBlock(lastNodeToRemove) = nullptr;
		freeList_[index]			   = newHead;
		freeListSize_[index] -= freeBlockSize;

		// 性能优化：禁用 CountBlock 断言，这会遍历整个链表严重影响性能
		// assert(CountBlock(freeList_[index]) == freeListSize_[index]);
		// assert(CountBlock(blockToFree) == freeBlockSize);

		// Return half memory to CentralCache
		CentralCache::GetInstance().Deallocate(blockToFree, memorySize);

		// Adaptive strategy: halve next allocate count (fast response to memory pressure)
		nextAllocateCount_[index] = std::max(nextAllocateCount_[index] / 2, size_t{4});
	}
}

std::optional<std::byte*> ThreadCache::FetchFromCentralCache(size_t memorySize)
{
	size_t blockCount = ComputeAllocateCount(memorySize);
	return CentralCache::GetInstance()
		.Allocate(memorySize, blockCount)
		.transform(
			[this, memorySize, blockCount](std::byte* memoryList)
			{
				const size_t index		= SizeUtil::GetIndex(memorySize);
				std::byte*	 listEnd	= memoryList;
				size_t		 listLength = 1;

				while (GetNextBlock(listEnd) != nullptr && listLength < blockCount)
				{
					listEnd = GetNextBlock(listEnd);
					listLength++;
				}
				// Ensure the last node's next pointer is explicitly set to nullptr
				// This prevents issues with uninitialized memory in Windows debug mode
				GetNextBlock(listEnd) = nullptr;
				GetNextBlock(listEnd) = freeList_[index];
				freeList_[index]	  = GetNextBlock(memoryList);
				freeListSize_[index] += blockCount - 1;

				return memoryList;
			});
}

size_t ThreadCache::ComputeAllocateCount(size_t size)
{
	size_t index = SizeUtil::GetIndex(size);
	if (index >= SizeUtil::CACHE_LIST_SIZE)
		return 1;

	// Slow start strategy: start with 16 blocks, double each time
	// Higher initial count reduces CentralCache interactions
	size_t		 minBlocks = 16;
	const size_t result	   = std::max(nextAllocateCount_[index], minBlocks);

	// Calculate next allocate count: double it (slow start strategy)
	size_t nextCount = result * 2;

	// Set upper limit based on object size
	size_t maxBlocks;
	if (size <= 128)
	{
		maxBlocks = 256; // Small objects (≤128B): max 256 blocks
	}
	else if (size <= 1024)
	{
		maxBlocks = 128; // Medium objects (≤1KB): max 128 blocks
	}
	else
	{
		maxBlocks = 64; // Large objects (>1KB): max 64 blocks
	}

	// Apply limits
	nextCount = std::min(nextCount, maxBlocks);
	nextCount = std::min(nextCount, MAX_FREE_BYTES_PER_LIST / size / 2);
	nextCount = std::min(nextCount, SizeUtil::MAX_UNIT_COUNT);

	// Update for next allocation
	nextAllocateCount_[index] = nextCount;

	return result;
}

} // namespace MemoryPoolV2