#pragma once

// #include <iostream>

template <typename T, size_t BlockSize = 4096>
class MemoryPool
{
public:
	using value_type = T;

	template <typename U>
	struct rebind
	{
		using other = MemoryPool<U, BlockSize>;
	};

	MemoryPool() noexcept;
	~MemoryPool() noexcept;

	// 每个拷贝都是新的独立实例
	MemoryPool(const MemoryPool&) noexcept;
	// 禁用拷贝赋值
	MemoryPool& operator=(const MemoryPool&) noexcept = delete;

	// 模板拷贝构造（用于 rebind 机制，从不同类型的 allocator 构造）
	template <typename U, size_t OtherBlockSize>
	MemoryPool(const MemoryPool<U, OtherBlockSize>&) noexcept : MemoryPool() {}

	// 移动默认
	MemoryPool(MemoryPool&&) noexcept = default;
	MemoryPool& operator=(MemoryPool&&) noexcept = default;

	T* allocate(size_t n);
	void deallocate(T* p, size_t n);
private:
	union Slot
	{
		T element;
		Slot* next;
	};

	Slot* currentBlock_;  // 当前内存块
	Slot* currentSlot_;   // 当前块中的当前位置
	Slot* lastSlot_;      // 当前块的末尾
	Slot* freeSlot_;      // 空闲链表头

	// 计算对齐后的大小（能容纳T，且为指针大小的整数倍）
	static constexpr size_t paddedSlotSize = (sizeof(Slot) + sizeof(Slot*) - 1) & ~(sizeof(Slot*) - 1);
	// 将 BlockSize 向下对齐到 paddedSlotSize_ 的整数倍
	static constexpr size_t alignedBlockSize = (BlockSize / paddedSlotSize) * paddedSlotSize;
	// 每个块可以容纳多少个对象（至少保留1个用于链表）
	static constexpr size_t slotsPerBlock = alignedBlockSize / paddedSlotSize - 1;

	// 分配新的内存块
	void allocateBlock();
};