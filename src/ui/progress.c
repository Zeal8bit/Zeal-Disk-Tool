/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 *
 * SPDX-License-Identifier: Apache-2.0 */
#include "disk.h"
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
static HWND hwndProgress = NULL;
static HWND hwndWindow = NULL;

extern int winWidth;
extern int winHeight;
extern int winX;
extern int winY;

void disk_init_progress_bar(void)
{
    INITCOMMONCONTROLSEX icex = {sizeof(icex), ICC_PROGRESS_CLASS};
    InitCommonControlsEx(&icex);

    int width = 350;
    int height = 100;

    int centerX = winX + winWidth / 2;
    int centerY = winY + winHeight / 2;

    hwndWindow =
        CreateWindowEx(0, WC_DIALOG, "Copying file...", WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, centerX - width / 2,
                       centerY - height / 2, width, height, NULL, NULL, GetModuleHandle(NULL), NULL);

    hwndProgress = CreateWindowEx(0, PROGRESS_CLASS, NULL, WS_CHILD | WS_VISIBLE, 20, 20, 300, 20, hwndWindow, NULL,
                                  GetModuleHandle(NULL), NULL);

    SendMessage(hwndProgress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessage(hwndProgress, PBM_SETPOS, 0, 0);

    ShowWindow(hwndWindow, SW_SHOW);
    UpdateWindow(hwndWindow);
}

void disk_update_progress_bar(int percent)
{
    if (hwndProgress) {
        SendMessage(hwndProgress, PBM_SETPOS, percent, 0);

        // Process UI messages so it updates
        MSG msg;
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
}

void disk_destroy_progress_bar(void)
{
    if (hwndWindow) {
        DestroyWindow(hwndWindow);
        hwndWindow = NULL;
        hwndProgress = NULL;
    }
}
#else
void disk_init_progress_bar(void)
{
}

void disk_update_progress_bar(int percent)
{
    (void) percent;
}

void disk_destroy_progress_bar(void)
{
}
#endif
