#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "bus_base.h"
#include "mips_cache.h"
#include "mips_cop.h"
#include "mips_tlb.h"
#include "mips_hook.h"

typedef __int128_t int128_t;
typedef __uint128_t uint128_t;

const bool kLazyInterruptPolling = false && !kUseCachedInterp;
const int kMipsInstLogCount = 2048;

enum class ExceptionCause {
  kInt = 0,
  kTlbMod = 1,
  kTlbMissLoad = 2,
  kTlbMissStore = 3,
  kAddrl = 4,
  kAddrs = 5,
  kIbus = 6,
  kDbus = 7,
  kSyscall = 8,
  kBkpt = 9,
  kRi = 10,
  kCop = 11,
  kOvf = 12
};

class MipsLog {
 public:
  uint64_t pc_;
  uint32_t inst_;
  uint64_t gpr_[32];
  std::string ToString(bool is_64bit);
};

struct DelayedLoadOp {
  bool is_active_;
  int delay_counter_;
  int cop_id_;  // -1 if dst is not cop
  int dst_;
  uint32_t value_;
};

struct MipsConfig {
  bool is_64bit_;
  bool use_big_endian_;
  bool has_load_delay_;
  bool has_exception_;            // RSP doesn't have exception
  bool allow_misaligned_access_;  // RSP allows misaligned memory access
  bool has_cop0_;                 // RSP doesn't have conventional cop0
  bool has_tlb_; 
  bool has_fpu_;
  bool has_isolate_cache_bit_;     // for PSX
  uint8_t cop_decoding_override_;  // if (x & cop_id), redirect lwc/swc/mfc/mtc/cfc/ctc -> cop
  bool use_hook_;
  uint16_t cpi_;  // u8.8 fixed point
};

class MipsBase {
 public:
  MipsBase();
  MipsBase(MipsConfig config);
  ~MipsBase();
  void Reset();
  int Run(int cycle);
  int RunCached(int cycle);
  void RunInst();
  void ConnectCop(std::shared_ptr<MipsCopBase> cop, int idx);
  void ConnectBus(std::shared_ptr<BusBase> bus);
  void ConnectHook(std::shared_ptr<MipsHookBase> hook, int idx);
  void SetPc(uint64_t pc);
  void SetPcDuringInst(uint64_t pc);
  uint64_t GetPc();
  uint64_t GetGpr(int idx);
  void SetGpr(int idx, uint64_t value);
  void SetLlbit(bool llbit);
  bool GetHalt();
  void SetHalt(bool halt);
  uint64_t GetTimestamp();
  void CheckCompare();
  void ClearCompareInterrupt();
  void CheckInterrupt();
  MipsCopBase* GetCop(int idx) { return cop_[idx].get(); };
  MipsTlbBase* GetTlb() { return tlb_.get(); };
  MipsCache& GetMipsCache() { return cache_; };
  void DumpProcessorLog();

 private:
  inst_ptr_t GetInstFuncPtr(uint32_t opcode);
  uint32_t ReadGpr32(int idx);
  void WriteGpr32(int idx, uint32_t value);
  void WriteGpr32Sext(int idx, int32_t value);
  uint64_t ReadGpr64(int idx);
  void WriteGpr64(int idx, uint64_t value);
  void JumpRel(int32_t offset);
  void LinkForJump(int dst_reg);
  void Jump32(uint32_t dst);
  void Jump64(uint64_t dst);
  void QueueDelayedLoad(int dst, uint64_t value);
  void QueueDelayedCopLoad(int cop_id, int dst, uint64_t value);
  void ExecuteDelayedLoad();
  void TriggerException(ExceptionCause cause);
  void CheckHook();
  bool IsCopEnabled(int cop_id);
  uint32_t Fetch(uint64_t address);
  LoadResult8 Load8(uint64_t address);
  LoadResult16 Load16(uint64_t address);
  LoadResult32 Load32(uint64_t address);
  LoadResult64 Load64(uint64_t address);
  void Store8(uint64_t address, uint8_t value);
  void Store16(uint64_t address, uint16_t value);
  void Store32(uint64_t address, uint32_t value);
  void Store64(uint64_t address, uint64_t value);

  void OnNewBlock(uint64_t address);
  void InvalidateBlock(uint64_t address);

