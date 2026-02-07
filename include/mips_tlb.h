#pragma once
#include <cstdint>

class MipsTlbEntry {
 public:
  MipsTlbEntry() {
    entry_lo0_ = 0;
    entry_lo1_ = 0;
    entry_hi_ = 0;
    page_mask_ = 0;
  }
  MipsTlbEntry(uint64_t entry_lo0, uint64_t entry_lo1, uint64_t entry_hi, uint64_t page_mask) {
    entry_lo0_ = entry_lo0;
    entry_lo1_ = entry_lo1;
    entry_hi_ = entry_hi;
    page_mask_ = page_mask;
  }

  uint64_t entry_lo0_;
  uint64_t entry_lo1_;
  uint64_t entry_hi_;
  uint64_t page_mask_;
};

struct MipsTlbTranslationResult {
  bool found_;
  bool read_only_;
  uint64_t address_;
};

class MipsTlbBase {
 public:
  virtual void Reset() = 0;
  virtual MipsTlbTranslationResult TranslateAddress(uint64_t address) = 0;
  virtual const MipsTlbEntry& GetTlbEntry(int idx) = 0;
  virtual void SetTlbEntry(int idx, const MipsTlbEntry& entry) = 0;
  virtual uint64_t GetEntryHi() = 0;
  virtual void SetEntryHi(uint64_t value) = 0;
  virtual uint64_t GetEntryLo0() = 0;
  virtual void SetEntryLo0(uint64_t value) = 0;
  virtual uint64_t GetEntryLo1() = 0;
  virtual void SetEntryLo1(uint64_t value) = 0;
  virtual uint64_t GetPageMask() = 0;
  virtual void SetPageMask(uint64_t value) = 0;
  virtual uint32_t GetIndex() = 0;
  virtual void SetIndex(uint32_t value) = 0;
  virtual void InformTlbException(uint64_t address) = 0;
  virtual uint32_t ProbeTlbEntry() = 0;
};