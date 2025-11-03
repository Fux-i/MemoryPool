#pragma once
#include <assert.h>
#include <cstddef>
#include <span>
#include <thread>

namespace MemoryPoolV2
{

class SizeUtil
{
public:
	static constexpr size_t ALIGNMENT = sizeof(void*);	// 所在平台的指针大小
    static constexpr size_t PAGE_SIZE = 4096;
    static constexpr size_t MAX_UNIT_COUNT = PAGE_SIZE / ALIGNMENT;
	static constexpr size_t MAX_CACHED_UNIT_SIZE = 1 << 14; // 16KB
	static constexpr size_t CACHE_LIST_SIZE = MAX_CACHED_UNIT_SIZE / ALIGNMENT;

	// 内存大小向上对齐到指针大小整数倍
	static size_t AlignSize(const size_t rawSize, const size_t alignment = ALIGNMENT)
	{
		return (rawSize + alignment - 1) & ~(alignment - 1);
	}

	// 根据内存大小获取对应序号
	static size_t GetIndex(const size_t rawSize)
	{
		return AlignSize(rawSize) / ALIGNMENT - 1;
	}
};

class MemorySpan
{
public:
	MemorySpan(std::byte* data, const size_t size) : data_(data), size_(size) {}
	MemorySpan(const MemorySpan&) = default;
	MemorySpan& operator=(const MemorySpan&) = default;
	MemorySpan(MemorySpan&&) = default;
	MemorySpan& operator=(MemorySpan&&) = default;
	~MemorySpan() = default;
	
	[[nodiscard]] std::byte* GetData() const { return data_; }
	[[nodiscard]] size_t GetSize() const { return size_; }

	auto operator<=>(const MemorySpan& other) const { return data_ <=> other.data_; }
	bool operator==(const MemorySpan& other) const { return data_ == other.data_ && size_ == other.size_; }

	[[nodiscard]] MemorySpan SubSpan(const size_t offset, const size_t size) const
	{
		assert(offset <= size_ &&  size <= size_ - offset);
		return MemorySpan{data_ + offset, size};
	}
	[[nodiscard]] MemorySpan SubSpan(const size_t offset) const
	{
		assert(offset <= size_);
		return MemorySpan{data_ + offset, size_ - offset};
	}

private:
	std::byte* data_;
	size_t size_;
};

class AtomicFlagGuard
{
public:
	explicit AtomicFlagGuard(std::atomic_flag& flag) : flag_(flag)
	{
		while (flag_.test_and_set(std::memory_order_acquire))
			std::this_thread::yield();
	}

	~AtomicFlagGuard(){ flag_.clear(std::memory_order_release); }

private:
	std::atomic_flag& flag_;
};

[[nodiscard]]
inline std::byte*& GetNextBlock(std::byte* ptr)
{
	assert(ptr != nullptr);
	return *reinterpret_cast<std::byte**>(ptr);
}

[[nodiscard]]
inline size_t CountBlock(std::byte* ptr)
{
	size_t result = 0;
	std::byte* current = ptr;
	while (current != nullptr)
	{
		result++;
		current = GetNextBlock(current);
	}
	return result;
}

}
