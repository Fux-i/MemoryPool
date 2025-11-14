# TCMalloc-Like Memory Pool

## ThreadCache

Thread local storage, top layer

- freeList_
- freeListSize_
- nextAllocateCount_

1. FetchFromCentralCache
2. ComputeAllocateCount
   - get from and update `nextAllocateCount_`
3. Allocate
   - evaluate memory size
     - too large or no enough memory in `freeList_`: FetchFromCentralCache
     - else, get from `freeList_`
4. Deallocate
   - evaluate memory size
     - zero or nullptr, return
     - too large, -> `CentralCache` -> Deallocate
     - else, add to `freeList_`
       - check if a list is too large(need to be recycled)
         - Yes, recycle half of the list

## CentralCache

Middle layer, bucket spin lock(std::atomic_flag)

- `freeLists_`: array
- `freeListSizes_`: array(block count)
- `statusLists_`: array
- `pageMaps_`: array of maps{byte*, page span}
- `nextAllocateMemoryGroupCount_`: array

1. GetPageFromPageCache
   - ->`PageCache`->AllocatePage
2. RecordAllocatedMemorySpan
3. GetAllocatedPageCount
4. Allocate
   - block count == 0?
     - Yes, return
     - No, request size too large?
       - Yes -> `PageCache` -> AllocateUnit
       - No, add spin lock, freeList's size is enough?
         - Yes,
           - get certain block from `freeLists_` to result
         - No, GetPageFromPageCache
           - split by memorySize and make a list(block count) to result
           - add page span to `pageMaps_`
           - add the rest memory to free list
5. Deallocate
   - size is too large?
     - Yes -> `PageCache` -> DeallocateUnit
     - No, do... until empty
       - Add memoryList to `freeList_` and update `freeListSizes_`
       - find the page span that the memory is in the charge of
       - if page span can be recycled
         - Remove all memory block from `freeList_` and update `freeListSizes_`
         - ->`PageCache`->DeallocatePage
         - halve next allocate count(fast response to memory pressure)

### PageSpan

Manage 

## PageCache

Contact with system directly, global mutex

- `mutex_`
- `freePageStore_`: map{size(page count), memory span}
- `freePageMap_`: map{ptr, memory span}

1. SystemAlloc
   - [win] VirtualAlloc
   - [other] mmap
2. SystemFree
   - [win] VirtualFree
   - [other] munmap
3. AllocateUnit
   - call SystemAlloc
4. DeallocateUnit
   - call SystemFree
5. AllocatePage
   - find enough memory in `freePageStore_` 
     - yes, allocate and recycle the rest
     - no, SystemAlloc and recycle the rest
6. DeallocatePage
   - try to find continuous memory in `freePageMap_`
     - combine and reinsert to `store` and `map`
