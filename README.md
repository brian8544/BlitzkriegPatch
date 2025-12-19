# Blitzkrieg 1 – Modern Resolution Patch

This project fixes one of the longest-standing issues in **Blitzkrieg 1**: the hardcoded resolution limit that makes the game difficult to play on modern displays.

Blitzkrieg 1 does not officially support resolutions higher than **1600×1200×32**.

It has always been possible to force higher resolutions by editing `config.cfg` (to for example `2560x1440x32`), but this causes annoying issues;

- The resolution does not appear in the settings menu
- Changing *any* option (such as volume) forces the game back to **640×480**

This patched dll fixes both of them.

---

## What This Patched DLL Does?

- Removes the hardcoded **1600×1200** resolution limit inside `gfx.dll`
- Enables modern resolutions such as:
  - 1920×1080
  - 2560×1440
  - 3840×2160 (4K)
- Prevents the game from resetting to **640×480** when changing options (e.g. volume)
- Works with the **Steam version** of *Blitzkrieg Anthology*

---

## Download (Recommended)

You **do not need to compile anything** if you just want to play.

**Patched DLL available here:**  
https://github.com/brian8544/BlitzkriegPatch/releases

---

## Installation Guide (For Players)

1. Download the patched `gfx.dll` from the **Releases** page
2. Navigate to your Blitzkrieg installation folder: `SteamFolder\steamapps\common\Blitzkrieg Anthology\Blitzkrieg`
3. Replace the original `gfx.dll` with the mine.
4. Set your desired resolution in-game.

## Compiling the Patcher (Optional)

If you want to build the patcher yourself:

- **Compile the application as x86 (32-bit)**

This is important to avoid unknown issues when working with a 32-bit game DLL.

## License

This project is provided for educational and preservation purposes.  
All original game assets and binaries belong to their respective owners.