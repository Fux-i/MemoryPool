#pragma once

#include "Common.h"

namespace MemoryPoolV2
{

class PageSpan
{
  public:
	PageSpan(const MemorySpan span, const size_t unitSize)
		: memory_(span), unitSize_(unitSize), totalUnitCount_(span.GetSize() / unitSize),
		  allocatedUnitCount_(0)
	{
	}

	// generate comparison operators
	auto operator<=>(const PageSpan& other) const
	{
		return memory_.GetData() <=> other.memory_.GetData();
	}

	void Allocate(MemorySpan memory);
	void Deallocate(MemorySpan memory);

	[[nodiscard]] bool empty() const
	{
		return allocatedUnitCount_ == 0;
	}
	[[nodiscard]] bool IsInCharge(MemorySpan memory) const;

	[[nodiscard]] size_t GetSize() const
	{
		return memory_.GetSize();
	}
	[[nodiscard]] std::byte* GetData() const
	{
		return memory_.GetData();
	}
	[[nodiscard]] size_t GetUnitSize() const
	{
		return unitSize_;
	}
	[[nodiscard]] MemorySpan GetMemorySpan() const
	{
		return memory_;
	}

  private:
	MemorySpan memory_;
	size_t	   unitSize_;
	size_t	   totalUnitCount_;
	size_t	   allocatedUnitCount_;
};

} // namespace MemoryPoolV2
