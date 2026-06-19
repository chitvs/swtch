/* swtch -- A minimal terminal stopwatch.
 *
 * MIT LICENSE
 *
 * Copyright (c) 2026 Alessandro Chitarrini
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/* Expose CLOCK_MONOTONIC and clock_gettime() from <time.h>.
 * Without this, strict C99/C11 mode hides POSIX extensions. */
#define _POSIX_C_SOURCE 200809L
#define MIN_WIDTH 83

/* 1 = running, 0 = stopped */
#define INIT_STATE 1

#include <stdio.h>
#include <stdint.h>
#include <termios.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <time.h>
#include <sys/ioctl.h>

/* Terminal */

/* The original terminal attributes, saved before we touch anything.
 * term_restore() puts them back exactly as we found them. */
static struct termios orig_termios;

static void term_restore(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    /* cursor back on */
    printf("\e[?25h\n");
}

static void term_raw(void) {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);

    /* every exit path restores the terminal. */
    atexit(term_restore);

    raw = orig_termios;
    /* no line buffering, no echo */
    raw.c_lflag &= ~(ICANON | ECHO);
    /* return immediately if no input */
    raw.c_cc[VMIN]  = 0;
    /* ...but wait up to 100ms first */
    raw.c_cc[VTIME] = 1;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

    /* hide cursor */
    printf("\e[?25l");
}

/* Ctrl-C would normally leave the terminal in raw mode. We catch SIGINT
 * so that atexit() still fires and term_restore() gets called. */
static void handle_sigint(int sig) {
    (void)sig;
    exit(0);
}

/* terminal width via ioctl, handles resize and zoom with no signal handling. */
static int term_width(void) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
    return 80;
}

/* Time */

/* Milliseconds on the monotonic clock. Monotonic means the value only ever
 * increases, so pausing/resuming NTP or the user changing the wall clock
 * cannot make our elapsed time go backward or jump forward. */
static inline int64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* Stopwatch */

/* We track time as two separate quantities that are added together on read:
 *
 *   elapsed: milliseconds fully accounted for, from all completed
 *            running periods before the last pause.
 *
 *   start  : the monotonic timestamp at which the current running
 *            period began. While running, (now - start) is the
 *            unaccounted-for time since the last resume.
 *
 * On pause we fold (now - start) into elapsed and stop updating start.
 * On resume we set start = now and let it run again.
 * sw_read() collapses both into one number without branching on history. */
typedef struct {
    int64_t start;
    int64_t elapsed;
    int running;
    int laps;
} sw_t;

static inline int64_t sw_read(const sw_t *sw) {
    return sw->elapsed + (sw->running ? now_ms() - sw->start : 0);
}

/* Time formatting helper */
typedef struct {
    int h, m, s, ms;
} sw_time_t;

static inline sw_time_t sw_format(int64_t total_ms) {
    sw_time_t t;
    t.h = total_ms / 3600000; total_ms %= 3600000;
    t.m = total_ms / 60000; total_ms %= 60000;
    t.s = total_ms / 1000; total_ms %= 1000;
    t.ms = total_ms;
    return t;
}

static void sw_init(sw_t *sw) {
    sw->start = now_ms();
    sw->elapsed = 0;
    sw->laps = 0;
}

static void sw_pause(sw_t *sw) {
    if (!sw->running) return;
    /* commit this period to elapsed */
    sw->elapsed += now_ms() - sw->start;
    sw->running = 0;
}

static void sw_resume(sw_t *sw) {
    if (sw->running) return;
    /* start a fresh period from right now */
    sw->start = now_ms();
    sw->running = 1;
}

static void sw_reset(sw_t *sw) {
    int was_running = sw->running;
    sw_init(sw);
    sw->running = was_running;
}

/* Display */

/* build into a buffer first, then clamp to terminal width so the line
 * never wraps and \r always lands on the correct row. */
