#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#else
    #define _DEFAULT_SOURCE
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crocket.h"

#ifndef _WIN32
    #include <time.h>
    #include <unistd.h>
    void Sleep(uint32_t ms) {
        usleep(ms * 1000);
    }
    uint32_t GetTickCount(void) {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (uint32_t)(ts.tv_sec * (uint64_t)1000ull + ts.tv_nsec / (uint64_t)1000000ull);
    }
#endif

int main(void) {
    float t = 0.0f, t0 = 0.0f;
    uint32_t tref = 0;
    bool playing = false;
    int res;

    res = crocket_init("crocket_test.ctf", NULL, 125 * 8);
    printf("mode: %s\n", res ? "client" : "player");

    for (;;) {
        // advance the time when playing
        if (playing) {
            t = t0 + (GetTickCount() - tref) * 0.001f;
        }

        // main state update
        res = crocket_update(&t);

        // update
        if ((res & CROCKET_EVENT_PLAY) || (res & CROCKET_EVENT_SEEK)) {
            t0 = t;
            tref = GetTickCount();
        }
        playing = !!(res & CROCKET_STATE_PLAYING);

        // dump current status
        printf("  %s %c t=%06.2f | foo=%07.2f bar=%07.2f baz=%07.2f\r",
            playing ? "|>" : "[]",
            (res & CROCKET_STATE_CONNECTED) ? '*' : ' ',
            t, foo, bar, baz);
        fflush(stdout);

        // switch to player mode when disconnected
        if (res & CROCKET_EVENT_DISCONNECT) {
            crocket_set_mode(CROCKET_MODE_PLAYER);
        }

        // wait for next "frame"
        Sleep(20);
    }

    crocket_done();
    return 0;
}
