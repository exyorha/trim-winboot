#include <fstream>
#include <bit>
#include <cstring>

#include "WinbootImage.h"
#include "DOSTypes.h"
#include "CompressionStream.h"
#include "msload_extension.h"

#include <lz4hc.h>

WinbootImage::WinbootImage() = default;

WinbootImage::~WinbootImage() = default;

void WinbootImage::load(const std::filesystem::path &path) {
    std::ifstream stream;
    stream.exceptions(std::ios::failbit | std::ios::eofbit | std::ios::badbit);
    stream.open(path, std::ios::in | std::ios::binary);
    load(stream);
}

void WinbootImage::load(std::istream &stream) {
    stream.seekg(0, std::ios::end);

    auto size = static_cast<size_t>(stream.tellg());

    stream.seekg(0);

    std::vector<unsigned char> data(size);

    stream.read(reinterpret_cast<char *>(data.data()), size);
    load(std::move(data));
}

void WinbootImage::save(const std::filesystem::path &path) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::eofbit | std::ios::badbit);
    stream.open(path, std::ios::in | std::ios::trunc | std::ios::binary);
    save(stream);
}

void WinbootImage::save(std::ostream &stream) {
    stream.write(reinterpret_cast<const char *>(m_data.data()), m_data.size());

    auto exeHeader = getEXEHeader(true);
    if(exeHeader->e_magic != EXEHeaderMagic) {
        /*
         * If the MSDCM portion was removed, then we need to add a five sector
         * padding to the end of the file.
         * MSLOAD appears to read at an inappropriate disk location (possibly
         * beyond the end of drive) and gets confused by the drive error
         * without this padding (which must be at least 5 sectors) at the end
         * of file.
         */
        std::vector<char> padding(5 * 512);
        stream.write(padding.data(), padding.size());
    }
}

void WinbootImage::load(std::vector<unsigned char> &&data) {
    m_data = std::move(data);

    static_assert(std::endian::native == std::endian::little, "Little-endian system is expected");

    /*
     * winboot.sys is a dual-start executable: it a normal DOS exe, containing
     * an EXEPack-compressed a component named 'MSDCM' in the EXE structure, and
     * the MS-DOS kernel (IO.SYS/MSDOS.SYS combo) inside what's functionally is
     * the EXE header. 'MSDCM' is only needed to start Windows.
     */
    auto exe = getEXEHeader();

    if(exe->e_cblp < 1) {
        throw std::logic_error("e_cblp indicates zero pages");
    }

    auto totalExeSize = 512 * exe->e_cp;

    if(exe->e_cblp != 0) {
        totalExeSize = totalExeSize - 512 + exe->e_cblp;
    }

    if(totalExeSize != m_data.size()) {
        std::stringstream error;
        error << "exe size doesn't match: " << totalExeSize << " indicated ("
              << exe->e_cp << " 512-byte pages, " << exe->e_cblp << " bytes "
              "in the last page), but actual file size is " << m_data.size()
              << " bytes";

        throw std::logic_error(error.str());
    }

    if(dosSizeBytes() > totalExeSize) {
        throw std::logic_error("EXE header (DOS) portion overruns the executable");
    }
}

EXEHeader *WinbootImage::getEXEHeader(bool evenIfInvaid) {
    EXEHeader *header = nullptr;

    if(m_data.size() >= sizeof(EXEHeader)) {
        header = reinterpret_cast<EXEHeader *>(m_data.data());
    }

    if(!header || (!evenIfInvaid && header->e_magic != EXEHeaderMagic))
        throw std::logic_error("winboot has no MZ header. Already removed?");

    return header;
}

size_t WinbootImage::dosSizeParagraphs() {
    /*
     * DOS portion of WINBOOT.SYS (everything but the MSDCM exe) is stored
     * in the 'e_cparhdr' field in the MZ header. Note that this field
     * stays valid even when the MZ header is removed, because MSLOAD needs
     * it, so we access the MZ header without validity checks.
     */

    return getEXEHeader(true)->e_cparhdr;
}

size_t WinbootImage::dosSizeBytes() {
    return 16 * dosSizeParagraphs();
}


void WinbootImage::extractMSDCM(const std::filesystem::path &path) {
    std::ofstream stream;
    stream.exceptions(std::ios::failbit | std::ios::eofbit | std::ios::badbit);
    stream.open(path, std::ios::in | std::ios::trunc | std::ios::binary);
    extractMSDCM(stream);
}

