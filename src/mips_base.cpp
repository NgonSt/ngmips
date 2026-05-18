#include "mips_base.h"

#include <fmt/format.h>

#include "mips_cache.h"
#include "mips_cop0.h"
#include "mips_cop_dummy.h"
#include "mips_decode.h"
#include "mips_fpu.h"
#include "mips_hook_dummy.h"
#include "mips_tlb_dummy.h"
#include "mips_tlb_normal.h"
#include "panic.h"

#define MIPS_TEMPLATE \
  template <typename TlbType, bool kIs64Bit, bool kHasLoadDelay, bool kHasCop0>

#define MIPS_BASE \
  MipsBase<TlbType, kIs64Bit, kHasLoadDelay, kHasCop0>

namespace {

const bool kLogCpu = false;
const bool kEnablePsxSpecific = false;
const bool kLogKernel = false;
const bool kPanicOnUnalignedJump = true;
const bool kLogMipsState = false;
const bool kEnableIdleLoopDetection = true;

const bool kLogNullWrites = false;
const bool kPanicOnNullJumps = false;
const bool kPanicOnNullWrites = false;
const uint32_t kPsxKnownNullWritePc[] = {
    0x00000F00, 0xBFC04E90, 0xBFC05164, 0x800585E4,
    0x80059C50, 0x80058788};

bool get_overflow_add_i32(uint32_t result, uint32_t lhs, uint32_t rhs) {
  return ((lhs ^ result) & (rhs ^ result)) & (1 << 31);
}

bool get_overflow_add_i64(uint64_t result, uint64_t lhs, uint64_t rhs) {
  return ((lhs ^ result) & (rhs ^ result)) & (1ULL << 63);
}

bool get_overflow_sub_i32(uint32_t result, uint32_t lhs, uint32_t rhs) {
  return ((lhs ^ rhs) & (lhs ^ result)) & (1 << 31);
}

bool get_overflow_sub_i64(uint64_t result, uint64_t lhs, uint64_t rhs) {
  return ((lhs ^ rhs) & (lhs ^ result)) & (1ULL << 63);
}

int64_t sext_i8_to_i64(int8_t value) {
  int64_t result = value;
  int shamt = (sizeof(int64_t) - sizeof(int8_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int64_t sext_i16_to_i64(int16_t value) {
  int64_t result = value;
  int shamt = (sizeof(int64_t) - sizeof(int16_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int64_t sext_i32_to_i64(int32_t value) {
  int64_t result = value;
  int shamt = (sizeof(int64_t) - sizeof(int32_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int128_t sext_i64_to_i128(int64_t value) {
  int128_t result = value;
  int shamt = (sizeof(int128_t) - sizeof(int64_t)) * 8;
  result <<= shamt;
  result >>= shamt;
  return result;
}

int32_t sext_itype_imm_i32(ITypeInst inst) {
  int32_t imm = inst.imm();
  int shamt = 16;
  imm <<= shamt;
  imm >>= shamt;
  return imm;
}

int64_t sext_itype_imm_i64(ITypeInst inst) {
  int64_t imm = inst.imm();
  int shamt = 16 + 32;
  imm <<= shamt;
  imm >>= shamt;
  return imm;
}

int32_t sext_itype_imm_branch(ITypeInst inst) {
  int32_t imm = inst.imm();
  imm <<= 16;
  imm >>= 14;
  return imm;
}

}  // namespace

MIPS_TEMPLATE
MIPS_BASE::MipsBase() : MipsBase(MipsConfig{}) {}

MIPS_TEMPLATE
MIPS_BASE::MipsBase(MipsConfig config) {
  config_ = config;

  for (int i = 0; i < 32; i++) {
    gpr_[i] = 0;
  }
  hi_ = 0;
  lo_ = 0;
  pc_ = 0;
  next_pc_ = 0;
  llbit_ = false;

  cycle_spent_ = 0;
  cpi_counter_ = 0;
  cycle_spent_total_ = 0;
  interrupt_poll_counter_ = 0;
  has_branch_delay_ = false;
  branch_delay_dst_ = 0;

  compare_interrupt_ = false;
  cop_cause_ = 0;

  halt_ = false;
  mips_log_index_ = 0;

  if constexpr (kHasCop0) {
    cop_[0] = std::make_shared<MipsCop0>();
  } else {
    cop_[0] = std::make_shared<MipsCopDummy>();
  }
  if (config_.has_fpu_) {
    cop_[1] = std::make_shared<MipsFpu>();
  } else {
    cop_[1] = std::make_shared<MipsCopDummy>();
  }
  cop_[2] = std::make_shared<MipsCopDummy>();
  cop_[3] = std::make_shared<MipsCopDummy>();
  for (int i = 0; i < 4; i++) {
    cop_[i]->ConnectCpu(this);
  }
  hook_[0] = std::make_shared<MipsHookDummy>();
  hook_[1] = std::make_shared<MipsHookDummy>();

  cache_.ConnectTlb(&tlb_);
}

MIPS_TEMPLATE
MIPS_BASE::~MipsBase() {
  if (cycle_spent_total_ != 0) {
    DumpProcessorLog();
  }
}

MIPS_TEMPLATE
void MIPS_BASE::Reset() {
  for (int i = 0; i < 32; i++) {
    gpr_[i] = 0;
  }
  hi_ = 0;
  lo_ = 0;
  pc_ = 0;
  next_pc_ = 0;
  llbit_ = false;

  cycle_spent_total_ = 0;
  cpi_counter_ = 0;

  has_branch_delay_ = false;
  branch_delay_dst_ = 0;

  compare_interrupt_ = false;
  cop_cause_ = 0;

  delayed_load_op_.is_active_ = false;

  cache_.Reset();
  halt_ = false;

  mips_log_index_ = 0;

  for (int i = 0; i < 4; i++) {
    cop_[i]->Reset();
  }

  tlb_.Reset();

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->Reset();
    }
  }

  if (false) {
    // Test decoding
    std::pair<uint32_t, MipsInstId> data[] = {
        {0x00621821, MipsInstId::kAddu},
        {0x24A50002, MipsInstId::kAddiu},
        {0x00431024, MipsInstId::kAnd},
        {0x32A2FFFF, MipsInstId::kAndi},
        {0x1040000A, MipsInstId::kBeq},
        {0x1440FFE3, MipsInstId::kBne},
        {0x1CC0FFAF, MipsInstId::kBgtz},
        {0x18400005, MipsInstId::kBlez},
        {0x04C10006, MipsInstId::kBgez},
        // {0x04C10006, MipsInstId::kBgezal},
        {0x07000003, MipsInstId::kBltz},
        // {0x07000003, MipsInstId::kBltzal},
        {0x45000005, MipsInstId::kBcf},  // BC1F
        // {0x45000005, MipsInstId::kBcf}, // BC1FL
        {0x45010002, MipsInstId::kBct},   // BC1T
        {0x45030005, MipsInstId::kBctl},  // BC1TL
        {0x50400006, MipsInstId::kBeql},
        {0x54400001, MipsInstId::kBnel},
        {0x0603FFFD, MipsInstId::kBgezl},  // BGEZL
        {0x5D000001, MipsInstId::kBgtzl},  // BGTZL
        {0x5A200020, MipsInstId::kBlezl},  // BLEZL
        {0x04620009, MipsInstId::kBltzl},  // BLTZL
        {0xBC8D0000, MipsInstId::kCache},
    };

    for (const auto [opcode, expected] : data) {
      const MipsInstId actual = Decode(opcode);
      if (actual != expected) {
        PANIC("Decoding bugged: {:08X} -> {}", opcode, GetInstName(opcode));
      }
    }
  }
}

MIPS_TEMPLATE
int MIPS_BASE::Run(int cycle) {
  if (config_.use_cached_interpreter_) {
    return RunCached(cycle);
  }
  cycle_spent_ = 0;
  if (!kLazyInterruptPolling) {
    CheckInterrupt();
  }
  CheckCompare();
  while (cycle_spent_ < cycle) {
    if (halt_) {
      if (cycle_spent_ < cycle) {
        int delta = cycle - cycle_spent_;
        cycle_spent_total_ += delta;
        return cycle;
      } else {
        return cycle_spent_;
      }
    }
    RunInst();
  }
  return cycle_spent_;
}

MIPS_TEMPLATE
int MIPS_BASE::RunCached(int cycle) {
  cycle_spent_ = 0;
  while (cycle_spent_ < cycle) {
    if constexpr (kHasCop0) {
      if (++interrupt_poll_counter_ >= kInterruptCheckInterval) {
        interrupt_poll_counter_ = 0;
        CheckInterrupt();
        CheckCompare();
      }
    }
    if (halt_) {
      if (cycle_spent_ < cycle) {
        int delta = cycle - cycle_spent_;
        cycle_spent_total_ += delta;
        return cycle;
      } else {
        return cycle_spent_;
      }
    }

    if (cache_.HasPendingWork()) {
      cache_.ExecuteCacheClear();
    }

    const MipsCacheBlock<MipsBase>* block = cache_.GetBlock(pc_);
    if (block == nullptr) {
      OnNewBlock(pc_);
      block = cache_.GetBlock(pc_);
      if (block == nullptr) {
        PANIC("Block creation failed");
      }
    }

    const int length = block->length_;
    const MipsCacheEntry<MipsBase>* entries = block->entries_;

    int executed = 0;
    for (int i = 0; i < length; i++) {
      // If a previous instruction (exception, branch-likely nullification)
      // changed PC to outside this block, stop executing the block
      if (i > 0 && pc_ != entries[i].address_) {
        break;
      }

      const uint32_t opcode = entries[i].opcode_;
      const uint32_t pc = entries[i].address_;
      const inst_ptr_t fp = entries[i].func_;

      MipsLog log;
      if (kLogCpu || kLogMipsState) {
        log.pc_ = pc_;
        log.inst_ = opcode;
        for (int i = 0; i < 32; i++) {
          log.gpr_[i] = ReadGpr64(i);
        }
      }

      if (kLogMipsState) {
        mips_log_[mips_log_index_] = log;
        mips_log_index_++;
        if (mips_log_index_ >= kMipsInstLogCount) {
          mips_log_index_ = 0;
        }
      }

      if (kLogCpu) {
        fmt::print("{}\n", log.ToString(kIs64Bit));
      }

      if (config_.use_hook_) {
        for (auto& hook : hook_) {
          hook->OnPreExecute(pc_, opcode);
        }
      }

      if (has_branch_delay_) {
        next_pc_ = branch_delay_dst_;
        has_branch_delay_ = false;
      } else {
        next_pc_ = pc_ + 4;
      }

      (this->*fp)(opcode);
      if constexpr (kHasLoadDelay) {
        ExecuteDelayedLoad();
      }

      pc_ = next_pc_ & 0xFFFFFFFF;
      executed++;
    }

    if (kEnablePsxSpecific) {
      CheckHook();
    }

    cpi_counter_ += executed * config_.cpi_;
    int cpi_integer = cpi_counter_ >> 8;
    cpi_counter_ &= 0xFF;
    cycle_spent_ += cpi_integer;
    cycle_spent_total_ += cpi_integer;
  }

  return cycle_spent_;
}

MIPS_TEMPLATE
void MIPS_BASE::ConnectCop(std::shared_ptr<MipsCopBase> cop, int idx) {
  cop_[idx] = cop;
}

MIPS_TEMPLATE
void MIPS_BASE::ConnectBus(std::shared_ptr<BusBase> bus) {
  bus_ = bus;
}

MIPS_TEMPLATE
void MIPS_BASE::ConnectHook(std::shared_ptr<MipsHookBase> hook, int idx) {
  hook_[idx] = hook;
}

MIPS_TEMPLATE
void MIPS_BASE::SetPc(uint64_t pc) {
  pc_ = pc;
  next_pc_ = pc + 4;
}

MIPS_TEMPLATE
void MIPS_BASE::SetPcDuringInst(uint64_t pc) {
  pc_ = pc;
  next_pc_ = pc;
}

MIPS_TEMPLATE
uint64_t MIPS_BASE::GetPc() {
  return pc_;
}

MIPS_TEMPLATE
uint64_t MIPS_BASE::GetGpr(int idx) {
  if (idx == 0) {
    return 0;
  }
  return gpr_[idx];
}

MIPS_TEMPLATE
void MIPS_BASE::SetGpr(int idx, uint64_t value) {
  if (idx == 0) {
    return;
  }
  gpr_[idx] = value;
}

MIPS_TEMPLATE
void MIPS_BASE::SetLlbit(bool llbit) {
  llbit_ = llbit;
}

MIPS_TEMPLATE
bool MIPS_BASE::GetHalt() {
  return halt_;
}

MIPS_TEMPLATE
void MIPS_BASE::SetHalt(bool halt) {
  halt_ = halt;
}

MIPS_TEMPLATE
uint64_t MIPS_BASE::GetTimestamp() {
  return cycle_spent_total_;
}

MIPS_TEMPLATE
void MIPS_BASE::CheckCompare() {
  if (!kHasCop0 || config_.has_isolate_cache_bit_) {
    return;
  }

  uint32_t cause = cop_[0]->Read32Internal(13);
  uint8_t ip = (cause >> 8) & 3;
  ip |= bus_->GetInterrupt() ? (1 << 2) : 0;
  ip |= compare_interrupt_ ? (1 << 7) : 0;
  cause &= ~(0xFF << 8);
  cause |= ip << 8;
  cop_[0]->Write32Internal(13, cause);

  if (cop_[0]->Read32Internal(128)) {
    compare_interrupt_ = true;
    CheckInterrupt();
  }
}

MIPS_TEMPLATE
void MIPS_BASE::ClearCompareInterrupt() {
  compare_interrupt_ = false;
}

MIPS_TEMPLATE
void MIPS_BASE::CheckInterrupt() {
  if (!config_.has_exception_ || !kHasCop0) {
    return;
  }

  uint32_t sr = cop_[0]->Read32Internal(12);
  bool ie = sr & 1;
  bool exl = sr & 2;
  bool erl = sr & 4;
  bool cpu_intr_enabled = ie && !exl && !erl;
  uint32_t cause = cop_[0]->Read32Internal(13);

  uint8_t ip = (cause >> 8) & 3;
  ip |= bus_->GetInterrupt() ? (1 << 2) : 0;
  ip |= compare_interrupt_ ? (1 << 7) : 0;

  uint8_t im = sr >> 8;
  bool intr_pending = (im & ip) != 0;

  if (kLazyInterruptPolling) {
    // If we enable lazy polling, this function will be called in the middle of instruction
    // Solution: we run instruction clean-up manually (aka. a dirty hack)
    if (cpu_intr_enabled && intr_pending) {
      pc_ = next_pc_;
      cpi_counter_ += config_.cpi_;
      int cpi_integer = cpi_counter_ >> 8;
      cpi_counter_ &= 0xFF;
      cycle_spent_ += cpi_integer;
      cycle_spent_total_ += cpi_integer;

      TriggerException(ExceptionCause::kInt);
    }
  } else {
    if (cpu_intr_enabled && intr_pending) {
      TriggerException(ExceptionCause::kInt);
    }
  }
}

MIPS_TEMPLATE
void MIPS_BASE::RunInst() {
  uint32_t opcode = Fetch(pc_);
  bool is_this_inst_bd = has_branch_delay_;
  if (has_branch_delay_) {
    next_pc_ = branch_delay_dst_;
    has_branch_delay_ = false;
  } else {
    next_pc_ = pc_ + 4;
  }

  MipsLog log;
  if (kLogCpu || kLogMipsState) {
    log.pc_ = pc_;
    log.inst_ = opcode;
    for (int i = 0; i < 32; i++) {
      log.gpr_[i] = ReadGpr64(i);
    }
  }

  if (kLogMipsState) {
    mips_log_[mips_log_index_] = log;
    mips_log_index_++;
    if (mips_log_index_ >= kMipsInstLogCount) {
      mips_log_index_ = 0;
    }
  }

  if (kLogCpu) {
    fmt::print("{}\n", log.ToString(kIs64Bit));
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnPreExecute(pc_, opcode);
    }
  }

  inst_ptr_t fp = GetInstFuncPtr(opcode);
  (this->*fp)(opcode);

  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  pc_ = next_pc_;
  cpi_counter_ += config_.cpi_;
  int cpi_integer = cpi_counter_ >> 8;
  cpi_counter_ &= 0xFF;
  cycle_spent_ += cpi_integer;
  cycle_spent_total_ += cpi_integer;

  if (kEnablePsxSpecific && is_this_inst_bd) {
    CheckHook();
  }
}

MIPS_TEMPLATE
auto MIPS_BASE::GetInstFuncPtr(uint32_t opcode) -> inst_ptr_t {
  switch (Decode(opcode)) {
    case MipsInstId::kAdd:
      return &MipsBase::InstAdd;
    case MipsInstId::kAddu:
      return &MipsBase::InstAddu;
    case MipsInstId::kAddi:
      return &MipsBase::InstAddi;
    case MipsInstId::kAddiu:
      return &MipsBase::InstAddiu;
    case MipsInstId::kAnd:
      return &MipsBase::InstAnd;
    case MipsInstId::kAndi:
      return &MipsBase::InstAndi;
    case MipsInstId::kDiv:
      return &MipsBase::InstDiv;
    case MipsInstId::kDivu:
      return &MipsBase::InstDivu;
    case MipsInstId::kMult:
      return &MipsBase::InstMult;
    case MipsInstId::kMultu:
      return &MipsBase::InstMultu;
    case MipsInstId::kNor:
      return &MipsBase::InstNor;
    case MipsInstId::kOr:
      return &MipsBase::InstOr;
    case MipsInstId::kOri:
      return &MipsBase::InstOri;
    case MipsInstId::kSll:
      return &MipsBase::InstSll;
    case MipsInstId::kSllv:
      return &MipsBase::InstSllv;
    case MipsInstId::kSra:
      return &MipsBase::InstSra;
    case MipsInstId::kSrav:
      return &MipsBase::InstSrav;
    case MipsInstId::kSrl:
      return &MipsBase::InstSrl;
    case MipsInstId::kSrlv:
      return &MipsBase::InstSrlv;
    case MipsInstId::kSub:
      return &MipsBase::InstSub;
    case MipsInstId::kSubu:
      return &MipsBase::InstSubu;
    case MipsInstId::kXor:
      return &MipsBase::InstXor;
    case MipsInstId::kXori:
      return &MipsBase::InstXori;
    case MipsInstId::kLui:
      return &MipsBase::InstLui;
    case MipsInstId::kSlt:
      return &MipsBase::InstSlt;
    case MipsInstId::kSltu:
      return &MipsBase::InstSltu;
    case MipsInstId::kSlti:
      return &MipsBase::InstSlti;
    case MipsInstId::kSltiu:
      return &MipsBase::InstSltiu;
    case MipsInstId::kBeq:
      return &MipsBase::InstBeq;
    case MipsInstId::kBne:
      return &MipsBase::InstBne;
    case MipsInstId::kBgtz:
      return &MipsBase::InstBgtz;
    case MipsInstId::kBlez:
      return &MipsBase::InstBlez;
    case MipsInstId::kBgez:
      return &MipsBase::InstBgez;
    case MipsInstId::kBgezal:
      return &MipsBase::InstBgezal;
    case MipsInstId::kBltz:
      return &MipsBase::InstBltz;
    case MipsInstId::kBltzal:
      return &MipsBase::InstBltzal;
    case MipsInstId::kJ:
      return &MipsBase::InstJ;
    case MipsInstId::kJal:
      return &MipsBase::InstJal;
    case MipsInstId::kJr:
      return &MipsBase::InstJr;
    case MipsInstId::kJalr:
      return &MipsBase::InstJalr;
    case MipsInstId::kSyscall:
      return &MipsBase::InstSyscall;
    case MipsInstId::kBreak:
      return &MipsBase::InstBreak;
    case MipsInstId::kLb:
      return &MipsBase::InstLb;
    case MipsInstId::kLbu:
      return &MipsBase::InstLbu;
    case MipsInstId::kLh:
      return &MipsBase::InstLh;
    case MipsInstId::kLhu:
      return &MipsBase::InstLhu;
    case MipsInstId::kLw:
      return &MipsBase::InstLw;
    case MipsInstId::kLwl:
      return &MipsBase::InstLwl;
    case MipsInstId::kLwr:
      return &MipsBase::InstLwr;
    case MipsInstId::kLwc:
      return &MipsBase::InstLwc;
    case MipsInstId::kSb:
      return &MipsBase::InstSb;
    case MipsInstId::kSh:
      return &MipsBase::InstSh;
    case MipsInstId::kSw:
      return &MipsBase::InstSw;
    case MipsInstId::kSwl:
      return &MipsBase::InstSwl;
    case MipsInstId::kSwr:
      return &MipsBase::InstSwr;
    case MipsInstId::kSwc:
      return &MipsBase::InstSwc;
    case MipsInstId::kMfhi:
      return &MipsBase::InstMfhi;
    case MipsInstId::kMflo:
      return &MipsBase::InstMflo;
    case MipsInstId::kMthi:
      return &MipsBase::InstMthi;
    case MipsInstId::kMtlo:
      return &MipsBase::InstMtlo;
    case MipsInstId::kCop:
      return &MipsBase::InstCop;
    case MipsInstId::kMfc:
      return &MipsBase::InstMfc;
    case MipsInstId::kCfc:
      return &MipsBase::InstCfc;
    case MipsInstId::kMtc:
      return &MipsBase::InstMtc;
    case MipsInstId::kCtc:
      return &MipsBase::InstCtc;
    case MipsInstId::kNop:
      return &MipsBase::InstNop;
    case MipsInstId::kBcf:
      return kIs64Bit ? &MipsBase::InstBcf : &MipsBase::InstUnknown;
    case MipsInstId::kBcfl:
      return kIs64Bit ? &MipsBase::InstBcfl : &MipsBase::InstUnknown;
    case MipsInstId::kBct:
      return kIs64Bit ? &MipsBase::InstBct : &MipsBase::InstUnknown;
    case MipsInstId::kBctl:
      return kIs64Bit ? &MipsBase::InstBctl : &MipsBase::InstUnknown;
    case MipsInstId::kBeql:
      return kIs64Bit ? &MipsBase::InstBeql : &MipsBase::InstUnknown;
    case MipsInstId::kBnel:
      return kIs64Bit ? &MipsBase::InstBnel : &MipsBase::InstUnknown;
    case MipsInstId::kBgezl:
      return kIs64Bit ? &MipsBase::InstBgezl : &MipsBase::InstUnknown;
    case MipsInstId::kBgezall:
      return kIs64Bit ? &MipsBase::InstBgezall : &MipsBase::InstUnknown;
    case MipsInstId::kBgtzl:
      return kIs64Bit ? &MipsBase::InstBgtzl : &MipsBase::InstUnknown;
    case MipsInstId::kBlezl:
      return kIs64Bit ? &MipsBase::InstBlezl : &MipsBase::InstUnknown;
    case MipsInstId::kBltzl:
      return kIs64Bit ? &MipsBase::InstBltzl : &MipsBase::InstUnknown;
    case MipsInstId::kBltzall:
      return kIs64Bit ? &MipsBase::InstBltzall : &MipsBase::InstUnknown;
    case MipsInstId::kCache:
      return kIs64Bit ? &MipsBase::InstCache : &MipsBase::InstUnknown;
    case MipsInstId::kDadd:
      return kIs64Bit ? &MipsBase::InstDadd : &MipsBase::InstUnknown;
    case MipsInstId::kDaddu:
      return kIs64Bit ? &MipsBase::InstDaddu : &MipsBase::InstUnknown;
    case MipsInstId::kDaddi:
      return kIs64Bit ? &MipsBase::InstDaddi : &MipsBase::InstUnknown;
    case MipsInstId::kDaddiu:
      return kIs64Bit ? &MipsBase::InstDaddiu : &MipsBase::InstUnknown;
    case MipsInstId::kDsub:
      return kIs64Bit ? &MipsBase::InstDsub : &MipsBase::InstUnknown;
    case MipsInstId::kDsubu:
      return kIs64Bit ? &MipsBase::InstDsubu : &MipsBase::InstUnknown;
    case MipsInstId::kDmult:
      return kIs64Bit ? &MipsBase::InstDmult : &MipsBase::InstUnknown;
    case MipsInstId::kDmultu:
      return kIs64Bit ? &MipsBase::InstDmultu : &MipsBase::InstUnknown;
    case MipsInstId::kDdiv:
      return kIs64Bit ? &MipsBase::InstDdiv : &MipsBase::InstUnknown;
    case MipsInstId::kDdivu:
      return kIs64Bit ? &MipsBase::InstDdivu : &MipsBase::InstUnknown;
    case MipsInstId::kDsll:
      return kIs64Bit ? &MipsBase::InstDsll : &MipsBase::InstUnknown;
    case MipsInstId::kDsll32:
      return kIs64Bit ? &MipsBase::InstDsll32 : &MipsBase::InstUnknown;
    case MipsInstId::kDsllv:
      return kIs64Bit ? &MipsBase::InstDsllv : &MipsBase::InstUnknown;
    case MipsInstId::kDsra:
      return kIs64Bit ? &MipsBase::InstDsra : &MipsBase::InstUnknown;
    case MipsInstId::kDsra32:
      return kIs64Bit ? &MipsBase::InstDsra32 : &MipsBase::InstUnknown;
    case MipsInstId::kDsrav:
      return kIs64Bit ? &MipsBase::InstDsrav : &MipsBase::InstUnknown;
    case MipsInstId::kDsrl:
      return kIs64Bit ? &MipsBase::InstDsrl : &MipsBase::InstUnknown;
    case MipsInstId::kDsrl32:
      return kIs64Bit ? &MipsBase::InstDsrl32 : &MipsBase::InstUnknown;
    case MipsInstId::kDsrlv:
      return kIs64Bit ? &MipsBase::InstDsrlv : &MipsBase::InstUnknown;
    case MipsInstId::kDmfc:
      return kIs64Bit ? &MipsBase::InstDmfc : &MipsBase::InstUnknown;
    case MipsInstId::kDmtc:
      return kIs64Bit ? &MipsBase::InstDmtc : &MipsBase::InstUnknown;
    case MipsInstId::kLd:
      return kIs64Bit ? &MipsBase::InstLd : &MipsBase::InstUnknown;
    case MipsInstId::kLdc:
      return kIs64Bit ? &MipsBase::InstLdc : &MipsBase::InstUnknown;
    case MipsInstId::kLdl:
      return kIs64Bit ? &MipsBase::InstLdl : &MipsBase::InstUnknown;
    case MipsInstId::kLdr:
      return kIs64Bit ? &MipsBase::InstLdr : &MipsBase::InstUnknown;
    case MipsInstId::kLwu:
      return kIs64Bit ? &MipsBase::InstLwu : &MipsBase::InstUnknown;
    case MipsInstId::kSd:
      return kIs64Bit ? &MipsBase::InstSd : &MipsBase::InstUnknown;
    case MipsInstId::kSdc:
      return kIs64Bit ? &MipsBase::InstSdc : &MipsBase::InstUnknown;
    case MipsInstId::kSdl:
      return kIs64Bit ? &MipsBase::InstSdl : &MipsBase::InstUnknown;
    case MipsInstId::kSdr:
      return kIs64Bit ? &MipsBase::InstSdr : &MipsBase::InstUnknown;
    case MipsInstId::kSync:
      return kIs64Bit ? &MipsBase::InstSync : &MipsBase::InstUnknown;
    case MipsInstId::kUnknown:
      return &MipsBase::InstUnknown;
  }
  return &MipsBase::InstUnknown;
}

MIPS_TEMPLATE
uint32_t MIPS_BASE::ReadGpr32(int idx) {
  return gpr_[idx];
}

MIPS_TEMPLATE
void MIPS_BASE::WriteGpr32(int idx, uint32_t value) {
  if (idx == 0) {
    return;
  }
  if (kHasLoadDelay && delayed_load_op_.is_active_ && delayed_load_op_.cop_id_ < 0 && delayed_load_op_.dst_ == idx) {
    delayed_load_op_.is_active_ = false;
  }
  gpr_[idx] = value;
}

MIPS_TEMPLATE
uint64_t MIPS_BASE::ReadGpr64(int idx) {
  return kIs64Bit ? gpr_[idx] : sext_i32_to_i64(gpr_[idx] & 0xFFFFFFFF);
}

MIPS_TEMPLATE
void MIPS_BASE::WriteGpr32Sext(int idx, int32_t value) {
  if (idx == 0) {
    return;
  }
  if (kHasLoadDelay && delayed_load_op_.is_active_ && delayed_load_op_.cop_id_ < 0 && delayed_load_op_.dst_ == idx) {
    delayed_load_op_.is_active_ = false;
  }
  gpr_[idx] = (int64_t)value;
}

MIPS_TEMPLATE
void MIPS_BASE::WriteGpr64(int idx, uint64_t value) {
  if (idx == 0) {
    return;
  }
  if (kHasLoadDelay && delayed_load_op_.is_active_ && delayed_load_op_.cop_id_ < 0 && delayed_load_op_.dst_ == idx) {
    delayed_load_op_.is_active_ = false;
  }
  gpr_[idx] = value;
}

MIPS_TEMPLATE
void MIPS_BASE::JumpRel(int32_t offset) {
  has_branch_delay_ = true;
  branch_delay_dst_ = pc_ + 4 + offset;

  if (kPanicOnNullJumps && (branch_delay_dst_ == 0)) {
    DumpProcessorLog();
    PANIC("Jump to null pointer");
  }
}

MIPS_TEMPLATE
void MIPS_BASE::LinkForJump(int dst_reg) {
  // Sext the address to i64. Zelda OoT expects this I think
  WriteGpr64(dst_reg, sext_i32_to_i64(pc_ + 8));
}

MIPS_TEMPLATE
void MIPS_BASE::Jump32(uint32_t dst) {
  has_branch_delay_ = true;
  branch_delay_dst_ = dst;

  if (kPanicOnNullJumps && (dst == 0)) {
    DumpProcessorLog();
    PANIC("Jump to null pointer");
  }
}

MIPS_TEMPLATE
void MIPS_BASE::Jump64(uint64_t dst) {
  has_branch_delay_ = true;
  branch_delay_dst_ = dst;

  if (kPanicOnNullJumps && (dst == 0)) {
    DumpProcessorLog();
    PANIC("Jump to null pointer");
  }
}

MIPS_TEMPLATE
void MIPS_BASE::QueueDelayedLoad(int dst, uint64_t value) {
  if constexpr (!kHasLoadDelay) {
    WriteGpr64(dst, value);
    return;
  }
  if (delayed_load_op_.is_active_) {
    if constexpr (kHasLoadDelay) {
      ExecuteDelayedLoad();
    }
    if (delayed_load_op_.is_active_) {
      PANIC("Consecutive loads not handled");
    }
  }
  /* Load delay is only in MIPS I. So only lower 32-bit of the passed value is relevant */
  delayed_load_op_.is_active_ = true;
  delayed_load_op_.delay_counter_ = 0;
  delayed_load_op_.dst_ = dst;
  delayed_load_op_.cop_id_ = -1;
  delayed_load_op_.value_ = value;
}

MIPS_TEMPLATE
void MIPS_BASE::QueueDelayedCopLoad(int cop_id, int dst, uint64_t value) {
  if constexpr (!kHasLoadDelay) {
    cop_[cop_id]->Write32(dst, value);
    return;
  }
  if (delayed_load_op_.is_active_) {
    if constexpr (kHasLoadDelay) {
      ExecuteDelayedLoad();
    }
    if (delayed_load_op_.is_active_) {
      PANIC("Consecutive loads not handled");
    }
  }
  delayed_load_op_.is_active_ = true;
  delayed_load_op_.delay_counter_ = 0;
  delayed_load_op_.dst_ = dst;
  delayed_load_op_.cop_id_ = cop_id;
  delayed_load_op_.value_ = value;
}

MIPS_TEMPLATE
void MIPS_BASE::ExecuteDelayedLoad() {
  if (!delayed_load_op_.is_active_) {
    return;
  }

  delayed_load_op_.delay_counter_++;
  if (delayed_load_op_.delay_counter_ != 2) {
    return;
  }

  if (delayed_load_op_.cop_id_ < 0) {
    WriteGpr32(delayed_load_op_.dst_, delayed_load_op_.value_);
  } else {
    cop_[delayed_load_op_.cop_id_]->Write32(delayed_load_op_.dst_, delayed_load_op_.value_);
    if (kLazyInterruptPolling) {
      if (delayed_load_op_.cop_id_ == 0) {
        CheckInterrupt();
      }
    }
  }

  delayed_load_op_.is_active_ = false;
  delayed_load_op_.delay_counter_ = 0;
}

MIPS_TEMPLATE
void MIPS_BASE::TriggerException(ExceptionCause cause) {
  if (!config_.has_exception_) {
    if (cause != ExceptionCause::kOvf && cause != ExceptionCause::kBkpt) {
      fmt::print("Exception not handled : {}\n", static_cast<int>(cause));
    }
    return;
  }
  if (false) {
    uint32_t sr = cop_[0]->Read32Internal(12);
    uint32_t cause_reg = cop_[0]->Read32Internal(13);
    fmt::print("Cycle {} | Exception: {} | PC: {:08X} | SR: {:08X} | CAUSE: {:08X}\n", cycle_spent_total_, static_cast<int>(cause), pc_, sr, cause_reg);
  }

  if (kEnablePsxSpecific) {
    uint32_t opcode_next = Fetch(pc_);
    if (((opcode_next >> 24) & 0xFE) == 0x4A) {
      // Interrupt on GTE instruction
      pc_ -= 4;
    }
  }

  uint64_t epc = pc_;
  bool bd = false;
  if (has_branch_delay_) {
    has_branch_delay_ = false;
    bd = true;
    epc -= 4;
    uint32_t opcode = Fetch(epc);
    if (!DoesInstHaveDelaySlot(opcode)) {
      DumpProcessorLog();
      PANIC("NON BRANCH ON BD: {:08X} ({})", opcode, MipsInst(opcode).Disassemble(epc));
    }
  }

  uint32_t cause_reg_old = cop_[0]->Read32Internal(13);
  uint32_t cause_reg_new = 0;
  // Update excode
  cause_reg_new |= static_cast<uint32_t>(cause) << 2;
  // TODO: BT bit
  cause_reg_new |= bus_->GetInterrupt() ? (1 << 10) : 0;
  cause_reg_new |= compare_interrupt_ ? (1 << 15) : 0;
  cause_reg_new |= (cause_reg_old & 0x0300);
  if (cause == ExceptionCause::kCop) {
    cause_reg_new |= cop_cause_ << 28;
  }
  cause_reg_new |= bd ? (1 << 31) : 0;

  cop_[0]->Write32Internal(13, cause_reg_new);

  uint32_t sr_reg = cop_[0]->Read32Internal(12);
  bool exl = sr_reg & 2;
  if (kIs64Bit) {
    // Handle EXL bit
    if (!exl) {
      cop_[0]->Write64Internal(14, epc);
      sr_reg |= 2;
    }
  } else {
    uint32_t sr_mode = sr_reg & 0x3F;
    sr_reg &= ~0x3F;
    sr_reg |= (sr_mode << 2) & 0x3F;
  }

  cop_[0]->Write32Internal(12, sr_reg);

  // NOTE: Is it really working?
  if (kEnablePsxSpecific) {
    uint32_t vec_address_general = (sr_reg & (1 << 22)) ? 0xBFC00180 : 0x80000080;
    pc_ = vec_address_general;
    next_pc_ = pc_;
  } else {
    uint32_t vec_address_general = 0x80000180;
    if (cause == ExceptionCause::kTlbMod || cause == ExceptionCause::kTlbMissLoad || cause == ExceptionCause::kTlbMissStore) {
      vec_address_general = exl ? 0x80000180 : 0x80000000;
      fmt::print("TLB exception {} | BadVAddr = {:08X}\n", static_cast<int>(cause), cop_[0]->Read32Internal(8));
      if (true) {
        // TLB exception is likely a sign of something going wrong in N64. panic
        DumpProcessorLog();
        PANIC("TLB exception");
      }
    }
    pc_ = vec_address_general;
    next_pc_ = pc_;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::CheckHook() {
  switch (pc_ & 0xFFFFFFFF) {
    case 0xA0: {
      int func_id = ReadGpr32(9);
      if (kLogKernel) {
        fmt::print("Kernel call A: {:02X}\n", func_id);
      }
      if (func_id == 0x3C) {
        char c = ReadGpr32(4);
        bus_->Store8(0x1F802080, c);
      }
      break;
    }
    case 0xB0: {
      int func_id = ReadGpr32(9);
      if (kLogKernel && func_id != 0x0B) {
        fmt::print("Kernel call B: {:02X}\n", func_id);
      }
      if (func_id == 0x3D) {
        char c = ReadGpr32(4);
        bus_->Store8(0x1F802080, c);
      }
      break;
    }
    case 0xC0: {
      int func_id = ReadGpr32(9);
      if (kLogKernel) {
        fmt::print("Kernel call C: {:02X}\n", func_id);
      }
      break;
    }
  }
}

MIPS_TEMPLATE
bool MIPS_BASE::IsCopEnabled(int cop_id) {
  if constexpr (kHasCop0) {
    uint32_t sr = cop_[0]->Read32Internal(12);
    bool cop_enabled = (cop_id == 0) || (sr & (1 << (cop_id + 28)));
    // bool cop_enabled = (cop_id != 1) || (sr & (1 << (cop_id + 28)));
    if (!cop_enabled) {
      fmt::print("COP{} unusable @ {:08X}\n", cop_id, pc_);
    }
    return cop_enabled;
  }
  return true;
}

MIPS_TEMPLATE
void MIPS_BASE::DumpProcessorLog() {
  if (kLogMipsState) {
    std::string processor_name = kHasCop0 ? "CPU" : "RSP";
    fmt::print("===== Processor log dump ({}) =====\n", processor_name);
    for (int i = 0; i < kMipsInstLogCount; i++) {
      int index = (mips_log_index_ + i) % kMipsInstLogCount;
      fmt::print("{}\n", mips_log_[index].ToString(kIs64Bit));
    }
  }
}

MIPS_TEMPLATE
uint32_t MIPS_BASE::Fetch(uint64_t address) {
  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissLoad);
    return 0;
  }

  uint32_t fetched = bus_->Fetch(tlb_result.address_);
  return fetched;
}

MIPS_TEMPLATE
LoadResult8 MIPS_BASE::Load8(uint64_t address) {
  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissLoad);
    return LoadResult8{.has_value = false, .value = 0};
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnLoad8(address);
    }
  }

  LoadResult8 result = bus_->Load8(tlb_result.address_);
  if (!result.has_value) {
    fmt::print("PC: {:08X} | Load from unmapped address: {:08X}\n", pc_, address & 0xFFFFFFFF);
    DumpProcessorLog();
    PANIC("Load from unmapped address");
  }
  return result;
}

MIPS_TEMPLATE
LoadResult16 MIPS_BASE::Load16(uint64_t address) {
  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissLoad);
    return LoadResult16{.has_value = false, .value = 0};
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnLoad16(address);
    }
  }