static void display(const sw_t *sw) {
    sw_time_t t = sw_format(sw_read(sw));

    char buf[256];
    int len = snprintf(buf, sizeof(buf),
        "%s %02d:%02d:%02d.%03d   laps: %d"
        "   [space] pause  [l] lap  [r] reset  [q] quit",
        sw->running ? "[RUNNING]" : "[STOPPED]",
        t.h, t.m, t.s, t.ms, sw->laps);

    printf("\r%-*.*s", len, len, buf);
    fflush(stdout);
}

/* Display the Logo */
static void print_logo(void) {
    printf("\n"
           "   ______      __/ /______/ /_ \n"
           "  / ___/ | /| / / __/ ___/ __ \\\n"
           " (__  )| |/ |/ / /_/ /__/ / / /\n"
           "/____/ |__/|__/\\__/\\___/_/ /_/ \n\n");
}

/* Main loop */

int main(void) {
    /* sigaction() is the correct POSIX way to handle signals. Unlike the
     * deprecated signal(), its behavior is well-defined across all platforms:
     * the handler is not reset after the first call, and interrupted syscalls
     * are restarted automatically when SA_RESTART is set. */
    struct sigaction sa;
    sa.sa_handler = handle_sigint;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    /* kill <pid> must also restore the terminal */
    sigaction(SIGTERM, &sa, NULL);

    sw_t sw = { .running = INIT_STATE };
    sw_init(&sw);
    term_raw();

    print_logo();

    /* define the two states of the program */
    enum { STATE_RUNNING, STATE_SIZE_ERROR } state = STATE_RUNNING;

    while (1) {

        /* if the terminal is too small, we don't start the timer*/
        int w = term_width();

        if (w < MIN_WIDTH) {
            if (state == STATE_RUNNING) {
                /* transition to error state: pause and clear the ENTIRE screen */
                if (sw.running) sw_pause(&sw);
                
                /* \e[2J clears the whole screen, \e[H moves the cursor to top-left 
                 * \e[?25l ensures the cursor stays hidden */
                printf("\e[2J\e[H\e[?25l");
                printf("\n  [!] Terminal too small. Please resize the window to continue.\n");
                fflush(stdout);
                
                state = STATE_SIZE_ERROR;
            }

            /* we still need to call read() here. 
             * This maintains our 100ms loop delay (VTIME=1) and 
             * allows the user to press 'q' to quit if they get stuck. */
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                if (c == 'q') exit(0);
            }
            
            /* skip the rest of the loop so we don't draw the stopwatch */
            continue; 
        } else {
            /* if the terminal gets large again, restore the UI */
            if (state == STATE_SIZE_ERROR) {
                /* clear the screen of the error message and ensure cursor is hidden */
                printf("\e[2J\e[H\e[?25l");
                /* reprint the logo */
                print_logo();
                state = STATE_RUNNING;
            }
            
            display(&sw);
        }

        /* VTIME=1 makes read() block for up to 100ms before returning 0.
         * no sleep() or timer needed. */
        char c;
        if (read(STDIN_FILENO, &c, 1) != 1) continue;

        switch (c) {
        case ' ':
            if (state == STATE_RUNNING) {
                sw.running ? sw_pause(&sw) : sw_resume(&sw);
            }
            break;
        case 'l':
            if (state == STATE_RUNNING) {
                /* Print the lap above the running display line. We move to a
                 * new line first so the lap is not overwritten on the next \r. */
                sw.laps++;
                {
                    sw_time_t t = sw_format(sw_read(&sw));
                    /* \r moves to the start of the stopwatch line.
                     * \e[K clears it entirely so we don't leave a trail.
                     * Then we print the lap and move to a new line. */
                    printf("\r\e[K  lap %d: %02d:%02d:%02d.%03d\n",
                        sw.laps, t.h, t.m, t.s, t.ms);
                }
            }
            break;
        case 'r':
            sw_reset(&sw);
            break;
        case 'q':
            exit(0);
        }
    }
}