#pragma once
#ifndef MICA_UTIL_TSC_H_
#define MICA_UTIL_TSC_H_

#include <time.h>
#include "mica/common.h"

namespace mica {
namespace util {
static uint64_t rdtsc() {

  time_t sec_since_epoch = time(nullptr);
  uint64_t ns_per_sec = 1000000000;
  uint64_t ns_since_epoch = sec_since_epoch * ns_per_sec;
  return ns_since_epoch;

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