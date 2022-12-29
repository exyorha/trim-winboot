#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include <stdexcept>

#include "WinbootImage.h"

static const struct option options[] {
    { "help",          no_argument,       nullptr, 0 },
    { "extract-msdcm", required_argument, nullptr, 0 },
    { "remove-msdcm",  no_argument,       nullptr, 0 },
    { "compress",      no_argument,       nullptr, 0 },
    { "remove-logo",   no_argument,       nullptr, 0 },
    { nullptr,         0,                 nullptr, 0 }
};

static void usage(const char *appname) {
    printf(
           "MS-DOS 7 WINBOOT.SYS size reduction tool.\n"
           "\n"
           "Usage: %s [OPTIONS] <INPUT FILE> <OUTPUT FILE>\n"
           "Options:\n"
           "  --help                      Print this message\n"
           "  --extract-msdcm=<FILENAME>  Extract the MSDCM portion of WINBOOT.SYS into a separate file.\n"
           "                              Can be combined with --remove-msdcm to remove it afterwards.\n"
           "\n"
           "  --remove-msdcm              Remove the MSDCM portion of WINBOOT.SYS. Note: will make\n"
           "                              Windows boot potentially problematic unless MSDCM is saved\n"
           "                              as JO.SYS beforehand.\n"
           "\n"
           "  --compress                  Compress WINBOOT.SYS with LZ4 compression algorithm.\n"
           "  --remove-logo               Remove the built-in logo without impairing functionality.\n",
           appname);
}

int main(int argc, char **argv) {
    int optindex;
    int result;
    const char *extractMSDCMTo = nullptr;
    bool removeMSDCM = false;
    bool compress = false;
    bool removeLogo = false;

    while((result = getopt_long(argc, argv, "", options, &optindex)) != -1) {
        switch(result) {
            case '?':
            case ':':
                fprintf(stderr, "Try %s --help for usage.\n", argv[0]);
                return 1;

            case 0:
                switch(optindex) {
                    case 0: // --help
                        usage(argv[0]);
                        return 0;

                    case 1: // --extract-msdcm
                        extractMSDCMTo = optarg;
                        break;

                    case 2: // --remove-msdcm
                        removeMSDCM = true;
                        break;

                    case 3: // --compress
                        compress = true;
                        break;

                    case 4: // --remove-logo
                        removeLogo = true;
                        break;

                    default:
                        throw std::logic_error("unexpected optindex from getopt_long");
                }
                break;

            default:
                throw std::logic_error("unexpected return value from getopt_long");
        }
    }

    if(argc - optind < 2) {
        fprintf(stderr, "Try %s --help for usage.\n", argv[0]);
        return 1;
    }

    auto input = argv[optind];
    auto output = argv[optind + 1];

    WinbootImage image;
    image.load(input);

    if(extractMSDCMTo) {
        image.extractMSDCM(extractMSDCMTo);
    }

    if(removeMSDCM) {
        image.removeMSDCM();
    }

    if(removeLogo) {
        image.removeLogo();
    }

    if(compress) {
        image.compress();
    }

    image.save(output);
}
