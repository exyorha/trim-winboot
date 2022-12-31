#include "CMDecompressor.h"

#include <memory>
#include <stdexcept>
#include <functional>

#include <x86emu.h>

struct X86EMUDeleter {
    inline void operator()(x86emu_t *emu) const {
        x86emu_done(emu);
    }
};

using X86EMUPointer = std::unique_ptr<x86emu_t, X86EMUDeleter>;

static constexpr uint16_t CMSignature = 0x4D43;

bool isCMCompressed(const unsigned char *data, size_t size) {
    return size > sizeof(CMSignature) && *reinterpret_cast<const uint16_t *>(data) == CMSignature;
}

static const unsigned char *get(const unsigned char *&data, const unsigned char *limit, size_t bytes) {
    if(data + bytes > limit)
        throw std::logic_error("input stream overrun");

    auto result = data;
    data += bytes;

    return result;
}

static inline uint8_t get8(const unsigned char *&data, const unsigned char *limit) {
    return *get(data, limit, 1);
}

static inline uint16_t get16(const unsigned char *&data, const unsigned char *limit) {
    return *reinterpret_cast<const uint16_t *>(get(data, limit, sizeof(uint16_t)));
}

static void walkCompressedBlocks(
    const unsigned char *&data,
    const unsigned char *limit,
    std::function<void(const unsigned char *compressedData,
                       size_t compressedDataLength,
                       size_t uncompressedDataLength)> &&handler) {
    while(true) {
        auto isValid = get8(data, limit);

        if(isValid != 0 && isValid != 1)
            throw std::logic_error("incorrect isValid value");

        auto compressedLength = get16(data, limit);

        if((isValid == 0 && compressedLength != 0) ||
           (isValid != 0 && compressedLength == 0)) {
            throw std::logic_error("incorrect compressedLength value");
        }

        if(!isValid)
            break;

        auto uncompressedLength = get16(data, limit);

        auto blockData = get(data, limit, compressedLength);

        handler(blockData, compressedLength, uncompressedLength);
    }
}

static void map(x86emu_t *emu, uint32_t baseAddress, std::vector<unsigned char> &buf) {
    if((baseAddress & (X86EMU_PAGE_SIZE - 1)) != 0)
        throw std::logic_error("base address is not page-aligned");

    if((buf.size() & (X86EMU_PAGE_SIZE - 1)) != 0)
        throw std::logic_error("buffer length is not page-aligned");

    for(size_t offset = 0, size = buf.size(); offset < size; offset += X86EMU_PAGE_SIZE) {
        x86emu_set_page(emu, baseAddress + offset, buf.data() + offset);
    }

    x86emu_set_perm(emu, baseAddress, baseAddress + buf.size(), X86EMU_PERM_R | X86EMU_PERM_W | X86EMU_PERM_X | X86EMU_PERM_VALID);
}

