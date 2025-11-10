#include <chrono>
#include <iostream>
#include <list>
#include <vector>

#include "MemoryPool_v1/MemoryPool.h"
#include "MemoryPool_v2/CentralCache.h"
#include "MemoryPool_v2/ThreadCache.h"

using namespace MemoryPoolV2;

static void test_v1()
{
	std::cout << "====== test MemoryPool v1 ======\n";
	std::list<int, MemoryPool<int, 123>> l;

	for (int i = 0; i < 10; i++) l.push_back(2);
	for (int i = 0; i < 5; i++) l.pop_back();
	for (int i = 0; i < 5; i++) l.push_back(3);

	auto copyList = l;
	for (int i = 0; i < 5; i++) copyList.pop_back();
	for (int i = 0; i < 5; i++) copyList.push_back(3);

	std::cout << "【done】MemoryPool v1 test passed\n\n";
}

static void test_v2_basic_allocate_deallocate()
{
	std::cout << "====== Test V2: Basic Allocate/Deallocate ======\n";

	auto& cache = ThreadCache::GetInstance();

	// Test small object allocation
	std::cout << "Testing small object (64 bytes)...\n";
	auto ptr1 = cache.Allocate(64);
	assert(ptr1.has_value());
	std::cout << "【done】Allocated 64 bytes at " << ptr1.value() << "\n";

	cache.Deallocate(ptr1.value(), 64);
	std::cout << "【done】Deallocated 64 bytes\n";

	// Test medium object allocation
	std::cout << "Testing medium object (1024 bytes)...\n";
	auto ptr2 = cache.Allocate(1024);
	assert(ptr2.has_value());
	std::cout << "【done】Allocated 1024 bytes at " << ptr2.value() << "\n";

	cache.Deallocate(ptr2.value(), 1024);
	std::cout << "【done】Deallocated 1024 bytes\n";

	std::cout << "【done】Basic allocate/deallocate test passed\n\n";
}

static void test_v2_dynamic_adjustment()
{
	std::cout << "====== Test V2: Dynamic Adjustment Strategy ======\n";

	auto&			   cache	= ThreadCache::GetInstance();
	const size_t	   testSize = 128;
	std::vector<void*> ptrs;

	std::cout << "Allocating multiple blocks (128 bytes each) to test slow start...\n";

	// First allocation should trigger allocation from CentralCache
	for (int i = 0; i < 20; i++)
	{
		auto ptr = cache.Allocate(testSize);
		assert(ptr.has_value());
		ptrs.push_back(ptr.value());
		if (i == 0)
			std::cout << "  First allocation (should fetch from CentralCache)\n";
		if (i == 10)
			std::cout << "  Allocating more blocks (may use cached blocks)...\n";
	}
	std::cout << "【done】Allocated 20 blocks successfully\n";

	// Deallocate all to test recycle strategy
	std::cout << "Deallocating all blocks to test recycle strategy...\n";
	for (auto ptr : ptrs)
	{
		cache.Deallocate(ptr, testSize);
	}
	std::cout << "【done】Deallocated all blocks (should trigger recycle to CentralCache)\n";

	std::cout << "【done】Dynamic adjustment test passed\n\n";
}

static void test_v2_large_memory()
{
	std::cout << "====== Test V2: Large Memory Allocation ======\n";

	auto&		 cache	   = ThreadCache::GetInstance();

	// Test allocation larger than MAX_CACHED_UNIT_SIZE (16KB)
	const size_t largeSize = 32 * 1024; // 32KB
	std::cout << "Allocating large memory block (32KB)...\n";

	auto ptr = cache.Allocate(largeSize);
	assert(ptr.has_value());
	std::cout << "【done】Allocated 32KB at " << ptr.value() << "\n";
	std::cout << "  (Should bypass ThreadCache and go directly to PageCache)\n";

	cache.Deallocate(ptr.value(), largeSize);
	std::cout << "【done】Deallocated 32KB\n";

	std::cout << "【done】Large memory test passed\n\n";
}