void WinbootImage::extractMSDCM(std::ostream &stream) {
    static constexpr size_t exeHeaderAllocationBytes = 32;
    static constexpr size_t exeHeaderAllocationParagraphs = exeHeaderAllocationBytes / 16;

    static_assert(exeHeaderAllocationBytes >= sizeof(EXEHeader), "EXE header allocation needs to be increased");

    auto originalHeaderParagraphs = dosSizeParagraphs();
    auto originalHeaderBytes = 16 * originalHeaderParagraphs;

    if(originalHeaderBytes < exeHeaderAllocationBytes) {
        throw std::logic_error("EXE header allocation needs to be decreased");
    }

    auto exeHeader = getEXEHeader();

    /*
     * We are shrinking the EXE header area from originalHeaderBytes to
     * exeHeaderAllocation.
     */

    std::vector<char> header(exeHeaderAllocationBytes);
    auto newExe = reinterpret_cast<EXEHeader *>(header.data());

    auto newTotalSize = m_data.size() - originalHeaderBytes + exeHeaderAllocationBytes;

    *newExe = *exeHeader;
    newExe->e_cparhdr = exeHeaderAllocationParagraphs;
    newExe->e_cp = (newTotalSize + 511) / 512;
    newExe->e_cblp = newTotalSize & 511;
    if(newExe->e_crlc != 0) {
        throw std::logic_error("MSDCM contains relocations, which are not currently supported");
    }

    stream.write(header.data(), header.size());
    stream.write(reinterpret_cast<const char *>(m_data.data()) + originalHeaderBytes, m_data.size() - originalHeaderBytes);
}

void WinbootImage::removeMSDCM() {
    auto exeHeader = getEXEHeader(true);

    /*
     * To remove MSDCM, we truncate the whole image to only preserve the DOS
     * portion, and then zero out the first sector, containing the MZ header
     * (except e_cparhdr, which we need to preserve for MSLOAD) to make sure
     * that WINBOOT.SYS fails when trying to load MSDCM.
     */

    auto savedSize = exeHeader->e_cparhdr;

    m_data.resize(16 * savedSize);

    memset(exeHeader, 0, 512);

    exeHeader->e_cparhdr = savedSize;
}


void WinbootImage::compress() {
    /*
     * Get the DOS ('payload') portion.
     */
    auto dosSize = dosSizeBytes();
    if(dosSize < MSLOADSize + 2) {
        throw std::logic_error("DOS portion is too short (doesn't fit the MSLOAD)");
    }

    auto payload = m_data.data() + MSLOADSize;
    auto payloadSize = dosSize - MSLOADSize;

    static constexpr uint16_t LZMagic = 0x5A4C; // 'LZ'

    if(*reinterpret_cast<const uint16_t *>(payload) == LZMagic) {
        throw std::logic_error("WINBOOT.SYS is already LZ-compressed");
    }

    /*
     * Pack LZ4 into a compact framed format:
     * 2 bytes: 0x5A4C ('LZ')
     * 2 bytes: source size, paragraphs
     * zero or more blocks:
     *   2 bytes: compressed length, bytes
     *   the specified number of bytes
     * 2 bytes: zero
     */

    //LZ4HC_CLEVEL_MAX;

    CompressionStream outputStream;

    /*
     * Stream header
     */
    outputStream.reserveOutputBytes(4); // header

    unsigned char *headerData;
    outputStream.getAvailableArea(headerData);

    reinterpret_cast<uint16_t *>(headerData)[0] = LZMagic;
    reinterpret_cast<uint16_t *>(headerData)[1] = payloadSize / 16;

    outputStream.advanceOutputPointer(4);

    static constexpr size_t blockSize = 63 * 1024;
    for(size_t pos = 0; pos < payloadSize; pos += blockSize) {
        auto chunk = std::min<size_t>(blockSize, payloadSize - pos);

        outputStream.reserveOutputBytes(2 + LZ4_compressBound(chunk));

        unsigned char *blockData;
        size_t blockDataLength = outputStream.getAvailableArea(blockData);

        auto result = LZ4_compress_HC(
            reinterpret_cast<const char *>(payload + pos),
            reinterpret_cast<char *>(blockData + 2),
            chunk,
            blockDataLength - 2,
            LZ4HC_CLEVEL_MAX
        );
        if(result <= 0)
            throw std::logic_error("LZ4_compress_HC failed");

        if(result > blockSize)
            throw std::logic_error("LZ4-compressed block length exceeds the limit");

        *reinterpret_cast<uint16_t *>(blockData) = static_cast<uint16_t>(result);

        outputStream.advanceOutputPointer(2 + result);
    }

    /*
     * Stream terminator
     */
    unsigned char *terminatorData;
    outputStream.getAvailableArea(terminatorData);

    reinterpret_cast<uint16_t *>(terminatorData)[0] = 0;
    outputStream.advanceOutputPointer(2);

    auto compressedPayload = outputStream.finish();
    if(compressedPayload.size() > payloadSize) {
        throw std::logic_error("compressed payload length exceeds the uncompressed length");
    }

    /*
     * Transplant the compressed payload back in.
     */

    memcpy(m_data.data() + MSLOADSize, compressedPayload.data(), compressedPayload.size());

    /*
     * Update file sizes, relocate MSDCM (if present).
     */
    cutDOSAt(MSLOADSize + compressedPayload.size());

    /*
     * Now, insert our unpacking extension into MSLOAD.
     */

    static constexpr size_t msloadFinalBranchPos = 0x4EB;
    static constexpr size_t msloadExtensionPos   = 0x701;

    memcpy(m_data.data() + msloadExtensionPos, msload_extension, sizeof(msload_extension));

    /*
     * And patch the final branch into the payload to pass control into our
     * extension instead.
     */
    unsigned char *finalBranch = &m_data[msloadFinalBranchPos];
    finalBranch[0] = 0xE9; // JMP NEAR
    *reinterpret_cast<int16_t *>(&finalBranch[1]) = msloadExtensionPos - (msloadFinalBranchPos + 3);
}

