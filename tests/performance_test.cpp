#include <algorithm>
#include <chrono>
#include <cstring>
#include <gtest/gtest.h>
#include <iomanip>
#include <iostream>
#include <random>
#include <thread>
#include <vector>

#include "MemoryPool_v2/MemoryPool.h"

using namespace MemoryPoolV2;

// ==================== Performance Testing Utilities ====================

class PerformanceTimer
{
  public:
	PerformanceTimer() : start_(std::chrono::high_resolution_clock::now())
	{
	}

	double ElapsedMs() const
	{
		auto end = std::chrono::high_resolution_clock::now();
		return std::chrono::duration<double, std::milli>(end - start_).count();
	}

  private:
	std::chrono::high_resolution_clock::time_point start_;
};

void PrintResult(const std::string& testName, double poolTime, double mallocTime)
{
	std::cout << "\n[Test Results - " << testName << "]\n";
	std::cout << "  MemoryPoolV2: " << std::fixed << std::setprecision(3) << std::setw(10)
			  << poolTime << " ms\n";
	std::cout << "  malloc/free:  " << std::setw(10) << mallocTime << " ms\n";
	std::cout << "  --------------------------------------------\n";

	double speedup = mallocTime / poolTime;
	if (speedup >= 1.0)
	{
		std::cout << "  Performance Gain: " << std::setprecision(2) << speedup << "x faster";
		std::cout << " (" << std::setprecision(1) << ((speedup - 1.0) * 100) << "% improvement)\n";

		if (speedup > 1.0)
		{
			int barLength = static_cast<int>(std::min(speedup * 20, 80.0));
			std::cout << "  [" << std::string(barLength, '=') << ">\n";
		}
	}
	else
	{
		std::cout << "  Performance: " << std::setprecision(2) << (1.0 / speedup) << "x slower\n";
	}
	std::cout << std::endl;
}

// ==================== Core Test Scenarios ====================

// Test 1: Single-threaded massive small objects with continuous reuse
TEST(MemoryPoolV2_Performance, Test1_SingleThread_MassiveSmallObjects)
{
	const size_t kObjectSize = 32;
	const size_t kPoolSize	 = 1000;
	const size_t kIterations = 100'0000;

	std::cout << "\n" << std::string(70, '=') << "\n";
	std::cout << "TEST 1: Single-threaded Massive Small Objects\n";
	std::cout << std::string(70, '=') << "\n";
	std::cout << "Scenario: Continuous reuse of small object pool\n";
	std::cout << "Object size: " << kObjectSize << " bytes\n";
	std::cout << "Pool size: " << kPoolSize << " objects\n";
	std::cout << "Iterations: " << kIterations << " (churn cycles)\n";

	// MemoryPoolV2 test
	double poolTime = 0.0;
	{
		std::vector<void*>					  pool(kPoolSize, nullptr);
		std::mt19937						  gen(42);
		std::uniform_int_distribution<size_t> dist(0, kPoolSize - 1);

		PerformanceTimer timer;

		// Pre-allocate pool
		for (size_t i = 0; i < kPoolSize; ++i)
		{
			auto result = MemoryPool::Allocate(kObjectSize);
			if (result.has_value())
			{
				pool[i] = result.value();
			}
		}

		// Churn: continuously free and reallocate random slots
		for (size_t iter = 0; iter < kIterations; ++iter)
		{
			size_t idx = dist(gen);

			// Free old
			if (pool[idx])
			{
				MemoryPool::Deallocate(pool[idx], kObjectSize);
			}

			// Allocate new
			auto result = MemoryPool::Allocate(kObjectSize);
			if (result.has_value())
			{
				pool[idx]						   = result.value();
				*static_cast<int*>(result.value()) = static_cast<int>(iter);
			}
		}

		// Cleanup
		for (auto ptr : pool)
		{
			if (ptr)
			{
				MemoryPool::Deallocate(ptr, kObjectSize);
			}
		}

		poolTime = timer.ElapsedMs();
	}

	// malloc/free test
	double mallocTime = 0.0;
	{
		std::vector<void*>					  pool(kPoolSize, nullptr);
		std::mt19937						  gen(42);
		std::uniform_int_distribution<size_t> dist(0, kPoolSize - 1);

		PerformanceTimer timer;

		// Pre-allocate pool
		for (size_t i = 0; i < kPoolSize; ++i)
		{
			pool[i] = malloc(kObjectSize);
		}

		// Churn: continuously free and reallocate random slots
		for (size_t iter = 0; iter < kIterations; ++iter)
		{
			size_t idx = dist(gen);

			// Free old
			if (pool[idx])
			{
				free(pool[idx]);
			}

			// Allocate new
			pool[idx]					  = malloc(kObjectSize);
			*static_cast<int*>(pool[idx]) = static_cast<int>(iter);
		}

		// Cleanup
		for (auto ptr : pool)
		{
			if (ptr)
			{
				free(ptr);
			}
		}

		mallocTime = timer.ElapsedMs();
	}

	PrintResult("Single-threaded Massive Small Objects", poolTime, mallocTime);
}

