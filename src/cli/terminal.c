/**
 * SPDX-FileCopyrightText: 2026 David Higgins <zoul0813@me.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "cli.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#include <wchar.h>
#else
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#endif

enum {
    KEY_CTRL_A = 1,
    KEY_CTRL_C = 3,
    KEY_CTRL_D = 4,
    KEY_CTRL_E = 5,
    KEY_BACKSPACE = 8,
    KEY_TAB = 9,
    KEY_CTRL_U = 21,
    KEY_ESCAPE = 27,
    KEY_SPACE = 32,
    KEY_DEL = 127,
    ASCII_LIMIT = 128,
    INPUT_BYTE_LIMIT = 256,
    KEY_UP = INPUT_BYTE_LIMIT,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
    KEY_IGNORE
};

enum {
    DEFAULT_TERMINAL_COLUMNS = 80,
    /* Five prompt columns plus two columns kept clear at the right edge. */
    VIEWPORT_RESERVED_COLUMNS = 7,
    TAB_COLUMNS = 8,
    UTF8_MAX_BYTES = 4,
    UTF16_MAX_UNITS = 2,
    ESCAPE_SEQUENCE_CAPACITY = 16,
    ESCAPE_TIMEOUT_MS = 100,
    CSI_FINAL_BYTE_MIN = 0x40,
    CSI_FINAL_BYTE_MAX = 0x7e,
    UTF8_CONTINUATION_MASK = 0xc0,
    UTF8_CONTINUATION_TAG = 0x80
};
#ifdef _WIN32
/* _getwch returns a prefix followed by a scan code for navigation keys.
 * These are console scan codes, not Win32 VK_* virtual-key codes. */
enum {
    SCAN_PREFIX = 0,
    SCAN_EXTENDED_PREFIX = 0xe0,
    SCAN_UP = 72,
    SCAN_DOWN = 80,
    SCAN_LEFT = 75,
    SCAN_RIGHT = 77,
    SCAN_HOME = 71,
    SCAN_END = 79,
    SCAN_DELETE = 83,
    UTF16_HIGH_SURROGATE_FIRST = 0xd800,
    UTF16_HIGH_SURROGATE_LAST = 0xdbff
};

static DWORD input_mode;
static DWORD output_mode;
static UINT output_codepage;
static HANDLE input_handle;
static HANDLE output_handle;

static bool editing_begin(void)
{
    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    output_handle = GetStdHandle(STD_ERROR_HANDLE);
    if (GetConsoleMode(input_handle, &input_mode) == 0 || GetConsoleMode(output_handle, &output_mode) == 0) {
        return false;
    }
    output_codepage = GetConsoleOutputCP();
    if (SetConsoleMode(output_handle, output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) == 0) {
        return false;
    }
    if (SetConsoleMode(input_handle, input_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) !=
        0) {
        if (SetConsoleOutputCP(CP_UTF8) != 0) {
            return true;
        }
        SetConsoleMode(input_handle, input_mode);
    }
    SetConsoleMode(output_handle, output_mode);
    SetConsoleOutputCP(output_codepage);
    return false;
}

static void editing_end(void)
{
    SetConsoleMode(input_handle, input_mode);
    SetConsoleMode(output_handle, output_mode);
    SetConsoleOutputCP(output_codepage);
}

static int columns(void)
{
    CONSOLE_SCREEN_BUFFER_INFO info;
    return GetConsoleScreenBufferInfo(output_handle, &info) != 0 ? info.srWindow.Right - info.srWindow.Left + 1
                                                                 : DEFAULT_TERMINAL_COLUMNS;
}

