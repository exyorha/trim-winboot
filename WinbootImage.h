#ifndef WINBOOT_IMAGE_H
#define WINBOOT_IMAGE_H

#include <filesystem>
#include <ios>
#include <vector>

struct EXEHeader;

class WinbootImage {
public:
    WinbootImage();
    ~WinbootImage();

    WinbootImage(const WinbootImage &other) = delete;
    WinbootImage &operator =(const WinbootImage &other) = delete;

    void load(const std::filesystem::path &path);
    void load(std::istream &stream);
    void load(std::vector<unsigned char> &&data);

    inline const std::vector<unsigned char> &data() const {
        return m_data;
    }

    void save(const std::filesystem::path &path);
    void save(std::ostream &stream);

    void extractMSDCM(const std::filesystem::path &path);
    void extractMSDCM(std::ostream &stream);

    void removeMSDCM();

    void compress();

    void removeLogo();

private:
    EXEHeader *getEXEHeader(bool evenIfInvalid = false);
    size_t dosSizeParagraphs();
    size_t dosSizeBytes();

    void cutDOSAt(size_t position);

    /*
     * This includes both the MZ header sector (the first one) and the three
     * sectors of MSLOAD itself.
     */
    static constexpr size_t MSLOADSize = 0x800;

    std::vector<unsigned char> m_data;
};

#endif
