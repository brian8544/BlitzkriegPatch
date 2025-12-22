

# Blitzkrieg 1 – Modern Resolution Patch

This project fixes one of the longest-standing issues in **Blitzkrieg 1**: the hardcoded resolution limit that makes the game difficult to play on modern displays.

Blitzkrieg 1 does not officially support resolutions higher than **1600×1200×32**.

It has always been possible to force higher resolutions by editing `config.cfg` (to for example `2560x1440x32`), but this causes annoying issues;

- The resolution does not appear in the settings menu.
- Changing *any* option (such as volume) forces the game back to **640×480**.
- Main menu hardcoded to low resolutions.

![proof ingame](https://i.imgur.com/75vANI8.jpeg)  
![proof menu](https://i.imgur.com/xBQfxua.jpeg)

## Installation Guide (For Players)

1. Download the latest [release](https://github.com/brian8544/BlitzkriegPatch/release).
2. Navigate to your Blitzkrieg installation folder: `SteamFolder\steamapps\common\Blitzkrieg Anthology\Blitzkrieg`.
3. Replace the original files with the mine.
4. Set your desired resolution in-game.
5. [OPTIONAL] Rename the patched game executable back to `Game.exe`, so you can run it through steam.

## Compiling the Patcher (Optional)

If you want to build the patcher yourself:

- **Compile the application as x86 (32-bit)**

This is important to avoid unknown issues when working with a 32-bit binaries.

## License

This project is provided for educational and preservation purposes.  
All original game assets and binaries belong to their respective owners.
