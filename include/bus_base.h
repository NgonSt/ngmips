#pragma once
#include <cstdint>

struct LoadResult8 {
  bool has_value;
  uint8_t value;
};

struct LoadResult16 {
  bool has_value;
  uint16_t value;
};

struct LoadResult32 {
  bool has_value;
  uint32_t value;
};

struct LoadResult64 {
  bool has_value;
  uint64_t value;
};

class BusBase {
 public:
  virtual void Reset() = 0;
  virtual LoadResult8 Load8(uint64_t address) = 0;
  virtual LoadResult16 Load16(uint64_t address) = 0;
  virtual LoadResult32 Load32(uint64_t address) = 0;
  virtual LoadResult64 Load64(uint64_t address) = 0;
  virtual void Store8(uint64_t address, uint8_t value) = 0;
  virtual void Store16(uint64_t address, uint16_t value) = 0;
  virtual void Store32(uint64_t address, uint32_t value) = 0;
  virtual void Store64(uint64_t address, uint64_t value) = 0;
  virtual bool GetInterrupt() = 0;
};