#include "paging.hpp"

#include <array>

#include "asmfunc.h"
#include "memory_manager.hpp"
#include "task.hpp"

#include "logger.hpp"

int printk(const char* format, ...);

namespace {
  const uint64_t kPageSize4K = 4096;
  const uint64_t kPageSize2M = 512 * kPageSize4K;
  const uint64_t kPageSize1G = 512 * kPageSize2M;

  //1st table:1 page
  alignas(kPageSize4K) std::array<uint64_t, 512> pml4_table;
  //2nd table:1 page
  alignas(kPageSize4K) std::array<uint64_t, 512> pdp_table;
  //3rd table:64 page(=2th table 64 entry)
  alignas(kPageSize4K)
    std::array<std::array<uint64_t, 512>, kPageDirectoryCount> page_directory;
  
  size_t max(int a, int b){
    return a > b ? (size_t)a : (size_t)b;
  }
}

void SetupIdentityPageTable() {//direct mpping, all hugepage
  pml4_table[0] = reinterpret_cast<uint64_t>(&pdp_table[0]) | 0x003;
  for (int i_pdpt = 0; i_pdpt < page_directory.size(); ++i_pdpt) {
    pdp_table[i_pdpt] = reinterpret_cast<uint64_t>(&page_directory[i_pdpt]) | 0x003;
    for (int i_pd = 0; i_pd < 512; ++i_pd) {
      page_directory[i_pdpt][i_pd] = i_pdpt * kPageSize1G + i_pd * kPageSize2M | 0x083;
    }
  }

  // #@@range_begin(clear_wp)
  ResetCR3();
  SetCR0(GetCR0() & 0xfffeffff); // Clear WP
  // #@@range_end(clear_wp)
}

void InitializePaging() {
  SetupIdentityPageTable();
}

void ResetCR3() {
  SetCR3(reinterpret_cast<uint64_t>(&pml4_table[0]));
}

