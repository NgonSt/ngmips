#include "mips_fpu.h"

#include <cmath>
#include <fmt/format.h>

#include "mips_base.h"
#include "panic.h"

namespace {

class FpuRTypeInst {
 private:
  uint32_t raw_;

 public:
  FpuRTypeInst(uint32_t opcode) {
    raw_ = opcode;
  }

  uint8_t funct() { return raw_ & 0b111111; };
  uint8_t fd() { return (raw_ >> 6) & 0b11111; };
  uint8_t fs() { return (raw_ >> 11) & 0b11111; };
  uint8_t ft() { return (raw_ >> 16) & 0b11111; };
  uint8_t fmt() { return (raw_ >> 21) & 0b11111; };
  uint8_t op() { return (raw_ >> 26) & 0b111111; };
};

template <class T>
bool compare(uint32_t opcode, T fs, T ft) {
  uint8_t cond = opcode & 0x7;
  bool allow_unordered = opcode & 0x8;
  bool gt = fs > ft;   // Greater Than
  bool lt = fs < ft;   // Less than
  bool eq = fs == ft;  // Equal
  bool un = false;     // Unordered

  bool result = false;
  switch (cond) {
    case 0:  // F
      result = false;
      break;
    case 1:  // NGLE
      result = !gt && !lt && !eq;
      break;
    case 2:  // EQ
      result = eq;
      break;
    case 3:  // NGL
      result = !gt && !lt;
      break;
    case 4:  // LT
      result = lt;
      break;
    case 5:  // NGE
      result = !gt && !eq;
      break;
    case 6:  // LE
      result = lt || eq;
      break;
    case 7:  // NGT
      result = !gt;
      break;
    default:
      PANIC("Unknown condition {}\n", cond);
      break;
  }

  // fmt::print("C {} | {} vs {} = {}\n", cond, ft, fs, result);
  return result;
}

int64_t round_f32(f32_t value, int rm) {
  switch (rm) {
    case 0:
      return nearbyintf(value);
    case 1:
      return truncf(value);
    case 2:
      return ceilf(value);
    case 3:
      return floorf(value);
  }
  return 0;
}

int64_t round_f64(f64_t value, int rm) {
  switch (rm) {
    case 0:
      return nearbyint(value);
    case 1:
      return trunc(value);
    case 2:
      return ceil(value);
    case 3:
      return floor(value);
  }
  return 0;
}

}  // namespace

MipsFpu::MipsFpu() {
}

void MipsFpu::ConnectCpu(MipsBase* cpu) {
  cpu_ = cpu;
}

void MipsFpu::Reset() {
  for (int i = 0; i < 32; i++) {
    fpr_[i] = 0;
  }
  fcr31_ = 0;
}

void MipsFpu::Command(uint32_t command) {
  // fmt::print("FPU command: {:08X}\n", command);
  uint8_t funct = command & 0x3F;
  switch (funct) {
    case 0x00:
      InstAdd(command);
      break;
    case 0x01:
      InstSub(command);
      break;
    case 0x02:
      InstMul(command);
      break;
    case 0x03:
      InstDiv(command);
      break;
    case 0x04:
      InstSqrt(command);
      break;
    case 0x05:
      InstAbs(command);
      break;
    case 0x06:
      InstMov(command);
      break;
    case 0x07:
      InstNeg(command);
      break;
    case 0x08:
      InstRoundL(command);
      break;
    case 0x09:
      InstTruncL(command);
      break;
    case 0x0A:
      InstCeilL(command);
      break;
    case 0x0B:
      InstFloorL(command);
      break;
    case 0x0C:
      InstRoundW(command);
      break;
    case 0x0D:
      InstTruncW(command);
      break;
    case 0x0E:
      InstCeilW(command);
      break;
    case 0x0F:
      InstFloorW(command);
      break;
    case 0x20:
      InstCvtS(command);
      break;
    case 0x21:
      InstCvtD(command);
      break;
    case 0x24:
      InstCvtW(command);
      break;
    case 0x25:
      InstCvtL(command);
      break;
    default:
      if (funct >= 0x30) {
        InstC(command);
      } else {
        fmt::print("Unknown FPU instruction: {:08X}\n", command);
      }
      break;
  }
}

