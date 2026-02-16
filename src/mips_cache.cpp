#include "mips_cache.h"

#include "panic.h"

namespace {

constexpr uint64_t kPhysicalUnmappedAddress = 0xFFFFFFFFFFFFFFFFUL;

uint64_t calculate_physical_address_psx(uint64_t address) {
  address &= 0xFFFFFFFF;

  return kPhysicalUnmappedAddress;
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
  full_clear_queued_ = false;
  pending_invalidations_.clear();
  cache_.clear();
  lookup_cache_index_ = 0;
  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].address_ = 0;
    lookup_cache_[i].block_ = nullptr;
  }
}

void MipsCache::ConnectTlb(std::shared_ptr<MipsTlbBase> tlb) {
  tlb_ = tlb;
}

MipsCacheBlock* MipsCache::GetBlock(uint64_t address) {
  // Fast path for kseg0/kseg1: avoid virtual TLB call
  address &= 0xFFFFFFFF;
  bool is_kseg0 = address >= 0x80000000 && address < 0xA0000000;
  bool is_kseg1 = address >= 0xA0000000 && address < 0xC0000000;
  if (is_kseg0 || is_kseg1) {
    address = address & 0x1FFFFFFF;
  } else {
    MipsTlbTranslationResult result = tlb_->TranslateAddress(address);
    if (!result.found_) {
      return nullptr;
    }
    address = result.address_;
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
  MipsTlbTranslationResult result = tlb_->TranslateAddress(address);
  if (!result.found_) {
    return nullptr;
  }
  address = result.address_;

  return nullptr;
}

void MipsCache::InsertBlock(const MipsCacheBlock& block) {
  MipsCacheBlock block_copy = block;
  MipsTlbTranslationResult result = tlb_->TranslateAddress(block_copy.start_);
  if (!result.found_) {
    return;
  }
  uint64_t offset = block_copy.end_ - block_copy.start_;
  block_copy.start_ = result.address_;
  block_copy.end_ = result.address_ + offset;

  cache_.insert(std::make_pair(block_copy.start_, block_copy));

  // Clear lookup cache since hash map may have rehashed, invalidating pointers
  // This is safe but conservative - only happens on new block creation
  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].block_ = nullptr;
  }
}

void MipsCache::InvalidateBlock(uint64_t address) {
  MipsTlbTranslationResult result = tlb_->TranslateAddress(address);
  if (!result.found_) {
    return;
  }
  address = result.address_;

  bool is_in_cache = cache_.find(address) != cache_.end();
  if (is_in_cache) {
    pending_invalidations_.insert(address);
    return;
  }

  if (pending_invalidations_.find(address) != pending_invalidations_.end()) {
    return;
  }

  auto it = cache_.begin();
  while (it != cache_.end()) {
    if (address >= it->second.start_ && address < it->second.end_) {
      pending_invalidations_.insert(it->second.start_);
      break;
    } else {
      ++it;
    }
  }
}

void MipsCache::InvalidateBlockRange(uint64_t start, uint64_t end) {
  MipsTlbTranslationResult result = tlb_->TranslateAddress(start);
  if (!result.found_) {
    return;
  }
  uint64_t phys_start = result.address_;
  uint64_t phys_end = phys_start + (end - start);

  auto it = cache_.begin();
  while (it != cache_.end()) {
    if (it->second.start_ < phys_end && it->second.end_ > phys_start) {
      pending_invalidations_.insert(it->second.start_);
    }
    ++it;
  }
}

void MipsCache::QueueCacheClear() {
  full_clear_queued_ = true;
}

void MipsCache::ExecuteCacheClear() {
  if (full_clear_queued_) {
    cache_.clear();
    pending_invalidations_.clear();
    full_clear_queued_ = false;
    // Clear lookup cache
    for (int i = 0; i < kLookupCacheSize; i++) {
      lookup_cache_[i].address_ = 0;
      lookup_cache_[i].block_ = nullptr;
    }
    return;
  }

  // Process individual invalidations
  if (!pending_invalidations_.empty()) {
    for (uint64_t address : pending_invalidations_) {
      // Find the block that contains this address
      auto it = cache_.begin();
      while (it != cache_.end()) {
        if (address >= it->second.start_ && address < it->second.end_) {
          // Invalidate lookup cache entries pointing to this block
          for (int i = 0; i < kLookupCacheSize; i++) {
            if (lookup_cache_[i].block_ == &it->second) {
              lookup_cache_[i].block_ = nullptr;
            }
          }
          it = cache_.erase(it);
          break;
        } else {
          ++it;
        }
      }
    }
    pending_invalidations_.clear();
  }
}