namespace {

WithError<PageMapEntry*> SetNewPageMapIfNotPresent(PageMapEntry& entry) {
  if (entry.bits.present) {
    return { entry.Pointer(), MAKE_ERROR(Error::kSuccess) };
  }

  auto [ child_map, err ] = NewPageMap();
  if (err) {
    return { nullptr, err };
  }

  entry.SetPointer(child_map);
  entry.bits.present = 1;

  return { child_map, MAKE_ERROR(Error::kSuccess) };
}

//try to added by kk@huge page allocate
WithError<PageMapEntry*> SetNewHugePageMapIfNotPresent(PageMapEntry& entry) {
  if (entry.bits.present) {
    return { entry.Pointer(), MAKE_ERROR(Error::kSuccess) };
  }
  //NewHugePageMapに変更
  auto [ child_map, err ] = NewHugePageMap();
  if (err) {
    return { nullptr, err };
  }

  entry.SetPointer(child_map);
  entry.bits.present = 1;
  if (err == Error::kSuccess) printk("NewHugePageMap success\n");
  return {child_map, MAKE_ERROR(Error::kSuccess)};
}

// #@@range_begin(setup_pagemap)
WithError<size_t> SetupPageMap(
    PageMapEntry* page_map, int page_map_level, LinearAddress4Level addr,
    size_t num_4kpages, bool writable) {
  while (num_4kpages > 0) {
    const auto entry_index = addr.Part(page_map_level);
    
    auto [ child_map, err ] = SetNewPageMapIfNotPresent(page_map[entry_index]);
    if (err) {
      return { num_4kpages, err };
    }
    page_map[entry_index].bits.user = 1;
    
    if (page_map_level == 1) {
      page_map[entry_index].bits.writable = writable;
      --num_4kpages;
    } else {
      page_map[entry_index].bits.writable = true;
      auto [ num_remain_pages, err ] =
        SetupPageMap(child_map, page_map_level - 1, addr, num_4kpages, writable);
// #@@range_end(setup_pagemap)
      if (err) {
        return { num_4kpages, err };
      }
      num_4kpages = num_remain_pages;
    }

    if (entry_index == 511) {
      break;
    }

    addr.SetPart(page_map_level, entry_index + 1);
    for (int level = page_map_level - 1; level >= 1; --level) {
      addr.SetPart(level, 0);
    }
  }

  return { num_4kpages, MAKE_ERROR(Error::kSuccess) };
}

//added by kk
WithError<size_t> SetupHugePageMap(
    PageMapEntry* page_map, int page_map_level, LinearAddress4Level addr,
    size_t num_2mpages, bool writable) {

  while (num_2mpages > 0) {
    const auto entry_index = addr.Part(page_map_level);
    
    auto [ child_map, err ] = SetNewHugePageMapIfNotPresent(page_map[entry_index]);
    if (err) {
      printk("SetNewHugePageMapIfNotPresent err=%d\n", err);
      return {num_2mpages, err};
    }
    page_map[entry_index].bits.user = 1;
    
    
    if (page_map_level == 2) {
      page_map[entry_index].bits.writable = writable;
      page_map[entry_index].bits.huge_page = 1;
      --num_2mpages;
    } else {
      page_map[entry_index].bits.writable = true;
      auto [ num_remain_pages, err ] =
        SetupHugePageMap(child_map, page_map_level - 1, addr, num_2mpages, writable);
// #@@range_end(setup_pagemap)
      if (err) {
        printk("SetupHugePageMap err=%d\n", err);
        return { num_2mpages, err };
      }
      num_2mpages = num_remain_pages;
    }

    if (entry_index == 511) {
      break;
    }

    addr.SetPart(page_map_level, entry_index + 1);
    for (int level = page_map_level - 1; level >= 2; --level) {
      addr.SetPart(level, 0);
    }
  }
  return {num_2mpages, MAKE_ERROR(Error::kSuccess)};
}

//changed for hugepage free
// #@@range_begin(clean_page_map)
Error CleanPageMap(
    PageMapEntry* page_map, int page_map_level, LinearAddress4Level addr) {
  for (int i = addr.Part(page_map_level); i < 512; ++i) {
    auto entry = page_map[i];
// #@@range_end(clean_page_map)
    if (!entry.bits.present) {
      continue;
    }

    //hugepage free prepare
    bool is_hugepage = false;
    if (page_map_level == 2) is_hugepage = page_map[i].bits.huge_page;

    if (page_map_level > 1 && !is_hugepage) {
      if (auto err = CleanPageMap(entry.Pointer(), page_map_level - 1, addr)) {
        return err;
      }
    }

// #@@range_begin(clean_page_map_free)
    if (entry.bits.writable) {
      const auto entry_addr = reinterpret_cast<uintptr_t>(entry.Pointer());
      const FrameID map_frame{entry_addr / kBytesPerFrame};
      int frame_num = is_hugepage ? HugePage4kNum : 1;
      if (auto err = memory_manager->Free(map_frame, frame_num)) {
        return err;
      }
    }
    page_map[i].data = 0;
// #@@range_end(clean_page_map_free)
  }

  return MAKE_ERROR(Error::kSuccess);
}

const FileMapping* FindFileMapping(const std::vector<FileMapping>& fmaps,
                                   uint64_t causal_vaddr) {
  for (const FileMapping& m : fmaps) {
    if (m.vaddr_begin <= causal_vaddr && causal_vaddr < m.vaddr_end) {
      return &m;
    }
  }
  return nullptr;
}

Error PreparePageCache(FileDescriptor& fd, const FileMapping& m,
                       uint64_t causal_vaddr) {
  LinearAddress4Level page_vaddr{causal_vaddr};
  page_vaddr.parts.offset = 0;
  if (auto err = SetupPageMaps(page_vaddr, 1)) {
    return err;
  }

  const long file_offset = page_vaddr.value - m.vaddr_begin;
  void* page_cache = reinterpret_cast<void*>(page_vaddr.value);
  fd.Load(page_cache, 4096, file_offset);
  return MAKE_ERROR(Error::kSuccess);
}

// #@@range_begin(set_page_content)
Error SetPageContent(PageMapEntry* table, int part,
                     LinearAddress4Level addr, PageMapEntry* content) {
  if (part == 1) {
    const auto i = addr.Part(part);
    table[i].SetPointer(content);
    table[i].bits.writable = 1;
    InvalidateTLB(addr.value);
    return MAKE_ERROR(Error::kSuccess);
  }

  const auto i = addr.Part(part);
  return SetPageContent(table[i].Pointer(), part - 1, addr, content);
}
// #@@range_end(set_page_content)

// #@@range_begin(copy_one_page)
Error CopyOnePage(uint64_t causal_addr) {
  auto [ p, err ] = NewPageMap();
  if (err) {
    return err;
  }
  const auto aligned_addr = causal_addr & 0xffff'ffff'ffff'f000;
  memcpy(p, reinterpret_cast<const void*>(aligned_addr), 4096);
  return SetPageContent(reinterpret_cast<PageMapEntry*>(GetCR3()), 4,
                        LinearAddress4Level{causal_addr}, p);
}
// #@@range_end(copy_one_page)

} // namespace