  LoadResult16 result = bus_->Load16(tlb_result.address_);
  if (!result.has_value) {
    fmt::print("PC: {:08X} | Load from unmapped address: {:08X}\n", pc_, address & 0xFFFFFFFF);
    DumpProcessorLog();
    PANIC("Load from unmapped address");
  }
  return result;
}

MIPS_TEMPLATE
LoadResult32 MIPS_BASE::Load32(uint64_t address) {
  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissLoad);
    return LoadResult32{.has_value = false, .value = 0};
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnLoad32(address);
    }
  }

  LoadResult32 result = bus_->Load32(tlb_result.address_);
  if (!result.has_value) {
    fmt::print("PC: {:08X} | Load from unmapped address: {:08X}\n", pc_, address & 0xFFFFFFFF);
    DumpProcessorLog();
    PANIC("Load from unmapped address");
  }
  return result;
}

MIPS_TEMPLATE
LoadResult64 MIPS_BASE::Load64(uint64_t address) {
  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissLoad);
    return LoadResult64{.has_value = false, .value = 0};
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnLoad64(address);
    }
  }

  LoadResult64 result = bus_->Load64(tlb_result.address_);
  if (!result.has_value) {
    fmt::print("PC: {:08X} | Load from unmapped address: {:08X}\n", pc_, address & 0xFFFFFFFF);
    DumpProcessorLog();
    PANIC("Load from unmapped address");
  }
  return result;
}

