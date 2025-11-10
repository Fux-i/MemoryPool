# 高性能内存池实现 (MemoryPool)

> 🎯 **项目亮点**: STL 兼容分配器，以及类 tcmalloc 的三层架构高性能内存池，针对多线程场景优化，
> 在多线程小对象频繁创建与回收的场景下性能达到标准 malloc/free 的 8 倍左右

## MemoryPool_v1 - STL 兼容分配器

### 功能特性

**1. 标准 STL Allocator 接口**

完全符合 C++ 标准分配器要求，可无缝集成到 STL 容器中

```cpp
std::list<int, MemoryPool<int, 123>> l;
```

**2. Union Slot 内存复用技术**

巧妙利用 union 特性，空闲块直接存储指针，实现零额外开销的自由链表

```cpp
union slot
{
    T element;
    slot* next;
};

// 计算对齐后的大小（能容纳T，且为指针大小的整数倍）
static constexpr size_t paddedSlotSize_ = (sizeof(slot) + sizeof(slot*) - 1) & ~(sizeof(slot*) - 1);
```

**3. 自动内存对齐与块管理**

支持自定义块大小（默认 4096 bytes），编译期自动计算最优对齐参数

```cpp
// 将 BlockSize 向下对齐到 paddedSlotSize_ 的整数倍
static constexpr size_t aligned_block_size_ = (BlockSize / paddedSlotSize_) * paddedSlotSize_;
// 每个块可以容纳多少个对象（至少保留1个用于链表）
static constexpr size_t slots_per_block_ = aligned_block_size_ / paddedSlotSize_ - 1;
```

## MemoryPool_v2 - 类 tcmalloc 的三层架构高性能内存池

采用 **ThreadCache-CentralCache-PageCache** 三层架构，借鉴 Google TCMalloc 设计理念，实现慢开始快回收策略，支持动态自适应调整。

### 架构设计

```
┌──────────────┐
│ ThreadCache  │  线程本地缓存，无锁访问
└──────┬───────┘
       │ TLS (Thread Local Storage)
┌──────▼───────┐
│CentralCache  │  中心缓存，桶锁 (Bucket Lock)
└──────┬───────┘
       │ 页面管理
┌──────▼───────┐
│  PageCache   │  大页缓存，全局锁，页面合并/拆分
└──────────────┘
```

### 核心设计亮点 🌟

#### 1. **慢开始快回收策略** (Slow Start, Fast Recycle)

- **分配时慢开始**: 初始少量分配（4 块），逐步增长，避免内存浪费
- **回收时快速响应**: 超过阈值立即归还一半，下次分配量减半，快速响应内存压力
- **动态自适应**: ThreadCache 和 CentralCache 两层协同，自动调整内存块数量

#### 2. **无锁 TLS + 细粒度桶锁**

- **ThreadCache**: `thread_local` 单例，无需加锁，极低开销
- **CentralCache**: 基于哈希桶的细粒度自旋锁，减少锁竞争
- **PageCache**: 全局锁，但访问频率低（仅在跨层分配时）

#### 3. **跨层内存流动**

```
Allocate:  ThreadCache → CentralCache → PageCache → System
Deallocate: System ← PageCache ← CentralCache ← ThreadCache
```

#### 4. **指针链表技巧（零开销自由链表）**

- 自由内存块本身存储下一个节点的指针：`*reinterpret_cast<std::byte**>(ptr) = next`
- 节省额外的链表节点开销，内存复用率高
- ThreadCache 和 CentralCache 均采用此技术实现高效的空闲块管理

#### 5. **内存对齐优化**

- 所有分配单位对齐到指针大小（`sizeof(void*)`）
- 使用位运算快速对齐：`(size + alignment - 1) & ~(alignment - 1)`
- 自动计算对齐后的索引，减少内存碎片

### 技术难点

#### 1. **内存块生命周期管理**

- CentralCache 使用 `std::map<std::byte*, PageSpan>` 记录已分配内存的元信息
- PageSpan 通过 `allocatedUnitCount_` 精确跟踪每个内存块的分配状态
- PageCache 回收时自动合并相邻的空闲页面（前向合并 + 后向合并）

#### 2. **并发控制与锁策略**

- ThreadCache: `thread_local` 单例，无锁热路径
- CentralCache: 基于哈希桶的细粒度 `atomic_flag` 自旋锁，配合 `std::this_thread::yield()` 避免忙等
- PageCache: 全局 `std::mutex`，仅在跨层分配时访问
- RAII 风格的 `AtomicFlagGuard` 确保异常安全

### 现代 C++ 特性应用

| 特性                         | 版本  | 应用场景                | 技术价值                             |
| ---------------------------- | ----- | ----------------------- | ------------------------------------ |
| `thread_local`               | C++11 | 线程本地存储，零锁开销  | 实现无锁热路径，核心性能优化         |
| `std::atomic_flag`           | C++11 | 自旋锁实现              | 轻量级同步原语，适合短临界区         |
| `std::byte`                  | C++17 | 类型安全的原始内存表示  | 避免指针混淆，增强代码安全性、可读性 |
| `std::optional`              | C++17 | 可选值处理              | 内存分配可空，避免空指针解引用       |
| `operator<=>`                | C++20 | PageSpan 三路比较运算符 | 简化排序逻辑，提升代码可维护性       |
| `std::optional::transform()` | C++23 | 链式调用简化错误处理    | 函数式编程风格，优雅处理可选值       |

### 性能基准测试

| 测试场景                                            | MemoryPoolV2 | malloc/free | 性能对比 |
| --------------------------------------------------- | ------------ | ----------- | -------- |
| **测试 1: 单线程 (32B 对象, 10 万次循环分配)**      | 2.12ms       | 1.40ms      | 0.66 倍  |
| **测试 2: 多线程 (16 线程, 32B 对象, 5 万次/线程)** | 2.57ms       | 20.45ms     | 7.96 倍  |
| **测试 3: 多线程随机大小 (16-128B, 4 万次/线程)**   | 3.12ms       | 24.23ms     | 7.76 倍  |

- 在大对象的分配场景下，本内存池的性能与系统分配器相当，因为内存池会直接调用底层分配接口
- 在单线程小对象分配场景下，本内存池性能不占优
- 但在高并发小对象分配场景下，本内存池显著优于系统分配器
