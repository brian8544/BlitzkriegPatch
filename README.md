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
1. Download the latest [release](https://github.com/brian8544/BlitzkriegPatch/releases).
2. Open your Blitzkrieg installation folder, usually:
```text
SteamFolder\steamapps\common\Blitzkrieg Anthology\Blitzkrieg
```
3. Copy the patch DLL into that folder.
4. Launch the game normally through Steam or `Game.exe`.
5. A new window pops up, choose your resolution and press **Save**.
Hold CTRL while launching the game to show the window again.
  
## Building
Build the project as a **Win32/x86 DLL**.
The DLL must export:
```text
GetModuleDescriptor
```
Blitzkrieg automatically scans DLLs in the game directory and calls this export when present.

## License
This project is provided for educational and preservation purposes.
All original game assets and binaries belong to their respective owners.