uint32_t MipsFpu::Read32(int idx) {
  if (idx < 32) {
    if (GetFr()) {
      return static_cast<uint32_t>(fpr_[idx]);
    } else {
      int phys = idx & ~1;
      if (idx & 1) {
        return static_cast<uint32_t>(fpr_[phys] >> 32);
      } else {
        return static_cast<uint32_t>(fpr_[phys]);
      }
    }
  } else if (idx == 32 + 0) {
    return 0xB00;
  } else if (idx == 32 + 31) {
    return fcr31_;
  }
  return 0;
}

void MipsFpu::Write32(int idx, uint32_t value) {
  if (idx < 32) {
    if (GetFr()) {
      fpr_[idx] = value;
    } else {
      int phys = idx & ~1;
      if (idx & 1) {
        fpr_[phys] = (fpr_[phys] & 0x00000000FFFFFFFF) | (static_cast<uint64_t>(value) << 32);
      } else {
        fpr_[phys] = (fpr_[phys] & 0xFFFFFFFF00000000) | value;
      }
    }
  } else if (idx == 32 + 31) {
    fcr31_ = value;
  }
}

uint64_t MipsFpu::Read64(int idx) {
  if (idx < 32) {
    if (!GetFr()) {
      if (idx & 1) {
        fmt::print("64bit read to odd-number fpr: {}\n", idx);
      }
      idx &= ~1;
    }
    return fpr_[idx];
  }
  return Read32(idx);
}

void MipsFpu::Write64(int idx, uint64_t value) {
  if (idx < 32) {
    if (!GetFr()) {
      if (idx & 1) {
        fmt::print("64bit write to odd-number fpr: {}\n", idx);
      }
      idx &= ~1;
    }
    fpr_[idx] = value;
  } else {
    Write32(idx, value);
  }
}

bool MipsFpu::GetFlag() {
  return (fcr31_ & (1 << 23)) != 0;
}

float MipsFpu::ReadF32(int idx) {
  uint32_t value = ReadI32(idx);
  return *reinterpret_cast<f32_t*>(&value);
}

void MipsFpu::WriteF32(int idx, f32_t value) {
  uint32_t i = *reinterpret_cast<uint32_t*>(&value);
  WriteI32(idx, i);
}

double MipsFpu::ReadF64(int idx) {
  uint64_t value = ReadI64(idx);
  return *reinterpret_cast<f64_t*>(&value);
}

void MipsFpu::WriteF64(int idx, f64_t value) {
  uint64_t i = *reinterpret_cast<uint64_t*>(&value);
  WriteI64(idx, i);
}

uint32_t MipsFpu::ReadI32(int idx) {
  if (GetFr()) {
    return static_cast<uint32_t>(fpr_[idx]);
  }

  int phys = idx & ~1;
  if (idx & 1) {
    return static_cast<uint32_t>(fpr_[phys] >> 32);
  }
  return static_cast<uint32_t>(fpr_[phys]);
}

void MipsFpu::WriteI32(int idx, uint32_t value) {
  if (GetFr()) {
    fpr_[idx] = value;
  } else {
    int phys = idx & ~1;
    if (idx & 1) {
      fpr_[phys] = (fpr_[phys] & 0x00000000FFFFFFFF) | (static_cast<uint64_t>(value) << 32);
    } else {
      fpr_[phys] = (fpr_[phys] & 0xFFFFFFFF00000000) | value;
    }
  }
}

uint64_t MipsFpu::ReadI64(int idx) {
  if (!GetFr()) {
    idx &= ~1;
  }
  return fpr_[idx];
}

void MipsFpu::WriteI64(int idx, uint64_t value) {
  if (!GetFr()) {
    idx &= ~1;
  }
  fpr_[idx] = value;
}

