#pragma once
#include <ankerl/unordered_dense.h>

#include <cstdint>
#include <set>

const int kCacheBlockMaxLength = 64;
const int kLookupCacheSize = 4;

template<typename MipsT>
struct MipsCacheEntry {
  uint32_t address_;
  uint32_t opcode_;
  void (MipsT::*func_)(uint32_t);
};

template<typename MipsT>
struct MipsCacheBlock {
  uint32_t start_;
  uint32_t end_;
  MipsCacheEntry<MipsT> entries_[kCacheBlockMaxLength];
  int length_;
  int cycle_;
};

template<typename MipsT, typename TlbType>
class MipsCache {
 public:
  MipsCache();
  void Reset();
  void ConnectTlb(TlbType* tlb);
  MipsCacheBlock<MipsT>* GetBlock(uint64_t address);
  MipsCacheBlock<MipsT>* GetOverlappingEntry(uint64_t address);
  void InsertBlock(const MipsCacheBlock<MipsT>& block);
  void InvalidateBlock(uint64_t address);
  void InvalidateBlockRange(uint64_t start, uint64_t end);
  size_t GetSize() { return cache_.size(); };
  void QueueCacheClear();
  void ExecuteCacheClear();
  bool HasPendingWork() const { return has_pending_work_; }

 private:
  ankerl::unordered_dense::map<uint64_t, MipsCacheBlock<MipsT>> cache_;
  std::set<uint64_t> pending_invalidations_;
  bool full_clear_queued_ = false;
  bool has_pending_work_ = false;

  // Multi-entry lookup cache for fast repeat access
  struct LookupCacheEntry {
    uint64_t address_;
    MipsCacheBlock<MipsT>* block_;
  };
  LookupCacheEntry lookup_cache_[kLookupCacheSize];
  int lookup_cache_index_;  // Round-robin insertion pointer

  TlbType* tlb_;
};
