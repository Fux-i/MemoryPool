#pragma once
#include <bitset>

#include "Common.h"

namespace MemoryPoolV2
{

class PageSpan
{
public:
	PageSpan(const MemorySpan span, const size_t unitSize): memory_(span), unitSize_(unitSize) {}

	// generate comparison operators
	auto operator<=>(const PageSpan& other) const { return memory_.GetData() <=> other.memory_.GetData(); }
	
	void Allocate(MemorySpan memory);
	void Deallocate(MemorySpan memory);

	[[nodiscard]] bool empty() const { return allocatedMap_.none(); }
	[[nodiscard]] bool IsInCharge(MemorySpan memory) const;

	[[nodiscard]] size_t GetSize() const { return memory_.GetSize(); }
	[[nodiscard]] std::byte* GetData() const { return memory_.GetData(); }
	[[nodiscard]] size_t GetUnitSize() const { return unitSize_; }
	[[nodiscard]] MemorySpan GetMemorySpan() const { return memory_; }

private:
	static constexpr size_t MAX_UNIT_COUNT = SizeUtil::PAGE_SIZE / SizeUtil::ALIGNMENT;

	MemorySpan memory_;
	size_t unitSize_;
	// allocating status
	std::bitset<MAX_UNIT_COUNT> allocatedMap_;
};

}

