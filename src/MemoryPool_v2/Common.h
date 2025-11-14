#pragma once
#include <cassert>
#include <cstddef>
#include <thread>

namespace MemoryPoolV2
{

class SizeUtil
{
  public:
	static constexpr size_t ALIGNMENT			 = sizeof(void*); // 所在平台的指针大小
	static constexpr size_t PAGE_SIZE			 = 4096;
	static constexpr size_t MAX_UNIT_COUNT		 = PAGE_SIZE / ALIGNMENT;
	static constexpr size_t MAX_CACHED_UNIT_SIZE = 1 << 15; // 32KB, max size at one allocation

	// Size classes: exponential growth for better performance
	// Small objects (8-128): step 8, 16, 32, 64, 128
	// Medium objects (128-1024): step by powers of 2
	// Large objects (1K-32K): step by 1K increments
	static constexpr size_t SIZE_CLASSES[] = {
		8,	   16,	  32,	 64,	128, // 0-4: tiny objects (8B steps)
		256,   384,	  512,				 // 5-7: small objects
		640,   768,	  896,	 1024,		 // 8-11: medium-small (128B steps)
		1280,  1536,  1792,	 2048,		 // 12-15: medium (256B steps)
		2560,  3072,  3584,	 4096,		 // 16-19: medium-large (512B steps)
		5120,  6144,  7168,	 8192,		 // 20-23: large (1KB steps)
		10240, 12288, 14336, 16384,		 // 24-27: extra large (2KB steps)
		20480, 24576, 28672, 32768		 // 28-31: huge (4KB steps)
	};

	static constexpr size_t CACHE_LIST_SIZE = std::size(SIZE_CLASSES);

	// 内存大小向上对齐到指针大小整数倍
	static constexpr size_t AlignSize(const size_t rawSize, const size_t alignment = ALIGNMENT)
	{
		return (rawSize + alignment - 1) & ~(alignment - 1);
	}

	static size_t GetSizeClass(const size_t rawSize)
	{
		if (rawSize == 0)
			return SIZE_CLASSES[0];
		if (rawSize > MAX_CACHED_UNIT_SIZE)
			return rawSize;

		if (rawSize <= 128)
			return (rawSize + 7) & ~7;

		if (rawSize <= 1024)
			return ((rawSize + 127) >> 7) << 7;

		if (rawSize <= 8192)
			return ((rawSize + 511) >> 9) << 9;

		return ((rawSize + 2047) >> 11) << 11;
	}

	static size_t GetIndex(const size_t rawSize)
	{
		if (rawSize == 0)
			return 0;

		const size_t sizeClass = GetSizeClass(rawSize);

		if (sizeClass <= 128)
			return (sizeClass >> 3) - 1;

		if (sizeClass <= 512)
			return 4 + ((sizeClass - 128) >> 7);

		if (sizeClass <= 1024)
			return 7 + ((sizeClass - 512) >> 7);

		if (sizeClass <= 2048)
			return 11 + ((sizeClass - 1024) >> 8);

		if (sizeClass <= 4096)
			return 15 + ((sizeClass - 2048) >> 9);

		if (sizeClass <= 8192)
			return 19 + ((sizeClass - 4096) >> 10);

		if (sizeClass <= 16384)
			return 23 + ((sizeClass - 8192) >> 11);

		if (sizeClass <= 32768)
			return 27 + ((sizeClass - 16384) >> 12);

		assert(false && "Size out of range for cache");
		return CACHE_LIST_SIZE - 1;
	}
};

class MemorySpan
{
  public:
	MemorySpan(std::byte* data, const size_t size) : data_(data), size_(size)
	{
	}
	MemorySpan(const MemorySpan&)			 = default;
	MemorySpan& operator=(const MemorySpan&) = default;
	MemorySpan(MemorySpan&&)				 = default;
	MemorySpan& operator=(MemorySpan&&)		 = default;
	~MemorySpan()							 = default;

	[[nodiscard]] std::byte* GetData() const
	{
		return data_;
	}
	[[nodiscard]] size_t GetSize() const
	{
		return size_;
	}

	auto operator<=>(const MemorySpan& other) const
	{
		return data_ <=> other.data_;
	}
	bool operator==(const MemorySpan& other) const
	{
		return data_ == other.data_ && size_ == other.size_;
	}

	[[nodiscard]] MemorySpan SubSpan(const size_t offset, const size_t size) const
	{
		assert(offset <= size_ && size <= size_ - offset);
		return MemorySpan{data_ + offset, size};
	}
	[[nodiscard]] MemorySpan SubSpan(const size_t offset) const
	{
		assert(offset <= size_);
		return MemorySpan{data_ + offset, size_ - offset};
	}

  private:
	std::byte* data_;
	size_t	   size_;
};

class AtomicFlagGuard
{
  public:
	explicit AtomicFlagGuard(std::atomic_flag& flag) : flag_(flag)
	{
		while (flag_.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
	}

	~AtomicFlagGuard()
	{
		flag_.clear(std::memory_order_release);
	}

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
	size_t			 result			= 0;
	std::byte*		 current		= ptr;
	constexpr size_t MAX_ITERATIONS = 1000000; // Reasonable upper bound
	while (current != nullptr && result < MAX_ITERATIONS)
	{
		result++;
		current = GetNextBlock(current);
	}
	if (result == MAX_ITERATIONS) throw std::bad_alloc();
	return result;
}

} // namespace MemoryPoolV2
