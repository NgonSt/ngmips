#include "mips_cache.h"

#include "panic.h"

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

MipsCacheBlock* MipsCache::GetBlock(uint64_t address) {
  if (!kUseCachedInterp) {
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

  return nullptr;
}

void MipsCache::InsertBlock(const MipsCacheBlock& block) {
  if (!kUseCachedInterp) {
    return;
  }

  cache_.insert(std::make_pair(block.start_, block));

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
  address &= 0xFFFFFFFF;

  // Queue the address for deferred deletion
  pending_invalidations_.push_back(address);
}

void MipsCache::InvalidateBlockRange(uint64_t start, uint64_t end) {
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