void WinbootImage::cutDOSAt(size_t newSize) {
    newSize = (newSize + 15) & ~15;

    size_t oldSize = dosSizeBytes();

    printf("Shrinking the DOS portion: new size: %zu, old size: %zu\n", newSize, oldSize);

    if(newSize > oldSize) {
        throw std::logic_error("WinbootImage::cutDOSAt: DOS size has grown");
    }

    auto header = getEXEHeader(true);

    bool hasMSDCM = header->e_magic == EXEHeaderMagic;

    if(hasMSDCM) {
        auto msdcmSize = m_data.size() - oldSize;
        auto moveup = oldSize - newSize;

        printf("MSDCM: relocating MSDCM body, %zu bytes, from %zu to %zu, moveup %zu bytes\n",
               msdcmSize, oldSize, newSize, moveup);

        memmove(m_data.data() + newSize,
                m_data.data() + oldSize,
                msdcmSize);

        auto newFullSize = newSize + msdcmSize;

        if(moveup & 15)
            throw std::logic_error("moveup is not paragraph-aligned");


        header->e_cp = (newFullSize + 511) / 512;
        header->e_cblp = (newFullSize & 511);
        header->e_cparhdr -= moveup / 16;
        if(header->e_crlc != 0) {
            throw std::logic_error("MSDCM contains relocations, which are not currently supported");
        }

        m_data.resize(newFullSize);
    } else {
        header->e_cparhdr = newSize / 16;

        m_data.resize(newSize);
    }
}

void WinbootImage::removeLogo() {
    /*
     * First,  we need to figure out where the logo starts.
     */
    static constexpr size_t dosFixedPortionInParagraphs = 0x12D5; // From the IO.SYS-proper portion of WINBOOT.SYS
    static constexpr size_t dosDynamicPortionLengthOffset = 0x803;

    auto fullDosSize = dosSizeBytes(); // DOS size including the logo, if any

    if(fullDosSize < dosFixedPortionInParagraphs * 16) {
        throw std::logic_error("WINBOOT.SYS is too short: doesn't fit IO.SYS");
    }

    size_t dosDynamicPortionInBytes = *reinterpret_cast<const uint16_t *>(&m_data[dosDynamicPortionLengthOffset]);

    size_t realDOSSize = dosFixedPortionInParagraphs * 16 + dosDynamicPortionInBytes + 0x800 - 0x700;
    if(realDOSSize > fullDosSize) {
        throw std::logic_error("WINBOOT.SYS is too short: doesn't fit IO.SYS+MSDOS.SYS");
    } else if(realDOSSize < fullDosSize) {
        cutDOSAt(realDOSSize);
    }
}
