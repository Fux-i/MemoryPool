#include <gtest/gtest.h>
#include "src/MemoryPool_v2/MemoryPool.h"
#include "src/MemoryPool_v2/Common.h"
#include <vector>
#include <thread>
#include <cstring>
#include <set>
#include <algorithm>

using namespace MemoryPoolV2;

// ============ 辅助函数 ============

// 获取对齐后的大小
size_t GetAlignedSize(size_t size) {
    return SizeUtil::AlignSize(size);
}

// ============ 基本分配测试 ============

TEST(MemoryPoolTest, AllocateZeroSize) {
    auto ptr = MemoryPool::Allocate(0);
    EXPECT_FALSE(ptr.has_value()) << "分配0字节应该失败";
}

TEST(MemoryPoolTest, AllocateMinimumSize) {
    size_t size = SizeUtil::ALIGNMENT; // 8字节
    auto ptr = MemoryPool::Allocate(size);
    
    ASSERT_TRUE(ptr.has_value()) << "分配" << size << "字节失败";
    ASSERT_NE(ptr.value(), nullptr);
    
    // 写入测试
    memset(ptr.value(), 0xAA, size);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[0], 0xAA);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[size - 1], 0xAA);
    
    MemoryPool::Deallocate(ptr.value(), size);
}

TEST(MemoryPoolTest, AllocateSmallSize) {
    size_t size = 32;
    auto ptr = MemoryPool::Allocate(size);
    
    ASSERT_TRUE(ptr.has_value());
    ASSERT_NE(ptr.value(), nullptr);
    
    memset(ptr.value(), 0xBB, size);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[size / 2], 0xBB);
    
    MemoryPool::Deallocate(ptr.value(), size);
}

TEST(MemoryPoolTest, AllocateMediumSize) {
    size_t size = 1024;
    auto ptr = MemoryPool::Allocate(size);
    
    ASSERT_TRUE(ptr.has_value());
    ASSERT_NE(ptr.value(), nullptr);
    
    memset(ptr.value(), 0xCC, size);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[0], 0xCC);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[size - 1], 0xCC);
    
    MemoryPool::Deallocate(ptr.value(), size);
}

TEST(MemoryPoolTest, AllocateMaxCachedSize) {
    size_t size = SizeUtil::MAX_CACHED_UNIT_SIZE; // 16KB
    auto ptr = MemoryPool::Allocate(size);
    
    ASSERT_TRUE(ptr.has_value());
    ASSERT_NE(ptr.value(), nullptr);
    
    memset(ptr.value(), 0xDD, size);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[size - 1], 0xDD);
    
    MemoryPool::Deallocate(ptr.value(), size);
}

TEST(MemoryPoolTest, AllocateLargeSize) {
    size_t size = 32 * 1024; // 32KB，超过缓存大小
    auto ptr = MemoryPool::Allocate(size);
    
    ASSERT_TRUE(ptr.has_value());
    ASSERT_NE(ptr.value(), nullptr);
    
    memset(ptr.value(), 0xEE, size);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[0], 0xEE);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[size - 1], 0xEE);
    
    MemoryPool::Deallocate(ptr.value(), size);
}

// ============ 内存对齐测试 ============

TEST(MemoryPoolTest, AllocationAlignment) {
    std::vector<size_t> unaligned_sizes = {1, 3, 5, 7, 9, 15, 17, 31, 33};
    
    for (size_t size : unaligned_sizes) {
        auto ptr = MemoryPool::Allocate(size);
        ASSERT_TRUE(ptr.has_value()) << "分配" << size << "字节失败";
        
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr.value());
        EXPECT_EQ(addr % SizeUtil::ALIGNMENT, 0) 
            << "地址 " << ptr.value() << " 未对齐到 " << SizeUtil::ALIGNMENT << " 字节";
        
        MemoryPool::Deallocate(ptr.value(), size);
    }
}

// ============ 多次分配回收测试 ============

TEST(MemoryPoolTest, MultipleAllocationsAndDeallocations) {
    const size_t count = 100;
    const size_t size = 64;
    std::vector<void*> pointers;
    
    // 分配
    for (size_t i = 0; i < count; ++i) {
        auto ptr = MemoryPool::Allocate(size);
        ASSERT_TRUE(ptr.has_value());
        pointers.push_back(ptr.value());
    }
    
    // 检查无重复指针
    std::set<void*> unique_ptrs(pointers.begin(), pointers.end());
    EXPECT_EQ(unique_ptrs.size(), count) << "存在重复指针";
    
    // 回收
    for (void* ptr : pointers) {
        MemoryPool::Deallocate(ptr, size);
    }
}

TEST(MemoryPoolTest, AllocateDeallocateAllocatePattern) {
    const size_t size = 128;
    std::vector<void*> first_batch;
    
    // 第一批分配
    for (int i = 0; i < 10; ++i) {
        auto ptr = MemoryPool::Allocate(size);
        ASSERT_TRUE(ptr.has_value());
        first_batch.push_back(ptr.value());
    }
    
    // 回收一半
    for (int i = 0; i < 5; ++i) {
        MemoryPool::Deallocate(first_batch[i], size);
    }
    
    // 再次分配（应该复用）
    std::vector<void*> second_batch;
    for (int i = 0; i < 5; ++i) {
        auto ptr = MemoryPool::Allocate(size);
        ASSERT_TRUE(ptr.has_value());
        second_batch.push_back(ptr.value());
    }
    
    // 回收剩余
    for (int i = 5; i < 10; ++i) {
        MemoryPool::Deallocate(first_batch[i], size);
    }
    for (void* ptr : second_batch) {
        MemoryPool::Deallocate(ptr, size);
    }
}

