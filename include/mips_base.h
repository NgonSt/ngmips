#pragma once
#include <cstdint>
#include <memory>
#include <string>

#include "bus_base.h"
#include "mips_cache.h"
#include "mips_cop.h"
#include "mips_hook.h"
#include "mips_tlb.h"
#include "mips_tlb_dummy.h"
#include "mips_tlb_normal.h"

typedef __int128_t int128_t;
typedef __uint128_t uint128_t;

const bool kLazyInterruptPolling = false;
const int kInterruptCheckInterval = 4;
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

struct MipsConfig {
  bool is_64bit_ = false;
  bool use_big_endian_ = false;
  bool has_load_delay_ = false;
  bool has_exception_ = false;
  bool allow_misaligned_access_ = false;
  bool has_cop0_ = false;
  bool has_fpu_ = false;
  bool use_cached_interpreter_ = false;
  bool has_isolate_cache_bit_ = false;
  bool use_hook_ = false;
  uint8_t cop_decoding_override_ = 0;
  uint16_t cpi_ = 0x180;
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

// Non-templated abstract base — stable API for external code (buses, COPs, etc.)
class MipsInterface {
 public:
  virtual ~MipsInterface() = default;
  virtual void Reset() = 0;
  virtual int Run(int cycle) = 0;
  virtual void ConnectCop(std::shared_ptr<MipsCopBase> cop, int idx) = 0;
  virtual void ConnectBus(std::shared_ptr<BusBase> bus) = 0;
  virtual void ConnectHook(std::shared_ptr<MipsHookBase> hook, int idx) = 0;
  virtual void SetPc(uint64_t pc) = 0;
  virtual void SetPcDuringInst(uint64_t pc) = 0;
  virtual uint64_t GetPc() = 0;
  virtual uint64_t GetGpr(int idx) = 0;
  virtual void SetGpr(int idx, uint64_t value) = 0;
  virtual void SetLlbit(bool llbit) = 0;
  virtual bool GetHalt() = 0;
  virtual void SetHalt(bool halt) = 0;
  virtual uint64_t GetTimestamp() = 0;
  virtual void CheckCompare() = 0;
  virtual void ClearCompareInterrupt() = 0;
  virtual void CheckInterrupt() = 0;
  virtual MipsCopBase* GetCop(int idx) = 0;
  virtual MipsTlbBase* GetTlb() = 0;
  virtual void DumpProcessorLog() = 0;
  virtual void QueueCacheClear() = 0;
};

template <
    typename TlbType,
    bool kIs64Bit,
    bool kHasLoadDelay,
    bool kHasCop0>
class MipsBase : public MipsInterface {
 public:
  MipsBase();
  MipsBase(MipsConfig config);
  ~MipsBase();
  void Reset() override;
  int Run(int cycle) override;
  int RunCached(int cycle);
  void RunInst();
  void ConnectCop(std::shared_ptr<MipsCopBase> cop, int idx) override;
  void ConnectBus(std::shared_ptr<BusBase> bus) override;
  void ConnectHook(std::shared_ptr<MipsHookBase> hook, int idx) override;
  void SetPc(uint64_t pc) override;
  void SetPcDuringInst(uint64_t pc) override;
  uint64_t GetPc() override;
  uint64_t GetGpr(int idx) override;
  void SetGpr(int idx, uint64_t value) override;
  void SetLlbit(bool llbit) override;
  bool GetHalt() override;
  void SetHalt(bool halt) override;
  uint64_t GetTimestamp() override;
  void CheckCompare() override;
  void ClearCompareInterrupt() override;
  void CheckInterrupt() override;
  MipsCopBase* GetCop(int idx) override { return cop_[idx].get(); }
  MipsTlbBase* GetTlb() override { return &tlb_; }
  void DumpProcessorLog() override;
  void QueueCacheClear() override { cache_.QueueCacheClear(); }

 private:
  using inst_ptr_t = void (MipsBase::*)(uint32_t);
  using Cache = MipsCache<MipsBase, TlbType>;

  auto GetInstFuncPtr(uint32_t opcode) -> inst_ptr_t;
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
  int interrupt_poll_counter_;
  uint64_t cycle_spent_total_;
  bool has_branch_delay_;
  uint64_t branch_delay_dst_;
  DelayedLoadOp delayed_load_op_;

  bool compare_interrupt_;
  int cop_cause_;

  std::shared_ptr<MipsHookBase> hook_[2];

  int mips_log_index_;
  MipsLog mips_log_[kMipsInstLogCount];

  Cache cache_;
  bool halt_;

  MipsConfig config_;

 protected:
  std::shared_ptr<BusBase> bus_;
  std::shared_ptr<MipsCopBase> cop_[4];
  TlbType tlb_;
};

using N64Mips = MipsBase<MipsTlbNormal, true, false, true>;
using RspMips = MipsBase<MipsTlbDummy, false, false, false>;

extern template class MipsBase<MipsTlbNormal, true, false, true>;
extern template class MipsBase<MipsTlbDummy, false, false, false>;
