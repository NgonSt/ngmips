#pragma once
#include "mips_tlb.h"

class MipsTlbNormal : public MipsTlbBase {
 public:
  MipsTlbNormal();
  void Reset();
  MipsTlbTranslationResult TranslateAddress(uint64_t address);
  const MipsTlbEntry& GetTlbEntry(int idx);
  void SetTlbEntry(int idx, const MipsTlbEntry& entry);
  uint64_t GetEntryHi();
  void SetEntryHi(uint64_t value);
  uint64_t GetEntryLo0();
  void SetEntryLo0(uint64_t value);
  uint64_t GetEntryLo1();
  void SetEntryLo1(uint64_t value);
  uint64_t GetPageMask();
  void SetPageMask(uint64_t value);
  uint32_t GetIndex();
  void SetIndex(uint32_t value);
  void InformTlbException(uint64_t address);
  uint32_t ProbeTlbEntry();

 private:
  MipsTlbEntry entry_[32];
  uint64_t entry_hi_ = 0;
  uint64_t entry_lo0_ = 0;
  uint64_t entry_lo1_ = 0;
  uint64_t page_mask_ = 0;
  uint32_t index_ = 0;
};