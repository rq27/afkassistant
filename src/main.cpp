// afkassistant - AFK auto-response plugin for SA-MP 0.3DL
// Copyright (C) 2026  Raziq Revano Ramadani
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <https://www.gnu.org/licenses/>.

#include <windows.h>
#include <string>
#include <regex>
#include <cstdio>
#include <cstring>
#include "MinHook.h"

// ─── Version offsets ──────────────────────────────────────────────────────────
struct SampOffsets {
    const char* name;
    DWORD addMessage;
    DWORD refChat;
};
static const SampOffsets g_offsets[] = {
    { "0.3.7-R1", 0x645A0, 0x21A0E4 },
    { "0.3.7-R2", 0x645A0, 0x21A0E4 },
    { "0.3.7-R3", 0x679F0, 0x26E8C8 },
    { "0.3.7-R4", 0x679F0, 0x26E8C8 },
    { "0.3.7-R5", 0x68170, 0x26EB80 },
    { "0.3.DL",   0x67BE0, 0x2ACA10 },
};

// ─── Global state ─────────────────────────────────────────────────────────────
static bool  g_pluginEnabled = true;
static bool  g_busy          = false;
static DWORD g_sampBase      = 0;
static DWORD g_refChatOffset = 0;

typedef void(__thiscall* tAddMessage)(void*, DWORD, const char*);
static tAddMessage oAddMessage = nullptr;

// ─── Utilities ────────────────────────────────────────────────────────────────

static bool IsGtaFocused() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    // Match by window handle, not title string
    HWND gtaWnd = FindWindowA("Grand theft auto San Andreas", nullptr);
    return gtaWnd != nullptr && fg == gtaWnd;
}

static std::string StripColorCodes(const std::string& s) {
    std::regex colorRe(R"(\{[0-9A-Fa-f]{6}\})");
    return std::regex_replace(s, colorRe, "");
}

// ─── Scan last 19 chat entries from CChat buffer ──────────────────────────────
static std::string GetLatestAfkCode() {
    if (!g_sampBase || !g_refChatOffset) return "";

    DWORD chatPtrAddr = g_sampBase + g_refChatOffset;
    DWORD chatPtr     = *(DWORD*)chatPtrAddr;
    if (!chatPtr) return "";

    // ChatEntry layout: timestamp(4) + prefix(28) + text(144) + unused(64) + type(4) + colors(8) = 252 bytes
    const int ENTRY_SIZE   = 252;
    const int ENTRY_OFFSET = 0x132;
    const int MAX_SCAN     = 19;

    // SA-MP uses a circular buffer of 100 chat slots.
    // Scan all entries to find the one with the highest timestamp (most recent),
    // then walk backwards from there to find the latest AFK code.
    // When timestamps are equal (common), index order is used as a tiebreaker.

    const int TOTAL_ENTRIES = 100;

    // Find the entry index with the largest timestamp = most recent entry
    int maxTs   = -1;
    int headIdx = 0;
    for (int i = 0; i < TOTAL_ENTRIES; i++) {
        char* base = (char*)(chatPtr + ENTRY_OFFSET + i * ENTRY_SIZE);
        if (IsBadReadPtr(base, 4)) continue;
        int ts = *(int*)base;
        if (ts > maxTs) { maxTs = ts; headIdx = i; }
    }

    // Walk backwards from headIdx up to MAX_SCAN entries, find the latest AFK code
    std::regex re(R"(afk\s+(\d{3}))", std::regex_constants::icase);
    std::string lastCode;

    for (int n = 0; n < MAX_SCAN; n++) {
        // Circular backwards index
        int i = (headIdx - n + TOTAL_ENTRIES) % TOTAL_ENTRIES;
        char* entryBase = (char*)(chatPtr + ENTRY_OFFSET + i * ENTRY_SIZE);
        if (IsBadReadPtr(entryBase, ENTRY_SIZE)) continue;

        char* szText = entryBase + 32;
        if (!IsBadReadPtr(szText, 144)) {
            std::string s = StripColorCodes(std::string(szText, strnlen(szText, 143)));
            std::smatch m;
            if (std::regex_search(s, m, re)) {
                lastCode = m[1].str();
                break; // take the first match = most recent
            }
        }
    }
    return lastCode;
}

