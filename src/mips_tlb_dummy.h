#pragma once
#include "mips_tlb.h"

class MipsTlbDummy : public MipsTlbBase {
 public:
  void Reset() override {};
  MipsTlbTranslationResult TranslateAddress(uint64_t address) override {
    MipsTlbTranslationResult result;
    result.found_ = true;
    result.read_only_ = false;
    result.address_ = address;
    return result;
  };
  const MipsTlbEntry& GetTlbEntry(int idx) override { return dummy_entry_; };
  void SetTlbEntry(int idx, const MipsTlbEntry& entry) override {};
  uint64_t GetEntryHi() override { return 0; };
  void SetEntryHi(uint64_t value) override {};
  uint64_t GetEntryLo0() override { return 0; };
  void SetEntryLo0(uint64_t value) override {};
  uint64_t GetEntryLo1() override { return 0; };
  void SetEntryLo1(uint64_t value) override {};
  uint64_t GetPageMask() override { return 0; };
  void SetPageMask(uint64_t value) override {};
  uint32_t GetIndex() override { return 0; };
  void SetIndex(uint32_t value) override {};
  void InformTlbException(uint64_t address) override {};
  uint32_t ProbeTlbEntry() override { return 0x80000000; };

 private:
  MipsTlbEntry dummy_entry_;
};