// Test 2: Multi-threaded massive small objects with object pool pattern
TEST(MemoryPoolV2_Performance, Test2_MultiThread_MassiveSmallObjects)
{
	const size_t kThreads	 = std::thread::hardware_concurrency();
	const size_t kObjectSize = 32;
	const size_t kPoolSize	 = 50;
	const size_t kIterations = 100'0000;

	std::cout << "\n" << std::string(70, '=') << "\n";
	std::cout << "TEST 2: Multi-threaded Massive Small Objects\n";
	std::cout << std::string(70, '=') << "\n";
	std::cout << "Scenario: Concurrent object pool churn (typical server pattern)\n";
	std::cout << "Thread count: " << kThreads << " (CPU cores)\n";
	std::cout << "Pool size per thread: " << kPoolSize << " objects\n";
	std::cout << "Iterations per thread: " << kIterations << "\n";
	std::cout << "Object size: " << kObjectSize << " bytes\n";
	std::cout << "Total allocations: " << (kThreads * kIterations) << "\n";

	auto workerPool = [&](size_t seed)
	{
		std::vector<void*>					  pool(kPoolSize, nullptr);
		std::mt19937						  gen(static_cast<unsigned int>(seed));
		std::uniform_int_distribution<size_t> dist(0, kPoolSize - 1);

		// Pre-allocate
		for (size_t i = 0; i < kPoolSize; ++i)
		{
			auto result = MemoryPool::Allocate(kObjectSize);
			if (result.has_value())
			{
				pool[i] = result.value();
			}
		}

		// Churn
		for (size_t iter = 0; iter < kIterations; ++iter)
		{
			size_t idx = dist(gen);
			if (pool[idx])
			{
				MemoryPool::Deallocate(pool[idx], kObjectSize);
			}
			auto result = MemoryPool::Allocate(kObjectSize);
			if (result.has_value())
			{
				pool[idx]						   = result.value();
				*static_cast<int*>(result.value()) = static_cast<int>(iter);
			}
		}

		// Cleanup
		for (auto ptr : pool)
		{
			if (ptr)
			{
				MemoryPool::Deallocate(ptr, kObjectSize);
			}
		}
	};

	auto workerMalloc = [&](size_t seed)
	{
		std::vector<void*>					  pool(kPoolSize, nullptr);
		std::mt19937						  gen(static_cast<unsigned int>(seed));
		std::uniform_int_distribution<size_t> dist(0, kPoolSize - 1);

		// Pre-allocate
		for (size_t i = 0; i < kPoolSize; ++i)
		{
			pool[i] = malloc(kObjectSize);
		}

		// Churn
		for (size_t iter = 0; iter < kIterations; ++iter)
		{
			size_t idx = dist(gen);
			if (pool[idx])
			{
				free(pool[idx]);
			}
			pool[idx]					  = malloc(kObjectSize);
			*static_cast<int*>(pool[idx]) = static_cast<int>(iter);
		}

		// Cleanup
		for (auto ptr : pool)
		{
			if (ptr)
			{
				free(ptr);
			}
		}
	};

	// MemoryPoolV2 test
	PerformanceTimer poolTimer;
	{
		std::vector<std::thread> threads;
		for (size_t i = 0; i < kThreads; ++i)
		{
			threads.emplace_back(workerPool, i + 1000);
		}
		for (auto& t : threads)
		{
			t.join();
		}
	}
	double poolTime = poolTimer.ElapsedMs();

	// malloc/free test
	PerformanceTimer mallocTimer;
	{
		std::vector<std::thread> threads;
		for (size_t i = 0; i < kThreads; ++i)
		{
			threads.emplace_back(workerMalloc, i + 1000);
		}
		for (auto& t : threads)
		{
			t.join();
		}
	}
	double mallocTime = mallocTimer.ElapsedMs();

	PrintResult("Multi-threaded Massive Small Objects", poolTime, mallocTime);
}

