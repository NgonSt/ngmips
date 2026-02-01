#pragma once
#include "mips_cop.h"

class MipsCopDummy : public MipsCopBase {
 public:
  MipsCopDummy() {};

  void ConnectCpu(MipsBase* cpu) override {};
  void Reset() override {};
  void Command(uint32_t command) override {};
  uint32_t Read32(int idx) override { return 0; };
  void Write32(int idx, uint32_t value) override {};
  uint64_t Read64(int idx) override { return 0; };
  void Write64(int idx, uint64_t value) override {};
  uint32_t Read32Internal(int idx) override { return 0; };
  void Write32Internal(int idx, uint32_t value) override {};
  uint64_t Read64Internal(int idx) override { return 0; };
  void Write64Internal(int idx, uint64_t value) override {};
  bool GetFlag() override { return false; };
};