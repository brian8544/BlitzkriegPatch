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

static void StartGfxThread()
{
    if (InterlockedCompareExchange(&g_threadStarted, 1, 0) != 0)
        return;

    HANDLE thread = CreateThread(
        NULL,
        0,
        GfxPollThread,
        NULL,
        0,
        NULL
    );

    if (thread)
        CloseHandle(thread);
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