#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <string.h>
#if !defined(_M_IX86)
#error This DLL must be built as Win32/x86.
#endif
#pragma comment(lib, "user32.lib")
#pragma comment(linker, "/EXPORT:GetModuleDescriptor=_GetModuleDescriptor@0")
#define IDC_RES_COMBO 1001
#define IDC_RES_SAVE  1002
#define IDC_RES_CANCEL 1003
#define IDC_RES_DONT_SHOW 1004

struct ResolutionChoice
{
    DWORD width;
    DWORD height;
    char name[64];
};

struct ResolutionDialogState
{
    ResolutionChoice choice;
    HWND combo;
    HWND dontShowCheck;
    bool accepted;
    bool doNotShowAgain;
};

static HMODULE g_self = NULL;

static volatile LONG g_started = 0;
static volatile LONG g_gamePatched = 0;
static volatile LONG g_gfxPatched = 0;
static volatile LONG g_gameTTPatched = 0;
static volatile LONG g_threadStarted = 0;

static ResolutionChoice g_resolution = { 1920, 1080, "1920 x 1080" };

static const char* GAME_CONFIG_FILE_NAME = "config.cfg";
static const char* NO_LAUNCHER_FILE_NAME = "nolauncher.txt";
static const DWORD MAX_RES_WIDTH = 7680;
static const DWORD MAX_RES_HEIGHT = 4320;

static const BYTE PATTERN_GAME_WIDTH_1024[5] = {
    0x68, 0x00, 0x04, 0x00, 0x00
};

static const BYTE PATTERN_GAME_HEIGHT_768[5] = {
    0x68, 0x00, 0x03, 0x00, 0x00
};

static const BYTE PATTERN_GFX_WIDTH_1600[5] = {
    0x68, 0x40, 0x06, 0x00, 0x00
};

static const BYTE PATTERN_GFX_HEIGHT_1200[5] = {
    0x68, 0xB0, 0x04, 0x00, 0x00
};

static const DWORD GFX_MAX_WIDTH = 1000000;
static const DWORD GFX_MAX_HEIGHT = 1000000;

// https://github.com/brian8544/BlitzkriegPatch/issues/2
// Bug: pUIScreen->Reposition(pGFX->GetScreenRect()) sets a layout made for 1024x768.
// UI elements are offset due to our higher resolotuions.
//
// Fix: Hook each GetScreenRect() callsite & after the real call
// returns (EAX -> CTRect<long>{x1,y1,x2,y2}), rewrite the rect in place to
// a 1024x768 box centered in it and continue. Our SCREEN_CENTER_FIXUP does the
// rewrite and InstallScreenCenterHook does the relocate+hook.
struct ScreenCenterSite
{
    const char* name;
    DWORD rvaStart;   // RVA (base 0x10000000) of block start
    DWORD rvaResume;  // RVA right after the GetScreenRect call
    const BYTE* expectedBytes;
    SIZE_T length;
};

static const BYTE SITE_BYTES_MAINMENU[14] = {
    0x8b, 0x43, 0x6c, 0x8d, 0x4c, 0x24, 0x70, 0x51,
    0x50, 0x8b, 0x10, 0xff, 0x52, 0x30
};

static const BYTE SITE_BYTES_CAMPAIGN[17] = {
    0x8b, 0x46, 0x6c, 0x8d, 0x8c, 0x24, 0xcc, 0x00,
    0x00, 0x00, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30
};

// CInterfaceCampaign::StartInterface, 2nd Reposition (after SetDescriptionText).
static const BYTE SITE_BYTES_CAMPAIGN_REFRESH[25] = {
    0x8b, 0x47, 0x6c, 0x8d, 0x8c, 0x24, 0xe0, 0x00,
    0x00, 0x00, 0x51, 0x50, 0x8b, 0x10, 0xc6, 0x84,
    0x24, 0x00, 0x01, 0x00, 0x00, 0x2a, 0xff, 0x52, 0x30
};

// CInterfaceChapter::Create. Load("ui\\common\\chapter") @ RVA 0x391B4.
static const BYTE SITE_BYTES_CHAPTER[14] = {
    0x8b, 0x45, 0x6c, 0x8d, 0x4c, 0x24, 0x2c, 0x51,
    0x50, 0x8b, 0x10, 0xff, 0x52, 0x30
};

// CInterfaceChapter, refresh on mission-button/warehouse state change.
static const BYTE SITE_BYTES_CHAPTER_REFRESH1[14] = {
    0x8b, 0x47, 0x6c, 0x8d, 0x4c, 0x24, 0x4c, 0x51,
    0x50, 0x8b, 0x10, 0xff, 0x52, 0x30
};

// CInterfaceChapter::SetMissionDescription, refresh on mission click.
static const BYTE SITE_BYTES_CHAPTER_REFRESH2[14] = {
    0x8d, 0x54, 0x24, 0x68, 0x52, 0x8b, 0x46, 0x6c,
    0x50, 0x8b, 0x08, 0xff, 0x51, 0x30
};

// CInterfaceAboutMission::Create. Load("ui\\common\\mission") @ RVA 0x408A7.
static const BYTE SITE_BYTES_MISSION[17] = {
    0x8b, 0x46, 0x6c, 0x8d, 0x8c, 0x24, 0x3c, 0x01,
    0x00, 0x00, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30
};

// CInterfaceAboutMission, refresh after objective flags placed on map.
static const BYTE SITE_BYTES_MISSION_REFRESH[17] = {
    0x8d, 0x94, 0x24, 0x3c, 0x01, 0x00, 0x00, 0x52,
    0x8b, 0x46, 0x6c, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30
};

// CInterfaceOptionsSettings::Create. Load("ui\\OptionsSettings" or "ui\\MissionOptionsSettings" depending on AreWeInMission) @ RVA 0x2A71D/0x2A72F.
static const BYTE SITE_BYTES_OPTIONSSETTINGS[14] = {
    0x8b, 0x43, 0x6c, 0x8d, 0x54, 0x24, 0x58, 0x52,
    0x50, 0x8b, 0x08, 0xff, 0x51, 0x30
};

// CInterfaceMPGamesList::Create (LAN/Internet/GameSpy-Galaxy server browser; one screen for all 3 via EMultiplayerConnectionType) @ RVA 0x16AE5.
static const BYTE SITE_BYTES_MPGAMESLIST[14] = {
    0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x3c, 0x51,
    0x50, 0x8b, 0x10, 0xff, 0x52, 0x30
};

// CInterfaceMPGamesList, refresh after the eConnType chatbutton switch
static const BYTE SITE_BYTES_MPGAMESLIST_REFRESH[14] = {
    0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x4c, 0x51,
    0x50, 0x8b, 0x10, 0xff, 0x52, 0x30
};

// CInterfaceMPStartingGame::Create (multiplayer lobby/staging room). Loads ("ui\\MuptiplayerStartingGame") @ RVA 0x199CA.
static const BYTE SITE_BYTES_MPSTARTINGGAME[14] = {
    0x8b, 0x46, 0x6c, 0x8d, 0x54, 0x24, 0x24, 0x8b,
    0x08, 0x52, 0x50, 0xff, 0x51, 0x30
};

