#ifndef OMG_MULTIPART_BOUNDARY_H
#define OMG_MULTIPART_BOUNDARY_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace omg {
// RFC 2046 section 5.1.1: the delimiter value is 1..70 bytes.
static const size_t multipartBoundaryMax = 70;

inline bool validMultipartBoundaryLength(size_t length) {
  return length > 0 && length <= multipartBoundaryMax;
}

inline bool matchesMultipartBoundary(const uint8_t* candidate, size_t candidateLength,
                                     const char* boundary, size_t boundaryLength) {
  return candidate && boundary && validMultipartBoundaryLength(boundaryLength) &&
         candidateLength == boundaryLength &&
         memcmp(candidate, boundary, boundaryLength) == 0;
}
} // namespace omg

#endif
