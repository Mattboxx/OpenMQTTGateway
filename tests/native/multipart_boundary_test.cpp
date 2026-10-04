#include "../../main/MultipartBoundary.h"
#include <cassert>
#include <cstring>
#include <cstdio>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main() {
  assert(!omg::validMultipartBoundaryLength(0));
  assert(omg::validMultipartBoundaryLength(1));
  assert(omg::validMultipartBoundaryLength(70));
  assert(!omg::validMultipartBoundaryLength(71));
  assert(!omg::validMultipartBoundaryLength(static_cast<size_t>(-1)));
  const char boundary[] = "OMG-boundary";
  // Deliberately no NUL: this is the binary parser's actual input shape.
  uint8_t candidate[sizeof(boundary) - 1];
  std::memcpy(candidate, boundary, sizeof(candidate));
  assert(omg::matchesMultipartBoundary(candidate, sizeof(candidate), boundary, sizeof(candidate)));
  candidate[sizeof(candidate) - 1] ^= 1;
  assert(!omg::matchesMultipartBoundary(candidate, sizeof(candidate), boundary, sizeof(candidate)));
  assert(!omg::matchesMultipartBoundary(candidate, 0, boundary, sizeof(candidate)));
  assert(!omg::matchesMultipartBoundary(nullptr, sizeof(candidate), boundary, sizeof(candidate)));
  assert(!omg::matchesMultipartBoundary(candidate, sizeof(candidate), nullptr, sizeof(candidate)));
  assert(!omg::matchesMultipartBoundary(candidate, 1, boundary, 71));
  const char zeroBoundary[] = {'a', 0, 'b'};
  const uint8_t zeroCandidate[] = {'a', 0, 'c'};
  assert(!omg::matchesMultipartBoundary(zeroCandidate, 3, zeroBoundary, 3));
  char maxBoundary[70];
  uint8_t maxCandidate[70];
  std::memset(maxBoundary, 'x', sizeof(maxBoundary));
  std::memset(maxCandidate, 'x', sizeof(maxCandidate));
  assert(omg::matchesMultipartBoundary(maxCandidate, 70, maxBoundary, 70));
  maxCandidate[69] = 'y';
  assert(!omg::matchesMultipartBoundary(maxCandidate, 70, maxBoundary, 70));
#ifdef _WIN32
  // Place a non-terminated mismatch immediately before an inaccessible page.
  // An unbounded string search could read the protected page; memcmp may not.
  SYSTEM_INFO systemInfo;
  GetSystemInfo(&systemInfo);
  const size_t page = systemInfo.dwPageSize;
  uint8_t* memory = static_cast<uint8_t*>(VirtualAlloc(nullptr, 2 * page,
                                                     MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
  assert(memory);
  DWORD previousProtection = 0;
  assert(VirtualProtect(memory + page, page, PAGE_NOACCESS, &previousProtection));
  uint8_t* boundedCandidate = memory + page - 70;
  std::memset(boundedCandidate, 'x', 70);
  assert(omg::matchesMultipartBoundary(boundedCandidate, 70, maxBoundary, 70));
  boundedCandidate[0] = 'y';
  assert(!omg::matchesMultipartBoundary(boundedCandidate, 70, maxBoundary, 70));
  assert(VirtualFree(memory, 0, MEM_RELEASE));
#endif
  std::puts("PASS: bounded binary multipart matching, non-NUL data, mismatch, invalid lengths and max boundary");
}
