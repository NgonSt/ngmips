#include "mips_cop0.h"

#include <fmt/core.h>

const bool kLogCop = false;

MipsCop0::MipsCop0() {
  badvaddr_ = 0;
  sr_ = 0;
  cause_ = 0;
  epc_ = 0;
  prid_ = 0;
}

void MipsCop0::Reset() {
  badvaddr_ = 0;
  sr_ = 0;
  cause_ = 0;
  epc_ = 0;
  prid_ = 0;
}

void MipsCop0::Command(uint32_t command) {
  switch (command) {
    case 0x10: {
      if (kLogCop) {
        fmt::print("RFE\n");
      }
      uint32_t sr_mode = sr_ & 0x3F;
      sr_ &= ~0x0F;
      sr_ |= (sr_mode >> 2) & 0xF;
      break;
    }
    default:
      break;
  }
}

uint32_t MipsCop0::Read32(int idx) {
  switch (idx) {
    case 8:
      return badvaddr_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
  }
  return 0;
}

void MipsCop0::Write32(int idx, uint32_t value) {
  if (kLogCop) {
    fmt::print("COP0 write: {}, {:08X}\n", idx, value);
  }
  switch (idx) {
    case 12:
      sr_ = value;
      break;
  }
}

uint64_t MipsCop0::Read64(int idx) {
  switch (idx) {
    case 8:
      return badvaddr_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
  }
  return 0;
}

void MipsCop0::Write64(int idx, uint64_t value) {
  if (kLogCop) {
    fmt::print("COP0 write: {}, {:08X}\n", idx, value);
  }
  switch (idx) {
    case 12:
      sr_ = value;
      break;
    case 13:
      fmt::print("Write to cause: {:08X}\n", value);
      cause_ = (cause_ & ~0x300) | (value & 0x300);
      break;
  }
}

uint32_t MipsCop0::Read32Internal(int idx) {
  switch (idx) {
    case 8:
      return badvaddr_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
  }
  return 0;
}

void MipsCop0::Write32Internal(int idx, uint32_t value) {
  switch (idx) {
    case 8:
      badvaddr_ = value;
      break;
    case 12:
      sr_ = value;
      break;
    case 13:
      cause_ = value;
      break;
    case 14:
      epc_ = value;
      break;
    case 15:
      prid_ = value;
      break;
  }
}

uint64_t MipsCop0::Read64Internal(int idx) {
  switch (idx) {
    case 8:
      return badvaddr_;
    case 12:
      return sr_;
    case 13:
      return cause_;
    case 14:
      return epc_;
    case 15:
      return 2;
  }
  return 0;
}

void MipsCop0::Write64Internal(int idx, uint64_t value) {
  switch (idx) {
    case 8:
      badvaddr_ = value;
      break;
    case 12:
      sr_ = value;
      break;
    case 13:
      cause_ = value;
      break;
    case 14:
      epc_ = value;
      break;
    case 15:
      prid_ = value;
      break;
  }
}