bool MipsFpu::GetFr() {
  bool fr = cpu_->GetCop(0)->Read32Internal(12) & (1 << 26);
  return fr;
}

void MipsFpu::InstAdd(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t ft_value = ReadF32(inst.ft());
      f32_t fd_value = fs_value + ft_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t ft_value = ReadF64(inst.ft());
      f64_t fd_value = fs_value + ft_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstSub(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t ft_value = ReadF32(inst.ft());
      f32_t fd_value = fs_value - ft_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t ft_value = ReadF64(inst.ft());
      f64_t fd_value = fs_value - ft_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstMul(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t ft_value = ReadF32(inst.ft());
      f32_t fd_value = fs_value * ft_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t ft_value = ReadF64(inst.ft());
      f64_t fd_value = fs_value * ft_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstDiv(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t ft_value = ReadF32(inst.ft());
      f32_t fd_value = fs_value / ft_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t ft_value = ReadF64(inst.ft());
      f64_t fd_value = fs_value / ft_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstSqrt(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t fd_value = sqrtf(fs_value);
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t fd_value = sqrt(fs_value);
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstAbs(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t fd_value = fabsf(fs_value);
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t fd_value = fabs(fs_value);
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstMov(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      WriteF32(inst.fd(), fs_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      WriteF64(inst.fd(), fs_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstNeg(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t fd_value = -fs_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t fd_value = -fs_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstRoundL(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int64_t fd_value = roundf(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int64_t fd_value = round(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstTruncL(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int64_t fd_value = truncf(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int64_t fd_value = trunc(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstCeilL(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int64_t fd_value = ceilf(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int64_t fd_value = ceil(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstFloorL(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int64_t fd_value = floorf(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int64_t fd_value = floor(fs_value);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstRoundW(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int32_t fd_value = roundf(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int32_t fd_value = round(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstTruncW(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int32_t fd_value = truncf(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int32_t fd_value = trunc(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstCeilW(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int32_t fd_value = ceilf(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int32_t fd_value = ceil(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstFloorW(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int32_t fd_value = floorf(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int32_t fd_value = floor(fs_value);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstCvtS(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f32_t fd_value = fs_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 20: {
      int32_t fs_value = ReadI32(inst.fs());
      f32_t fd_value = fs_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    case 21: {
      int64_t fs_value = ReadI64(inst.fs());
      f32_t fd_value = fs_value;
      WriteF32(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstCvtD(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f64_t fd_value = fs_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    case 20: {
      int32_t fs_value = ReadI32(inst.fs());
      f64_t fd_value = fs_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    case 21: {
      int64_t fs_value = ReadI64(inst.fs());
      f64_t fd_value = fs_value;
      WriteF64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstCvtW(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int32_t fd_value = round_f32(fs_value, fcr31_ & 0x3);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int32_t fd_value = round_f64(fs_value, fcr31_ & 0x3);
      WriteI32(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstCvtL(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      int64_t fd_value = round_f32(fs_value, fcr31_ & 0x3);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      int64_t fd_value = round_f64(fs_value, fcr31_ & 0x3);
      WriteI64(inst.fd(), fd_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }
}

void MipsFpu::InstC(uint32_t opcode) {
  FpuRTypeInst inst(opcode);
  bool flag = false;
  switch (inst.fmt()) {
    case 16: {
      f32_t fs_value = ReadF32(inst.fs());
      f32_t ft_value = ReadF32(inst.ft());
      flag = compare<f32_t>(opcode, fs_value, ft_value);
      break;
    }
    case 17: {
      f64_t fs_value = ReadF64(inst.fs());
      f64_t ft_value = ReadF64(inst.ft());
      flag = compare<f64_t>(opcode, fs_value, ft_value);
      break;
    }
    default:
      fmt::print("Unknown FPU instruction: {:08X}\n", opcode);
      break;
  }

  fcr31_ &= ~(1 << 23);
  fcr31_ |= flag ? (1 << 23) : 0;
}
