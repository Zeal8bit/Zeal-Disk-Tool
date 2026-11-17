/**
 * SPDX-FileCopyrightText: 2025 Zeal 8-bit Computer <contact@zeal8bit.com>
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <FL/Fl.H>
#include <FL/Fl_RGB_Image.H>
#include <FL/Fl_Window.H>

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MainWindow.hpp"
#include "app_icon.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void print_usage(const char *program)
{
    std::printf("Usage: %s [options] [<disk.img> ...]\n", program);
    std::printf("Options:\n");
    std::printf("  -h           Show this help message\n");
    std::printf("  <disk.img>   Load one or more disk images on startup\n");
#ifdef _WIN32
    std::printf("  -g           Allocate a console window (Windows only)\n");
#endif
}

bool has_image_extension(const char *path)
{
    const char *dot = std::strrchr(path, '.');
    if (dot == nullptr) {
        return false;
    }
    const char *extension = dot + 1;
    return std::tolower((unsigned char) extension[0]) == 'i' && std::tolower((unsigned char) extension[1]) == 'm' &&
           std::tolower((unsigned char) extension[2]) == 'g' && extension[3] == '\0';
}

}  // namespace

int main(int argc, char *argv[])
{
    std::vector<std::string> images;
    for (int index = 1; index < argc; ++index) {
        const char *argument = argv[index];
        if (std::strcmp(argument, "-h") == 0 || std::strcmp(argument, "--help") == 0) {
            print_usage(argv[0]);
            return 0;
        }
#ifdef _WIN32
        if (std::strcmp(argument, "-g") == 0) {
            __declspec(dllimport) int __stdcall AllocConsole(void);
            AllocConsole();
            std::freopen("CONOUT$", "w", stdout);
            std::freopen("CONOUT$", "w", stderr);
            std::freopen("CONIN$", "r", stdin);
            continue;
        }
#endif
        if (argument[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", argument);
            print_usage(argv[0]);
            return 2;
        }
        if (!has_image_extension(argument)) {
            std::fprintf(stderr, "Error: '%s' is not a .img file. Only disk images are supported.\n", argument);
            return 2;
        }
        images.emplace_back(argument);
    }

    Fl::scheme("gtk+");
    Fl::set_color(FL_BACKGROUND_COLOR, 0xef, 0xf2, 0xf6);
    /* A stable X11/Wayland application id lets desktop shells associate the
     * window icon with the zeal-disk-tool desktop entry. */
    Fl_Window::default_xclass("zeal-disk-tool");

    /* The RGBA pixel array is generated from packages/icons/zeal-disk-tool_32.png.
     * Fl_Window copies the image, so the stack object only needs to live until
     * the call returns. */
    const Fl_RGB_Image app_icon(s_app_icon_rgba, APP_ICON_WIDTH, APP_ICON_HEIGHT, 4);
    Fl_Window::default_icon(&app_icon);

    MainWindow window(images);
    window.position((Fl::w() - window.w()) / 2, (Fl::h() - window.h()) / 2);
    window.show();
    window.initialize();

    const int result = Fl::run();
    disks_close();
    return result;
}
