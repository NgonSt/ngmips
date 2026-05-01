#include "mips_cache.h"

#include "mips_base.h"
#include "panic.h"

// Bring in TLB types so they're complete for explicit instantiation
#include "mips_tlb_normal.h"
#include "mips_tlb_dummy.h"

namespace {

constexpr uint64_t kPhysicalUnmappedAddress = 0xFFFFFFFFFFFFFFFFUL;

}  // namespace

#define CACHE_TEMPLATE template<typename MipsT, typename TlbType>
#define CACHE_CLASS MipsCache<MipsT, TlbType>

CACHE_TEMPLATE
CACHE_CLASS::MipsCache() {
  lookup_cache_index_ = 0;
  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].address_ = 0;
    lookup_cache_[i].block_ = nullptr;
  }
}

CACHE_TEMPLATE
void CACHE_CLASS::Reset() {
  full_clear_queued_ = false;
  has_pending_work_ = false;
  pending_invalidations_.clear();
  cache_.clear();
  lookup_cache_index_ = 0;
  for (int i = 0; i < kLookupCacheSize; i++) {
    lookup_cache_[i].address_ = 0;
    lookup_cache_[i].block_ = nullptr;
  }
}

CACHE_TEMPLATE
void CACHE_CLASS::ConnectTlb(TlbType* tlb) {
  tlb_ = tlb;
}

CACHE_TEMPLATE
MipsCacheBlock<MipsT>* CACHE_CLASS::GetBlock(uint64_t address) {
  // Fast path for kseg0/kseg1: avoid virtual TLB call
  address &= 0xFFFFFFFF;
  bool is_kseg0 = address >= 0x80000000 && address < 0xA0000000;
  bool is_kseg1 = address >= 0xA0000000 && address < 0xC0000000;
  if (is_kseg0 || is_kseg1) {
    address = address & 0x1FFFFFFF;
  } else {
    auto result = tlb_->TranslateAddress(address);
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

CACHE_TEMPLATE
MipsCacheBlock<MipsT>* CACHE_CLASS::GetOverlappingEntry(uint64_t address) {
  auto result = tlb_->TranslateAddress(address);
  if (!result.found_) {
    return nullptr;
  }
  address = result.address_;

  return nullptr;
}

CACHE_TEMPLATE
void CACHE_CLASS::InsertBlock(const MipsCacheBlock<MipsT>& block) {
  MipsCacheBlock<MipsT> block_copy = block;
  auto result = tlb_->TranslateAddress(block_copy.start_);
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

CACHE_TEMPLATE
void CACHE_CLASS::InvalidateBlock(uint64_t address) {
  auto result = tlb_->TranslateAddress(address);
  if (!result.found_) {
    return;
  }
  address = result.address_;

  bool is_in_cache = cache_.find(address) != cache_.end();
  if (is_in_cache) {
    pending_invalidations_.insert(address);
    has_pending_work_ = true;
    return;
  }

  if (pending_invalidations_.find(address) != pending_invalidations_.end()) {
    return;
  }

  auto it = cache_.begin();
  while (it != cache_.end()) {
    if (address >= it->second.start_ && address < it->second.end_) {
      pending_invalidations_.insert(it->second.start_);
      has_pending_work_ = true;
      break;
    } else {
      ++it;
    }
  }
}

CACHE_TEMPLATE
void CACHE_CLASS::InvalidateBlockRange(uint64_t start, uint64_t end) {
  auto result = tlb_->TranslateAddress(start);
  if (!result.found_) {
    return;
  }
  uint64_t phys_start = result.address_;
  uint64_t phys_end = phys_start + (end - start);

  auto it = cache_.begin();
  while (it != cache_.end()) {
    if (it->second.start_ < phys_end && it->second.end_ > phys_start) {
      pending_invalidations_.insert(it->second.start_);
      has_pending_work_ = true;
    }
    ++it;
  }
}

CACHE_TEMPLATE
void CACHE_CLASS::QueueCacheClear() {
  full_clear_queued_ = true;
  has_pending_work_ = true;
}

CACHE_TEMPLATE
void CACHE_CLASS::ExecuteCacheClear() {
  if (full_clear_queued_) {
    cache_.clear();
    pending_invalidations_.clear();
    full_clear_queued_ = false;
    has_pending_work_ = false;
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
    has_pending_work_ = false;
  }
}

// Explicit instantiations — keep definitions out of other TUs
template class MipsCache<N64Mips, MipsTlbNormal>;
template class MipsCache<RspMips, MipsTlbDummy>;