// ============ 不同大小混合测试 ============

TEST(MemoryPoolTest, MixedSizeAllocations) {
    std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192};
    std::vector<std::pair<void*, size_t>> allocations;
    
    // 分配不同大小
    for (size_t size : sizes) {
        auto ptr = MemoryPool::Allocate(size);
        ASSERT_TRUE(ptr.has_value()) << "分配" << size << "字节失败";
        allocations.push_back({ptr.value(), size});
        
        // 写入数据验证
        memset(ptr.value(), static_cast<int>(size & 0xFF), size);
    }
    
    // 验证数据完整性
    for (const auto& [ptr, size] : allocations) {
        EXPECT_EQ(static_cast<unsigned char*>(ptr)[0], static_cast<unsigned char>(size & 0xFF));
    }
    
    // 回收
    for (const auto& [ptr, size] : allocations) {
        MemoryPool::Deallocate(ptr, size);
    }
}

// ============ 压力测试 ============

TEST(MemoryPoolTest, StressTest) {
    const size_t iterations = 1000;
    const size_t size = 256;
    
    for (size_t i = 0; i < iterations; ++i) {
        std::vector<void*> ptrs;
        
        // 分配10个块
        for (int j = 0; j < 10; ++j) {
            auto ptr = MemoryPool::Allocate(size);
            ASSERT_TRUE(ptr.has_value());
            ptrs.push_back(ptr.value());
        }
        
        // 回收
        for (void* ptr : ptrs) {
            MemoryPool::Deallocate(ptr, size);
        }
    }
}

// ============ 并发测试 ============

TEST(MemoryPoolTest, ConcurrentAllocations) {
    const int num_threads = 4;
    const int allocations_per_thread = 100;
    const size_t size = 128;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, size]() {
            std::vector<void*> local_ptrs;
            
            // 分配
            for (int i = 0; i < allocations_per_thread; ++i) {
                auto ptr = MemoryPool::Allocate(size);
                if (ptr.has_value()) {
                    local_ptrs.push_back(ptr.value());
                    // 写入线程ID
                    memset(ptr.value(), t, size);
                }
            }
            
            // 验证数据
            for (void* ptr : local_ptrs) {
                EXPECT_EQ(static_cast<unsigned char*>(ptr)[0], static_cast<unsigned char>(t));
            }
            
            success_count += local_ptrs.size();
            
            // 回收
            for (void* ptr : local_ptrs) {
                MemoryPool::Deallocate(ptr, size);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count, num_threads * allocations_per_thread);
}

TEST(MemoryPoolTest, ConcurrentMixedOperations) {
    const int num_threads = 4;
    const int operations = 50;
    
    std::vector<std::thread> threads;
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([=]() {
            std::vector<std::pair<void*, size_t>> allocations;
            
            for (int i = 0; i < operations; ++i) {
                // 交替分配不同大小
                size_t size = (i % 3 == 0) ? 64 : (i % 3 == 1) ? 256 : 1024;
                
                auto ptr = MemoryPool::Allocate(size);
                if (ptr.has_value()) {
                    allocations.push_back({ptr.value(), size});
                }
                
                // 偶尔回收一些
                if (i % 10 == 0 && !allocations.empty()) {
                    auto [p, s] = allocations.back();
                    MemoryPool::Deallocate(p, s);
                    allocations.pop_back();
                }
            }
            
            // 回收剩余
            for (auto [ptr, size] : allocations) {
                MemoryPool::Deallocate(ptr, size);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
}

// ============ 边界条件测试 ============

TEST(MemoryPoolTest, BoundarySize) {
    // 测试缓存边界附近的大小
    std::vector<size_t> boundary_sizes = {
        SizeUtil::MAX_CACHED_UNIT_SIZE - 8,
        SizeUtil::MAX_CACHED_UNIT_SIZE,
        SizeUtil::MAX_CACHED_UNIT_SIZE + 8
    };
    
    for (size_t size : boundary_sizes) {
        auto ptr = MemoryPool::Allocate(size);
        ASSERT_TRUE(ptr.has_value()) << "分配边界大小" << size << "失败";
        ASSERT_NE(ptr.value(), nullptr);
        
        MemoryPool::Deallocate(ptr.value(), size);
    }
}

TEST(MemoryPoolTest, VeryLargeAllocation) {
    size_t size = 1024 * 1024; // 1MB
    auto ptr = MemoryPool::Allocate(size);
    
    ASSERT_TRUE(ptr.has_value());
    ASSERT_NE(ptr.value(), nullptr);
    
    // 简单验证（避免填充太慢）
    static_cast<unsigned char*>(ptr.value())[0] = 0xFF;
    static_cast<unsigned char*>(ptr.value())[size - 1] = 0xFF;
    
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[0], 0xFF);
    EXPECT_EQ(static_cast<unsigned char*>(ptr.value())[size - 1], 0xFF);
    
    MemoryPool::Deallocate(ptr.value(), size);
}

// ============ 主函数 ============

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

