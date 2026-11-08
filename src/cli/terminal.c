/**
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
    KEY_UP = 256,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_HOME,
    KEY_END,
    KEY_DELETE,
    KEY_IGNORE
};
#ifdef _WIN32
static DWORD input_mode, output_mode;
static UINT output_codepage;
static HANDLE input_handle, output_handle;

static bool editing_begin(void)
{
    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    output_handle = GetStdHandle(STD_ERROR_HANDLE);
    if (!GetConsoleMode(input_handle, &input_mode) || !GetConsoleMode(output_handle, &output_mode)) {
        return false;
    }
    output_codepage = GetConsoleOutputCP();
    if (!SetConsoleMode(output_handle, output_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        return false;
    }
    if (SetConsoleMode(input_handle, input_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT))) {
        if (SetConsoleOutputCP(CP_UTF8)) {
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
    return GetConsoleScreenBufferInfo(output_handle, &info) ? info.srWindow.Right - info.srWindow.Left + 1 : 80;
}

static int read_key(void)
{
    static unsigned char encoded[4];
    static int at, count;
    if (at < count) {
        return encoded[at++];
    }
    wint_t key = _getwch();
    if (key == 0 || key == 0xe0) {
        switch (_getwch()) {
        case 72:
            return KEY_UP;
        case 80:
            return KEY_DOWN;
        case 75:
            return KEY_LEFT;
        case 77:
            return KEY_RIGHT;
        case 71:
            return KEY_HOME;
        case 79:
            return KEY_END;
        case 83:
            return KEY_DELETE;
        default:
            return KEY_IGNORE;
        }
    }
    if (key < 128) {
        return (int) key;
    }
    wchar_t text[2] = {(wchar_t) key, 0};
    int length = 1;
    if (key >= 0xd800 && key <= 0xdbff) {
        text[1] = (wchar_t) _getwch();
        length = 2;
    }
    count =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length, (char*) encoded, sizeof(encoded), NULL, NULL);
    at = 0;
    return count ? encoded[at++] : KEY_IGNORE;
}
#else
static struct termios saved_mode, edit_mode;
static const int signals[] = {SIGINT, SIGTERM, SIGHUP, SIGQUIT, SIGTSTP};
static struct sigaction saved_signals[5];
static volatile sig_atomic_t active;

static void interrupted(int number)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_mode);
    for (size_t i = 0; i < 5; ++i) {
        if (signals[i] == number) {
            struct sigaction handler;
            sigaction(number, &saved_signals[i], &handler);
            sigset_t unblocked, blocked;
            sigemptyset(&unblocked);
            sigaddset(&unblocked, number);
            sigprocmask(SIG_UNBLOCK, &unblocked, &blocked);
            raise(number);
            sigprocmask(SIG_SETMASK, &blocked, NULL);
            /* Execution resumes here after a suspended process continues. */
            sigaction(number, &handler, NULL);
            if (active) {
                tcsetattr(STDIN_FILENO, TCSANOW, &edit_mode);
            }
            break;
        }
    }
}

static bool editing_begin(void)
{
    if (!isatty(STDERR_FILENO) || tcgetattr(STDIN_FILENO, &saved_mode)) {
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
    size_t i;
    for (i = 0; i < 5; ++i) {
        if (sigaction(signals[i], &handler, &saved_signals[i])) {
            break;
        }
    }
    if (i == 5 && !tcsetattr(STDIN_FILENO, TCSANOW, &edit_mode)) {
        active = 1;
        return true;
    }
    while (i) {
        --i, sigaction(signals[i], &saved_signals[i], NULL);
    }
    return false;
}

static void editing_end(void)
{
    active = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_mode);
    for (size_t i = 0; i < 5; ++i) {
        sigaction(signals[i], &saved_signals[i], NULL);
    }
}

static int columns(void)
{
    struct winsize size;
    return !ioctl(STDERR_FILENO, TIOCGWINSZ, &size) && size.ws_col ? size.ws_col : 80;
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
    return poll(&fd, 1, 100) > 0 ? read_byte() : EOF;
}

