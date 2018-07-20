#define TESTING
#include <algorithm>
#include <list>
#include <mutex>
#include <thread>

#include <lib/system/allocators.hpp>
#include <lib/system/queues.hpp>
#include <lib/system/structures.hpp>

#include "gtest/gtest.h"

void testRegionAllocator(uint32_t size, uint32_t pages, uint32_t num, uint32_t& resultPages) {
  RegionAllocator a(size, pages);

  std::vector<RegionPtr> ptrs;

  for (uint32_t i = 0; i < num; ++i) {
    ptrs.push_back(a.allocateNext(4));
    ASSERT_EQ(ptrs.back().size(), 4);
    *((uint32_t*)ptrs.back().get()) = i;
  }

  uint32_t i = 0;
  for (auto ptr : ptrs) {
    ASSERT_EQ(i, *(uint32_t*)ptr.get());
    ++i;
  }

  resultPages = a.getPagesNum();
}

TEST(region_allocator, one_page) {
  uint32_t p;
  testRegionAllocator(400, 2, 10, p);
  ASSERT_EQ(p, 2);
}

TEST(region_allocator, start_pages) {
  uint32_t p;
  testRegionAllocator((sizeof(Region) + 4) * 10, 2, 15, p);
  ASSERT_EQ(p, 2);
}

TEST(region_allocator, alloc_pages) {
  uint32_t p1, p2;
  testRegionAllocator((sizeof(Region) + 4) * 10, 2, 50, p1);
  ASSERT_EQ(p1, 5);

  testRegionAllocator((sizeof(Region) + 4) * 10, 2, 51, p2);
  ASSERT_EQ(p2, 6);
}

TEST(region_allocator, alloc_with_frees) {
  RegionAllocator a(1000, 1);

  for (uint32_t i = 0; i < 100; ++i) {
    auto p = a.allocateNext(i + 1);
    *(char*)p.get() = 'x';
  }

  ASSERT_EQ(a.getPagesNum(), 1);
}

TEST(region_allocator, alloc_with_resizes) {
  RegionAllocator a((sizeof(Region) + 1) * 100 + 101, 1);

  std::vector<RegionPtr> regs;

  for (uint32_t i = 0; i < 100; ++i) {
    auto p = a.allocateNext(i + 1);
    *(char*)p.get() = 'x';

    a.shrinkLast(1);
    regs.push_back(p);
  }

  ASSERT_EQ(a.getPagesNum(), 1);
}

TEST(region_allocator, multithreaded_stress) {
  RegionAllocator a(10000, 10);
  uint64_t total = 0;

  std::mutex m;
  std::list<RegionPtr> regs;
  uint64_t lTot = 0;

  std::thread wr([&]() {
                   for (uint32_t i = 0; i < 1000000; ++i) {
                     uint32_t s = rand() % 100 + 4;
                     auto p = a.allocateNext(s);
                     *(uint32_t*)p.get() = i;
                     total+= i;

                     if (i % 7 == 0)
                       a.shrinkLast(std::max((uint32_t)4, s % 25));

                     {
                       std::lock_guard<std::mutex> l(m);
                       regs.push_back(p);
                     }
                   }
                 });

  auto rrout = [&]() {
                 std::vector<RegionPtr> inCase;
                 uint64_t t = 0;

                 for (uint32_t i = 0; i < 250000; ++i) {
                   for (;;) {
                     RegionPtr p;
                     {
                       std::lock_guard<std::mutex> l(m);
                       if (regs.empty()) continue;
                       p = regs.front();
                       regs.pop_front();
                     }
                     t+= *(uint32_t*)p.get();
                     if (i % 17 == 0) inCase.push_back(p);
                     break;
                   }
                 }

                 std::lock_guard<std::mutex> l(m);
                 lTot+= t;
               };

  std::thread p1(rrout);
  std::thread p2(rrout);
  std::thread p3(rrout);
  std::thread p4(rrout);

  wr.join();
  p1.join();
  p2.join();
  p3.join();
  p4.join();

  ASSERT_EQ(lTot, total);
}

TEST(fuqueue, consecutive) {
  FUQueue<uint32_t, 1000> q;

  for (uint32_t i = 0; i < 1000; ++i) {
    auto s = q.lockWrite();
    s->element = i;
    q.unlockWrite(s);
  }

  for (uint32_t i = 0; i < 1000; ++i) {
    auto s = q.lockRead();
    ASSERT_EQ(s->element, i);
    q.unlockRead(s);
  }
}

TEST(fuqueue, overwrite) {
  FUQueue<uint32_t, 1000> q;

  for (int i = 0; i < 100; ++i) {
    for (uint32_t i = 0; i < 200; ++i) {
      auto s = q.lockWrite();
      s->element = i;
      q.unlockWrite(s);
    }

    for (uint32_t i = 0; i < 200; ++i) {
      auto s = q.lockRead();
      ASSERT_EQ(s->element, i);
      q.unlockRead(s);
    }
  }
}


