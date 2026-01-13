#pragma once
#include <cstdint>
#include <ankerl/unordered_dense.h>
#include <unordered_map>

const bool kUseCachedInterp = true;
const int kCacheBlockMaxLength = 32;
const int kLookupCacheSize = 4;

class MipsBase;
typedef void (MipsBase::*inst_ptr_t)(uint32_t);

struct MipsCacheEntry {
  uint32_t address_;
  uint32_t opcode_;
  inst_ptr_t func_;
};

struct MipsCacheBlock {
  uint32_t start_;
  uint32_t end_;
  MipsCacheEntry entries_[kCacheBlockMaxLength];
  int length_;
  int cycle_;
  bool valid_;
  bool in_use_;
};

class MipsCache {
 public:
  MipsCache();
  void Reset();
  MipsCacheBlock* GetBlock(uint64_t address);
  MipsCacheBlock* GetOverlappingEntry(uint64_t address);
  void InsertBlock(const MipsCacheBlock& block);
  void InvalidateBlock(uint64_t address);
  void InvalidateBlockRange(uint64_t start, uint64_t end);
  size_t GetSize() { return cache_.size(); };
  void DeleteInUseBlock(uint64_t address);
  void ClearCache();

 private:
  ankerl::unordered_dense::map<uint64_t, MipsCacheBlock> cache_;

  // Multi-entry lookup cache for fast repeat access
  struct LookupCacheEntry {
    uint64_t address_;
    MipsCacheBlock* block_;
  };
  LookupCacheEntry lookup_cache_[kLookupCacheSize];
  int lookup_cache_index_;  // Round-robin insertion pointer
};