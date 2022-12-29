#ifndef COMPRESSION_STREAM_H
#define COMPRESSION_STREAM_H

#include <vector>
#include <cstring>

class CompressionStream {
public:
    CompressionStream();
    ~CompressionStream();

    CompressionStream(const CompressionStream &other) = delete;
    CompressionStream &operator =(const CompressionStream &other) = delete;

    void reserveOutputBytes(size_t size);

    size_t getAvailableArea(unsigned char *&data);
    void advanceOutputPointer(size_t size);

    std::vector<unsigned char> &&finish();

private:
    size_t reservedSize() const;

    std::vector<unsigned char> m_data;
    size_t m_size;
};

#endif
