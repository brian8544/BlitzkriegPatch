#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>

bool patchFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        std::cerr << "Error: Cannot open " << filename << std::endl;
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());
    file.close();

    // Patch to 4K (3840x2160)
    const uint16_t maxWidth = 3840;
    const uint16_t maxHeight = 2160;
    bool foundPatches = false;

    // Find width (1600 = 0x640)
    for (size_t i = 0; i < data.size() - 5; i++) {
        if (data[i] == 0x68 &&
            data[i + 1] == 0x40 && data[i + 2] == 0x06 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00) {

            std::cout << "Found width limit (1600) at offset: 0x"
                << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << std::endl;

            // Patch to 4K width
            data[i + 1] = maxWidth & 0xFF;
            data[i + 2] = (maxWidth >> 8) & 0xFF;

            std::cout << "  Patched to: " << maxWidth << " (0x"
                << std::hex << maxWidth << std::dec << ")" << std::endl;
            foundPatches = true;
        }
    }

    // Find height (1200 = 0x4B0)
    for (size_t i = 0; i < data.size() - 5; i++) {
        if (data[i] == 0x68 &&
            data[i + 1] == 0xB0 && data[i + 2] == 0x04 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00) {

            std::cout << "Found height limit (1200) at offset: 0x"
                << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << std::endl;

            // Patch to 4K height
            data[i + 1] = maxHeight & 0xFF;
            data[i + 2] = (maxHeight >> 8) & 0xFF;

            std::cout << "  Patched to: " << maxHeight << " (0x"
                << std::hex << maxHeight << std::dec << ")" << std::endl;
            foundPatches = true;
        }
    }

    if (!foundPatches) {
        std::cerr << "Error: Could not find resolution limits" << std::endl;
        return false;
    }

    // Write patched file
    std::string outputName = filename.substr(0, filename.find_last_of('.')) + "_patched.dll";
    std::ofstream outFile(outputName, std::ios::binary);
    if (!outFile) {
        std::cerr << "Error: Cannot write to " << outputName << std::endl;
        return false;
    }

    outFile.write(reinterpret_cast<const char*>(data.data()), data.size());
    outFile.close();

    std::cout << "\nSuccessfully created: " << outputName << std::endl;
    return true;
}

int main(int argc, char* argv[]) {
    // Check if GFX.dll exists
    std::string dllPath = "gfx.dll";
    std::ifstream testFile(dllPath);
    if (!testFile) {
        std::cerr << "Error: Cannot find " << dllPath << " in current directory" << std::endl;
        return 1;
    }
    testFile.close();

    std::cout << "Target file: " << dllPath << std::endl;

    // Apply patch
    if (patchFile(dllPath)) {
        std::cout << "Patching completed successfully!" << std::endl;
    }
    else {
        std::cerr << "\nPatching failed!" << std::endl;
        return 1;
    }

    return 0;
}