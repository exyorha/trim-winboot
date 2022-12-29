#include "CompressionStream.h"

#include <stdexcept>

CompressionStream::CompressionStream() : m_size(0) {

}

CompressionStream::~CompressionStream() = default;

size_t CompressionStream::reservedSize() const {
    return m_data.size() - m_size;
}

void CompressionStream::reserveOutputBytes(size_t size) {
    if(reservedSize() < size) {
        m_data.resize(m_size + size);
    }
}

size_t CompressionStream::getAvailableArea(unsigned char *&data) {
    data = m_data.data() + m_size;

    return m_data.size() - m_size;
}

void CompressionStream::advanceOutputPointer(size_t size) {
    if(size > reservedSize())
        throw std::logic_error("CompressionStream::advanceOutputPointer: overrun");

    m_size += size;
}

std::vector<unsigned char> &&CompressionStream::finish() {
    m_data.resize(m_size);
    m_data.shrink_to_fit();

    return std::move(m_data);
}

