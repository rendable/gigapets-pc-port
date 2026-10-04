// Win32 window helpers (hand-declared to avoid windows.h clashing with raylib).

#include "common.h"

// Hand-declared instead of #include <windows.h> to avoid its Rectangle/
// CloseWindow/DrawText/PlaySound clashes with raylib. user32.lib is already
// linked (CMakeLists.txt); kernel32.lib is linked implicitly by default.
struct WinRect { long left, top, right, bottom; };

struct WinMonitorInfo { unsigned long cbSize; WinRect rcMonitor; WinRect rcWork; unsigned long dwFlags; };

extern "C" {
    __declspec(dllimport) int __stdcall MessageBoxA(void* hWnd, const char* text, const char* caption, unsigned type);
    __declspec(dllimport) int __stdcall GetWindowRect(void* hWnd, struct WinRect* rect);
    __declspec(dllimport) void* __stdcall MonitorFromWindow(void* hWnd, unsigned long flags);
    __declspec(dllimport) int __stdcall GetMonitorInfoA(void* hMonitor, struct WinMonitorInfo* info);
    __declspec(dllimport) int __stdcall MoveWindow(void* hWnd, int x, int y, int w, int h, int repaint);
}

void show_error_dialog(const char* message) {
    MessageBoxA(nullptr, message, "GigaPets PC Port", 0x10 /* MB_ICONERROR */);
}

// F11: grow the (still windowed) window to the largest whole-number multiple
// of the native 320x240 that fits the current monitor's usable area, so the
// picture stays pixel-sharp instead of being stretched. Press again to
// restore the previous size/position.
void toggle_expand_window() {
    static bool saved_valid = false;
    static WinRect saved = {0, 0, 0, 0};
    void* hwnd = GetWindowHandle();
    if (!hwnd) return;
    if (IsWindowMaximized()) RestoreWindow();
    WinRect outer;
    if (!GetWindowRect(hwnd, &outer)) return;
    int cw = GetScreenWidth(), ch = GetScreenHeight();
    int deco_w = (int)(outer.right - outer.left) - cw;
    int deco_h = (int)(outer.bottom - outer.top) - ch;
    WinMonitorInfo mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoA(MonitorFromWindow(hwnd, 2 /* MONITOR_DEFAULTTONEAREST */), &mi)) return;
    int work_w = (int)(mi.rcWork.right - mi.rcWork.left);
    int work_h = (int)(mi.rcWork.bottom - mi.rcWork.top);
    int scale = std::min((work_w - deco_w) / NATIVE_W, (work_h - deco_h) / NATIVE_H);
    if (scale < 1) scale = 1;
    int tw = NATIVE_W * scale + deco_w, th = NATIVE_H * scale + deco_h;
    bool already_expanded = (cw == NATIVE_W * scale && ch == NATIVE_H * scale);
    if (already_expanded && saved_valid) {
        MoveWindow(hwnd, (int)saved.left, (int)saved.top, (int)(saved.right - saved.left), (int)(saved.bottom - saved.top), 1);
        saved_valid = false;
        return;
    }
    if (!already_expanded) { saved = outer; saved_valid = true; }
    MoveWindow(hwnd, (int)mi.rcWork.left + (work_w - tw) / 2, (int)mi.rcWork.top + (work_h - th) / 2, tw, th, 1);
}