// ─── Input helpers ────────────────────────────────────────────────────────────
static void PressKey(WORD vk, DWORD delayAfter = 50) {
    INPUT inp[2] = {};
    inp[0].type       = INPUT_KEYBOARD; inp[0].ki.wVk = vk;
    inp[1].type       = INPUT_KEYBOARD; inp[1].ki.wVk = vk;
    inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT));
    Sleep(delayAfter);
}

static void TypeChar(char c) {
    INPUT inp[2] = {};
    inp[0].type       = INPUT_KEYBOARD;
    inp[0].ki.wScan   = (WORD)c;
    inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
    inp[1].type       = INPUT_KEYBOARD;
    inp[1].ki.wScan   = (WORD)c;
    inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
    SendInput(1, &inp[0], sizeof(INPUT));
    Sleep(30);
    SendInput(1, &inp[1], sizeof(INPUT));
    Sleep(40);
}

static void TypeString(const char* s) {
    for (; *s; ++s) TypeChar(*s);
}

static void ClearChatbox() {
    INPUT ctrl[1] = {};
    ctrl[0].type   = INPUT_KEYBOARD;
    ctrl[0].ki.wVk = VK_CONTROL;
    SendInput(1, ctrl, sizeof(INPUT));
    Sleep(60);
    PressKey('A', 80);
    ctrl[0].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, ctrl, sizeof(INPUT));
    Sleep(80);
    PressKey(VK_DELETE, 120);
}

// ─── Input hooks ──────────────────────────────────────────────────────────────
#ifndef LLKHF_INJECTED
#define LLKHF_INJECTED 0x00000010
#endif
#ifndef LLMHF_INJECTED
#define LLMHF_INJECTED 0x00000001
#endif

static HHOOK g_hKeyboardHook = nullptr;
static HHOOK g_hMouseHook    = nullptr;
static volatile bool g_isMinusHeld = false;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* kbs = (KBDLLHOOKSTRUCT*)lParam;
        
        // Let simulated (injected) keystrokes pass
        if (kbs->flags & LLKHF_INJECTED) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
        
        // Let the minus key releases pass so we can detect them, block keydowns
        if (kbs->vkCode == VK_OEM_MINUS || kbs->vkCode == VK_SUBTRACT) {
            if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP) {
                g_isMinusHeld = false;
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }
            // Block repeat keydowns of the minus key while holding
            return 1;
        }
        
        // Block all other physical keyboard inputs
        return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
        
        // Let simulated (injected) mouse events pass
        if (ms->flags & LLMHF_INJECTED) {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }
        
        // Block all other physical mouse inputs
        return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ─── Type a local status message (not sent to server) ─────────────────────
