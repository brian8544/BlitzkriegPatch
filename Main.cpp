#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <iomanip>

bool patchGfxDll(const std::string &filename)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "Error: Cannot open " << filename << "\n";
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    // Allow the same size as the original developers intended: https://github.com/nival/Blitzkrieg/blob/793501a063eccf6ca522e19705f0af2a16cae0be/Sources/src/GFX/GFXObjectFactory.cpp#L67
    const uint32_t maxWidth = 1000000;
    const uint32_t maxHeight = 1000000;
    bool foundPatches = false;

    std::cout << "Patching GFX.dll...\n";

    for (size_t i = 0; i < data.size() - 5; i++)
    {
        if (data[i] == 0x68 &&
            data[i + 1] == 0x40 && data[i + 2] == 0x06 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00)
        {
            std::cout << "Found width limit (1600) at offset: 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << "\n";

            data[i + 1] = maxWidth & 0xFF;
            data[i + 2] = (maxWidth >> 8) & 0xFF;

            std::cout << "  Patched to: " << maxWidth << " (0x"
                      << std::hex << maxWidth << std::dec << ")\n";
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
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << "\n";

            data[i + 1] = maxHeight & 0xFF;
            data[i + 2] = (maxHeight >> 8) & 0xFF;

            std::cout << "  Patched to: " << maxHeight << " (0x"
                      << std::hex << maxHeight << std::dec << ")\n";
            foundPatches = true;
        }
    }

    if (!foundPatches)
    {
        std::cerr << "Error: Could not find resolution limits...\n";
        std::cerr << "The file may already be patched or using a modded dll?\n";
        return false;
    }

    std::string backupName = filename.substr(0, filename.find_last_of('.')) + "_original.dll";
    std::ofstream backupFile(backupName, std::ios::binary);
    if (!backupFile)
    {
        std::cerr << "Error: Cannot create backup " << backupName << "\n";
        return false;
    }

    std::ifstream originalFile(filename, std::ios::binary);
    std::vector<uint8_t> originalData((std::istreambuf_iterator<char>(originalFile)),
                                      std::istreambuf_iterator<char>());
    originalFile.close();

    backupFile.write(reinterpret_cast<const char *>(originalData.data()), originalData.size());
    backupFile.close();
    std::cout << "Created backup: " << backupName << "\n";

    std::ofstream outFile(filename, std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Error: Cannot write to " << filename << "\n";
        return false;
    }

    outFile.write(reinterpret_cast<const char *>(data.data()), data.size());
    outFile.close();

    std::cout << "Successfully patched: " << filename << "\n\n";
    return true;
}

bool patchGameExe(const std::string &filename, const std::string &name,
                  uint16_t menuWidth, uint16_t menuHeight, const std::string &outputSuffix)
{
    std::ifstream file(filename, std::ios::binary);
    if (!file)
    {
        std::cerr << "Error: Cannot open " << filename << "\n";
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
    file.close();

    bool foundPatches = false;

    std::cout << "Patching Game.exe " << name << " version...\n";

    for (size_t i = 0; i < data.size() - 5; i++)
    {
        if (data[i] == 0x68 &&
            data[i + 1] == 0x00 && data[i + 2] == 0x04 &&
            data[i + 3] == 0x00 && data[i + 4] == 0x00)
        {
            std::cout << "Found InterMission width limit (1024) at offset: 0x"
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << "\n";

            data[i + 1] = menuWidth & 0xFF;
            data[i + 2] = (menuWidth >> 8) & 0xFF;

            std::cout << "  Patched to: " << menuWidth << " (0x"
                      << std::hex << menuWidth << std::dec << ")\n";
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
                      << std::hex << std::setw(8) << std::setfill('0') << i << std::dec << "\n";

            data[i + 1] = menuHeight & 0xFF;
            data[i + 2] = (menuHeight >> 8) & 0xFF;

            std::cout << "  Patched to: " << menuHeight << " (0x"
                      << std::hex << menuHeight << std::dec << ")\n";
            foundPatches = true;
        }
    }

    if (!foundPatches)
    {
        std::cerr << "Error: Could not find resolution limits\n";
        return false;
    }

    std::string outputName = filename.substr(0, filename.find_last_of('.')) + outputSuffix;
    std::ofstream outFile(outputName, std::ios::binary);
    if (!outFile)
    {
        std::cerr << "Error: Cannot write to " << outputName << "\n";
        return false;
    }

    outFile.write(reinterpret_cast<const char *>(data.data()), data.size());
    outFile.close();

    std::cout << "Successfully created: " << outputName << "\n\n";
    return true;
}

int main(int argc, char *argv[])
{
    std::string dllPath = "GFX.dll";
    std::string exePath = "Game.exe";

    std::ifstream testDll(dllPath);
    std::ifstream testExe(exePath);

    if (!testDll)
    {
        std::cerr << "Error: Cannot find " << dllPath << " in current directory\n";
        return 1;
    }
    if (!testExe)
    {
        std::cerr << "Error: Cannot find " << exePath << " in current directory\n";
        return 1;
    }
    testDll.close();
    testExe.close();

    std::cout << "Target files: " << dllPath << ", " << exePath << "\n\n";

    bool gfxSuccess = patchGfxDll(dllPath);
    if (!gfxSuccess)
    {
        std::cerr << "\nPatching GFX.dll failed! Cannot continue.\n";
        return 1;
    }

    bool allGameExeSuccess = true;
    allGameExeSuccess &= patchGameExe(exePath, "1080p", 1920, 1080, "_1080p.exe");
    allGameExeSuccess &= patchGameExe(exePath, "1440p", 2560, 1440, "_1440p.exe");
    allGameExeSuccess &= patchGameExe(exePath, "2160p", 3840, 2160, "_2160p.exe");
    // allGameExeSuccess &= patchGameExe(exePath, "4320p", 7680, 4320, "_4320.exe");

    if (allGameExeSuccess)
    {
        std::cout << "\nPatching completed successfully!\n";
        std::cout << "Run a patched executable (Game_X.exe) directly, or rename it to Game.exe to launch via Steam.\n";
        std::cout << "Visit: https://github.com/brian8544/BlitzkriegPatch for more information.\n";
        return 0;
    }
    else
    {
        std::cerr << "\nGame.exe patching failed!\n";
        return 1;
    }

    return 0;
}