#include "PageSpan.h"

namespace MemoryPoolV2
{

void PageSpan::Allocate(const MemorySpan memory)
{
	// check if the memory is in charge
	assert(IsInCharge(memory));
	const ptrdiff_t addressOffset = memory.GetData() - memory_.GetData();
	const size_t index = addressOffset / unitSize_;
	assert(allocatedMap_[index] == 0);
	allocatedMap_[index] = true;
}

void PageSpan::Deallocate(const MemorySpan memory)
{
	// check if the memory is in charge
	assert(IsInCharge(memory));
	const ptrdiff_t addressOffset = memory.GetData() - memory_.GetData();
	const size_t index = addressOffset / unitSize_;
	assert(allocatedMap_[index] == 1);
	allocatedMap_[index] = false;
}

bool PageSpan::IsInCharge(const MemorySpan memory) const
{
	if (memory.GetSize() != unitSize_) return false;
	if (memory.GetData() < memory_.GetData()) return false;
	// check start address
	const ptrdiff_t addressOffset = memory.GetData() - memory_.GetData();
	if (addressOffset % unitSize_ != 0) return false;
	// check end address
	return addressOffset + unitSize_ <= memory_.GetSize();
}
}