// Test 3: Multi-threaded random small objects with varying sizes
TEST(MemoryPoolV2_Performance, Test3_MultiThread_RandomSmallObjects)
{
	const size_t kThreads	 = std::thread::hardware_concurrency();
	const size_t kPoolSize	 = 40;
	const size_t kIterations = 100'0000;
	const size_t kMinSize	 = 16;
	const size_t kMaxSize	 = 128;

	std::cout << "\n" << std::string(70, '=') << "\n";
	std::cout << "TEST 3: Multi-threaded Random Small Objects\n";
	std::cout << std::string(70, '=') << "\n";
	std::cout << "Scenario: Concurrent object churn with varying sizes\n";
	std::cout << "Thread count: " << kThreads << " (CPU cores)\n";
	std::cout << "Pool size per thread: " << kPoolSize << " objects\n";
	std::cout << "Iterations per thread: " << kIterations << "\n";
	std::cout << "Object size range: " << kMinSize << "-" << kMaxSize << " bytes\n";
	std::cout << "Total allocations: " << (kThreads * kIterations) << "\n";

	auto workerPool = [&](size_t seed)
	{
		std::mt19937						  gen(static_cast<unsigned int>(seed));
		std::uniform_int_distribution<size_t> sizeDist(kMinSize, kMaxSize);
		std::uniform_int_distribution<size_t> indexDist(0, kPoolSize - 1);

		std::vector<std::pair<void*, size_t>> pool(kPoolSize, {nullptr, 0});

		// Pre-allocate with random sizes
		for (size_t i = 0; i < kPoolSize; ++i)
		{
			size_t size	  = sizeDist(gen);
			auto   result = MemoryPool::Allocate(size);
			if (result.has_value())
			{
				pool[i] = {result.value(), size};
			}
		}

		// Churn with varying sizes
		for (size_t iter = 0; iter < kIterations; ++iter)
		{
			size_t idx = indexDist(gen);
			if (pool[idx].first)
			{
				MemoryPool::Deallocate(pool[idx].first, pool[idx].second);
			}
			size_t size	  = sizeDist(gen);
			auto   result = MemoryPool::Allocate(size);
			if (result.has_value())
			{
				pool[idx]						   = {result.value(), size};
				*static_cast<int*>(result.value()) = static_cast<int>(iter);
			}
		}

		// Cleanup
		for (auto [ptr, size] : pool)
		{
			if (ptr)
			{
				MemoryPool::Deallocate(ptr, size);
			}
		}
	};

	auto workerMalloc = [&](size_t seed)
	{
		std::mt19937						  gen(static_cast<unsigned int>(seed));
		std::uniform_int_distribution<size_t> sizeDist(kMinSize, kMaxSize);
		std::uniform_int_distribution<size_t> indexDist(0, kPoolSize - 1);

		std::vector<std::pair<void*, size_t>> pool(kPoolSize, {nullptr, 0});

		// Pre-allocate with random sizes
		for (size_t i = 0; i < kPoolSize; ++i)
		{
			size_t size = sizeDist(gen);
			pool[i]		= {malloc(size), size};
		}

		// Churn with varying sizes
		for (size_t iter = 0; iter < kIterations; ++iter)
		{
			size_t idx = indexDist(gen);
			if (pool[idx].first)
			{
				free(pool[idx].first);
			}
			size_t size							= sizeDist(gen);
			pool[idx]							= {malloc(size), size};
			*static_cast<int*>(pool[idx].first) = static_cast<int>(iter);
		}

		// Cleanup
		for (auto [ptr, size] : pool)
		{
			if (ptr)
			{
				free(ptr);
			}
		}
	};

	// MemoryPoolV2 test
	PerformanceTimer poolTimer;
	{
		std::vector<std::thread> threads;
		for (size_t i = 0; i < kThreads; ++i)
		{
			threads.emplace_back(workerPool, i + 1000);
		}
		for (auto& t : threads)
		{
			t.join();
		}
	}
	double poolTime = poolTimer.ElapsedMs();

	// malloc/free test
	PerformanceTimer mallocTimer;
	{
		std::vector<std::thread> threads;
		for (size_t i = 0; i < kThreads; ++i)
		{
			threads.emplace_back(workerMalloc, i + 1000);
		}
		for (auto& t : threads)
		{
			t.join();
		}
	}
	double mallocTime = mallocTimer.ElapsedMs();

	PrintResult("Multi-threaded Random Small Objects", poolTime, mallocTime);
}

// ==================== Main Function ====================

int main(int argc, char** argv)
{
	::testing::InitGoogleTest(&argc, argv);

	std::cout << "\n";
	std::cout << std::string(70, '=') << "\n";
	std::cout << "  MemoryPoolV2 Performance Benchmark Suite\n";
	std::cout << std::string(70, '=') << "\n";
	std::cout << "  Build Configuration:\n";
	std::cout << "    - Release mode (-O3 optimization)\n";
	std::cout << "    - NDEBUG (assertions disabled)\n";
	std::cout << "    - march=native (CPU-specific optimization)\n";
	std::cout << "\n";
	std::cout << "  Memory Pool Features:\n";
	std::cout << "    - Three-tier architecture (ThreadCache/CentralCache/PageCache)\n";
	std::cout << "    - Thread-local cache (TLS) for lock-free allocation\n";
	std::cout << "    - Fine-grained bucket locks for reduced contention\n";
	std::cout << "    - Slow-start fast-recycle strategy\n";
	std::cout << "\n";
	std::cout << "  Test Focus:\n";
	std::cout << "    1. Single-threaded massive small objects\n";
	std::cout << "    2. Multi-threaded massive small objects\n";
	std::cout << "    3. Multi-threaded random small objects\n";
	std::cout << std::string(70, '=') << "\n";

	auto startTime = std::chrono::high_resolution_clock::now();

	int result = RUN_ALL_TESTS();

	auto   endTime	 = std::chrono::high_resolution_clock::now();
	double totalTime = std::chrono::duration<double>(endTime - startTime).count();

	std::cout << "\n";
	std::cout << std::string(70, '=') << "\n";
	std::cout << "  Benchmark Summary\n";
	std::cout << std::string(70, '=') << "\n";
	std::cout << "  Total time: " << std::fixed << std::setprecision(2) << totalTime
			  << " seconds\n";
	std::cout << "\n";
	std::cout << std::string(70, '=') << "\n";

	return result;
}
