#pragma once
#include <optional>

#include "ThreadCache.h"

namespace MemoryPoolV2
{

class MemoryPool
{
  public:
	[[nodiscard]]
	static std::optional<void*> Allocate(size_t memorySize)
	{
		return ThreadCache::GetInstance().Allocate(memorySize);
	}

	static void Deallocate(void* ptr, size_t memorySize)
	{
		ThreadCache::GetInstance().Deallocate(ptr, memorySize);
	}
};

} // namespace MemoryPoolV2