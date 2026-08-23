# Blitzkrieg 1 – Modern Resolution Patch

This fixes many issues regarding Blitzkrieg 1, to make the game playable again on modern displays.

Blitzkrieg 1 does not officially support resolutions higher than **1600×1200×32**.

Although it has always been possible to force higher resolutions by editing config.cfg to for example 2560x1440x32, but this causes annoying issues such as:
- The resolution does not appear in the settings menu.
- Changing *any* option (such as volume) forces the game back to **640×480**.
- Main menu hardcoded to low resolutions.

![proof ingame](https://i.imgur.com/75vANI8.jpeg)  
![proof main menu](https://i.imgur.com/FkElIC4.png)
![proof mission chapter](https://i.imgur.com/1DAAW7A.png)

## Installation Guide (For Players) 
1. Download the latest [release](https://github.com/brian8544/BlitzkriegPatch/releases).
2. In Steam, right click Blitzkrieg 1 > Manage > Browse local files. Open the Blitzkrieg folder.
3. Copy the patch DLL into that folder.
4. Launch the game.
5. A new window pops up, choose your resolution and press **Save**.
> [!TIP]
> Hold CTRL while launching the game to show the window again.
  
## Building
Build the project as a **Win32/x86 DLL**.
The DLL must export:
```text
GetModuleDescriptor
```
Blitzkrieg automatically scans DLLs in the game directory and calls this export when present.

## License
This project is provided for educational and preservation purposes.
All original game assets and binaries belong to [Nival Interactive](http://nival.com/).
