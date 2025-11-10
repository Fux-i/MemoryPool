#pragma once

#include "MemoryPool.decl.hpp"
#include <iostream>
#include <vector>

// 调试输出开关
#define MemoryPool_DEBUG 1

#if MemoryPool_DEBUG
#define POOL_LOG(x) std::cout << x
#else
#define POOL_LOG(x) ;
#endif

// 相等性比较运算符
template <typename T, size_t BlockSize>
bool operator==(const MemoryPool<T, BlockSize>& lhs, const MemoryPool<T, BlockSize>& rhs) noexcept
{
	POOL_LOG("operator==(&" << &lhs << ", &" << &rhs << ") -> true\n");
	return true;
}

template <typename T, size_t BlockSize>
bool operator!=(const MemoryPool<T, BlockSize>& lhs, const MemoryPool<T, BlockSize>& rhs) noexcept
{
	POOL_LOG("operator!=(&" << &lhs << ", &" << &rhs << ") -> false\n");
	return false;
}

// 构造函数
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool() noexcept
	: currentBlock_(nullptr), currentSlot_(nullptr), lastSlot_(nullptr), freeSlot_(nullptr)
{
	static_assert(BlockSize >= 2 * paddedSlotSize, "BlockSize too small");
	POOL_LOG("\n[construct] MemoryPool<" << typeid(T).name() << ", " << BlockSize
										 << ">(this=" << this << ")\n");
	POOL_LOG("	sizeof(T)=" << sizeof(T) << ", sizeof(slot)=" << sizeof(Slot)
							<< ", paddedSlotSize=" << paddedSlotSize << "\n");
	POOL_LOG("	BlockSize=" << BlockSize << ", aligned BlockSize=" << alignedBlockSize
							<< ", available slots=" << slotsPerBlock << "\n");
}

// 析构函数
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::~MemoryPool() noexcept
{
	POOL_LOG("\n[destroy] MemoryPool(this=" << this << ")\n");
	// 释放所有分配的内存块
	int	  blockCount = 0;
	Slot* curr		 = currentBlock_;
	while (curr != nullptr)
	{
		Slot* next = curr->next;
		operator delete(reinterpret_cast<void*>(curr));
		blockCount++;
		curr = next;
	}
	POOL_LOG("freed " << blockCount << " blocks\n");
}

// 拷贝构造函数（创建新的独立实例）
template <typename T, size_t BlockSize>
MemoryPool<T, BlockSize>::MemoryPool(const MemoryPool&) noexcept
	: currentBlock_(nullptr), currentSlot_(nullptr), lastSlot_(nullptr), freeSlot_(nullptr)
{
	POOL_LOG("\n[copy construct] MemoryPool<" << typeid(T).name() << ", " << BlockSize
											  << ">(this=" << this << ")\n");
}

// 分配内存
template <typename T, size_t BlockSize>
T* MemoryPool<T, BlockSize>::allocate(size_t n)
{
	POOL_LOG("[allocate] allocate(" << n << ") ");

	// 如果请求的数量不是1，回退到标准分配
	if (n != 1)
	{
		T* ptr = static_cast<T*>(operator new(n * sizeof(T)));
		POOL_LOG("-> " << ptr << " (default，n≠1)\n");
		return ptr;
	}

	// 优先从空闲链表分配
	if (freeSlot_ != nullptr)
	{
		T* result = reinterpret_cast<T*>(freeSlot_);
		freeSlot_ = freeSlot_->next;
		POOL_LOG("-> " << result << " (free slot)\n");
		return result;
	}

	// 检查当前块是否还有空间
	if (currentSlot_ >= lastSlot_)
	{
		POOL_LOG("(full, new block) ");
		allocateBlock();
	}

	T* result = reinterpret_cast<T*>(currentSlot_++);
	POOL_LOG("-> " << result << " (current block)\n");
	return result;
}

// 释放内存
template <typename T, size_t BlockSize>
void MemoryPool<T, BlockSize>::deallocate(T* p, size_t n)
{
	POOL_LOG("[free] deallocate(" << p << ", " << n << ")");

	if (p == nullptr)
	{
		POOL_LOG(" (nullptr)\n");
		return;
	}

	// 如果不是单个对象，使用标准释放
	if (n != 1)
	{
		operator delete(p);
		POOL_LOG(" (default，n≠1)\n");
		return;
	}

	// 将释放的内存加入空闲链表
	Slot* slotPtr = reinterpret_cast<Slot*>(p);
	slotPtr->next = freeSlot_;
	freeSlot_	  = slotPtr;
	POOL_LOG(" (to free list)\n");
}

// 分配新的内存块
template <typename T, size_t BlockSize>
void MemoryPool<T, BlockSize>::allocateBlock()
{
	POOL_LOG("\n[allocate block] " << alignedBlockSize << " bytes\n");

	// 分配一大块内存，用char*是为了方便指针运算（char*指针+1移动1字节）
	auto* newBlock = reinterpret_cast<char*>(operator new(alignedBlockSize));
	POOL_LOG("	new block: " << static_cast<void*>(newBlock) << "\n");

	// 将新块链接到块链表中（使用第一个 slot 存储链表指针）
	reinterpret_cast<Slot*>(newBlock)->next = currentBlock_;
	currentBlock_							= reinterpret_cast<Slot*>(newBlock);

	// 设置当前块的起始和结束位置（跳过第一个用于链表的 slot）
	char* body								= newBlock + paddedSlotSize;
	currentSlot_							= reinterpret_cast<Slot*>(body);
	lastSlot_								= reinterpret_cast<Slot*>(newBlock + alignedBlockSize);

	POOL_LOG("	range: [" << static_cast<void*>(currentSlot_) << ", "
						  << static_cast<void*>(lastSlot_) << ")\n");
	POOL_LOG("	available slots: " << slotsPerBlock << "\n");
}