MIPS_TEMPLATE
void MIPS_BASE::Store8(uint64_t address, uint8_t value) {
  if (config_.has_isolate_cache_bit_ && (cop_[0]->Read32Internal(12) & (1 << 16))) {
    return;
  }

  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissStore);
    return;
  }
  if (tlb_result.read_only_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMod);
    return;
  }

  if (kLogNullWrites || kPanicOnNullWrites) {
    if (address == 0 || address == 0x80000000 || address == 0xA0000000) {
      bool is_pc_known = false;
      for (auto known_pc : kPsxKnownNullWritePc) {
        is_pc_known |= pc_ == known_pc;
      }
      if (!is_pc_known) {
        fmt::print("PC:{:08X} | [NULL] <- {:02X}\n", pc_, value);
        if (kPanicOnNullWrites) {
          DumpProcessorLog();
          PANIC("Null write");
        }
      }
    }
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnStore8(address, value);
    }
  }

  bus_->Store8(tlb_result.address_, value);
  if (config_.use_cached_interpreter_) {
    // InvalidateBlock(address);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::Store16(uint64_t address, uint16_t value) {
  if (config_.has_isolate_cache_bit_ && (cop_[0]->Read32Internal(12) & (1 << 16))) {
    return;
  }

  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissStore);
    return;
  }
  if (tlb_result.read_only_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMod);
    return;
  }

  if (kLogNullWrites || kPanicOnNullWrites) {
    if (address == 0 || address == 0x80000000 || address == 0xA0000000) {
      bool is_pc_known = false;
      for (auto known_pc : kPsxKnownNullWritePc) {
        is_pc_known |= pc_ == known_pc;
      }
      if (!is_pc_known) {
        fmt::print("PC:{:08X} | [NULL] <- {:04X}\n", pc_, value);
        if (kPanicOnNullWrites) {
          DumpProcessorLog();
          PANIC("Null write");
        }
      }
    }
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnStore16(address, value);
    }
  }

  bus_->Store16(tlb_result.address_, value);
  if (config_.use_cached_interpreter_) {
    // InvalidateBlock(address);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::Store32(uint64_t address, uint32_t value) {
  if (config_.has_isolate_cache_bit_ && (cop_[0]->Read32Internal(12) & (1 << 16))) {
    return;
  }

  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissStore);
    return;
  }
  if (tlb_result.read_only_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMod);
    return;
  }

  if (kLogNullWrites || kPanicOnNullWrites) {
    if (address == 0 || address == 0x80000000 || address == 0xA0000000) {
      bool is_pc_known = false;
      for (auto known_pc : kPsxKnownNullWritePc) {
        is_pc_known |= pc_ == known_pc;
      }
      if (!is_pc_known) {
        fmt::print("PC:{:08X} | [NULL] <- {:08X}\n", pc_, value);
        if (kPanicOnNullWrites) {
          DumpProcessorLog();
          PANIC("Null write");
        }
      }
    }
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnStore32(address, value);
    }
  }

  bus_->Store32(tlb_result.address_, value);
  if (config_.use_cached_interpreter_) {
    // InvalidateBlock(tlb_result.address_);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::Store64(uint64_t address, uint64_t value) {
  if (config_.has_isolate_cache_bit_ && (cop_[0]->Read32Internal(12) & (1 << 16))) {
    return;
  }

  MipsTlbTranslationResult tlb_result = tlb_.TranslateAddress(address);
  if (!tlb_result.found_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMissStore);
    return;
  }
  if (tlb_result.read_only_) {
    cop_[0]->Write64Internal(8, address);
    tlb_.InformTlbException(address);
    TriggerException(ExceptionCause::kTlbMod);
    return;
  }

  if (config_.use_hook_) {
    for (auto& hook : hook_) {
      hook->OnStore64(address, value);
    }
  }

  bus_->Store64(tlb_result.address_, value);
  if (config_.use_cached_interpreter_) {
    // InvalidateBlock(address);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::OnNewBlock(uint64_t address) {
  address &= 0xFFFFFFFF;

  auto& cache = cache_;
  MipsCacheBlock<MipsBase> block;
  block.start_ = address;

  int block_length = 0;
  uint64_t inst_address = address;
  bool has_delay_slot = false;

  for (int i = 0; i < kCacheBlockMaxLength; i++) {
    block.entries_[i].func_ = nullptr;
  }

  for (int i = 0; i < kCacheBlockMaxLength - 1; i++) {
    uint32_t opcode = Fetch(inst_address);
    MipsCacheEntry<MipsBase> entry;
    entry.address_ = inst_address;
    entry.opcode_ = opcode;
    entry.func_ = GetInstFuncPtr(opcode);
    block.entries_[i] = entry;
    inst_address += 4;
    block_length++;
    if (IsInstBranch(opcode)) {
      has_delay_slot = DoesInstHaveDelaySlot(opcode);
      break;
    }
  }

  if (has_delay_slot) {
    uint64_t delay_slot_inst_ = Fetch(inst_address);
    MipsCacheEntry<MipsBase> delay_slot_entry;
    delay_slot_entry.address_ = inst_address;
    delay_slot_entry.opcode_ = delay_slot_inst_;
    delay_slot_entry.func_ = GetInstFuncPtr(delay_slot_inst_);
    block.entries_[block_length] = delay_slot_entry;
    inst_address += 4;
    block_length++;
  }

  block.end_ = address + block_length * 4;
  block.length_ = block_length;
  block.cycle_ = block_length * config_.cpi_;

  cache.InsertBlock(block);
  if (false) {
    fmt::print("New block #{}: {:08X}-{:08X} ({} inst)\n", cache.GetSize(), address, inst_address, block_length);
    for (int i = 0; i < block_length; i++) {
      const MipsCacheEntry<MipsBase>& entry = block.entries_[i];
      fmt::print("{:08X} | {}\n", entry.address_, MipsInst(entry.opcode_).Disassemble(entry.address_));
    }
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InvalidateBlock(uint64_t address) {
  if (!config_.use_cached_interpreter_) {
    return;
  }

  address &= 0xFFFFFFFF;
  cache_.InvalidateBlock(address);
}

MIPS_TEMPLATE
void MIPS_BASE::InstAdd(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value + rt_value;
  if (config_.has_exception_ && get_overflow_add_i32(rd_value, rs_value, rt_value)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr32Sext(inst.rd(), rd_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstAddu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value + rt_value;
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstAddi(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t imm = sext_itype_imm_i32(inst);
  uint32_t rt_value = rs_value + imm;
  if (config_.has_exception_ && get_overflow_add_i32(rt_value, rs_value, imm)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr32Sext(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstAddiu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t imm = sext_itype_imm_i32(inst);
  uint32_t rt_value = rs_value + imm;
  WriteGpr32Sext(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstAnd(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = rs_value & rt_value;
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstAndi(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint16_t imm = inst.imm();
  uint64_t rt_value = rs_value & imm;
  WriteGpr64(inst.rt(), rt_value);

  // fmt::print("andi | {:08X} AND {:08X} = {:08X}\n", rs_value, imm, rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDiv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int32_t rs_value = ReadGpr32(inst.rs());
  int32_t rt_value = ReadGpr32(inst.rt());
  int32_t hi = 0;
  int32_t lo = 0;
  bool rs_msb = rs_value & (1 << 31);
  if (rt_value == 0) {
    hi = rs_value;
    lo = rs_msb ? 1 : 0xFFFFFFFF;
  } else if ((uint32_t)rs_value == 0x80000000 && (uint32_t)rt_value == 0xFFFFFFFF) {
    hi = 0;
    lo = 0x80000000;
  } else {
    int32_t rs_signed = rs_value;
    int32_t rt_signed = rt_value;
    lo = rs_signed / rt_signed;
    hi = rs_signed % rt_signed;
  }
  lo_ = sext_i32_to_i64(lo);
  hi_ = sext_i32_to_i64(hi);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDivu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t hi = 0;
  uint32_t lo = 0;
  if (rt_value == 0) {
    hi = rs_value;
    lo = 0xFFFFFFFF;
  } else {
    lo = rs_value / rt_value;
    hi = rs_value % rt_value;
  }
  lo_ = sext_i32_to_i64(lo);
  hi_ = sext_i32_to_i64(hi);
}

MIPS_TEMPLATE
void MIPS_BASE::InstMult(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  int64_t rs_signed = sext_i32_to_i64(rs_value);
  int64_t rt_signed = sext_i32_to_i64(rt_value);
  int64_t result = rs_signed * rt_signed;
  hi_ = sext_i32_to_i64(result >> 32);
  lo_ = sext_i32_to_i64(result & 0xFFFFFFFF);
}

MIPS_TEMPLATE
void MIPS_BASE::InstMultu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint64_t rs_long = rs_value;
  uint64_t rt_long = rt_value;
  uint64_t result = rs_long * rt_long;
  hi_ = sext_i32_to_i64(result >> 32);
  lo_ = sext_i32_to_i64(result & 0xFFFFFFFF);
}

MIPS_TEMPLATE
void MIPS_BASE::InstNor(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = 0xFFFFFFFFFFFFFFFFULL ^ (rs_value | rt_value);
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstOr(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = rs_value | rt_value;
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstOri(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint16_t imm = inst.imm();
  uint64_t rt_value = rs_value | imm;
  WriteGpr64(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSll(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  if (inst.shamt() == 0) {
    WriteGpr32Sext(inst.rd(), rt_value);
    return;
  }
  uint32_t rd_value = rt_value << inst.shamt();
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSllv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rd_value = rt_value << (rs_value & 31);
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSra(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  // NOTE: This is actually incorrect for VR4300. It shifts 64bit value, making upper 32bit of rt relavant
  int32_t rt_value = ReadGpr32(inst.rt());
  int32_t rd_value = rt_value >> inst.shamt();
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSrav(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  // NOTE: Same as sra.
  int32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rs_value = ReadGpr32(inst.rs());
  int32_t rd_value = rt_value >> (rs_value & 31);
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSrl(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rt_value >> inst.shamt();
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSrlv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rd_value = rt_value >> (rs_value & 31);
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSub(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value - rt_value;
  if (config_.has_exception_ && get_overflow_sub_i32(rd_value, rs_value, rt_value)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr32Sext(inst.rd(), rd_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstSubu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint32_t rs_value = ReadGpr32(inst.rs());
  uint32_t rt_value = ReadGpr32(inst.rt());
  uint32_t rd_value = rs_value - rt_value;
  WriteGpr32Sext(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstXor(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = rs_value ^ rt_value;
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstXori(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint16_t imm = inst.imm();
  uint64_t rt_value = rs_value ^ imm;
  WriteGpr64(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstLui(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint32_t imm = inst.imm();
  uint32_t rt_value = imm << 16;
  WriteGpr32Sext(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSlt(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int64_t rs_value = ReadGpr64(inst.rs());
  int64_t rt_value = ReadGpr64(inst.rt());
  uint32_t rd_value = rs_value < rt_value ? 1 : 0;
  WriteGpr32(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSltu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint32_t rd_value = rs_value < rt_value ? 1 : 0;
  WriteGpr32(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSlti(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint32_t rt_value = rs_value < imm ? 1 : 0;
  WriteGpr32(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSltiu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t imm = sext_itype_imm_i64(inst);
  uint32_t rt_value = rs_value < imm ? 1 : 0;
  WriteGpr32(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstBeq(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  if (kEnableIdleLoopDetection && (opcode == 0x1000FFFF)) {
    uint32_t delay_op = Fetch(pc_ + 4);
    if (delay_op == 0x00000000) {
      cycle_spent_ += 100;
      cycle_spent_total_ += 100;
    }
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  int64_t rt_value = ReadGpr64(inst.rt());
  if (rs_value == rt_value) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBne(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  int64_t rt_value = ReadGpr64(inst.rt());
  if (rs_value != rt_value) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBgtz(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value > 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBlez(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value <= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBgez(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value >= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBgezal(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  LinkForJump(31);
  if (rs_value >= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBltz(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value < 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBltzal(uint32_t opcode) {
  // HACK: No load delay on branch
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  LinkForJump(31);
  if (rs_value < 0) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstJ(uint32_t opcode) {
  JTypeInst inst = MipsInst(opcode).GetJType();
  uint32_t dst = ((pc_ + 4) & 0xF0000000) | (inst.address() << 2);
  Jump32(dst);

  if (kEnableIdleLoopDetection && (dst == pc_)) {
    uint32_t delay_op = Fetch(pc_ + 4);
    if (delay_op == 0x00000000) {
      cycle_spent_ += 100;
      cycle_spent_total_ += 100;
    }
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstJal(uint32_t opcode) {
  JTypeInst inst = MipsInst(opcode).GetJType();
  uint32_t dst = ((pc_ + 4) & 0xF0000000) | (inst.address() << 2);
  LinkForJump(31);
  Jump32(dst);
}

MIPS_TEMPLATE
void MIPS_BASE::InstJr(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  // if (!config_.allow_misaligned_access_ && (rs_value & 3)) {
  if (rs_value & 3) {
    fmt::print("Unaligned jump\n");
    if (kPanicOnUnalignedJump) {
      DumpProcessorLog();
      PANIC("Unaligned jump");
    }
  }
  rs_value &= ~3;
  Jump64(rs_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstJalr(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  // if (!config_.allow_misaligned_access_ && (rs_value & 3)) {
  if (rs_value & 3) {
    fmt::print("Unaligned jump\n");
    if (kPanicOnUnalignedJump) {
      DumpProcessorLog();
      PANIC("Unaligned jump");
    }
  }
  rs_value &= ~3;
  LinkForJump(inst.rd());
  Jump64(rs_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSyscall(uint32_t opcode) {
  TriggerException(ExceptionCause::kSyscall);
}

MIPS_TEMPLATE
void MIPS_BASE::InstBreak(uint32_t opcode) {
  TriggerException(ExceptionCause::kBkpt);

  if constexpr (!kHasCop0) {
    // HACK: signal to cop0
    cop_[0]->Command(0);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLb(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  LoadResult8 load_result = Load8(address);
  if (load_result.has_value) {
    int64_t rt_value = sext_i8_to_i64(load_result.value);
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLbu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  LoadResult8 load_result = Load8(address);
  if (load_result.has_value) {
    uint8_t rt_value = load_result.value;
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLh(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 1)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult16 load_result = Load16(address);
  if (load_result.has_value) {
    int64_t rt_value = sext_i16_to_i64(load_result.value);
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLhu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 1)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult16 load_result = Load16(address);
  if (load_result.has_value) {
    uint16_t rt_value = load_result.value;
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLw(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 3)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult32 load_result = Load32(address);
  if (load_result.has_value) {
    int64_t rt_value = sext_i32_to_i64(load_result.value);
    QueueDelayedLoad(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLwl(uint32_t opcode) {
  // HACK: Execute pending load
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  int address_unalignment = address & 3;

  if (config_.use_big_endian_) {
    address_unalignment = 3 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address + i;
    LoadResult8 load_result = Load8(addr);
    if (load_result.has_value) {
      int shamt = (3 - i) * 8;
      rt_value &= ~(0xFF << shamt);
      rt_value |= load_result.value << shamt;
    }
  }

  WriteGpr32Sext(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstLwr(uint32_t opcode) {
  // HACK: Execute pending load
  if constexpr (kHasLoadDelay) {
    ExecuteDelayedLoad();
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  int address_unalignment = address & 3;

  address_unalignment = 3 - address_unalignment;
  if (config_.use_big_endian_) {
    address_unalignment = 3 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address - i;
    LoadResult8 load_result = Load8(addr);
    if (load_result.has_value) {
      int shamt = i * 8;
      rt_value &= ~(0xFF << shamt);
      rt_value |= load_result.value << shamt;
    }
  }

  WriteGpr32Sext(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstLwc(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  if (config_.cop_decoding_override_ & (1 << cop_id)) {
    InstCop(opcode);
    return;
  }

  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  LoadResult32 load_result = Load32(address);
  if (load_result.has_value) {
    uint32_t copt_value = load_result.value;
    QueueDelayedCopLoad(cop_id, inst.rt(), copt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstSb(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  Store8(address, rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSh(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 1)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrs);
    return;
  }
  uint32_t rt_value = ReadGpr32(inst.rt());
  Store16(address, rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSw(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 3)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrs);
    return;
  }
  uint32_t rt_value = ReadGpr32(inst.rt());
  Store32(address, rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSwl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  int address_unalignment = address & 3;

  if (config_.use_big_endian_) {
    address_unalignment = 3 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address + i;
    int shamt = (3 - i) * 8;
    Store8(addr, rt_value >> shamt);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstSwr(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t rt_value = ReadGpr32(inst.rt());
  int address_unalignment = address & 3;

  address_unalignment = 3 - address_unalignment;
  if (config_.use_big_endian_) {
    address_unalignment = 3 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address - i;
    int shamt = i * 8;
    Store8(addr, rt_value >> shamt);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstSwc(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  if (config_.cop_decoding_override_ & (1 << cop_id)) {
    InstCop(opcode);
    return;
  }

  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }

  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint32_t copt_value = cop_[cop_id]->Read32(inst.rt());
  Store32(address, copt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstMfhi(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  WriteGpr64(inst.rd(), hi_);
}

MIPS_TEMPLATE
void MIPS_BASE::InstMflo(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  WriteGpr64(inst.rd(), lo_);
}

MIPS_TEMPLATE
void MIPS_BASE::InstMthi(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  hi_ = rs_value;
}

MIPS_TEMPLATE
void MIPS_BASE::InstMtlo(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  lo_ = rs_value;
}

MIPS_TEMPLATE
void MIPS_BASE::InstCop(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }

  uint32_t cop_command = opcode;
  cop_[cop_id]->Command(cop_command);

  if (kLazyInterruptPolling) {
    uint8_t command_id = cop_command & 0x3F;
    if (command_id == 0x10 || command_id == 0x18) {
      // ERET or RFE
      CheckInterrupt();
    }
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstMfc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;

  if (config_.cop_decoding_override_ & (1 << cop_id)) {
    InstCop(opcode);
    return;
  }

  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }

  uint32_t cop_value = cop_[cop_id]->Read32(inst.rd());
  WriteGpr32Sext(inst.rt(), cop_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstCfc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;

  if (config_.cop_decoding_override_ & (1 << cop_id)) {
    InstCop(opcode);
    return;
  }

  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }

  uint32_t cop_value = cop_[cop_id]->Read32(inst.rd() + 32);
  WriteGpr32Sext(inst.rt(), cop_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstMtc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;

  if (config_.cop_decoding_override_ & (1 << cop_id)) {
    InstCop(opcode);
    return;
  }

  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }

  uint32_t rt_value = ReadGpr32(inst.rt());
  cop_[cop_id]->Write32(inst.rd(), rt_value);
  if (kLazyInterruptPolling) {
    if (cop_id == 0) {
      CheckInterrupt();
    }
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstCtc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;

  if (config_.cop_decoding_override_ & (1 << cop_id)) {
    InstCop(opcode);
    return;
  }

  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }

  uint32_t rt_value = ReadGpr32(inst.rt());
  cop_[cop_id]->Write32(inst.rd() + 32, rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstNop(uint32_t opcode) {
}

MIPS_TEMPLATE
void MIPS_BASE::InstBcf(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  if (!cop_[cop_id]->GetFlag()) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBcfl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  if (!cop_[cop_id]->GetFlag()) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBct(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  if (cop_[cop_id]->GetFlag()) {
    JumpRel(sext_itype_imm_branch(inst));
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBctl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  if (cop_[cop_id]->GetFlag()) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBeql(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  int64_t rt_value = ReadGpr64(inst.rt());
  if (rs_value == rt_value) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBnel(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  int64_t rt_value = ReadGpr64(inst.rt());
  if (rs_value != rt_value) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBgezl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value >= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBgezall(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  LinkForJump(31);
  if (rs_value >= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBgtzl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value > 0) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBlezl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value <= 0) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBltzl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  if (rs_value < 0) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstBltzall(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  int64_t rs_value = ReadGpr64(inst.rs());
  LinkForJump(31);
  if (rs_value < 0) {
    JumpRel(sext_itype_imm_branch(inst));
  } else {
    next_pc_ = pc_ + 8;
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstCache(uint32_t opcode) {
  int16_t offset = opcode;
  uint8_t op = (opcode >> 16) & 0x1F;
  uint8_t base = (opcode >> 21) & 0x1F;
  uint64_t base_value = ReadGpr64(base);
  uint64_t address = base_value + offset;
  bool is_dst_dcache = op & 1;
  if (is_dst_dcache) {
    return;
  }

  // I-cache operation: invalidate the 32-byte cache line containing this address
  if (config_.use_cached_interpreter_) {
    uint64_t line_start = address & ~0x1F;
    uint64_t line_end = line_start + 32;
    cache_.InvalidateBlockRange(line_start, line_end);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstDadd(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint128_t rd_value = rs_value + rt_value;
  if (get_overflow_add_i64(rd_value, rs_value, rt_value)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr64(inst.rd(), rd_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstDaddu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint128_t rd_value = rs_value + rt_value;
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDaddi(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t rt_value = rs_value + imm;
  if (get_overflow_add_i64(rt_value, rs_value, imm)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr64(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstDaddiu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t rt_value = rs_value + imm;
  WriteGpr64(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsub(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = rs_value - rt_value;
  if (get_overflow_sub_i64(rd_value, rs_value, rt_value)) {
    TriggerException(ExceptionCause::kOvf);
  } else {
    WriteGpr64(inst.rd(), rd_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsubu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = rs_value - rt_value;
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDmult(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  int128_t rs_signed = (int64_t)rs_value;
  int128_t rt_signed = (int64_t)rt_value;
  int128_t result = rs_signed * rt_signed;
  hi_ = result >> 64;
  lo_ = result & 0xFFFFFFFFFFFFFFFFULL;
}

MIPS_TEMPLATE
void MIPS_BASE::InstDmultu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint128_t rs_value = ReadGpr64(inst.rs());
  uint128_t rt_value = ReadGpr64(inst.rt());
  uint128_t result = rs_value * rt_value;
  hi_ = result >> 64;
  lo_ = result & 0xFFFFFFFFFFFFFFFFULL;
}

MIPS_TEMPLATE
void MIPS_BASE::InstDdiv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int64_t rs_value = ReadGpr64(inst.rs());
  int64_t rt_value = ReadGpr64(inst.rt());
  int64_t hi = 0;
  int64_t lo = 0;
  bool rs_msb = rs_value & (1ULL << 63);
  if (rt_value == 0) {
    hi = rs_value;
    lo = rs_msb ? 1 : 0xFFFFFFFFFFFFFFFFULL;
  } else if ((uint64_t)rs_value == 0x8000000000000000ULL && (uint64_t)rt_value == 0xFFFFFFFFFFFFFFFFULL) {
    hi = 0;
    lo = 0x8000000000000000ULL;
  } else {
    int64_t rs_signed = rs_value;
    int64_t rt_signed = rt_value;
    lo = rs_signed / rt_signed;
    hi = rs_signed % rt_signed;
  }
  lo_ = lo;
  hi_ = hi;
}

MIPS_TEMPLATE
void MIPS_BASE::InstDdivu(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t hi = 0;
  uint64_t lo = 0;
  if (rt_value == 0) {
    hi = rs_value;
    lo = 0xFFFFFFFFFFFFFFFFULL;
  } else {
    lo = rs_value / rt_value;
    hi = rs_value % rt_value;
  }
  lo_ = lo;
  hi_ = hi;
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsll(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rt_value = ReadGpr64(inst.rt());
  if (inst.shamt() == 0) {
    WriteGpr64(inst.rd(), rt_value);
    return;
  }
  uint64_t rd_value = rt_value << inst.shamt();
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsll32(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = rt_value << (inst.shamt() + 32);
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsllv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rd_value = rt_value << (rs_value & 63);
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsra(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int64_t rt_value = ReadGpr64(inst.rt());
  if (inst.shamt() == 0) {
    WriteGpr64(inst.rd(), rt_value);
    return;
  }
  int64_t rd_value = rt_value >> inst.shamt();
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsra32(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int64_t rt_value = ReadGpr64(inst.rt());
  int64_t rd_value = rt_value >> (inst.shamt() + 32);
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsrav(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  int64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rs_value = ReadGpr64(inst.rs());
  int64_t rd_value = rt_value >> (rs_value & 63);
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsrl(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rt_value = ReadGpr64(inst.rt());
  if (inst.shamt() == 0) {
    WriteGpr64(inst.rd(), rt_value);
    return;
  }
  uint64_t rd_value = rt_value >> inst.shamt();
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsrl32(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rd_value = rt_value >> (inst.shamt() + 32);
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDsrlv(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint64_t rt_value = ReadGpr64(inst.rt());
  uint64_t rs_value = ReadGpr64(inst.rs());
  uint64_t rd_value = rt_value >> (rs_value & 63);
  WriteGpr64(inst.rd(), rd_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDmfc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  uint64_t cop_value = cop_[cop_id]->Read64(inst.rd());
  WriteGpr64(inst.rt(), cop_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstDmtc(uint32_t opcode) {
  RTypeInst inst = MipsInst(opcode).GetRType();
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  uint64_t rt_value = ReadGpr64(inst.rt());
  cop_[cop_id]->Write64(inst.rd(), rt_value);
  if (kLazyInterruptPolling) {
    if (cop_id == 0) {
      CheckInterrupt();
    }
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLd(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 7)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult64 load_result = Load64(address);
  if (load_result.has_value) {
    int64_t rt_value = load_result.value;
    WriteGpr64(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLdc(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  LoadResult64 load_result = Load64(address);
  if (load_result.has_value) {
    uint64_t copt_value = load_result.value;
    cop_[cop_id]->Write64(inst.rt(), copt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstLdl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint64_t rt_value = ReadGpr64(inst.rt());
  int address_unalignment = address & 7;
  uint64_t address_aligned = address & ~7ULL;

  if (config_.use_big_endian_) {
    address_unalignment = 7 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address + i;
    LoadResult8 load_result = Load8(addr);
    if (load_result.has_value) {
      int shamt = (7 - i) * 8;
      rt_value &= ~(0xFFULL << shamt);
      rt_value |= (uint64_t)load_result.value << shamt;
    }
  }

  WriteGpr64(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstLdr(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint64_t rt_value = ReadGpr64(inst.rt());
  int address_unalignment = address & 7;
  uint64_t address_aligned = address & ~7ULL;

  address_unalignment = 7 - address_unalignment;
  if (config_.use_big_endian_) {
    address_unalignment = 7 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address - i;
    LoadResult8 load_result = Load8(addr);
    if (load_result.has_value) {
      int shamt = i * 8;
      rt_value &= ~(0xFFULL << shamt);
      rt_value |= (uint64_t)load_result.value << shamt;
    }
  }

  WriteGpr64(inst.rt(), rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstLwu(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 3)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrl);
    return;
  }
  LoadResult32 load_result = Load32(address);
  if (load_result.has_value) {
    uint64_t rt_value = load_result.value;
    WriteGpr64(inst.rt(), rt_value);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstSd(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  if (!config_.allow_misaligned_access_ && (address & 7)) {
    cop_[0]->Write64Internal(8, address);
    TriggerException(ExceptionCause::kAddrs);
    return;
  }
  uint64_t rt_value = ReadGpr64(inst.rt());
  Store64(address, rt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSdc(uint32_t opcode) {
  uint8_t cop_id = (opcode >> 26) & 3;
  if (!IsCopEnabled(cop_id)) {
    cop_cause_ = cop_id;
    TriggerException(ExceptionCause::kCop);
    return;
  }
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint64_t copt_value = cop_[cop_id]->Read64(inst.rt());
  Store64(address, copt_value);
}

MIPS_TEMPLATE
void MIPS_BASE::InstSdl(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint64_t rt_value = ReadGpr64(inst.rt());
  int address_unalignment = address & 7;

  if (config_.use_big_endian_) {
    address_unalignment = 7 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address + i;
    int shamt = (7 - i) * 8;
    Store8(addr, rt_value >> shamt);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstSdr(uint32_t opcode) {
  ITypeInst inst = MipsInst(opcode).GetIType();
  uint64_t rs_value = ReadGpr64(inst.rs());
  int32_t imm = sext_itype_imm_i32(inst);
  uint64_t address = rs_value + imm;
  uint64_t rt_value = ReadGpr64(inst.rt());
  int address_unalignment = address & 7;

  address_unalignment = 7 - address_unalignment;
  if (config_.use_big_endian_) {
    address_unalignment = 7 - address_unalignment;
  }
  for (int i = 0; i < address_unalignment + 1; i++) {
    uint64_t addr = address - i;
    int shamt = i * 8;
    Store8(addr, rt_value >> shamt);
  }
}

MIPS_TEMPLATE
void MIPS_BASE::InstSync(uint32_t opcode) {
  // Do nothing
}

MIPS_TEMPLATE
void MIPS_BASE::InstUnknown(uint32_t opcode) {
  fmt::print("Unknown instruction: {:08X} @ {:08X}\n", opcode, pc_);
  DumpProcessorLog();
  PANIC("Unknown instruction");
}

std::string MipsLog::ToString(bool is_64bit) {
  std::string result;
  result += fmt::format("PC: {:08X} | OPCODE: {:08X} | {} | ", pc_ & 0xFFFFFFFF, inst_, MipsInst(inst_).Disassemble(pc_));
  for (int i = 1; i < 32; i++) {
    if (is_64bit) {
      result += fmt::format("{}: {:016X}", GetMipsRegName(i), gpr_[i]);
    } else {
      result += fmt::format("{}: {:08X}", GetMipsRegName(i), gpr_[i]);
    }
    if (i != 31) {
      result += " | ";
    }
  }
  return result;
}

// Explicit instantiations — keep definitions out of other TUs
template class MipsBase<MipsTlbNormal, true, false, true>;
template class MipsBase<MipsTlbDummy, false, false, false>;