std::vector<unsigned char> cmDecompress(const unsigned char *data, size_t size) {
    auto begin = data;

    auto limit = data + size;

    auto signature = get16(data, limit);
    if(signature != CMSignature) {
        throw std::logic_error("'CM' signature is not valid at the beginning of the stream");
    }

    auto startOfDataStream = data;

    /*
     * Walk the whole file to get to the decompressor.
     */

    walkCompressedBlocks(data, limit, [](
        const unsigned char *compressedData,
        size_t compressedDataLength,
        size_t uncompressedDataLength) {

        (void)compressedData;
        (void)compressedDataLength;
        (void)uncompressedDataLength;
    });

    /*
     * Align to a paragraph boundary, because the decompressor is always
     * paragraph-aligned.
     */
    auto streamLength = data - begin;
    if((streamLength % 16) != 0) {
        get(data, limit, 16 - streamLength % 16);
    }

    auto startOfDecompressor = data;
    auto decompressorLength = limit - data;

    auto signature2 = get16(data, limit);
    if(signature2 != CMSignature) {
        throw std::logic_error("'CM' signature is not valid at the beginning of the decompressor");
    }

    auto decompressorEntry = get16(data, limit);

    auto decompressorLengthParagraphs = get16(data, limit);
    auto decompressorLengthBytes = 16 * decompressorLengthParagraphs;

    if(decompressorLengthBytes > decompressorLength)
        throw std::logic_error("the decompressor is too long");

    decompressorLength = decompressorLengthBytes;

    /*
     * Do all the necessary setup for the simulator where we are going to run
     * the decompressor.
     */
    auto rawEmu = x86emu_new(0, 0);
    if(rawEmu == nullptr)
        throw std::bad_alloc();

    X86EMUPointer emu(rawEmu);

    /*
     * Our simulated memory is set up as follows:
     * (seg 0000): 0x00000 - 0x00100 - stack (256 bytes)
     * (seg 0010): 0x00100 - 0x10000 - decompressor
     * (seg 1000): 0x10000 - 0x20000 - input buffer
     * (seg 2000): 0x20000 - 0x30000 - output buffer
     */
    static constexpr size_t StackSize = 256;
    std::vector<unsigned char> decompressor(65536);
    std::vector<unsigned char> inputBuffer(65536);
    std::vector<unsigned char> outputBuffer(65536);

    if(decompressorLength + StackSize > decompressor.size())
        throw std::logic_error("the decompressor is too long");

    decompressor[0] = 0xF4; // HLT, to stop the thing

    map(emu.get(), 0x00000, decompressor);
    map(emu.get(), 0x10000, inputBuffer);
    map(emu.get(), 0x20000, outputBuffer);

    memcpy(decompressor.data() + StackSize, startOfDecompressor, decompressorLength);

    x86emu_set_seg_register(emu.get(), emu->x86.R_SS_SEL, 0);
    emu->x86.R_SP = StackSize;

    /*
     * Now, walk the blocks again, doing decompression.
     */

    std::vector<unsigned char> output;

    data = startOfDataStream;
    walkCompressedBlocks(data, limit, [&inputBuffer, &emu, decompressorEntry, &output, &outputBuffer](
        const unsigned char *compressedData,
        size_t compressedDataLength,
        size_t uncompressedDataLength) {

        /*
         * Copy in the whole block.
         */

        auto compressedDataBegin = compressedData;
        auto compressedDataLimit = compressedData + compressedDataLength;

        memcpy(inputBuffer.data(), compressedData, compressedDataLimit - compressedData);

        auto signature = get16(compressedData, compressedDataLimit);
        if(signature != 0x5344)
            throw std::logic_error("DS' signature is not valid at the beginning of a compressed block");

        (void)get16(compressedData, compressedDataLimit); // appears to be unused

        auto control = get16(compressedData, compressedDataLimit);

        x86emu_set_seg_register(emu.get(), emu->x86.R_CS_SEL, StackSize / 16);
        emu->x86.R_IP = decompressorEntry;

        x86emu_set_seg_register(emu.get(), emu->x86.R_DS_SEL, 0x1000);
        emu->x86.R_SI = compressedData - compressedDataBegin;

        x86emu_set_seg_register(emu.get(), emu->x86.R_ES_SEL, 0x2000);
        emu->x86.R_DI = 0;

        emu->x86.R_AX = control;
        emu->x86.R_BX = 1;
        emu->x86.R_CX = (uncompressedDataLength + 511) / 512;
        emu->x86.R_DX = 0;

        // Set up the stack for a far return onto our HLT instruction.
        x86emu_write_word(emu.get(), 0xFC, 0x0000);
        x86emu_write_word(emu.get(), 0xFE, 0x0000);
        emu->x86.R_SP = 0xFC;

        auto result = x86emu_run(emu.get(), 0);

        if(result != 0 ||
           emu->x86.R_CS != 0 ||
           emu->x86.R_IP != 1) {
            printf("CS %04X IP %04X stop %u\n", emu->x86.R_CS, emu->x86.R_IP, result);
            throw std::logic_error("unexpected simulator halt");
        }

        if((emu->x86.R_FLG & FB_CF) != 0)
            throw std::logic_error("the decompressor has failed");

        if(emu->x86.R_SI != compressedDataLength && emu->x86.R_SI != compressedDataLength + 1)
            throw std::logic_error("the decompressor didn't consume the expected amount of data");

        if(emu->x86.R_DI != uncompressedDataLength)
            throw std::logic_error("the decompressor didn't produce the expected amount of data");

        output.insert(output.end(), outputBuffer.begin(), outputBuffer.begin() + uncompressedDataLength);
    });


    return output;
}