static int read_key(void)
{
    static unsigned char encoded[UTF8_MAX_BYTES];
    static int at;
    static int count;
    if (at < count) {
        return encoded[at++];
    }
    wint_t key = _getwch();
    if (key == SCAN_PREFIX || key == SCAN_EXTENDED_PREFIX) {
        switch (_getwch()) {
        case SCAN_UP:
            return KEY_UP;
        case SCAN_DOWN:
            return KEY_DOWN;
        case SCAN_LEFT:
            return KEY_LEFT;
        case SCAN_RIGHT:
            return KEY_RIGHT;
        case SCAN_HOME:
            return KEY_HOME;
        case SCAN_END:
            return KEY_END;
        case SCAN_DELETE:
            return KEY_DELETE;
        default:
            return KEY_IGNORE;
        }
    }
    if (key < ASCII_LIMIT) {
        return (int) key;
    }
    wchar_t text[UTF16_MAX_UNITS] = {(wchar_t) key, 0};
    int length = 1;
    if (key >= UTF16_HIGH_SURROGATE_FIRST && key <= UTF16_HIGH_SURROGATE_LAST) {
        text[1] = (wchar_t) _getwch();
        length = 2;
    }
    count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, (char*) encoded, sizeof(encoded), NULL, NULL);
    at = 0;
    return count != 0 ? encoded[at++] : KEY_IGNORE;
}
#else
static struct termios saved_mode;
static struct termios edit_mode;
static const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};

enum {
    SIGNAL_COUNT = sizeof(signals) / sizeof(signals[0])
};
static struct sigaction saved_signals[SIGNAL_COUNT];
static volatile sig_atomic_t active;

static void interrupted(int number)
{
    /* Restore the terminal before delivering the signal's previous action.
     * If a stopped process resumes, reinstall editing mode for the active read. */
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_mode);
    for (size_t i = 0; i < SIGNAL_COUNT; ++i) {
        if (signals[i] == number) {
            struct sigaction handler;
            sigaction(number, &saved_signals[i], &handler);
            sigset_t unblocked;
            sigset_t blocked;
            sigemptyset(&unblocked);
            sigaddset(&unblocked, number);
            sigprocmask(SIG_UNBLOCK, &unblocked, &blocked);
            raise(number);
            sigprocmask(SIG_SETMASK, &blocked, NULL);
            /* Execution resumes here after a suspended process continues. */
            sigaction(number, &handler, NULL);
            if (active != 0) {
                tcsetattr(STDIN_FILENO, TCSANOW, &edit_mode);
            }
            break;
        }
    }
}

static bool editing_begin(void)
{
    if (isatty(STDERR_FILENO) == 0 || tcgetattr(STDIN_FILENO, &saved_mode) != 0) {
        return false;
    }
    edit_mode = saved_mode;
    edit_mode.c_lflag &= ~(ICANON | ECHO | ISIG | IEXTEN);
    edit_mode.c_iflag &= ~(IXON | ICRNL);
    edit_mode.c_cc[VMIN] = 1;
    edit_mode.c_cc[VTIME] = 0;
    struct sigaction handler = {0};
    handler.sa_handler = interrupted;
    sigemptyset(&handler.sa_mask);
    /* Raw editing disables the terminal's normal signal processing. While it
     * is active, external termination/suspend signals must still restore echo
     * and canonical input before exiting or handing the terminal back. */
    size_t i;
    for (i = 0; i < SIGNAL_COUNT; ++i) {
        if (sigaction(signals[i], &handler, &saved_signals[i]) != 0) {
            break;
        }
    }
    if (i == SIGNAL_COUNT && tcsetattr(STDIN_FILENO, TCSANOW, &edit_mode) == 0) {
        active = 1;
        return true;
    }
    while (i != 0) {
        --i, sigaction(signals[i], &saved_signals[i], NULL);
    }
    return false;
}

static void editing_end(void)
{
    active = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_mode);
    for (size_t i = 0; i < SIGNAL_COUNT; ++i) {
        sigaction(signals[i], &saved_signals[i], NULL);
    }
}

