#pragma once
#ifndef MICA_UTIL_TSC_H_
#define MICA_UTIL_TSC_H_

#include "mica/common.h"
#include <chrono>

namespace mica {
namespace util {
static uint64_t rdtsc() {
  std::chrono::time_point<std::chrono::system_clock> now =
      std::chrono::system_clock::now();
  std::chrono::nanoseconds ns_duration = now.time_since_epoch();

  return static_cast<uint64_t>(ns_duration.count());

  //  uint64_t rax;
//  uint64_t rdx;
//  asm volatile("rdtsc" : "=a"(rax), "=d"(rdx));
//  return (rdx << 32) | rax;
}

static uint64_t rdtscp() {
  uint64_t rax;
  uint64_t rdx;
  uint32_t aux;
  asm volatile("rdtscp" : "=a"(rax), "=d"(rdx), "=c"(aux) : :);
  return (rdx << 32) | rax;
}
}
}

#endif