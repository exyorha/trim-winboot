#ifndef CM_DECOMPRESSOR_H
#define CM_DECOMPRESSOR_H

#include <vector>
#include <cstring>

bool isCMCompressed(const unsigned char *data, size_t size);
std::vector<unsigned char> cmDecompress(const unsigned char *data, size_t size);

#endif
