#include <gtest/gtest.h>
#include "src/MemoryPool_v2/ThreadCache.h"
#include "src/MemoryPool_v2/Common.h"
#include <vector>
#include <thread>
#include <cstring>
#include <set>

using namespace MemoryPoolV2;

// ============ 辅助函数 ============

// 计算链表长度
size_t CountListLength(std::byte* head) {
    size_t count = 0;
    std::set<std::byte*> visited;
    while (head != nullptr) {
        if (!visited.insert(head).second) {
            // 检测到环
            return count;
        }
        count++;
        head = *reinterpret_cast<std::byte**>(head);
    }
    return count;
}

// ============ 基本分配测试 ============

TEST(ThreadCacheTest, AllocateSmallBlock) {
    auto& cache = ThreadCache::GetInstance();
    size_t size = 64;
    
    auto ptr = cache.Allocate(size);
    ASSERT_TRUE(ptr.has_value());
    ASSERT_NE(ptr.value(), nullptr);
    
    // 验证对齐
    uintptr_t addr = reinterpret_cast<uintptr_t>(ptr.value());
    EXPECT_EQ(addr % SizeUtil::ALIGNMENT, 0);
    
    cache.Deallocate(ptr.value(), size);
}

TEST(ThreadCacheTest, AllocateDeallocateMultipleTimes) {
    auto& cache = ThreadCache::GetInstance();
    size_t size = 128;
    const int count = 20;
    std::vector<void*> ptrs;
    
    // 分配
    for (int i = 0; i < count; ++i) {
        auto ptr = cache.Allocate(size);
        ASSERT_TRUE(ptr.has_value());
        ptrs.push_back(ptr.value());
    }
    
    // 验证无重复
    std::set<void*> unique_ptrs(ptrs.begin(), ptrs.end());
    EXPECT_EQ(unique_ptrs.size(), count);
    
    // 回收
    for (void* ptr : ptrs) {
        cache.Deallocate(ptr, size);
    }
}

// ============ 动态调整测试 ============

TEST(ThreadCacheTest, SlowStartStrategy) {
    auto& cache = ThreadCache::GetInstance();
    size_t size = 256;
    std::vector<void*> ptrs;
    
    // 多次分配，触发慢开始策略
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < 10; ++i) {
            auto ptr = cache.Allocate(size);
            ASSERT_TRUE(ptr.has_value());
            ptrs.push_back(ptr.value());
        }
        
        // 回收一部分
        for (int i = 0; i < 5 && !ptrs.empty(); ++i) {
            cache.Deallocate(ptrs.back(), size);
            ptrs.pop_back();
        }
    }
    
    // 清理剩余
    for (void* ptr : ptrs) {
        cache.Deallocate(ptr, size);
    }
}

TEST(ThreadCacheTest, RecycleOnOverflow) {
    auto& cache = ThreadCache::GetInstance();
    size_t size = 128;
    
    // 分配大量内存块，触发回收
    std::vector<void*> ptrs;
    for (int i = 0; i < 3000; ++i) {
        auto ptr = cache.Allocate(size);
        if (ptr.has_value()) {
            ptrs.push_back(ptr.value());
        }
    }
    
    // 回收所有（应该触发向CentralCache回收）
    for (void* ptr : ptrs) {
        cache.Deallocate(ptr, size);
    }
    
    // 再次分配，验证正常工作
    auto ptr = cache.Allocate(size);
    ASSERT_TRUE(ptr.has_value());
    cache.Deallocate(ptr.value(), size);
}

// ============ 不同大小测试 ============

TEST(ThreadCacheTest, MultipleSizeClasses) {
    auto& cache = ThreadCache::GetInstance();
    std::vector<size_t> sizes = {8, 16, 32, 64, 128, 256, 512, 1024};
    std::vector<std::pair<void*, size_t>> allocations;
    
    // 为每个大小分配多个块
    for (size_t size : sizes) {
        for (int i = 0; i < 5; ++i) {
            auto ptr = cache.Allocate(size);
            ASSERT_TRUE(ptr.has_value()) << "分配" << size << "字节失败";
            allocations.push_back({ptr.value(), size});
        }
    }
    
    // 回收
    for (auto [ptr, size] : allocations) {
        cache.Deallocate(ptr, size);
    }
}

TEST(ThreadCacheTest, LargeBlockBypassCache) {
    auto& cache = ThreadCache::GetInstance();
    size_t large_size = SizeUtil::MAX_CACHED_UNIT_SIZE + 1024;
    
    // 大块应该绕过ThreadCache直接去CentralCache
    auto ptr = cache.Allocate(large_size);
    ASSERT_TRUE(ptr.has_value());
    ASSERT_NE(ptr.value(), nullptr);
    
    cache.Deallocate(ptr.value(), large_size);
}

