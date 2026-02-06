#pragma once
#include <stdfloat>

#include "mips_cop.h"

using f32_t = float;
using f64_t = double;

class MipsFpu : public MipsCopBase {
 public:
  MipsFpu();

  void ConnectCpu(MipsBase* cpu) override;
  void Reset() override;
  void Command(uint32_t command) override;
  uint32_t Read32(int idx) override;
  void Write32(int idx, uint32_t value) override;
  uint64_t Read64(int idx) override;
  void Write64(int idx, uint64_t value) override;
  uint32_t Read32Internal(int idx) override { return 0; }
  void Write32Internal(int idx, uint32_t value) override {}
  uint64_t Read64Internal(int idx) override { return 0; }
  void Write64Internal(int idx, uint64_t value) override {}
  bool GetFlag() override;

 private:
  float ReadF32(int idx);
  void WriteF32(int idx, f32_t value);
  double ReadF64(int idx);
  void WriteF64(int idx, f64_t value);
  bool GetFr();

  void InstAdd(uint32_t opcode);
  void InstSub(uint32_t opcode);
  void InstMul(uint32_t opcode);
  void InstDiv(uint32_t opcode);
  void InstSqrt(uint32_t opcode);
  void InstAbs(uint32_t opcode);
  void InstMov(uint32_t opcode);
  void InstNeg(uint32_t opcode);
  void InstRoundL(uint32_t opcode);
  void InstTruncL(uint32_t opcode);
  void InstCeilL(uint32_t opcode);
  void InstFloorL(uint32_t opcode);
  void InstRoundW(uint32_t opcode);
  void InstTruncW(uint32_t opcode);
  void InstCeilW(uint32_t opcode);
  void InstFloorW(uint32_t opcode);
  void InstCvtS(uint32_t opcode);
  void InstCvtD(uint32_t opcode);
  void InstCvtW(uint32_t opcode);
  void InstCvtL(uint32_t opcode);
  void InstC(uint32_t opcode);

  MipsBase* cpu_;

  uint64_t fpr_[32];
  uint32_t fcr31_;
};