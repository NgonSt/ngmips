#include "mips_cache.h"

#include "panic.h"

namespace {

constexpr uint64_t kPhysicalUnmappedAddress = 0xFFFFFFFFFFFFFFFFUL;

uint64_t calculate_physical_address_psx(uint64_t address) {
  address &= 0xFFFFFFFF;

  bool is_valid_kuseg = address < 0x20000000;
  bool is_unmapped_kuseg = (address >= 0x20000000) && (address < 0x80000000);
  bool is_kseg0 = (address >= 0x80000000) && (address < 0xA0000000);
  bool is_kseg1 = (address >= 0xA0000000) && (address < 0xC0000000);
  bool is_kseg2 = address >= 0xC0000000;
  if (is_unmapped_kuseg || is_kseg2) {
    return kPhysicalUnmappedAddress;
  }

  uint32_t address_mapped = address & 0x1FFFFFFC;
  return address_mapped;
}

}  // namespace

MipsCache::MipsCache() {
  lookup_cache_index_ = 0;
  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].address_ = 0;
    lookup_cache_[i].block_ = nullptr;
  }
}

void MipsCache::Reset() {
  cache_.clear();
  lookup_cache_index_ = 0;
  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].address_ = 0;
    lookup_cache_[i].block_ = nullptr;
  }
}

MipsCacheBlock* MipsCache::GetBlock(uint64_t address) {
  if (!kUseCachedInterp) {
    return nullptr;
  }
  address = calculate_physical_address_psx(address);

  if (address == kPhysicalUnmappedAddress) {
    return nullptr;
  }

  // Check multi-entry lookup cache (linear search is fast for small sizes)
  for (int i = 0; i < kLookupCacheSize; i++) {
    if (lookup_cache_[i].address_ == address && lookup_cache_[i].block_ != nullptr) [[likely]] {
      return lookup_cache_[i].block_;  // Cache hit
    }
  }

  // Cache miss: look up in hash map
  auto found = cache_.find(address);
  if (found != cache_.end()) {
    // Insert into lookup cache using round-robin
    int insert_index = lookup_cache_index_;
    lookup_cache_[insert_index].address_ = address;
    lookup_cache_[insert_index].block_ = &found->second;
    lookup_cache_index_ = (lookup_cache_index_ + 1) & (kLookupCacheSize - 1);
    return lookup_cache_[insert_index].block_;
  }
  return nullptr;
}

MipsCacheBlock* MipsCache::GetOverlappingEntry(uint64_t address) {
  if (!kUseCachedInterp) {
    return nullptr;
  }
  address = calculate_physical_address_psx(address);

  return nullptr;
}

void MipsCache::InsertBlock(const MipsCacheBlock& block) {
  if (!kUseCachedInterp) {
    return;
  }

  MipsCacheBlock block_copy = block;
  block_copy.start_ = calculate_physical_address_psx(block_copy.start_);
  block_copy.valid_ = true;
  block_copy.in_use_ = false;

  if (block_copy.start_ == kPhysicalUnmappedAddress) {
    return;
  }

  cache_.insert(std::make_pair(block_copy.start_, block_copy));

  // Clear lookup cache since hash map may have rehashed, invalidating pointers
  // This is safe but conservative - only happens on new block creation
  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].block_ = nullptr;
  }
}

void MipsCache::InvalidateBlock(uint64_t address) {
  if (!kUseCachedInterp) {
    return;
  }
  address = calculate_physical_address_psx(address);

  if (address == kPhysicalUnmappedAddress) {
    return;
  }

  auto it = cache_.begin();
  while (it != cache_.end()) {
    if (address >= it->second.start_ && address < it->second.end_) {
      for (int i = 0; i < kLookupCacheSize; i++) {
        if (lookup_cache_[i].block_ == &it->second) {
          lookup_cache_[i].block_ = nullptr;
        }
      }
      if (it->second.in_use_) {
        it->second.valid_ = false;
        ++it;
      } else {
        it = cache_.erase(it);
      }
    } else {
      ++it;
    }
  }
}

void MipsCache::InvalidateBlockRange(uint64_t start, uint64_t end) {
}

void MipsCache::DeleteInUseBlock(uint64_t address) {
  if (!kUseCachedInterp) {
    return;
  }
  address = calculate_physical_address_psx(address);

  if (address == kPhysicalUnmappedAddress) {
    return;
  }

  auto it = cache_.find(address);
  if (it != cache_.end()) {
    if (it->second.in_use_) {
      cache_.erase(it);
    }
    else {
      PANIC("DeleteInUseBlock called on non-in-use block");
    }
  }
}

void MipsCache::ClearCache() {
  auto it = cache_.begin();
  while (it != cache_.end()) {
    for (int i = 0; i < kLookupCacheSize; i++) {
      if (lookup_cache_[i].block_ == &it->second) {
        lookup_cache_[i].block_ = nullptr;
      }
    }
    if (it->second.in_use_) {
      it->second.valid_ = false;
      ++it;
    } else {
      it = cache_.erase(it);
    }
  }

  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].address_ = 0;
    lookup_cache_[i].block_ = nullptr;
  }
}
