#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>

struct ResolutionConfig
{
    std::string name;
    uint16_t menuWidth;
    uint16_t menuHeight;
    std::string outputSuffix;
};

bool patchGfxDll(const std::string &filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "Error: Cannot open " << filename << std::endl;
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    /*
     * Allow the same size as the original developers intended
     * https://github.com/nival/Blitzkrieg/blob/793501a063eccf6ca522e19705f0af2a16cae0be/Sources/src/GFX/GFXObjectFactory.cpp#L67
     */
    const uint16_t maxWidth = 1000000;
    const uint16_t maxHeight = 1000000;
    bool foundPatches = false;

    std::cout << "Patching gfx.dll..." << std::endl;

    for (size_t i = 0; i < data.size() - 5; i++)
    {
        if (data[i] == 0x68 &&
            data[i + 1] == 0x40 && data[i + 2] == 0x06 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00)
        {

            std::cout << "Found width limit (1600) at offset: 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << std::endl;

            data[i + 1] = maxWidth & 0xFF;
            data[i + 2] = (maxWidth >> 8) & 0xFF;

            std::cout << "  Patched to: " << maxWidth << " (0x"
                      << std::hex << maxWidth << std::dec << ")" << std::endl;
            foundPatches = true;
        }
    }

    for (size_t i = 0; i < data.size() - 5; i++)
    {
        if (data[i] == 0x68 &&
            data[i + 1] == 0xB0 && data[i + 2] == 0x04 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00)
        {

            std::cout << "Found height limit (1200) at offset: 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << std::endl;

            data[i + 1] = maxHeight & 0xFF;
            data[i + 2] = (maxHeight >> 8) & 0xFF;

            std::cout << "  Patched to: " << maxHeight << " (0x"
                      << std::hex << maxHeight << std::dec << ")" << std::endl;
            foundPatches = true;
        }
    }

    if (!foundPatches)
    {
        std::cerr << "Error: Could not find resolution limits" << std::endl;
        return false;
    }

    std::string outputName = filename.substr(0, filename.find_last_of('.')) + "_patched.dll";
    std::ofstream outFile(outputName, std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Error: Cannot write to " << outputName << std::endl;
        return false;
    }

    outFile.write(reinterpret_cast<const char *>(data.data()), data.size());
    outFile.close();

    std::cout << "Successfully created: " << outputName << std::endl
              << std::endl;
    return true;
}

bool patchGameExe(const std::string &filename, const ResolutionConfig &config)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "Error: Cannot open " << filename << std::endl;
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    bool foundPatches = false;

    std::cout << "Patching game.exe " << config.name << " version..." << std::endl;

    for (size_t i = 0; i < data.size() - 5; i++)
    {
        if (data[i] == 0x68 &&
            data[i + 1] == 0x00 && data[i + 2] == 0x04 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00)
        {

            std::cout << "Found InterMission width limit (1024) at offset: 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << std::endl;

            data[i + 1] = config.menuWidth & 0xFF;
            data[i + 2] = (config.menuWidth >> 8) & 0xFF;

            std::cout << "  Patched to: " << config.menuWidth << " (0x"
                      << std::hex << config.menuWidth << std::dec << ")" << std::endl;
            foundPatches = true;
        }
    }

    for (size_t i = 0; i < data.size() - 5; i++)
    {
        if (data[i] == 0x68 &&
            data[i + 1] == 0x00 && data[i + 2] == 0x03 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00)
        {

            std::cout << "Found InterMission height limit (768) at offset: 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << std::endl;

            data[i + 1] = config.menuHeight & 0xFF;
            data[i + 2] = (config.menuHeight >> 8) & 0xFF;

            std::cout << "  Patched to: " << config.menuHeight << " (0x"
                      << std::hex << config.menuHeight << std::dec << ")" << std::endl;
            foundPatches = true;
        }
    }

    if (!foundPatches)
    {
        std::cerr << "Error: Could not find resolution limits" << std::endl;
        return false;
    }

    std::string outputName = filename.substr(0, filename.find_last_of('.')) + config.outputSuffix;
    std::ofstream outFile(outputName, std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Error: Cannot write to " << outputName << std::endl;
        return false;
    }

    outFile.write(reinterpret_cast<const char *>(data.data()), data.size());
    outFile.close();

    std::cout << "Successfully created: " << outputName << std::endl
              << std::endl;
    return true;
}

int main(int argc, char *argv[])
{
    std::string dllPath = "gfx.dll";
    std::string exePath = "game.exe";

    std::ifstream testDll(dllPath);
    std::ifstream testExe(exePath);

    if (!testDll)
    {
        std::cerr << "Error: Cannot find " << dllPath << " in current directory" << std::endl;
        return 1;
    }
    if (!testExe)
    {
        std::cerr << "Error: Cannot find " << exePath << " in current directory" << std::endl;
        return 1;
    }
    testDll.close();
    testExe.close();

    std::cout << "Target files: " << dllPath << ", " << exePath << std::endl
              << std::endl;

    if (!patchGfxDll(dllPath))
    {
        std::cerr << "\nPatching gfx.dll failed!" << std::endl;
        return 1;
    }

    std::vector<ResolutionConfig> configs = {
        {"1080p", 1920, 1080, "_patched_1080p.exe"},
        {"1440p", 2560, 1440, "_patched_1440p.exe"},
        {"2160p", 3840, 2160, "_patched_2160p.exe"}};

    int successCount = 0;
    for (const auto &config : configs)
    {
        if (patchGameExe(exePath, config))
        {
            successCount++;
        }
    }

    if (successCount == configs.size())
    {
        std::cout << "Patching completed successfully!" << std::endl;
        std::cout << "Created gfx_patched.dll and " << successCount << " game.exe versions" << std::endl;
        std::cout << "You can either run one of the patched game versions directly or rename 'game_patched_X.exe' back to 'game.exe' to run it through Steam." << std::endl;
    }
    else
    {
        std::cerr << "\nPatching failed!" << std::endl;
        return 1;
    }

    return 0;
}