  void InstAdd(uint32_t opcode);
  void InstAddu(uint32_t opcode);
  void InstAddi(uint32_t opcode);
  void InstAddiu(uint32_t opcode);
  void InstAnd(uint32_t opcode);
  void InstAndi(uint32_t opcode);
  void InstDiv(uint32_t opcode);
  void InstDivu(uint32_t opcode);
  void InstMult(uint32_t opcode);
  void InstMultu(uint32_t opcode);
  void InstNor(uint32_t opcode);
  void InstOr(uint32_t opcode);
  void InstOri(uint32_t opcode);
  void InstSll(uint32_t opcode);
  void InstSllv(uint32_t opcode);
  void InstSra(uint32_t opcode);
  void InstSrav(uint32_t opcode);
  void InstSrl(uint32_t opcode);
  void InstSrlv(uint32_t opcode);
  void InstSub(uint32_t opcode);
  void InstSubu(uint32_t opcode);
  void InstXor(uint32_t opcode);
  void InstXori(uint32_t opcode);
  void InstLui(uint32_t opcode);
  void InstSlt(uint32_t opcode);
  void InstSltu(uint32_t opcode);
  void InstSlti(uint32_t opcode);
  void InstSltiu(uint32_t opcode);
  void InstBeq(uint32_t opcode);
  void InstBne(uint32_t opcode);
  void InstBgtz(uint32_t opcode);
  void InstBlez(uint32_t opcode);
  void InstBgez(uint32_t opcode);
  void InstBgezal(uint32_t opcode);
  void InstBltz(uint32_t opcode);
  void InstBltzal(uint32_t opcode);
  void InstJ(uint32_t opcode);
  void InstJal(uint32_t opcode);
  void InstJr(uint32_t opcode);
  void InstJalr(uint32_t opcode);
  void InstSyscall(uint32_t opcode);
  void InstBreak(uint32_t opcode);
  void InstLb(uint32_t opcode);
  void InstLbu(uint32_t opcode);
  void InstLh(uint32_t opcode);
  void InstLhu(uint32_t opcode);
  void InstLw(uint32_t opcode);
  void InstLwl(uint32_t opcode);
  void InstLwr(uint32_t opcode);
  void InstLwc(uint32_t opcode);
  void InstSb(uint32_t opcode);
  void InstSh(uint32_t opcode);
  void InstSw(uint32_t opcode);
  void InstSwl(uint32_t opcode);
  void InstSwr(uint32_t opcode);
  void InstSwc(uint32_t opcode);
  void InstMfhi(uint32_t opcode);
  void InstMflo(uint32_t opcode);
  void InstMthi(uint32_t opcode);
  void InstMtlo(uint32_t opcode);
  void InstCop(uint32_t opcode);
  void InstMfc(uint32_t opcode);
  void InstCfc(uint32_t opcode);
  void InstMtc(uint32_t opcode);
  void InstCtc(uint32_t opcode);
  void InstNop(uint32_t opcode);

  void InstBcf(uint32_t opcode);
  void InstBcfl(uint32_t opcode);
  void InstBct(uint32_t opcode);
  void InstBctl(uint32_t opcode);
  void InstBeql(uint32_t opcode);
  void InstBnel(uint32_t opcode);
  void InstBgezl(uint32_t opcode);
  void InstBgezall(uint32_t opcode);
  void InstBgtzl(uint32_t opcode);
  void InstBlezl(uint32_t opcode);
  void InstBltzl(uint32_t opcode);
  void InstBltzall(uint32_t opcode);
  void InstCache(uint32_t opcode);
  void InstDadd(uint32_t opcode);
  void InstDaddu(uint32_t opcode);
  void InstDaddi(uint32_t opcode);
  void InstDaddiu(uint32_t opcode);
  void InstDsub(uint32_t opcode);
  void InstDsubu(uint32_t opcode);
  void InstDmult(uint32_t opcode);
  void InstDmultu(uint32_t opcode);
  void InstDdiv(uint32_t opcode);
  void InstDdivu(uint32_t opcode);
  void InstDsll(uint32_t opcode);
  void InstDsll32(uint32_t opcode);
  void InstDsllv(uint32_t opcode);
  void InstDsra(uint32_t opcode);
  void InstDsra32(uint32_t opcode);
  void InstDsrav(uint32_t opcode);
  void InstDsrl(uint32_t opcode);
  void InstDsrl32(uint32_t opcode);
  void InstDsrlv(uint32_t opcode);
  void InstDmfc(uint32_t opcode);
  void InstDmtc(uint32_t opcode);
  void InstLd(uint32_t opcode);
  void InstLdc(uint32_t opcode);
  void InstLdl(uint32_t opcode);
  void InstLdr(uint32_t opcode);
  void InstLwu(uint32_t opcode);
  void InstSd(uint32_t opcode);
  void InstSdc(uint32_t opcode);
  void InstSdl(uint32_t opcode);
  void InstSdr(uint32_t opcode);
  void InstSync(uint32_t opcode);

  void InstUnknown(uint32_t opcode);

  uint64_t gpr_[32];
  uint64_t hi_;
  uint64_t lo_;
  uint64_t pc_;
  uint64_t next_pc_;
  bool llbit_;

  int cycle_spent_;
  int cpi_counter_;
  uint64_t cycle_spent_total_;
  bool has_branch_delay_;
  uint64_t branch_delay_dst_;
  DelayedLoadOp delayed_load_op_;

  bool compare_interrupt_;
  int cop_cause_;

  std::shared_ptr<MipsHookBase> hook_[2];

  int mips_log_index_;
  MipsLog mips_log_[kMipsInstLogCount];

  MipsCache cache_;
  bool halt_;

 protected:
  std::shared_ptr<BusBase> bus_;
  std::shared_ptr<MipsCopBase> cop_[4];
  std::shared_ptr<MipsTlbBase> tlb_;
  MipsConfig config_;
};