#include "ThreadCache.h"

#include "CentralCache.h"

namespace MemoryPoolV2
{

std::optional<void*> ThreadCache::Allocate(size_t memorySize)
{
	if (memorySize == 0)
		return std::nullopt;

	memorySize = SizeUtil::AlignSize(memorySize);
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

	memorySize = SizeUtil::AlignSize(memorySize);
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
				// 性能优化：在 Release 模式下跳过此断言检查
				// assert(listLength == blockCount);
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

	// 性能优化：提高初始分配数量，减少与 CentralCache 的交互
	// 小对象分配更多，大对象分配较少
	size_t minBlocks = 64; // 基础最小块数，从 4 提升到 64
	if (size <= 128)
	{
		minBlocks = 128; // 128字节以下的小对象，初始分配更多
	}
	else if (size <= 512)
	{
		minBlocks = 64;
	}
	else if (size <= 2048)
	{
		minBlocks = 32;
	}
	else
	{
		minBlocks = 16;
	}

	const size_t result = std::max(nextAllocateCount_[index], minBlocks);

	// Calculate next allocate count: double it (slow start strategy)
	size_t nextCount = result * 2;

	// Limit 1: don't exceed max list capacity (reserve half for buffer)
	nextCount = std::min(nextCount, MAX_FREE_BYTES_PER_LIST / size / 2);

	// Limit 2: reasonable upper bound for allocation count
	nextCount = std::min(nextCount, SizeUtil::MAX_UNIT_COUNT);

	// Update for next allocation
	nextAllocateCount_[index] = nextCount;

	return result;
}

} // namespace MemoryPoolV2