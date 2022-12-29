#include <stdio.h>

#include <fstream>
#include <vector>

int main(int argc, char **argv) {
    if(argc != 4) {
        fprintf(stderr, "Usage: makebin <INPUT FILE> <OUTPUT FILE> <SYMBOL>\n");
        return 1;
    }

    auto inFilename = argv[1];
    auto outFilename = argv[2];
    auto symbol = argv[3];

    std::vector<unsigned char> data;

    {
        std::ifstream input;
        input.exceptions(std::ios::failbit | std::ios::eofbit | std::ios::badbit);
        input.open(inFilename, std::ios::in | std::ios::binary);

        input.seekg(0, std::ios::end);
        data.resize(static_cast<size_t>(input.tellg()));
        input.seekg(0);

        input.read(reinterpret_cast<char *>(data.data()), data.size());
    }

    std::ofstream output;
    output.exceptions(std::ios::failbit | std::ios::eofbit | std::ios::badbit);
    output.open(outFilename, std::ios::out | std::ios::trunc | std::ios::binary);

    output << "static const unsigned char " << symbol << "[] = {\n";

    for(size_t offset = 0, size = data.size(); offset < data.size(); offset += 16) {
        size_t chunk = std::min<size_t>(16, size - offset);

        output << "    ";

        for(size_t byte = 0; byte < chunk; byte++) {
            output << "0x";
            output.width(2);
            output.fill('0');
            output << std::hex;
            output << static_cast<unsigned int>(data[offset + byte]);
            output << ", ";
        }

        output << "\n";
    }

    output << "};\n\n";

}