static void TypeDebugMessage(const char* msg) {
    HWND hwnd = FindWindowA("Grand theft auto San Andreas", nullptr);
    if (!hwnd) return;

    g_busy = true;

    // Copy message to clipboard for instant paste
    if (OpenClipboard(nullptr)) {
        EmptyClipboard();
        SIZE_T len = strlen(msg) + 1;
        HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, len);
        if (hMem) {
            memcpy(GlobalLock(hMem), msg, len);
            GlobalUnlock(hMem);
            SetClipboardData(CF_TEXT, hMem);
        }
        CloseClipboard();
    }

    SetForegroundWindow(hwnd);
    
    // Install low-level keyboard and mouse hooks to block physical inputs (except minus key release)
    g_isMinusHeld   = true;
    g_hKeyboardHook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);
    g_hMouseHook    = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandle(nullptr), 0);

    // Open chat box
    PressKey('T', 20); // 20ms delay is enough for the game to register T

    // Paste instantly via Ctrl+V
    INPUT pasteInp[4] = {};
    
    // Ctrl Down
    pasteInp[0].type = INPUT_KEYBOARD;
    pasteInp[0].ki.wVk = VK_CONTROL;
    
    // V Down
    pasteInp[1].type = INPUT_KEYBOARD;
    pasteInp[1].ki.wVk = 'V';
    
    // V Up
    pasteInp[2].type = INPUT_KEYBOARD;
    pasteInp[2].ki.wVk = 'V';
    pasteInp[2].ki.dwFlags = KEYEVENTF_KEYUP;
    
    // Ctrl Up
    pasteInp[3].type = INPUT_KEYBOARD;
    pasteInp[3].ki.wVk = VK_CONTROL;
    pasteInp[3].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(4, pasteInp, sizeof(INPUT));

    // Message loop to keep hooks active while minus is held down
    MSG wmsg;
    while (g_isMinusHeld) {
        if (!IsGtaFocused()) {
            g_isMinusHeld = false;
            break;
        }
        while (PeekMessage(&wmsg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&wmsg);
            DispatchMessage(&wmsg);
        }
        Sleep(5);
    }

    // Uninstall hooks immediately
    if (g_hKeyboardHook) {
        UnhookWindowsHookEx(g_hKeyboardHook);
        g_hKeyboardHook = nullptr;
    }
    if (g_hMouseHook) {
        UnhookWindowsHookEx(g_hMouseHook);
        g_hMouseHook = nullptr;
    }

    // Instantly clear the chat box and escape (realtime, zero delay)
    INPUT clearInp[8] = {};
    
    // Ctrl Down
    clearInp[0].type = INPUT_KEYBOARD;
    clearInp[0].ki.wVk = VK_CONTROL;
    
    // A Down
    clearInp[1].type = INPUT_KEYBOARD;
    clearInp[1].ki.wVk = 'A';
    
    // A Up
    clearInp[2].type = INPUT_KEYBOARD;
    clearInp[2].ki.wVk = 'A';
    clearInp[2].ki.dwFlags = KEYEVENTF_KEYUP;
    
    // Ctrl Up
    clearInp[3].type = INPUT_KEYBOARD;
    clearInp[3].ki.wVk = VK_CONTROL;
    clearInp[3].ki.dwFlags = KEYEVENTF_KEYUP;
    
    // Delete Down
    clearInp[4].type = INPUT_KEYBOARD;
    clearInp[4].ki.wVk = VK_DELETE;
    
    // Delete Up
    clearInp[5].type = INPUT_KEYBOARD;
    clearInp[5].ki.wVk = VK_DELETE;
    clearInp[5].ki.dwFlags = KEYEVENTF_KEYUP;

    // Escape Down
    clearInp[6].type = INPUT_KEYBOARD;
    clearInp[6].ki.wVk = VK_ESCAPE;

    // Escape Up
    clearInp[7].type = INPUT_KEYBOARD;
    clearInp[7].ki.wVk = VK_ESCAPE;
    clearInp[7].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(8, clearInp, sizeof(INPUT));

    g_busy = false;
}

// ─── Send a command to the server ─────────────────────────────────────────────
static void SendSampCommand(const char* cmd) {
    HWND hwnd = FindWindowA("Grand theft auto San Andreas", nullptr);
    if (!hwnd) return;

    g_busy = true;
    ::BlockInput(TRUE);

    SetForegroundWindow(hwnd);
    Sleep(150);

    PressKey('T', 200);
    ClearChatbox();
    TypeString(cmd);
    Sleep(150);
    PressKey(VK_RETURN, 80);

    ::BlockInput(FALSE);
    g_busy = false;
}

// ─── Process chat text (from real-time hook) ──────────────────────────────────
static void ProcessChatText(const char* szText) {
    if (!g_pluginEnabled || g_busy) return;

    std::string s = StripColorCodes(szText);

    std::regex re(R"(afk\s+(\d{3}))", std::regex_constants::icase);
    std::smatch m;
    if (std::regex_search(s, m, re)) {
        std::string code = m[1].str();
        char cmd[32];
        sprintf_s(cmd, "/afk %s", code.c_str());
        SendSampCommand(cmd);
    }
}