TEST(fuqueue, multithreaded_stress) {
  FUQueue<uint32_t, 1000> q;

  std::atomic<uint64_t> wSum = { 0 };
  std::atomic<uint64_t> rSum = { 0 };

  auto wrFunc = [&]() {
                  for (int i = 0; i < 100000; ++i) {
                    const uint32_t t = rand() % 500;
                    auto s = q.lockWrite();
                    s->element = t;
                    q.unlockWrite(s);

                    wSum.fetch_add(t, std::memory_order_relaxed);
                  }
                };

  auto rdFunc = [&]() {
                  for (int i = 0; i < 100000; ++i) {
                    auto s = q.lockRead();
                    rSum.fetch_add(s->element, std::memory_order_relaxed);
                    q.unlockRead(s);
                  }
                };

  std::thread w1(wrFunc);
  std::thread w2(wrFunc);
  std::thread w3(wrFunc);

  std::thread r1(rdFunc);
  std::thread r2(rdFunc);
  std::thread r3(rdFunc);
;

  w1.join();
  w2.join();
  w3.join();

  r1.join();
  r2.join();
  r3.join();
}

TEST(typed_allocator, one_page) {
  TypedAllocator<uint32_t, 100> allocator;

  std::array<uint32_t*, 100> uis;

  for (uint32_t i = 0; i < 100; ++i)
    uis[i] = allocator.emplace(i);

  for (uint32_t i = 0; i < 100; ++i)
    ASSERT_EQ(*(uis[i]), i);

  for (uint32_t i = 0; i < 100; ++i)
    allocator.remove(uis[i]);
}

TEST(typed_allocator, multiple_pages) {
  TypedAllocator<uint32_t, 100> allocator;

  std::array<uint32_t*, 1000> uis;

  for (uint32_t i = 0; i < 1000; ++i)
    uis[i] = allocator.emplace(i);

  for (uint32_t i = 0; i < 1000; ++i)
    ASSERT_EQ(*(uis[i]), i);

  for (uint32_t i = 0; i < 1000; ++i)
    allocator.remove(uis[i]);
}

TEST(typed_allocator, reput) {
  TypedAllocator<uint32_t, 10> allocator;

  uint32_t* first = allocator.emplace(42);
  allocator.remove(first);

  for (uint32_t i = 0; i < 100; ++i) {
    ASSERT_EQ(allocator.emplace(i), first);
    allocator.remove(first);
  }
}

TEST(typed_allocator, inconsistent) {
  TypedAllocator<uint32_t, 100> allocator;

  std::array<uint32_t*, 1000> uis;

  for (uint32_t i = 0; i < 1000; ++i) {
    uis[i] = allocator.emplace(i);
    if (i % 7 == 0) allocator.remove(uis[i]);
  }

  for (uint32_t i = 0; i < 1000; ++i)
    if (i % 7) { ASSERT_EQ(*(uis[i]), i); }
}

template <>
uint16_t getHashIndex(const uint32_t& h) {
  return h % (1 << 16);
}

TEST(fixed_hash_map, base) {
  FixedHashMap<uint32_t, uint64_t, uint16_t, 100000> hm;
  const uint32_t COUNT = 100000ll;
  uint32_t hs[COUNT];

  for (uint32_t i = 0; i < COUNT; ++i) {
    hs[i] = i;
    auto& c = hm.tryStore(hs[i]);
    ASSERT_EQ(c, 0);
    c = 2;
  }

  for (uint32_t i = 0; i < COUNT; ++i)
    ASSERT_EQ(hm.tryStore(hs[i]), 2);
}

TEST(fixed_hash_map, depush) {
  FixedHashMap<uint32_t, uint64_t, uint16_t, 10> hm;
  uint32_t hs[1000];

  for (uint32_t i = 0; i < 1000; ++i) {
    hs[i] = i;
    auto& c = hm.tryStore(hs[i]);
    ASSERT_EQ(c, 0);
    c = 2;
  }

  for (uint32_t i = 990; i < 1000; ++i)
    ASSERT_EQ(hm.tryStore(hs[i]), 2);

  for (uint32_t i = 0; i < 990; ++i) {
    auto c = hm.tryStore(hs[i]);
    ASSERT_EQ(c, 0);
  }
}

template <>
uint8_t getHashIndex(const uint16_t& h) {
  return h % (1 << 8);
}

TEST(fixed_hash_map, heap) {
  const uint16_t COUNT = 10000;
  FixedHashMap<uint16_t, uint32_t, uint8_t, COUNT> hm;
  uint16_t hs[COUNT];

  for (uint16_t i = 0; i < COUNT; ++i) {
    hs[i] = i;
    auto& c = hm.tryStore(hs[i]);
    ASSERT_EQ(c, 0);
    c = 7;
  }

  for (uint16_t i = 0; i < COUNT; ++i) {
    auto c = hm.tryStore(hs[i]);
    ASSERT_EQ(c, 7);
  }
}
