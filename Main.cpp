/*
    Blitzkrieg 1 binary patcher
    Fixes the hardcoded resolution limits in Blitzkrieg 1 binaries.

    TESTED ON:
    https://store.steampowered.com/app/313480

    IMPORTANT:
    - Stay Windows XP compatible.
    - Many players still run and compile on older machines.
    - Target x86 (32-bit) only to match the original game binaries.

    GUIDE:
	1.) Build this patcher as x86 (32-bit).
	2.) Put 'gfx.dll' and 'game.exe' in the same folder as this patcher.
	3.) Run the patcher.
	4.) It will patch 'GFX.dll' and three versions of 'game_X.exe' for 1080p, 1440p, and 2160p.
	5.) You can either run one of the patched game versions directly or rename 'game_X.exe' back to 'game.exe' to run it through Steam.

    LICENSE:
    https://github.com/nival/Blitzkrieg/blob/main/LICENSE.md
*/

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

    std::cout << "Patching gfx.dll...\n";

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
        std::cerr << "Error: Could not find resolution limits\n";
        return false;
    }

    std::string outputName = filename.substr(0, filename.find_last_of('.')) + "_patched.dll";
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

    std::cout << "Patching game.exe " << name << " version...\n";

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
    std::string dllPath = "gfx.dll";
    std::string exePath = "game.exe";

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

    if (!patchGfxDll(dllPath))
    {
        std::cerr << "\nPatching gfx.dll failed!\n";
        return 1;
    }

    int successCount = 0;

    if (patchGameExe(exePath, "1080p", 1920, 1080, "_patched_1080p.exe"))
        successCount++;
    if (patchGameExe(exePath, "1440p", 2560, 1440, "_patched_1440p.exe"))
        successCount++;
    if (patchGameExe(exePath, "2160p", 3840, 2160, "_patched_2160p.exe"))
        successCount++;

    if (successCount == 3)
    {
        std::cout << "Patching completed successfully!\n";
        std::cout << "You can either run one of the patched game versions directly or rename 'game_X.exe' back to 'game.exe' to run it through Steam.\n";
    }
    else
    {
        std::cerr << "\nPatching failed!\n";
        return 1;
    }

    return 0;
}