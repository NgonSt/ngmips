#pragma once
#include <fmt/format.h>
#include <stdio.h>
#include <signal.h>

#define PANIC(...)                            \
  fmt::print("{}:{} | ", __LINE__, __FILE__); \
  fmt::print(__VA_ARGS__);                            \
  fflush(stdout);                             \
  fflush(stderr);                             \
  raise(SIGABRT)