// ─── AddMessage hook ──────────────────────────────────────────────────────────
static void __fastcall hAddMessage(void* pChat, void* /*edx*/,
                                   DWORD color, const char* szText) {
    if (szText) {
        char* copy = _strdup(szText);
        CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            ProcessChatText((const char*)p);
            free(p);
            return 0;
        }, copy, 0, nullptr);
    }
    oAddMessage(pChat, color, szText);
}

// ─── Hotkey thread ────────────────────────────────────────────────────────────
static DWORD WINAPI HotkeyThread(LPVOID) {
    bool prevStar  = false;
    bool prevMinus = false;

    while (true) {
        Sleep(50);
        if (g_busy) continue;

        // If GTA is not focused, reset key states and skip
        if (!IsGtaFocused()) {
            prevStar  = false;
            prevMinus = false;
            continue;
        }

        // Numpad * → scan chat memory and send /afk with the latest code
        bool curStar = (GetAsyncKeyState(VK_MULTIPLY) & 0x8000) != 0;
        if (curStar && !prevStar) {
            CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
                if (g_busy) return 0;
                std::string code = GetLatestAfkCode();
                if (!code.empty()) {
                    char cmd[32];
                    sprintf_s(cmd, "/afk %s", code.c_str());
                    SendSampCommand(cmd);
                }
                return 0;
            }, nullptr, 0, nullptr);
        }
        prevStar = curStar;

        // - (keyboard or numpad) → toggle plugin on/off
        bool curMinus = ((GetAsyncKeyState(VK_OEM_MINUS) & 0x8000) != 0) ||
                        ((GetAsyncKeyState(VK_SUBTRACT)  & 0x8000) != 0);
        if (curMinus && !prevMinus) {
            g_pluginEnabled = !g_pluginEnabled;
            const char* msg = g_pluginEnabled
                ? "afk assistant: enabled"
                : "afk assistant: disabled";
            CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
                TypeDebugMessage((const char*)p);
                return 0;
            }, (LPVOID)msg, 0, nullptr);
        }
        prevMinus = curMinus;
    }
    return 0;
}

// ─── Version detection ────────────────────────────────────────────────────────
static const SampOffsets* DetectVersion(HMODULE dll) {
    char path[MAX_PATH];
    GetModuleFileNameA(dll, path, MAX_PATH);
    HANDLE hf = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, 0, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return nullptr;
    DWORD sz = GetFileSize(hf, nullptr);
    CloseHandle(hf);

    if      (sz < 955000)  return &g_offsets[0]; // R1
    else if (sz < 956000)  return &g_offsets[1]; // R2
    else if (sz < 1060000) return &g_offsets[2]; // R3
    else if (sz < 1075000) return &g_offsets[3]; // R4
    else if (sz < 1100000) return &g_offsets[4]; // R5
    else                   return &g_offsets[5]; // DL
}

// ─── Main thread ──────────────────────────────────────────────────────────────
static DWORD WINAPI MainThread(LPVOID) {

    HMODULE sampDll = nullptr;
    while (!sampDll) {
        sampDll = GetModuleHandleA("samp.dll");
        Sleep(500);
    }
    Sleep(2000);

    const SampOffsets* ver = DetectVersion(sampDll);
    if (!ver) return 1;

    g_sampBase      = (DWORD)sampDll;
    g_refChatOffset = ver->refChat;
    DWORD hookTarget = g_sampBase + ver->addMessage;

    if (MH_Initialize() != MH_OK) return 1;
    if (MH_CreateHook((LPVOID)hookTarget, &hAddMessage,
                      (LPVOID*)&oAddMessage) != MH_OK) return 1;
    if (MH_EnableHook((LPVOID)hookTarget) != MH_OK) return 1;

    CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    return 0;
}

// ─── DllMain ──────────────────────────────────────────────────────────────────
BOOL APIENTRY DllMain(HMODULE hMod, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    if (reason == DLL_PROCESS_DETACH) {
        MH_DisableHook(MH_ALL_HOOKS);
        MH_Uninitialize();
    }
    return TRUE;
}