static int read_key(void)
{
    int key = read_byte();
    if (key != 27) {
        return key;
    }
    int prefix = escape_byte();
    if (prefix != '[' && prefix != 'O') {
        return KEY_IGNORE;
    }
    char sequence[16];
    size_t n = 0;
    int c;
    while ((c = escape_byte()) != EOF && n < sizeof(sequence) - 1) {
        sequence[n++] = (char) c;
        if (c >= 0x40 && c <= 0x7e) {
            break;
        }
    }
    sequence[n] = 0;
    if (!strcmp(sequence, "A")) {
        return KEY_UP;
    }
    if (!strcmp(sequence, "B")) {
        return KEY_DOWN;
    }
    if (!strcmp(sequence, "C")) {
        return KEY_RIGHT;
    }
    if (!strcmp(sequence, "D")) {
        return KEY_LEFT;
    }
    if (!strcmp(sequence, "H") || !strcmp(sequence, "1~")) {
        return KEY_HOME;
    }
    if (!strcmp(sequence, "F") || !strcmp(sequence, "4~")) {
        return KEY_END;
    }
    if (!strcmp(sequence, "3~")) {
        return KEY_DELETE;
    }
    return KEY_IGNORE;
}
#endif
static size_t previous(const char* line, size_t at)
{
    if (at) {
        --at;
    }
    while (at && ((unsigned char) line[at] & 0xc0) == 0x80) {
        --at;
    }
    return at;
}

static size_t next(const char* line, size_t at)
{
    if (line[at]) {
        ++at;
    }
    while (line[at] && ((unsigned char) line[at] & 0xc0) == 0x80) {
        ++at;
    }
    return at;
}

/* A single-line viewport avoids wrapping for long host paths. Reserve two
 * columns for non-ASCII characters so wide glyphs cannot wrap the prompt. */
static int width(const char* line, size_t at)
{
    return line[at] == '\t' ? 8 : (unsigned char) line[at] < 128 ? 1 : 2;
}

static void redraw(const char* line, size_t cursor)
{
    int room = columns() - 7;
    if (room < 2) {
        room = 2;
    }
    size_t start = cursor;
    int used = 0;
    while (start) {
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
    for (size_t end = cursor; line[end];) {
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

void cli_history_add(cli_history* h, const char* line)
{
    const char* p = line;
    while (isspace((unsigned char) *p)) {
        ++p;
    }
    if (!*p) {
        return;
    }
    size_t length = strcspn(line, "\r\n");
    memcpy(h->entries[h->next], line, length);
    h->entries[h->next][length] = 0;
    h->next = (h->next + 1) % CLI_HISTORY_LIMIT;
    if (h->count < CLI_HISTORY_LIMIT) {
        ++h->count;
    }
}

cli_input cli_readline(cli_history* h, char line[CLI_LINE_CAPACITY], bool terminal)
{
    bool edit = terminal && editing_begin();
    if (!edit) {
        if (terminal) {
            fputs("zdt> ", stderr);
            fflush(stderr);
        }
        if (!fgets(line, CLI_LINE_CAPACITY, stdin)) {
            return ferror(stdin) ? CLI_INPUT_ERROR : CLI_INPUT_EOF;
        }
        if (!strchr(line, '\n') && !feof(stdin)) {
            int c;
            while ((c = getchar()) != '\n' && c != EOF) {
            }
            return CLI_INPUT_LONG;
        }
        return CLI_INPUT_OK;
    }
    char draft[CLI_LINE_CAPACITY] = {0};
    size_t length = 0, cursor = 0, distance = 0;
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
        if (key == 3) {
            line[0] = 0;
            break;
        }
        if (key == 4 && !length) {
            result = CLI_INPUT_EOF;
            break;
        }
        if (key == KEY_UP || key == KEY_DOWN) {
            if (key == KEY_UP && distance < h->count) {
                if (!distance) {
                    strcpy(draft, line);
                }
                ++distance;
            } else if (key == KEY_DOWN && distance) {
                --distance;
            } else {
                continue;
            }
            strcpy(line, distance ? h->entries[(h->next + CLI_HISTORY_LIMIT - distance) % CLI_HISTORY_LIMIT] : draft);
            length = cursor = strlen(line);
            overflow = false;
        } else if (key == KEY_LEFT) {
            cursor = previous(line, cursor);
        } else if (key == KEY_RIGHT) {
            cursor = next(line, cursor);
        } else if (key == KEY_HOME || key == 1) {
            cursor = 0;
        } else if (key == KEY_END || key == 5) {
            cursor = length;
        } else if (key == 21) {
            length = cursor = 0;
            line[0] = 0;
            overflow = false;
        } else if ((key == 127 || key == 8) && cursor) {
            size_t before = previous(line, cursor);
            memmove(line + before, line + cursor, length - cursor + 1);
            length -= cursor - before;
            cursor = before;
        } else if ((key == KEY_DELETE || key == 4) && cursor < length) {
            size_t after = next(line, cursor);
            memmove(line + cursor, line + after, length - after + 1);
            length -= after - cursor;
        } else if ((key == 9 || key >= 32) && key < 256 && key != 127) {
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