static const BYTE SITE_BYTES_MPCHAT[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x54, 0x24, 0x24, 0x52, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30 };
static const BYTE SITE_BYTES_MPCREATEGAME[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x30, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_MPCREATEGAME_REFRESH[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x40, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_MPMAPINVITE[14] = { 0x8b, 0x43, 0x6c, 0x8d, 0x4c, 0x24, 0x4c, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_MPMAPSETTINGS[14] = { 0x8b, 0x47, 0x6c, 0x8d, 0x54, 0x24, 0x2c, 0x52, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30 };
static const BYTE SITE_BYTES_ADDRESSBOOK[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x30, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_ADDRESSBOOK_REFRESH[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x40, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_PLAYERSTATS[14] = { 0x8b, 0x43, 0x6c, 0x8d, 0x54, 0x24, 0x60, 0x52, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30 };
static const BYTE SITE_BYTES_PLAYERSTATS_REFRESH[14] = { 0x8b, 0x43, 0x6c, 0x8d, 0x54, 0x24, 0x20, 0x52, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30 };
static const BYTE SITE_BYTES_UNITSMISSIONPERFORMANCE[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x24, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_UNITSMISSIONPERFORMANCE_REFRESH[22] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x24, 0xc7, 0x44, 0x24, 0x3c, 0xff, 0xff, 0xff, 0xff, 0x8b, 0x10, 0x51, 0x50, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_ADDUNITTOMISSION[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x18, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_ADDUNITTOMISSION_REFRESH[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x18, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_ENCYCLOPEDIA[14] = { 0x8b, 0x47, 0x6c, 0x8d, 0x4c, 0x24, 0x18, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_ENCYCLOPEDIA_REFRESH[14] = { 0x8b, 0x47, 0x6c, 0x8d, 0x4c, 0x24, 0x78, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_STATS[14] = { 0x8b, 0x43, 0x6c, 0x8d, 0x54, 0x24, 0x38, 0x52, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30 };
static const BYTE SITE_BYTES_STATS_REFRESH[14] = { 0x8b, 0x43, 0x6c, 0x8d, 0x54, 0x24, 0x50, 0x52, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30 };
static const BYTE SITE_BYTES_TOTALENCYCLOPEDIA[14] = { 0x8b, 0x43, 0x6c, 0x8d, 0x4c, 0x24, 0x3c, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_TOTALENCYCLOPEDIA_REFRESH[14] = { 0x8b, 0x43, 0x6c, 0x8d, 0x54, 0x24, 0x4c, 0x52, 0x50, 0x8b, 0x08, 0xff, 0x51, 0x30 };
static const BYTE SITE_BYTES_WAREHOUSE[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x24, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };
static const BYTE SITE_BYTES_WAREHOUSE_REFRESH[14] = { 0x8b, 0x46, 0x6c, 0x8d, 0x4c, 0x24, 0x24, 0x51, 0x50, 0x8b, 0x10, 0xff, 0x52, 0x30 };

// MD5: 619e8c342dc609b68384320acbc02a94
static const ScreenCenterSite SITES_STEAM_GOG[] = {
    { "mainmenu",                        0x0003EB48, 0x0003EB56, SITE_BYTES_MAINMENU,                        sizeof(SITE_BYTES_MAINMENU)                        },
    { "campaign",                        0x00035696, 0x000356A7, SITE_BYTES_CAMPAIGN,                        sizeof(SITE_BYTES_CAMPAIGN)                        },
    { "campaign_refresh",                0x00036358, 0x00036371, SITE_BYTES_CAMPAIGN_REFRESH,                sizeof(SITE_BYTES_CAMPAIGN_REFRESH)                },
    { "chapter",                         0x000391BA, 0x000391C8, SITE_BYTES_CHAPTER,                         sizeof(SITE_BYTES_CHAPTER)                         },
    { "chapter_refresh1",                0x00039F56, 0x00039F64, SITE_BYTES_CHAPTER_REFRESH1,                sizeof(SITE_BYTES_CHAPTER_REFRESH1)                },
    { "chapter_refresh2",                0x0003A5FA, 0x0003A608, SITE_BYTES_CHAPTER_REFRESH2,                sizeof(SITE_BYTES_CHAPTER_REFRESH2)                },
    { "mission",                         0x000408AD, 0x000408BE, SITE_BYTES_MISSION,                         sizeof(SITE_BYTES_MISSION)                         },
    { "mission_refresh",                 0x00041085, 0x00041096, SITE_BYTES_MISSION_REFRESH,                 sizeof(SITE_BYTES_MISSION_REFRESH)                 },
    { "optionssettings",                 0x0002A735, 0x0002A743, SITE_BYTES_OPTIONSSETTINGS,                 sizeof(SITE_BYTES_OPTIONSSETTINGS)                 },
    { "mpgameslist",                     0x00016AEB, 0x00016AF9, SITE_BYTES_MPGAMESLIST,                     sizeof(SITE_BYTES_MPGAMESLIST)                     },
    { "mpgameslist_refresh",             0x00016C6D, 0x00016C7B, SITE_BYTES_MPGAMESLIST_REFRESH,             sizeof(SITE_BYTES_MPGAMESLIST_REFRESH)             },
    { "mpstartinggame",                  0x00019A95, 0x00019AA3, SITE_BYTES_MPSTARTINGGAME,                  sizeof(SITE_BYTES_MPSTARTINGGAME)                  },
    { "mpchat",                          0x0001C732, 0x0001C740, SITE_BYTES_MPCHAT,                          sizeof(SITE_BYTES_MPCHAT)                          },
    { "mpcreategame",                    0x00021178, 0x00021186, SITE_BYTES_MPCREATEGAME,                    sizeof(SITE_BYTES_MPCREATEGAME)                    },
    { "mpcreategame_refresh",            0x00021274, 0x00021282, SITE_BYTES_MPCREATEGAME_REFRESH,            sizeof(SITE_BYTES_MPCREATEGAME_REFRESH)            },
    { "mpmapinvite",                     0x00023F90, 0x00023F9E, SITE_BYTES_MPMAPINVITE,                     sizeof(SITE_BYTES_MPMAPINVITE)                     },
    { "mpmapsettings",                   0x00024E98, 0x00024EA6, SITE_BYTES_MPMAPSETTINGS,                   sizeof(SITE_BYTES_MPMAPSETTINGS)                   },
    { "addressbook",                     0x00026732, 0x00026740, SITE_BYTES_ADDRESSBOOK,                     sizeof(SITE_BYTES_ADDRESSBOOK)                     },
    { "addressbook_refresh",             0x00026797, 0x000267A5, SITE_BYTES_ADDRESSBOOK_REFRESH,             sizeof(SITE_BYTES_ADDRESSBOOK_REFRESH)             },
    { "playerstats",                     0x00028FFE, 0x0002900C, SITE_BYTES_PLAYERSTATS,                     sizeof(SITE_BYTES_PLAYERSTATS)                     },
    { "playerstats_refresh",             0x0002976E, 0x0002977C, SITE_BYTES_PLAYERSTATS_REFRESH,             sizeof(SITE_BYTES_PLAYERSTATS_REFRESH)             },
    { "unitsmissionperformance",         0x0003196D, 0x0003197B, SITE_BYTES_UNITSMISSIONPERFORMANCE,         sizeof(SITE_BYTES_UNITSMISSIONPERFORMANCE)         },
    { "unitsmissionperformance_refresh", 0x00031A1B, 0x00031A31, SITE_BYTES_UNITSMISSIONPERFORMANCE_REFRESH, sizeof(SITE_BYTES_UNITSMISSIONPERFORMANCE_REFRESH) },
    { "addunittomission",                0x00034CBD, 0x00034CCB, SITE_BYTES_ADDUNITTOMISSION,                sizeof(SITE_BYTES_ADDUNITTOMISSION)                },
    { "addunittomission_refresh",        0x00034CFE, 0x00034D0C, SITE_BYTES_ADDUNITTOMISSION_REFRESH,        sizeof(SITE_BYTES_ADDUNITTOMISSION_REFRESH)        },
    { "encyclopedia",                    0x0003CDC3, 0x0003CDD1, SITE_BYTES_ENCYCLOPEDIA,                    sizeof(SITE_BYTES_ENCYCLOPEDIA)                    },
    { "encyclopedia_refresh",            0x0003D0A9, 0x0003D0B7, SITE_BYTES_ENCYCLOPEDIA_REFRESH,            sizeof(SITE_BYTES_ENCYCLOPEDIA_REFRESH)            },
    { "stats",                           0x00045505, 0x00045513, SITE_BYTES_STATS,                           sizeof(SITE_BYTES_STATS)                           },
    { "stats_refresh",                   0x000456F1, 0x000456FF, SITE_BYTES_STATS_REFRESH,                   sizeof(SITE_BYTES_STATS_REFRESH)                   },
    { "totalencyclopedia",               0x0004A0B0, 0x0004A0BE, SITE_BYTES_TOTALENCYCLOPEDIA,               sizeof(SITE_BYTES_TOTALENCYCLOPEDIA)               },
    { "totalencyclopedia_refresh",       0x0004A1D8, 0x0004A1E6, SITE_BYTES_TOTALENCYCLOPEDIA_REFRESH,       sizeof(SITE_BYTES_TOTALENCYCLOPEDIA_REFRESH)       },
    { "warehouse",                       0x0004C309, 0x0004C317, SITE_BYTES_WAREHOUSE,                       sizeof(SITE_BYTES_WAREHOUSE)                       },
    { "warehouse_refresh",               0x0004C3A4, 0x0004C3B2, SITE_BYTES_WAREHOUSE_REFRESH,               sizeof(SITE_BYTES_WAREHOUSE_REFRESH)               },
};

// MD5: aba28ef985ea0249db5a700d5c1b3129
// Missing: mpgameslist and mpmapinvite?
static const ScreenCenterSite SITES_RETAIL_2003[] = {
    { "mainmenu",                        0x0003d368, 0x0003d376, SITE_BYTES_MAINMENU,                        sizeof(SITE_BYTES_MAINMENU)                        },
    { "campaign",                        0x00033ec6, 0x00033ed7, SITE_BYTES_CAMPAIGN,                        sizeof(SITE_BYTES_CAMPAIGN)                        },
    { "campaign_refresh",                0x00034b88, 0x00034ba1, SITE_BYTES_CAMPAIGN_REFRESH,                sizeof(SITE_BYTES_CAMPAIGN_REFRESH)                },
    { "chapter",                         0x000379ba, 0x000379c8, SITE_BYTES_CHAPTER,                         sizeof(SITE_BYTES_CHAPTER)                         },
    { "chapter_refresh1",                0x00038756, 0x00038764, SITE_BYTES_CHAPTER_REFRESH1,                sizeof(SITE_BYTES_CHAPTER_REFRESH1)                },
    { "chapter_refresh2",                0x00038dfa, 0x00038e08, SITE_BYTES_CHAPTER_REFRESH2,                sizeof(SITE_BYTES_CHAPTER_REFRESH2)                },
    { "mission",                         0x0003f0cd, 0x0003f0de, SITE_BYTES_MISSION,                         sizeof(SITE_BYTES_MISSION)                         },
    { "mission_refresh",                 0x0003f8a5, 0x0003f8b6, SITE_BYTES_MISSION_REFRESH,                 sizeof(SITE_BYTES_MISSION_REFRESH)                 },
    { "optionssettings",                 0x00028f25, 0x00028f33, SITE_BYTES_OPTIONSSETTINGS,                 sizeof(SITE_BYTES_OPTIONSSETTINGS)                 },
    { "mpgameslist_refresh",             0x000b5d7a, 0x000b5d88, SITE_BYTES_MPGAMESLIST_REFRESH,             sizeof(SITE_BYTES_MPGAMESLIST_REFRESH)             },
    { "mpstartinggame",                  0x00019ce5, 0x00019cf3, SITE_BYTES_MPSTARTINGGAME,                  sizeof(SITE_BYTES_MPSTARTINGGAME)                  },
    { "mpchat",                          0x0001c842, 0x0001c850, SITE_BYTES_MPCHAT,                          sizeof(SITE_BYTES_MPCHAT)                          },
    { "mpcreategame",                    0x000212a8, 0x000212b6, SITE_BYTES_MPCREATEGAME,                    sizeof(SITE_BYTES_MPCREATEGAME)                    },
    { "mpcreategame_refresh",            0x000213a4, 0x000213b2, SITE_BYTES_MPCREATEGAME_REFRESH,            sizeof(SITE_BYTES_MPCREATEGAME_REFRESH)            },
    { "mpmapsettings",                   0x00023eb8, 0x00023ec6, SITE_BYTES_MPMAPSETTINGS,                   sizeof(SITE_BYTES_MPMAPSETTINGS)                   },
    { "addressbook",                     0x00025752, 0x00025760, SITE_BYTES_ADDRESSBOOK,                     sizeof(SITE_BYTES_ADDRESSBOOK)                     },
    { "addressbook_refresh",             0x000257b7, 0x000257c5, SITE_BYTES_ADDRESSBOOK_REFRESH,             sizeof(SITE_BYTES_ADDRESSBOOK_REFRESH)             },
    { "playerstats",                     0x000277ee, 0x000277fc, SITE_BYTES_PLAYERSTATS,                     sizeof(SITE_BYTES_PLAYERSTATS)                     },
    { "playerstats_refresh",             0x00027f5e, 0x00027f6c, SITE_BYTES_PLAYERSTATS_REFRESH,             sizeof(SITE_BYTES_PLAYERSTATS_REFRESH)             },
    { "unitsmissionperformance",         0x000301bd, 0x000301cb, SITE_BYTES_UNITSMISSIONPERFORMANCE,         sizeof(SITE_BYTES_UNITSMISSIONPERFORMANCE)         },
    { "unitsmissionperformance_refresh", 0x0003026b, 0x00030281, SITE_BYTES_UNITSMISSIONPERFORMANCE_REFRESH, sizeof(SITE_BYTES_UNITSMISSIONPERFORMANCE_REFRESH) },
    { "addunittomission",                0x000334ed, 0x000334fb, SITE_BYTES_ADDUNITTOMISSION,                sizeof(SITE_BYTES_ADDUNITTOMISSION)                },
    { "addunittomission_refresh",        0x0003352e, 0x0003353c, SITE_BYTES_ADDUNITTOMISSION_REFRESH,        sizeof(SITE_BYTES_ADDUNITTOMISSION_REFRESH)        },
    { "encyclopedia",                    0x0003b5e3, 0x0003b5f1, SITE_BYTES_ENCYCLOPEDIA,                    sizeof(SITE_BYTES_ENCYCLOPEDIA)                    },
    { "encyclopedia_refresh",            0x0003b8c9, 0x0003b8d7, SITE_BYTES_ENCYCLOPEDIA_REFRESH,            sizeof(SITE_BYTES_ENCYCLOPEDIA_REFRESH)            },
    { "stats",                           0x00043ce5, 0x00043cf3, SITE_BYTES_STATS,                           sizeof(SITE_BYTES_STATS)                           },
    { "stats_refresh",                   0x00043ed1, 0x00043edf, SITE_BYTES_STATS_REFRESH,                   sizeof(SITE_BYTES_STATS_REFRESH)                   },
    { "totalencyclopedia",               0x00048a00, 0x00048a0e, SITE_BYTES_TOTALENCYCLOPEDIA,               sizeof(SITE_BYTES_TOTALENCYCLOPEDIA)               },
    { "totalencyclopedia_refresh",       0x00048b28, 0x00048b36, SITE_BYTES_TOTALENCYCLOPEDIA_REFRESH,       sizeof(SITE_BYTES_TOTALENCYCLOPEDIA_REFRESH)       },
    { "warehouse",                       0x0004abe9, 0x0004abf7, SITE_BYTES_WAREHOUSE,                       sizeof(SITE_BYTES_WAREHOUSE)                       },
    { "warehouse_refresh",               0x0004ac84, 0x0004ac92, SITE_BYTES_WAREHOUSE_REFRESH,               sizeof(SITE_BYTES_WAREHOUSE_REFRESH)               },
};

// Fix: EAX -> CTRect<long>{x1,y1,x2,y2}. Centers a 1024x768 box in place. ECX/EDX scratching causwe original code always reloads them fresh afterwards anyway.
static const BYTE SCREEN_CENTER_FIXUP[55] = {
    0x8b, 0x48, 0x08, 0x2b, 0x08, 0x81, 0xe9, 0x00, 0x04, 0x00, 0x00,
    0xd1, 0xf9, 0x01, 0x08, 0x8b, 0x10, 0x81, 0xc2, 0x00, 0x04, 0x00,
    0x00, 0x89, 0x50, 0x08, 0x8b, 0x48, 0x0c, 0x2b, 0x48, 0x04, 0x81,
    0xe9, 0x00, 0x03, 0x00, 0x00, 0xd1, 0xf9, 0x01, 0x48, 0x04, 0x8b,
    0x50, 0x04, 0x81, 0xc2, 0x00, 0x03, 0x00, 0x00, 0x89, 0x50, 0x0c,
};

static ResolutionChoice g_resolutionList[256];
static int g_resolutionCount = 0;



static ResolutionChoice MakeResolution(DWORD w, DWORD h)
{
    ResolutionChoice r;
    r.width = w;
    r.height = h;
    wsprintfA(r.name, "%lu x %lu", w, h);
    return r;
}

static bool IsResolutionAllowed(DWORD w, DWORD h)
{
    if (w == 0 || h == 0)
        return false;

    if (w > MAX_RES_WIDTH || h > MAX_RES_HEIGHT)
        return false;

    if (w < 640 || h < 480)
        return false;

    return true;
}

static bool IsDisplayModeSupported(DWORD w, DWORD h)
{
    if (!IsResolutionAllowed(w, h))
        return false;

    DEVMODEA dm;
    DWORD mode = 0;

    while (true)
    {
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);

        if (!EnumDisplaySettingsA(NULL, mode, &dm))
            break;

        if (dm.dmPelsWidth == w && dm.dmPelsHeight == h)
            return true;

        ++mode;
    }

    DWORD desktopW = 0;
    DWORD desktopH = 0;

    DEVMODEA current;
    ZeroMemory(&current, sizeof(current));
    current.dmSize = sizeof(current);

    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &current))
    {
        desktopW = current.dmPelsWidth;
        desktopH = current.dmPelsHeight;
    }
    else
    {
        desktopW = (DWORD)GetSystemMetrics(SM_CXSCREEN);
        desktopH = (DWORD)GetSystemMetrics(SM_CYSCREEN);
    }

    return desktopW == w && desktopH == h;
}

static void GetDesktopResolution(DWORD* outW, DWORD* outH)
{
    DWORD w = 1920;
    DWORD h = 1080;

    DEVMODEA dm;
    ZeroMemory(&dm, sizeof(dm));
    dm.dmSize = sizeof(dm);

    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &dm))
    {
        if (dm.dmPelsWidth > 0 && dm.dmPelsHeight > 0)
        {
            w = dm.dmPelsWidth;
            h = dm.dmPelsHeight;
        }
    }
    else
    {
        int sw = GetSystemMetrics(SM_CXSCREEN);
        int sh = GetSystemMetrics(SM_CYSCREEN);

        if (sw > 0 && sh > 0)
        {
            w = (DWORD)sw;
            h = (DWORD)sh;
        }
    }

    if (!IsResolutionAllowed(w, h))
    {
        if (w > MAX_RES_WIDTH)
            w = MAX_RES_WIDTH;

        if (h > MAX_RES_HEIGHT)
            h = MAX_RES_HEIGHT;

        if (!IsResolutionAllowed(w, h))
        {
            w = 1920;
            h = 1080;
        }
    }

    if (outW)
        *outW = w;

    if (outH)
        *outH = h;
}

static bool SameResolution(const ResolutionChoice& a, DWORD w, DWORD h)
{
    return a.width == w && a.height == h;
}

static bool ResolutionExists(DWORD w, DWORD h)
{
    for (int i = 0; i < g_resolutionCount; ++i)
    {
        if (SameResolution(g_resolutionList[i], w, h))
            return true;
    }

    return false;
}

static void AddResolution(DWORD w, DWORD h)
{
    if (!IsResolutionAllowed(w, h))
        return;

    if (ResolutionExists(w, h))
        return;

    if (g_resolutionCount >= (int)(sizeof(g_resolutionList) / sizeof(g_resolutionList[0])))
        return;

    g_resolutionList[g_resolutionCount++] = MakeResolution(w, h);
}

static void SortResolutions()
{
    for (int i = 0; i < g_resolutionCount - 1; ++i)
    {
        for (int j = i + 1; j < g_resolutionCount; ++j)
        {
            DWORD areaI = g_resolutionList[i].width * g_resolutionList[i].height;
            DWORD areaJ = g_resolutionList[j].width * g_resolutionList[j].height;

            bool swapNeeded = false;

            if (areaJ < areaI)
                swapNeeded = true;
            else if (areaJ == areaI && g_resolutionList[j].width < g_resolutionList[i].width)
                swapNeeded = true;

            if (swapNeeded)
            {
                ResolutionChoice tmp = g_resolutionList[i];
                g_resolutionList[i] = g_resolutionList[j];
                g_resolutionList[j] = tmp;
            }
        }
    }
}

static void BuildResolutionList()
{
    g_resolutionCount = 0;

    DWORD desktopW = 1920;
    DWORD desktopH = 1080;
    GetDesktopResolution(&desktopW, &desktopH);

    DEVMODEA dm;
    DWORD mode = 0;

    while (true)
    {
        ZeroMemory(&dm, sizeof(dm));
        dm.dmSize = sizeof(dm);

        if (!EnumDisplaySettingsA(NULL, mode, &dm))
            break;

        AddResolution(dm.dmPelsWidth, dm.dmPelsHeight);
        ++mode;
    }

    AddResolution(desktopW, desktopH);
    SortResolutions();
}

static int FindResolutionIndex(DWORD w, DWORD h)
{
    for (int i = 0; i < g_resolutionCount; ++i)
    {
        if (SameResolution(g_resolutionList[i], w, h))
            return i;
    }

    return -1;
}

static bool IsCtrlPressed()
{
    SHORT ctrl = GetAsyncKeyState(VK_CONTROL);
    SHORT left = GetAsyncKeyState(VK_LCONTROL);
    SHORT right = GetAsyncKeyState(VK_RCONTROL);

    return ((ctrl | left | right) & 0x8000) != 0;
}

static void GetGameDirectory(char* outPath, DWORD outSize)
{
    if (!outPath || outSize == 0)
        return;

    outPath[0] = '\0';

    DWORD len = GetModuleFileNameA(NULL, outPath, outSize);

    if (len == 0 || len >= outSize)
    {
        outPath[0] = '\0';
        return;
    }

    for (DWORD i = len; i > 0; --i)
    {
        if (outPath[i - 1] == '\\' || outPath[i - 1] == '/')
        {
            outPath[i] = '\0';
            return;
        }
    }

    outPath[0] = '\0';
}

static void BuildPath(char* outPath, DWORD outSize, const char* fileName)
{
    if (!outPath || outSize == 0)
        return;

    GetGameDirectory(outPath, outSize);

    if ((DWORD)(lstrlenA(outPath) + lstrlenA(fileName) + 1) < outSize)
        lstrcatA(outPath, fileName);
}


static bool FileExistsA_Local(const char* fileName)
{
    char path[MAX_PATH];
    BuildPath(path, MAX_PATH, fileName);

    DWORD attrs = GetFileAttributesA(path);
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

static bool WriteEmptyFileA_Local(const char* fileName)
{
    char path[MAX_PATH];
    BuildPath(path, MAX_PATH, fileName);

    HANDLE file = CreateFileA(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    CloseHandle(file);
    return true;
}

static void DeleteFileA_Local(const char* fileName)
{
    char path[MAX_PATH];
    BuildPath(path, MAX_PATH, fileName);
    DeleteFileA(path);
}

static bool IsSupportedResolution(DWORD w, DWORD h, ResolutionChoice* out)
{
    if (!IsDisplayModeSupported(w, h))
        return false;

    if (out)
        *out = MakeResolution(w, h);

    return true;
}

static void FormatModeString(const ResolutionChoice& r, char* out, DWORD outSize)
{
    if (!out || outSize == 0)
        return;

    out[0] = '\0';
    wsprintfA(out, "%lux%lux32", r.width, r.height);
}

static bool IsBufferEmptyOrWhitespace(const char* data, DWORD size)
{
    if (!data || size == 0)
        return true;

    for (DWORD i = 0; i < size; ++i)
    {
        char c = data[i];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '\0')
            return false;
    }

    return true;
}

static char* ReadWholeFileA(const char* path, DWORD* outSize)
{
    if (outSize)
        *outSize = 0;

    HANDLE file = CreateFileA(
        path,
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file == INVALID_HANDLE_VALUE)
        return NULL;

    DWORD size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE)
    {
        CloseHandle(file);
        return NULL;
    }

    char* data = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size + 1);
    if (!data)
    {
        CloseHandle(file);
        return NULL;
    }

    DWORD read = 0;
    BOOL ok = TRUE;

    if (size > 0)
        ok = ReadFile(file, data, size, &read, NULL);

    CloseHandle(file);

    if (!ok)
    {
        HeapFree(GetProcessHeap(), 0, data);
        return NULL;
    }

    data[read] = '\0';

    if (outSize)
        *outSize = read;

    return data;
}

static bool WriteWholeFileA(const char* path, const char* data, DWORD size)
{
    HANDLE file = CreateFileA(
        path,
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (file == INVALID_HANDLE_VALUE)
        return false;

    DWORD written = 0;
    BOOL ok = TRUE;

    if (size > 0)
        ok = WriteFile(file, data, size, &written, NULL);

    CloseHandle(file);

    return ok && written == size;
}

static bool ParseModeString(const char* text, DWORD* outW, DWORD* outH)
{
    if (!text)
        return false;

    DWORD w = 0;
    DWORD h = 0;

    const char* p = text;

    while (*p >= '0' && *p <= '9')
    {
        w = (w * 10) + (DWORD)(*p - '0');
        ++p;
    }

    if (*p != 'x' && *p != 'X')
        return false;

    ++p;

    while (*p >= '0' && *p <= '9')
    {
        h = (h * 10) + (DWORD)(*p - '0');
        ++p;
    }

    if (!IsResolutionAllowed(w, h))
        return false;

    if (outW)
        *outW = w;

    if (outH)
        *outH = h;

    return true;
}

static char* FindLastItemStartBefore(char* base, char* before)
{
    if (!base || !before || before <= base)
        return NULL;

    char* result = NULL;

    for (char* p = base; p + 5 <= before; ++p)
    {
        if (strncmp(p, "<item", 5) == 0)
            result = p;
    }

    return result;
}

static bool FindGfxModeValueRange(char* data, char** outValueStart, char** outValueEnd)
{
    if (outValueStart)
        *outValueStart = NULL;

    if (outValueEnd)
        *outValueEnd = NULL;

    if (!data)
        return false;

    char* key = strstr(data, "<KeyName>GFX.Mode</KeyName>");
    if (!key)
        return false;

    char* itemStart = FindLastItemStartBefore(data, key);
    if (!itemStart)
        return false;

    char* itemEnd = strstr(key, "</item>");
    if (!itemEnd)
        return false;

    char* varOpen = strstr(itemStart, "<Var>");
    if (!varOpen || varOpen > itemEnd)
        return false;

    varOpen += 5;

    char* varClose = strstr(varOpen, "</Var>");
    if (!varClose || varClose > itemEnd)
        return false;

    if (outValueStart)
        *outValueStart = varOpen;

    if (outValueEnd)
        *outValueEnd = varClose;

    return true;
}

static void BuildGfxModeItemText(const ResolutionChoice& r, char* out, DWORD outSize)
{
    if (!out || outSize == 0)
        return;

    char mode[64];
    FormatModeString(r, mode, sizeof(mode));

    wsprintfA(
        out,
        "<item EditorType=\"3\" Flags=\"49\" Order=\"1\" Type=\"8\" InstantApply=\"1\">"
        "<Var>%s</Var>"
        "<Action>SetVideoMode</Action>"
        "<ActionFill>GetVideoModes</ActionFill>"
        "<Default Type=\"8\"><Var>1024x768x32</Var></Default>"
        "<KeyName>GFX.Mode</KeyName>"
        "</item>",
        mode
    );
}

static bool WriteMinimalGameConfig(const ResolutionChoice& r)
{
    char path[MAX_PATH];
    BuildPath(path, MAX_PATH, GAME_CONFIG_FILE_NAME);

    char item[1024];
    BuildGfxModeItemText(r, item, sizeof(item));

    char xml[2048];
    wsprintfA(
        xml,
        "<?xml version=\"1.0\"?>\r\n"
        "<base><Options><Vars>%s</Vars></Options></base>\r\n",
        item
    );

    return WriteWholeFileA(path, xml, (DWORD)lstrlenA(xml));
}

static bool LoadResolutionFromGameConfig(ResolutionChoice* out)
{
    char path[MAX_PATH];
    BuildPath(path, MAX_PATH, GAME_CONFIG_FILE_NAME);

    DWORD size = 0;
    char* data = ReadWholeFileA(path, &size);
    if (!data)
        return false;

    char* valueStart = NULL;
    char* valueEnd = NULL;
    bool result = false;

    if (FindGfxModeValueRange(data, &valueStart, &valueEnd))
    {
        char oldChar = *valueEnd;
        *valueEnd = '\0';

        DWORD w = 0;
        DWORD h = 0;

        if (ParseModeString(valueStart, &w, &h))
        {
            if (out)
                *out = MakeResolution(w, h);

            result = true;
        }

        *valueEnd = oldChar;
    }

    HeapFree(GetProcessHeap(), 0, data);
    return result;
}

static bool SaveResolutionToGameConfig(const ResolutionChoice& r)
{
    char path[MAX_PATH];
    BuildPath(path, MAX_PATH, GAME_CONFIG_FILE_NAME);

    DWORD size = 0;
    char* data = ReadWholeFileA(path, &size);

    if (!data || IsBufferEmptyOrWhitespace(data, size))
    {
        if (data)
            HeapFree(GetProcessHeap(), 0, data);

        return WriteMinimalGameConfig(r);
    }

    char mode[64];
    FormatModeString(r, mode, sizeof(mode));
    DWORD modeLen = (DWORD)lstrlenA(mode);

    char* valueStart = NULL;
    char* valueEnd = NULL;

    if (FindGfxModeValueRange(data, &valueStart, &valueEnd))
    {
        DWORD prefixLen = (DWORD)(valueStart - data);
        DWORD suffixLen = size - (DWORD)(valueEnd - data);
        DWORD newSize = prefixLen + modeLen + suffixLen;

        char* output = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newSize + 1);
        if (!output)
        {
            HeapFree(GetProcessHeap(), 0, data);
            return false;
        }

        CopyMemory(output, data, prefixLen);
        CopyMemory(output + prefixLen, mode, modeLen);
        CopyMemory(output + prefixLen + modeLen, valueEnd, suffixLen);
        output[newSize] = '\0';

        bool ok = WriteWholeFileA(path, output, newSize);

        HeapFree(GetProcessHeap(), 0, output);
        HeapFree(GetProcessHeap(), 0, data);

        return ok;
    }

    char item[1024];
    BuildGfxModeItemText(r, item, sizeof(item));
    DWORD itemLen = (DWORD)lstrlenA(item);

    char* varsClose = strstr(data, "</Vars>");
    if (varsClose)
    {
        DWORD prefixLen = (DWORD)(varsClose - data);
        DWORD suffixLen = size - prefixLen;
        DWORD newSize = prefixLen + itemLen + suffixLen;

        char* output = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, newSize + 1);
        if (!output)
        {
            HeapFree(GetProcessHeap(), 0, data);
            return false;
        }

        CopyMemory(output, data, prefixLen);
        CopyMemory(output + prefixLen, item, itemLen);
        CopyMemory(output + prefixLen + itemLen, varsClose, suffixLen);
        output[newSize] = '\0';

        bool ok = WriteWholeFileA(path, output, newSize);

        HeapFree(GetProcessHeap(), 0, output);
        HeapFree(GetProcessHeap(), 0, data);

        return ok;
    }

    HeapFree(GetProcessHeap(), 0, data);

    return WriteMinimalGameConfig(r);
}

static void CenterWindowOnScreen(HWND hwnd)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    int x = (screenW - width) / 2;
    int y = (screenH - height) / 2;

    SetWindowPos(
        hwnd,
        HWND_TOPMOST,
        x,
        y,
        0,
        0,
        SWP_NOSIZE | SWP_SHOWWINDOW
    );
}

static void ApplySelectedResolution(ResolutionDialogState* state)
{
    if (!state || !state->combo)
        return;

    int index = (int)SendMessageA(state->combo, CB_GETCURSEL, 0, 0);

    if (index >= 0 && index < g_resolutionCount)
        state->choice = g_resolutionList[index];
}

static LRESULT CALLBACK ResolutionDialogProc(
    HWND hwnd,
    UINT msg,
    WPARAM wParam,
    LPARAM lParam
)
{
    ResolutionDialogState* state =
        reinterpret_cast<ResolutionDialogState*>(
            GetWindowLongPtrA(hwnd, GWLP_USERDATA)
            );

    switch (msg)
    {
    case WM_CREATE:
    {
        CREATESTRUCTA* cs = reinterpret_cast<CREATESTRUCTA*>(lParam);
        state = reinterpret_cast<ResolutionDialogState*>(cs->lpCreateParams);

        SetWindowLongPtrA(
            hwnd,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(state)
        );

        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

        HWND title = CreateWindowExA(
            0,
            "STATIC",
            "Choose the resolution used by Blitzkrieg:",
            WS_CHILD | WS_VISIBLE,
            20,
            18,
            430,
            20,
            hwnd,
            NULL,
            g_self,
            NULL
        );

        SendMessageA(title, WM_SETFONT, (WPARAM)font, TRUE);

        HWND combo = CreateWindowExA(
            WS_EX_CLIENTEDGE,
            "COMBOBOX",
            "",
            WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
            20,
            55,
            450,
            360,
            hwnd,
            (HMENU)IDC_RES_COMBO,
            g_self,
            NULL
        );

        SendMessageA(combo, WM_SETFONT, (WPARAM)font, TRUE);

        for (int i = 0; i < g_resolutionCount; ++i)
        {
            SendMessageA(combo, CB_ADDSTRING, 0, (LPARAM)g_resolutionList[i].name);
        }

        int selectedIndex = FindResolutionIndex(state->choice.width, state->choice.height);

        if (selectedIndex < 0)
            selectedIndex = FindResolutionIndex(1920, 1080);

        if (selectedIndex < 0)
            selectedIndex = 0;

        SendMessageA(combo, CB_SETCURSEL, selectedIndex, 0);

        state->combo = combo;

        HWND ctrlHint = CreateWindowExA(
            0,
            "STATIC",
            "Hold CTRL while launching through Steam or executable to show this window again.",
            WS_CHILD | WS_VISIBLE,
            20,
            88,
            450,
            20,
            hwnd,
            NULL,
            g_self,
            NULL
        );

        SendMessageA(ctrlHint, WM_SETFONT, (WPARAM)font, TRUE);

        HWND check = CreateWindowExA(
            0,
            "BUTTON",
            "Do not show this window again",
            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
            20,
            118,
            260,
            22,
            hwnd,
            (HMENU)IDC_RES_DONT_SHOW,
            g_self,
            NULL
        );

        SendMessageA(check, WM_SETFONT, (WPARAM)font, TRUE);
        SendMessageA(check, BM_SETCHECK, BST_CHECKED, 0);
        state->dontShowCheck = check;
        state->doNotShowAgain = true;

        HWND saveButton = CreateWindowExA(
            0,
            "BUTTON",
            "Save",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            270,
            150,
            95,
            28,
            hwnd,
            (HMENU)IDC_RES_SAVE,
            g_self,
            NULL
        );

        SendMessageA(saveButton, WM_SETFONT, (WPARAM)font, TRUE);

        HWND cancelButton = CreateWindowExA(
            0,
            "BUTTON",
            "Cancel",
            WS_CHILD | WS_VISIBLE,
            375,
            150,
            95,
            28,
            hwnd,
            (HMENU)IDC_RES_CANCEL,
            g_self,
            NULL
        );

        SendMessageA(cancelButton, WM_SETFONT, (WPARAM)font, TRUE);

        CenterWindowOnScreen(hwnd);
        SetFocus(combo);

        return 0;
    }

    case WM_COMMAND:
    {
        WORD controlId = LOWORD(wParam);
        WORD notifyCode = HIWORD(wParam);

        if (controlId == IDC_RES_SAVE && notifyCode == BN_CLICKED)
        {
            ApplySelectedResolution(state);

            if (state)
            {
                state->accepted = true;
                state->doNotShowAgain =
                    state->dontShowCheck &&
                    SendMessageA(state->dontShowCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            }

            DestroyWindow(hwnd);
            return 0;
        }

        if (controlId == IDC_RES_CANCEL && notifyCode == BN_CLICKED)
        {
            if (state)
                state->accepted = false;

            DestroyWindow(hwnd);
            return 0;
        }

        return 0;
    }

    case WM_CLOSE:
        if (state)
            state->accepted = false;

        DestroyWindow(hwnd);
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE)
        {
            if (state)
                state->accepted = false;

            DestroyWindow(hwnd);
            return 0;
        }

        if (wParam == VK_RETURN)
        {
            ApplySelectedResolution(state);

            if (state)
            {
                state->accepted = true;
                state->doNotShowAgain =
                    state->dontShowCheck &&
                    SendMessageA(state->dontShowCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
            }

            DestroyWindow(hwnd);
            return 0;
        }

        break;
    }

    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

static bool AskUserForResolutionWithSaveFlag(
    const ResolutionChoice& initialChoice,
    ResolutionChoice* outChoice,
    bool* outAccepted,
    bool* outDoNotShowAgain
)
{
    BuildResolutionList();

    ResolutionDialogState state;
    state.choice = initialChoice;
    state.combo = NULL;
    state.dontShowCheck = NULL;
    state.accepted = false;
    state.doNotShowAgain = true;

    const char* className = "BKResolutionPatchResolutionDialog";

    WNDCLASSEXA wc;
    ZeroMemory(&wc, sizeof(wc));

    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = ResolutionDialogProc;
    wc.hInstance = g_self;
    wc.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(32512));
    wc.hIcon = LoadIconA(NULL, MAKEINTRESOURCEA(32512));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = className;

    RegisterClassExA(&wc);

    HWND hwnd = CreateWindowExA(
        WS_EX_TOPMOST | WS_EX_DLGMODALFRAME,
        className,
        "Blitzkrieg Resolution Patch",
        WS_POPUP | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        510,
        230,
        NULL,
        NULL,
        g_self,
        &state
    );

    if (hwnd)
    {
        ShowWindow(hwnd, SW_SHOW);
        UpdateWindow(hwnd);
        SetForegroundWindow(hwnd);

        MSG message;

        while (IsWindow(hwnd))
        {
            while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE))
            {
                if (!IsDialogMessageA(hwnd, &message))
                {
                    TranslateMessage(&message);
                    DispatchMessageA(&message);
                }
            }

            if (IsWindow(hwnd))
                WaitMessage();
        }
    }

    if (outChoice)
        *outChoice = state.choice;

    if (outAccepted)
        *outAccepted = state.accepted;

    if (outDoNotShowAgain)
        *outDoNotShowAgain = state.doNotShowAgain;

    return true;
}

static void LoadOrAskResolution()
{
    ResolutionChoice loaded;
    bool hasConfigResolution = LoadResolutionFromGameConfig(&loaded);
    bool forceDialog = IsCtrlPressed();
    bool skipDialog = FileExistsA_Local(NO_LAUNCHER_FILE_NAME);

    if (!forceDialog && skipDialog && hasConfigResolution)
    {
        g_resolution = loaded;
        return;
    }

    if (hasConfigResolution)
    {
        g_resolution = loaded;
    }
    else
    {
        DWORD desktopW = 1920;
        DWORD desktopH = 1080;
        GetDesktopResolution(&desktopW, &desktopH);
        g_resolution = MakeResolution(desktopW, desktopH);
    }

    bool accepted = false;
    bool doNotShowAgain = true;

    AskUserForResolutionWithSaveFlag(
        g_resolution,
        &g_resolution,
        &accepted,
        &doNotShowAgain
    );

    if (!accepted)
        ExitProcess(0);

    bool configSaved = SaveResolutionToGameConfig(g_resolution);

    if (!configSaved)
    {
        MessageBoxA(
            NULL,
            "Resolution was selected, but config.cfg could not be updated.\n\n"
            "Try checking folder permissions.",
            "Blitzkrieg Resolution Patch",
            MB_OK | MB_ICONWARNING | MB_TOPMOST
        );
        ExitProcess(0);
    }

    if (doNotShowAgain)
        WriteEmptyFileA_Local(NO_LAUNCHER_FILE_NAME);
    else
        DeleteFileA_Local(NO_LAUNCHER_FILE_NAME);
}

static bool BytesEqual(const BYTE* a, const BYTE* b, SIZE_T count)
{
    for (SIZE_T i = 0; i < count; ++i)
    {
        if (a[i] != b[i])
            return false;
    }

    return true;
}

static bool GetModuleHeaders(HMODULE module, BYTE** outBase, PIMAGE_NT_HEADERS32* outNt)
{
    if (!module || !outBase || !outNt)
        return false;

    BYTE* base = reinterpret_cast<BYTE*>(module);

    __try
    {
        PIMAGE_DOS_HEADER dos = reinterpret_cast<PIMAGE_DOS_HEADER>(base);

        if (dos->e_magic != IMAGE_DOS_SIGNATURE)
            return false;

        PIMAGE_NT_HEADERS32 nt =
            reinterpret_cast<PIMAGE_NT_HEADERS32>(base + dos->e_lfanew);

        if (nt->Signature != IMAGE_NT_SIGNATURE)
            return false;

        if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
            return false;

        *outBase = base;
        *outNt = nt;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

static bool WriteUInt32(BYTE* address, DWORD value)
{
    if (!address)
        return false;

    DWORD oldProtect = 0;

    if (!VirtualProtect(address, sizeof(DWORD), PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    *reinterpret_cast<DWORD*>(address) = value;

    FlushInstructionCache(GetCurrentProcess(), address, sizeof(DWORD));

    DWORD ignored = 0;
    VirtualProtect(address, sizeof(DWORD), oldProtect, &ignored);

    return true;
}

static bool WriteBytes(BYTE* address, const BYTE* data, SIZE_T size)
{
    if (!address || !data || size == 0)
        return false;

    DWORD oldProtect = 0;

    if (!VirtualProtect(address, size, PAGE_EXECUTE_READWRITE, &oldProtect))
        return false;

    memcpy(address, data, size);

    FlushInstructionCache(GetCurrentProcess(), address, size);

    DWORD ignored = 0;
    VirtualProtect(address, size, oldProtect, &ignored);

    return true;
}

static int PatchPatternInCodeSections(
    HMODULE module,
    const BYTE* pattern,
    SIZE_T patternSize,
    DWORD newImmediate
)
{
    BYTE* base = NULL;
    PIMAGE_NT_HEADERS32 nt = NULL;

    if (!GetModuleHeaders(module, &base, &nt))
        return 0;

    int patches = 0;

    PIMAGE_SECTION_HEADER section = IMAGE_FIRST_SECTION(nt);

    for (WORD s = 0; s < nt->FileHeader.NumberOfSections; ++s, ++section)
    {
        DWORD characteristics = section->Characteristics;

        bool isCode =
            (characteristics & IMAGE_SCN_CNT_CODE) != 0 ||
            (characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;

        if (!isCode)
            continue;

        BYTE* start = base + section->VirtualAddress;
        SIZE_T size = section->Misc.VirtualSize;

        if (!start || size < patternSize)
            continue;

        __try
        {
            for (SIZE_T i = 0; i + patternSize <= size; ++i)
            {
                BYTE* current = start + i;

                if (!BytesEqual(current, pattern, patternSize))
                    continue;

                // Pattern is PUSH imm32:
                //   68 xx xx xx xx
                if (WriteUInt32(current + 1, newImmediate))
                    ++patches;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            // Continue...
        }
    }

    return patches;
}

static void PatchGameExe()
{
    if (InterlockedCompareExchange(&g_gamePatched, 1, 0) != 0)
        return;

    HMODULE game = GetModuleHandleA(NULL);

    PatchPatternInCodeSections(
        game,
        PATTERN_GAME_WIDTH_1024,
        sizeof(PATTERN_GAME_WIDTH_1024),
        g_resolution.width
    );

    PatchPatternInCodeSections(
        game,
        PATTERN_GAME_HEIGHT_768,
        sizeof(PATTERN_GAME_HEIGHT_768),
        g_resolution.height
    );
}

static void PatchGfxDll(HMODULE gfx)
{
    if (!gfx)
        return;

    if (InterlockedCompareExchange(&g_gfxPatched, 1, 0) != 0)
        return;

    PatchPatternInCodeSections(
        gfx,
        PATTERN_GFX_WIDTH_1600,
        sizeof(PATTERN_GFX_WIDTH_1600),
        GFX_MAX_WIDTH
    );

    PatchPatternInCodeSections(
        gfx,
        PATTERN_GFX_HEIGHT_1200,
        sizeof(PATTERN_GFX_HEIGHT_1200),
        GFX_MAX_HEIGHT
    );
}

static bool PatchGfxIfLoaded()
{
    HMODULE gfx = GetModuleHandleA("GFX.dll");

    if (!gfx)
        gfx = GetModuleHandleA(".\\GFX.dll");

    if (!gfx)
        return false;

    PatchGfxDll(gfx);
    return true;
}

static bool InstallScreenCenterHook(BYTE* moduleBase, const ScreenCenterSite& site)
{
    BYTE* siteAddr = moduleBase + site.rvaStart;

    __try
    {
        if (!BytesEqual(siteAddr, site.expectedBytes, site.length))
            return false;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }

    if (site.length < 5 || site.length > 32)
        return false;

    SIZE_T trampolineSize = site.length + sizeof(SCREEN_CENTER_FIXUP) + 5;

    BYTE* trampoline = reinterpret_cast<BYTE*>(VirtualAlloc(
        NULL,
        trampolineSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_EXECUTE_READWRITE
    ));

    if (!trampoline)
        return false;

    memcpy(trampoline, siteAddr, site.length);
    memcpy(trampoline + site.length, SCREEN_CENTER_FIXUP, sizeof(SCREEN_CENTER_FIXUP));

    BYTE* jmpBackAt = trampoline + site.length + sizeof(SCREEN_CENTER_FIXUP);
    BYTE* resumeVa  = moduleBase + site.rvaResume;
    DWORD jmpBackRel = static_cast<DWORD>(resumeVa - (jmpBackAt + 5));

    jmpBackAt[0] = 0xE9;
    *reinterpret_cast<DWORD*>(jmpBackAt + 1) = jmpBackRel;

    FlushInstructionCache(GetCurrentProcess(), trampoline, trampolineSize);

    // Overwrite the original site: JMP to trampoline + NOP padding. 32 bytes covers every site > longest is 25 bytes.
    BYTE hookBuf[32];
    memset(hookBuf, 0x90, sizeof(hookBuf));

    DWORD jmpInRel = static_cast<DWORD>(trampoline - (siteAddr + 5));
    hookBuf[0] = 0xE9;
    *reinterpret_cast<DWORD*>(hookBuf + 1) = jmpInRel;

    return WriteBytes(siteAddr, hookBuf, site.length);
}

static void ApplySites(BYTE* base, const ScreenCenterSite* sites, int count, const char* buildName)
{
    int fail = 0;
    for (int i = 0; i < count; ++i)
        if (!InstallScreenCenterHook(base, sites[i]))
            ++fail;

    if (fail > 0)
    {
        char msg[128];
        wsprintfA(msg, "%s: %d/%d hooks failed.", buildName, fail, count);
        MessageBoxA(NULL, msg, "BlitzkriegPatch", MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }
}

static void PatchGameTTDll(HMODULE gameTT)
{
    if (!gameTT)
        return;

    if (InterlockedCompareExchange(&g_gameTTPatched, 1, 0) != 0)
        return;

    BYTE* base;
    PIMAGE_NT_HEADERS32 nt;
    if (!GetModuleHeaders(gameTT, &base, &nt))
        return;

    DWORD ts = nt->FileHeader.TimeDateStamp;

    if (ts == 0x58F571E9)
        ApplySites(base, SITES_STEAM_GOG, ARRAYSIZE(SITES_STEAM_GOG), "Steam/GOG");
    else if (ts == 0x3EC38C2F)
        ApplySites(base, SITES_RETAIL_2003, ARRAYSIZE(SITES_RETAIL_2003), "Retail 2003");
    else
    {
        char msg[256];
        wsprintfA(msg,
            "Unsupported GameTT.dll (timestamp: 0x%08x).\n\n"
            "Make sure you have applied the 1.2 update, then try again.\n\n"
            "If the issue persists, open a report at:\n"
            "https://github.com/brian8544/BlitzkriegPatch",
            ts);
        MessageBoxA(NULL, msg, "BlitzkriegPatch", MB_OK | MB_ICONWARNING | MB_TOPMOST);
    }
}

static bool PatchGameTTIfLoaded()
{
    HMODULE gameTT = GetModuleHandleA("GameTT.dll");

    if (!gameTT)
        gameTT = GetModuleHandleA(".\\GameTT.dll");

    if (!gameTT)
        return false;

    PatchGameTTDll(gameTT);
    return true;
}

static void PinSelf()
{
    HMODULE pinned = NULL;

    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
        reinterpret_cast<LPCSTR>(&PinSelf),
        &pinned
    );
}
static DWORD WINAPI GfxPollThread(LPVOID)
{
    for (int i = 0; i < 10000; ++i)
    {
        if (InterlockedCompareExchange(&g_gfxPatched, 0, 0) != 0)
            return 0;

        if (PatchGfxIfLoaded())
            return 0;

        Sleep(1);
    }

    return 0;
}

static DWORD WINAPI GameTTPollThread(LPVOID)
{
    for (int i = 0; i < 10000; ++i)
    {
        if (InterlockedCompareExchange(&g_gameTTPatched, 0, 0) != 0)
            return 0;

        if (PatchGameTTIfLoaded())
            return 0;

        Sleep(1);
    }

    return 0;
}

static void StartGfxThread()
{
    if (InterlockedCompareExchange(&g_threadStarted, 1, 0) != 0)
        return;

    HANDLE thread = CreateThread(NULL, 0, GfxPollThread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);

    thread = CreateThread(NULL, 0, GameTTPollThread, NULL, 0, NULL);
    if (thread) CloseHandle(thread);
}

static void StartPatch()
{
    if (InterlockedCompareExchange(&g_started, 1, 0) != 0)
        return;

    PinSelf();
    LoadOrAskResolution();
    PatchGameExe();
    PatchGfxIfLoaded();
    StartGfxThread();
}

extern "C" void* __stdcall GetModuleDescriptor()
{
    StartPatch();
    return NULL;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_self = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }

    return TRUE;
}