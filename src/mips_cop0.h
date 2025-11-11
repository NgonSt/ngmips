#pragma once
#include "mips_cop.h"

class MipsCop0 : public MipsCopBase {
 public:
  MipsCop0();

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

 private:
  uint64_t badvaddr_;
  uint32_t sr_;
  uint32_t cause_;
  uint64_t epc_;
  uint32_t prid_;
};