static int columns(void)
{
    struct winsize size;
    return ioctl(STDERR_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col != 0 ? size.ws_col : DEFAULT_TERMINAL_COLUMNS;
}

static int read_byte(void)
{
    unsigned char byte;
    ssize_t n;
    do {
        n = read(STDIN_FILENO, &byte, 1);
    } while (n < 0 && errno == EINTR);
    return n == 1 ? byte : EOF;
}

static int escape_byte(void)
{
    struct pollfd fd = {STDIN_FILENO, POLLIN, 0};
    return poll(&fd, 1, ESCAPE_TIMEOUT_MS) > 0 ? read_byte() : EOF;
}

static int read_key(void)
{
    int key = read_byte();
    if (key != KEY_ESCAPE) {
        return key;
    }
    int prefix = escape_byte();
    if (prefix != '[' && prefix != 'O') {
        return KEY_IGNORE;
    }
    char sequence[ESCAPE_SEQUENCE_CAPACITY];
    size_t n = 0;
    int c;
    while ((c = escape_byte()) != EOF && n < sizeof(sequence) - 1) {
        sequence[n++] = (char) c;
        if (c >= CSI_FINAL_BYTE_MIN && c <= CSI_FINAL_BYTE_MAX) {
            break;
        }
    }
    sequence[n] = 0;
    if (strcmp(sequence, "A") == 0) {
        return KEY_UP;
    }
    if (strcmp(sequence, "B") == 0) {
        return KEY_DOWN;
    }
    if (strcmp(sequence, "C") == 0) {
        return KEY_RIGHT;
    }
    if (strcmp(sequence, "D") == 0) {
        return KEY_LEFT;
    }
    if (strcmp(sequence, "H") == 0 || strcmp(sequence, "1~") == 0) {
        return KEY_HOME;
    }
    if (strcmp(sequence, "F") == 0 || strcmp(sequence, "4~") == 0) {
        return KEY_END;
    }
    if (strcmp(sequence, "3~") == 0) {
        return KEY_DELETE;
    }
    return KEY_IGNORE;
}
#endif
static size_t previous(const char* line, size_t at)
{
    if (at != 0) {
        --at;
    }
    while (at != 0 && ((unsigned char) line[at] & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION_TAG) {
        --at;
    }
    return at;
}

static size_t next(const char* line, size_t at)
{
    if (line[at] != 0) {
        ++at;
    }
    while (line[at] != 0 && ((unsigned char) line[at] & UTF8_CONTINUATION_MASK) == UTF8_CONTINUATION_TAG) {
        ++at;
    }
    return at;
}

/* A single-line viewport avoids wrapping for long host paths. Reserve two
 * columns for non-ASCII characters so wide glyphs cannot wrap the prompt. */
static int width(const char* line, size_t at)
{
    return line[at] == '\t' ? TAB_COLUMNS : (unsigned char) line[at] < ASCII_LIMIT ? 1 : 2;
}

static void redraw(const char* line, size_t cursor)
{
    int room = columns() - VIEWPORT_RESERVED_COLUMNS;
    if (room < 2) {
        room = 2;
    }
    size_t start = cursor;
    int used = 0;
    while (start != 0) {
        size_t before = previous(line, start);
        int w = width(line, before);
        if (used + w > room) {
            break;
        }
        start = before;
        used += w;
    }
    fputs("\r\033[2Kzdt> ", stderr);
    fwrite(line + start, 1, cursor - start, stderr);
    fputs("\0337", stderr);
    for (size_t end = cursor; line[end] != 0;) {
        int w = width(line, end);
        if (used + w > room) {
            break;
        }
        size_t after = next(line, end);
        fwrite(line + end, 1, after - end, stderr);
        end = after;
        used += w;
    }
    fputs("\0338", stderr);
    fflush(stderr);
}

void cli_history_add(cli_history* history, const char* line)
{
    const char* p = line;
    while (isspace((unsigned char) *p) != 0) {
        ++p;
    }
    if (*p == 0) {
        return;
    }
    size_t length = strcspn(line, "\r\n");
    memcpy(history->entries[history->next], line, length);
    history->entries[history->next][length] = 0;
    history->next = (history->next + 1) % CLI_HISTORY_LIMIT;
    if (history->count < CLI_HISTORY_LIMIT) {
        ++history->count;
    }
}

cli_input cli_readline(cli_history* history, char line[CLI_LINE_CAPACITY], bool terminal)
{
    bool edit = terminal && editing_begin();
    if (!edit) {
        if (terminal) {
            fputs("zdt> ", stderr);
            fflush(stderr);
        }
        if (fgets(line, CLI_LINE_CAPACITY, stdin) == NULL) {
            return ferror(stdin) != 0 ? CLI_INPUT_ERROR : CLI_INPUT_EOF;
        }
        if (strchr(line, '\n') == NULL && feof(stdin) == 0) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
            return CLI_INPUT_LONG;
        }
        return CLI_INPUT_OK;
    }
    char draft[CLI_LINE_CAPACITY] = {0};
    size_t length = 0;
    size_t cursor = 0;
    size_t distance = 0;
    bool overflow = false;
    cli_input result = CLI_INPUT_OK;
    line[0] = 0;
    redraw(line, cursor);
    for (;;) {
        int key = read_key();
        if (key == EOF) {
            result = CLI_INPUT_EOF;
            break;
        }
        if (key == '\r' || key == '\n') {
            result = overflow ? CLI_INPUT_LONG : CLI_INPUT_OK;
            break;
        }
        if (key == KEY_CTRL_C) {
            line[0] = 0;
            break;
        }
        if (key == KEY_CTRL_D && length == 0) {
            result = CLI_INPUT_EOF;
            break;
        }
        if (key == KEY_UP || key == KEY_DOWN) {
            if (key == KEY_UP && distance < history->count) {
                if (distance == 0) {
                    strcpy(draft, line);
                }
                ++distance;
            } else if (key == KEY_DOWN && distance != 0) {
                --distance;
            } else {
                continue;
            }
            strcpy(line,
                   distance != 0 ? history->entries[(history->next + CLI_HISTORY_LIMIT - distance) % CLI_HISTORY_LIMIT]
                                 : draft);
            cursor = strlen(line);
            length = cursor;
            overflow = false;
        } else if (key == KEY_LEFT) {
            cursor = previous(line, cursor);
        } else if (key == KEY_RIGHT) {
            cursor = next(line, cursor);
        } else if (key == KEY_HOME || key == KEY_CTRL_A) {
            cursor = 0;
        } else if (key == KEY_END || key == KEY_CTRL_E) {
            cursor = length;
        } else if (key == KEY_CTRL_U) {
            cursor = 0;
            length = 0;
            line[0] = 0;
            overflow = false;
        } else if ((key == KEY_DEL || key == KEY_BACKSPACE) && cursor != 0) {
            size_t before = previous(line, cursor);
            memmove(line + before, line + cursor, length - cursor + 1);
            length -= cursor - before;
            cursor = before;
        } else if ((key == KEY_DELETE || key == KEY_CTRL_D) && cursor < length) {
            size_t after = next(line, cursor);
            memmove(line + cursor, line + after, length - after + 1);
            length -= after - cursor;
        } else if ((key == KEY_TAB || key >= KEY_SPACE) && key < INPUT_BYTE_LIMIT && key != KEY_DEL) {
            if (length >= CLI_LINE_CAPACITY - 2) {
                overflow = true;
                fputc('\a', stderr);
                continue;
            }
            memmove(line + cursor + 1, line + cursor, length - cursor + 1);
            line[cursor++] = (char) key;
            ++length;
        } else {
            continue;
        }
        redraw(line, cursor);
    }
    editing_end();
    fputc('\n', stderr);
    fflush(stderr);
    return result;
}