// ============ 复用测试 ============

TEST(ThreadCacheTest, MemoryReuse) {
    auto& cache = ThreadCache::GetInstance();
    size_t size = 64;
    
    // 第一次分配
    auto ptr1 = cache.Allocate(size);
    ASSERT_TRUE(ptr1.has_value());
    void* addr1 = ptr1.value();
    
    // 回收
    cache.Deallocate(addr1, size);
    
    // 第二次分配（应该复用）
    auto ptr2 = cache.Allocate(size);
    ASSERT_TRUE(ptr2.has_value());
    void* addr2 = ptr2.value();
    
    // 可能复用同一地址（但不保证，因为可能从CentralCache获取）
    
    cache.Deallocate(addr2, size);
}

// ============ 零大小测试 ============

TEST(ThreadCacheTest, AllocateZeroSize) {
    auto& cache = ThreadCache::GetInstance();
    auto ptr = cache.Allocate(0);
    EXPECT_FALSE(ptr.has_value()) << "分配0字节应该返回nullopt";
}

TEST(ThreadCacheTest, DeallocateNullptr) {
    auto& cache = ThreadCache::GetInstance();
    // 应该安全处理
    cache.Deallocate(nullptr, 64);
}

TEST(ThreadCacheTest, DeallocateZeroSize) {
    auto& cache = ThreadCache::GetInstance();
    auto ptr = cache.Allocate(64);
    ASSERT_TRUE(ptr.has_value());
    
    // 应该安全处理
    cache.Deallocate(ptr.value(), 0);
    
    // 正常回收
    cache.Deallocate(ptr.value(), 64);
}

// ============ 内存对齐测试 ============

TEST(ThreadCacheTest, AlignmentCheck) {
    auto& cache = ThreadCache::GetInstance();
    std::vector<size_t> unaligned_sizes = {1, 3, 5, 7, 9, 15, 17, 31};
    
    for (size_t size : unaligned_sizes) {
        auto ptr = cache.Allocate(size);
        ASSERT_TRUE(ptr.has_value());
        
        uintptr_t addr = reinterpret_cast<uintptr_t>(ptr.value());
        EXPECT_EQ(addr % SizeUtil::ALIGNMENT, 0) 
            << "大小" << size << "的分配未对齐";
        
        cache.Deallocate(ptr.value(), size);
    }
}

// ============ 数据完整性测试 ============

TEST(ThreadCacheTest, DataIntegrity) {
    auto& cache = ThreadCache::GetInstance();
    size_t size = 512;
    const int count = 10;
    std::vector<void*> ptrs;
    
    // 分配并写入数据
    for (int i = 0; i < count; ++i) {
        auto ptr = cache.Allocate(size);
        ASSERT_TRUE(ptr.has_value());
        
        // 写入标识
        memset(ptr.value(), i, size);
        ptrs.push_back(ptr.value());
    }
    
    // 验证数据
    for (int i = 0; i < count; ++i) {
        unsigned char expected = static_cast<unsigned char>(i);
        EXPECT_EQ(static_cast<unsigned char*>(ptrs[i])[0], expected);
        EXPECT_EQ(static_cast<unsigned char*>(ptrs[i])[size - 1], expected);
    }
    
    // 回收
    for (void* ptr : ptrs) {
        cache.Deallocate(ptr, size);
    }
}

// ============ 线程本地性测试 ============

TEST(ThreadCacheTest, ThreadLocalBehavior) {
    const int num_threads = 4;
    const int allocs_per_thread = 50;
    const size_t size = 128;
    
    std::vector<std::thread> threads;
    std::atomic<int> success_count{0};
    
    for (int t = 0; t < num_threads; ++t) {
        threads.emplace_back([&, t]() {
            // 每个线程有自己的ThreadCache实例
            auto& cache = ThreadCache::GetInstance();
            std::vector<void*> local_ptrs;
            
            for (int i = 0; i < allocs_per_thread; ++i) {
                auto ptr = cache.Allocate(size);
                if (ptr.has_value()) {
                    local_ptrs.push_back(ptr.value());
                    // 写入线程ID
                    memset(ptr.value(), t, size);
                }
            }
            
            // 验证
            for (void* ptr : local_ptrs) {
                EXPECT_EQ(static_cast<unsigned char*>(ptr)[0], static_cast<unsigned char>(t));
            }
            
            success_count += local_ptrs.size();
            
            // 回收
            for (void* ptr : local_ptrs) {
                cache.Deallocate(ptr, size);
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    EXPECT_EQ(success_count, num_threads * allocs_per_thread);
}

// ============ 主函数 ============

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