WithError<PageMapEntry*> NewPageMap() {
  auto frame = memory_manager->Allocate(1);
  if (frame.error) {
    return { nullptr, frame.error };
  }
  auto e = reinterpret_cast<PageMapEntry*>(frame.value.Frame());
  memset(e, 0, sizeof(uint64_t) * 512);
  return { e, MAKE_ERROR(Error::kSuccess) };
}

//added by kk
WithError<PageMapEntry*> NewHugePageMap(){
  auto frame = memory_manager->AllocateHuge(1);
  if(frame.error){
    return {nullptr, frame.error};
  }

  auto e = reinterpret_cast<PageMapEntry*>(frame.value.Frame());
  memset(e, 0, sizeof(uint64_t) * 512 * 512);
  printk("Hugepage allocate success, addr = 0x%lx\n", (unsigned long)e);
  return {e, MAKE_ERROR(Error::kSuccess)};
}

Error FreePageMap(PageMapEntry* table) {
  const FrameID frame{reinterpret_cast<uintptr_t>(table) / kBytesPerFrame};
  return memory_manager->Free(frame, 1);
}

Error SetupPageMaps(LinearAddress4Level addr, size_t num_4kpages, bool writable) {
  auto pml4_table = reinterpret_cast<PageMapEntry*>(GetCR3());
  return SetupPageMap(pml4_table, 4, addr, num_4kpages, writable).error;
}

Error SetupHugePageMaps(LinearAddress4Level addr, size_t num_2mpages, bool writable) {
  auto pml4_table = reinterpret_cast<PageMapEntry*>(GetCR3());
  return SetupHugePageMap(pml4_table, 4, addr, num_2mpages, writable).error;
}

Error CleanPageMaps(LinearAddress4Level addr) {
  auto pml4_table = reinterpret_cast<PageMapEntry*>(GetCR3());
  return CleanPageMap(pml4_table, 4, addr);
}

// #@@range_begin(copy_page_maps)
Error CopyPageMaps(PageMapEntry* dest, PageMapEntry* src, int part, int start) {
  if (part == 1) {
    for (int i = start; i < 512; ++i) {
      if (!src[i].bits.present) {
        continue;
      }
      dest[i] = src[i];
      dest[i].bits.writable = 0;
    }
    return MAKE_ERROR(Error::kSuccess);
  }

  for (int i = start; i < 512; ++i) {
    if (!src[i].bits.present) {
      continue;
    }
    auto [ table, err ] = NewPageMap();
    if (err) {
      return err;
    }
    dest[i] = src[i];
    dest[i].SetPointer(table);
    if (auto err = CopyPageMaps(table, src[i].Pointer(), part - 1, 0)) {
      return err;
    }
  }
  return MAKE_ERROR(Error::kSuccess);
}
// #@@range_end(copy_page_maps)

// #@@range_begin(handle_pf)
Error HandlePageFault(uint64_t error_code, uint64_t causal_addr) {
  auto& task = task_manager->CurrentTask();
  const bool present = (error_code >> 0) & 1;
  const bool rw      = (error_code >> 1) & 1;
  const bool user    = (error_code >> 2) & 1;
  if (present && rw && user) {
    return CopyOnePage(causal_addr);
  } else if (present) {
    return MAKE_ERROR(Error::kAlreadyAllocated);
  }

  if (task.DPagingBegin() <= causal_addr && causal_addr < task.DPagingEnd()) {
    //added by kk@hugepage allocation
    if((causal_addr & (2_MiB -1)) == 0){
      printk("Try to Hugepage allocate\n");
      auto huge_success =
          SetupHugePageMaps(LinearAddress4Level{causal_addr}, 1);
      if(huge_success==Error::kSuccess)
        return huge_success;
    }
// #@@range_end(handle_pf)
    return SetupPageMaps(LinearAddress4Level{causal_addr}, 1);
  }
  if (auto m = FindFileMapping(task.FileMaps(), causal_addr)) {
    return PreparePageCache(*task.Files()[m->fd], *m, causal_addr);
  }
  return MAKE_ERROR(Error::kIndexOutOfRange);
}