static void test_v2_multiple_sizes()
{
	std::cout << "====== Test V2: Multiple Size Allocations ======\n";

	auto&								  cache = ThreadCache::GetInstance();
	std::vector<std::pair<void*, size_t>> allocations;

	// Test various sizes
	std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};

	std::cout << "Allocating various sizes...\n";
	for (size_t size : sizes)
	{
		auto ptr = cache.Allocate(size);
		assert(ptr.has_value());
		allocations.push_back({ptr.value(), size});
		std::cout << "【done】Allocated " << size << " bytes\n";
	}

	std::cout << "Deallocating in reverse order...\n";
	for (auto it = allocations.rbegin(); it != allocations.rend(); ++it)
	{
		cache.Deallocate(it->first, it->second);
	}
	std::cout << "【done】All deallocated\n";

	std::cout << "【done】Multiple sizes test passed\n\n";
}

static void test_v2_stress_test()
{
	std::cout << "====== Test V2: Stress Test ======\n";

	auto&		 cache		= ThreadCache::GetInstance();
	const int	 iterations = 1000;
	const size_t testSize	= 256;

	std::cout << "Performing " << iterations << " allocate/deallocate cycles...\n";

	auto start = std::chrono::high_resolution_clock::now();

	for (int i = 0; i < iterations; i++)
	{
		std::vector<void*> ptrs;

		// Allocate 10 blocks
		for (int j = 0; j < 10; j++)
		{
			auto ptr = cache.Allocate(testSize);
			assert(ptr.has_value());
			ptrs.push_back(ptr.value());
		}

		// Deallocate all
		for (auto ptr : ptrs)
		{
			cache.Deallocate(ptr, testSize);
		}
	}

	auto end	  = std::chrono::high_resolution_clock::now();
	auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);

	std::cout << "【done】Completed " << iterations << " cycles in " << duration.count() << " μs\n";
	std::cout << "  Average time per cycle: " << duration.count() / iterations << " μs\n";

	std::cout << "【done】Stress test passed\n\n";
}

static void test_v2_alignment()
{
	std::cout << "====== Test V2: Memory Alignment ======\n";

	auto& cache = ThreadCache::GetInstance();

	std::cout << "Testing non-aligned sizes (should auto-align to 8 bytes)...\n";
	std::vector<size_t> unalignedSizes = {1, 3, 5, 7, 9, 15, 17, 33};

	for (size_t size : unalignedSizes)
	{
		auto ptr = cache.Allocate(size);
		assert(ptr.has_value());

		// Check alignment
		uintptr_t addr = reinterpret_cast<uintptr_t>(ptr.value());
		assert(addr % 8 == 0 && "Address must be 8-byte aligned");

		std::cout << "【done】Size " << size << " aligned correctly at " << ptr.value() << "\n";

		cache.Deallocate(ptr.value(), size);
	}

	std::cout << "【done】Alignment test passed\n\n";
}

int main() // TODO: Release x64 环境会在 ThreadCache::FetchFromCentralCache 中陷入无限循环
{
	std::cout << "\n╔════════════════════════════════════════════════╗\n";
	std::cout << "║      Memory Pool Test Suite                   ║\n";
	std::cout << "╚════════════════════════════════════════════════╝\n\n";

	// Test v1
	// test_v1();

	// Test v2
	test_v2_basic_allocate_deallocate();
	test_v2_dynamic_adjustment();
	test_v2_large_memory();
	test_v2_multiple_sizes();
	test_v2_alignment();
	test_v2_stress_test();

	std::cout << "╔════════════════════════════════════════════════╗\n";
	std::cout << "║      All Tests Passed! 【done】                     ║\n";
	std::cout << "╚════════════════════════════════════════════════╝\n\n";

	return 0;
}