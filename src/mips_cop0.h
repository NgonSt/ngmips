#pragma once
#include "mips_cop.h"

class MipsCop0 : public MipsCopBase {
 public:
  MipsCop0();

  void ConnectCpu(MipsBase* cpu) override;
  void Reset() override;
  void Command(uint32_t command) override;
  uint32_t Read32(int idx) override;
  void Write32(int idx, uint32_t value) override;
  uint64_t Read64(int idx) override;
  void Write64(int idx, uint64_t value) override;
  uint32_t Read32Internal(int idx) override;
  void Write32Internal(int idx, uint32_t value) override;
  uint64_t Read64Internal(int idx) override;
  void Write64Internal(int idx, uint64_t value) override;
  bool GetFlag() override;
  bool CheckCompareInterrupt();

 private:
  void WriteCount(uint32_t value);
  uint32_t GetCount();

  MipsBase* cpu_;

  uint32_t index_;
  uint64_t entrylo0_;
  uint64_t entrylo1_;
  uint64_t context_;
  uint32_t page_mask_;
  uint32_t wired_;
  uint64_t badvaddr_;
  uint64_t entryhi_;
  uint32_t compare_;
  uint32_t sr_;
  uint32_t cause_;
  uint64_t epc_;
  uint64_t error_epc_;

  uint64_t count_start_timestamp_;
  uint64_t last_compare_check_timestamp_;
  bool surpress_compare_interrupt_;
};