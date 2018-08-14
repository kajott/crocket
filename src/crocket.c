//! \file crocket.c
//! \brief alternative Rocket client implementation
//! \note compile with -DCROCKET_PLAYER_ONLY to get a pure player version
//!       without any client, editing and saving functionality

// Copyright (C) 2018 Martin J. Fiedler (KeyJ^TRBL)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#ifdef _WIN32
    #define _CRT_NONSTDC_NO_WARNINGS  // MSVC: accept _strdup
    #define _CRT_SECURE_NO_WARNINGS   // MSVC: accept fopen
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
    #include <WinSock2.h>
    #include <ws2tcpip.h>
#else
    #define _DEFAULT_SOURCE  // glibc: accept strdup
    #include <unistd.h>
    #include <sys/types.h>
    #include <sys/socket.h>
    #include <sys/select.h>
    #include <netinet/in.h>
    #include <netdb.h>
    #include <time.h>
    #define closesocket close
    typedef int SOCKET;
    #define INVALID_SOCKET (-1)
    #define SOCKET_ERROR (-1)
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crocket.h"

#define var(s,n) float s;
#include "crocket_vars.h"
#undef var

typedef struct _key {
    unsigned int row;
    float value;
    unsigned char interpol;
} crocket_key_t;

typedef struct _track {
    float *p_var;
    const char* name;
    unsigned int nkeys;
    unsigned int alloc;
    crocket_key_t* keys;
} crocket_track_t;

crocket_track_t crocket_tracks[] = {
#define var(s,n) { &s, n, 0, 0, NULL },
#include "crocket_vars.h"
#undef var
{ NULL, }
};

static const unsigned int ntracks = (sizeof(crocket_tracks) / sizeof(crocket_track_t)) - 1;

int crocket_current_state = 0;              //!< current state/event bitmask
float crocket_timescale = 1.0f;             //!< seconds-to-rows conversion factor
#ifndef CROCKET_PLAYER_ONLY
char* crocket_save_file = NULL;             //!< save file name
int crocket_mode = 0;                       //!< current mode (client/player)
int crocket_current_row = -1;               //!< current row in editor
SOCKET crocket_socket = INVALID_SOCKET;     //!< current connection socket
struct sockaddr_in crocket_server_address;  //!< resolved server address
#endif // CROCKET_PLAYER_ONLY

#define INITIAL_KEY_ALLOC 16  //!< keys to allocate initially for each track
#define RECONNECT_TIMEOUT 20  //!< reconnect timeout in milliseconds

static void load_data(const unsigned char* pos);


///////////////////////////////////////////////////////////////////////////////
///// TRACK DATA ACCESS AND MANIPULATION                                  /////
///////////////////////////////////////////////////////////////////////////////

// get index of key that corresponds to a specific row, *plus 1*
// (so 0 = before first key)
static unsigned int find_key(const crocket_track_t* t, unsigned int row) {
    unsigned int a, b, c, pivot;
    if (!t->nkeys || (row < t->keys[0].row)) {
        return 0;  // before first key
    }
    a = 0;
    b = t->nkeys;
    while ((a + 1) < b) {
        c = (a + b) >> 1;
        pivot = t->keys[c].row;
        if (row == pivot) {
            return c + 1;  // shortcut for exact hit
        }
        if (row > pivot) { a = c; }
                    else { b = c; }
    }
    return a + 1;
}

static float sample(const crocket_track_t* t, float row) {
    const crocket_key_t* k;
    unsigned int pos;
    float x;
    if (!t->nkeys) { return 0.0f; }  // empty track
    pos = find_key(t, (row <= 0.0f) ? 0 : (unsigned int)row);
    if (!pos) { return t->keys[0].value; }  // before first key
    k = &t->keys[pos-1];
    if ((pos >= t->nkeys) || !k[0].interpol) { return k[0].value; }  // after last key, or uninterpolated
    x = (row - (float)k[0].row) / (float)(k[1].row - k[0].row);
    switch (k[0].interpol) {
        case 1:  /* linear */     break;
        case 2:  /* smoothstep */ x *= x * (3.0f - 2.0f * x);  break;
        case 3:  /* ramp-up */    x *= x; break;
        default: /* unknown */    x = 0.0f; break;
    }
    return k[0].value + x * (k[1].value - k[0].value);
}

#ifndef CROCKET_PLAYER_ONLY

static void set_key(unsigned int track_index, unsigned int row, float value, unsigned char interpol) {
    crocket_track_t* t;
    crocket_key_t* k;
    unsigned int pos;
    if (track_index >= ntracks) { return; }
    t = &crocket_tracks[track_index];
    pos = find_key(t, row);

    // update existing key
    if (pos && (t->keys[pos-1].row == row)) {
        k = &t->keys[pos-1];
        k->value = value;
        k->interpol = interpol;
        return;
    }

    // extend memory
    if (t->nkeys >= t->alloc) {
        t->alloc = t->alloc ? (t->alloc << 1) : INITIAL_KEY_ALLOC;
        t->keys = realloc(t->keys, t->alloc * sizeof(crocket_key_t));
        if (!t->keys) {
            t->nkeys = t->alloc = 0; return;  // oops, out of memory
        }
    }

    // insert key = move following keys forward
    if (pos < t->nkeys) {
        memmove(&t->keys[pos+1], &t->keys[pos], (t->nkeys - pos) * sizeof(crocket_key_t));
    }
    ++t->nkeys;

    // set key data
    k = &t->keys[pos];
    k->row = row;
    k->value = value;
    k->interpol = interpol;
}

static void delete_key(unsigned int track_index, unsigned int row) {
    crocket_track_t* t;
    unsigned int pos;
    if (track_index >= ntracks) { return; }
    t = &crocket_tracks[track_index];
    pos = find_key(t, row);
    if (!pos || (t->keys[pos-1].row != row)) {
        return;  // no such key
    }
    if (pos < t->nkeys) {
        memmove(&t->keys[pos-1], &t->keys[pos], (t->nkeys - pos) * sizeof(crocket_key_t));
    }
    --t->nkeys;
}

#endif // CROCKET_PLAYER_ONLY


///////////////////////////////////////////////////////////////////////////////
///// NETWORK CONNECTION HANDLING                                         /////
///////////////////////////////////////////////////////////////////////////////

#ifndef CROCKET_PLAYER_ONLY

static void disconnect(void) {
    if (crocket_socket != INVALID_SOCKET) {
        closesocket(crocket_socket);
        crocket_socket = INVALID_SOCKET;
    }
    if (crocket_current_state & CROCKET_STATE_CONNECTED) {
        crocket_current_state |= CROCKET_EVENT_DISCONNECT;
    }
    crocket_current_state &= ~CROCKET_STATE_CONNECTED;
}

static int xsend(const void* data, int bytes) {
    const char* data_pos = data;
    if (crocket_socket == INVALID_SOCKET) { return 0; }
    while (bytes > 0) {
        int res = send(crocket_socket, data_pos, bytes, 0);
        if (res <= 0) {
            disconnect();
            return 0;
        }
        bytes -= res;
        data_pos += res;
    }
    return 1;
}

static int xrecv(void* data, int bytes) {
    char* data_pos = data;
    if (crocket_socket == INVALID_SOCKET) { return 0; }
    while (bytes > 0) {
        int res = recv(crocket_socket, data_pos, bytes, 0);
        if (res <= 0) {
            disconnect();
            return 0;
        }
        bytes -= res;
        data_pos += res;
    }
    return 1;
}

static int handle_messages(int timeout_usec) {
    struct timeval tv;
    if (crocket_socket == INVALID_SOCKET) { return 0; }
    tv.tv_sec = 0;  // note: timeout_usec *must* be less than 1 million!
    tv.tv_usec = timeout_usec;
    for (;;) {
        unsigned char cmd;

        // new message pending?
        fd_set fds;
        int res;
        FD_ZERO(&fds);
        FD_SET(crocket_socket, &fds);
        res = select((int)crocket_socket + 1, &fds, NULL, NULL, &tv);
        if (res == 0) {
            return 1;  // no new messages
        }
        if (res != 1) {
            // an error occurred
            disconnect();
            return 0;  
        }

        // read command
        if (!xrecv(&cmd, 1)) { return 0; }
        switch (cmd) {

            case 0: {  // SET_KEY
                #pragma pack(push, 1)
                struct _set_key_params {
                    unsigned int track_index;
                    unsigned int row;
                    union _val { unsigned int i; float f; } value;
                    unsigned char interpol;
                } p;
                #pragma pack(pop)
                if (!xrecv(&p, 13)) { return 0; }
                p.value.i = ntohl(p.value.i);
                set_key(ntohl(p.track_index), ntohl(p.row), p.value.f, p.interpol);
                break; }

            case 1: {  // DELETE_KEY
                #pragma pack(push, 1)
                struct _delete_key_params {
                    unsigned int track_index;
                    unsigned int row;
                } p;
                #pragma pack(pop)
                if (!xrecv(&p, 8)) { return 0; }
                delete_key(ntohl(p.track_index), ntohl(p.row));
                break; }

            case 3: { // SET_ROW
                unsigned int row;
                if (!xrecv(&row, 4)) { return 0; }
                crocket_current_row = ntohl(row);
                crocket_current_state |= CROCKET_EVENT_SEEK;
                break; }

            case 4: { // PAUSE
                unsigned char mode;
                if (!xrecv(&mode, 1)) { return 0; }
                if (mode) {
                    crocket_current_state = (crocket_current_state | CROCKET_EVENT_STOP) & (~(CROCKET_EVENT_PLAY | CROCKET_STATE_PLAYING));
                }
                else {
                    crocket_current_state = (crocket_current_state | (CROCKET_EVENT_PLAY | CROCKET_STATE_PLAYING)) & (~CROCKET_EVENT_STOP);
                }
                break; }

            case 5: // SAVE_TRACKS
                crocket_current_state |= CROCKET_EVENT_SAVE;
                break;

            case 6: { // ACTION
                unsigned int action;
                if (!xrecv(&action, 4)) { return 0; }
                crocket_current_state |= CROCKET_EVENT_ACTION(ntohl(action));
                break; }

            default:  // unknown command
                break;
        }   // end of command switch
    }   // end of message receive loop
}

static void reconnect(void) {
    crocket_track_t* t;
    char server_greet[12];
#ifdef _WIN32
    DWORD timeout = RECONNECT_TIMEOUT, notimeout = 0;
#else
    struct timeval timeout, notimeout;
    timeout.tv_sec = (RECONNECT_TIMEOUT / 1000);
    timeout.tv_usec = (RECONNECT_TIMEOUT % 1000) * 1000;
    notimeout.tv_sec = notimeout.tv_usec = 0;
#endif

    // don't do anything if connected, else clean up the connection first
    if ((crocket_mode == CROCKET_MODE_PLAYER)
    ||  (crocket_current_state & CROCKET_STATE_CONNECTED))
        { return; }
    disconnect();

    // create socket
    crocket_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (crocket_socket == INVALID_SOCKET) { return; }

    // set timeout for connect()
    setsockopt(crocket_socket, SOL_SOCKET, SO_SNDTIMEO, (void*)&timeout, sizeof(timeout));
    setsockopt(crocket_socket, SOL_SOCKET, SO_RCVTIMEO, (void*)&timeout, sizeof(timeout));

    // connect to server
    if (connect(crocket_socket, (const struct sockaddr*)&crocket_server_address, sizeof(crocket_server_address))) {
        disconnect();
        return;
    }

    // reset timeout
    setsockopt(crocket_socket, SOL_SOCKET, SO_SNDTIMEO, (void*)&notimeout, sizeof(notimeout));
    setsockopt(crocket_socket, SOL_SOCKET, SO_RCVTIMEO, (void*)&notimeout, sizeof(notimeout));

    // CLIENT_GREET - SERVER_GREET exchange
    if (!xsend("hello, synctracker!", 19)
    ||  !xrecv(server_greet, 12)
    ||  memcmp(server_greet, "hello, demo!", 12)) {
        disconnect();
        return;
    }

    // give the server a list of all tracks (and clear them while we're at it)
    for (t = crocket_tracks;  t->name;  ++t) {
        #pragma pack(push, 1)
        struct _get_track_cmd {
            unsigned char cmd;
            unsigned int name_length;
        } cmd;
        #pragma pack(pop)
        unsigned int name_length = (unsigned int)strlen(t->name);
        t->nkeys = 0;
        cmd.cmd = 2;  // GET_TRACK
        cmd.name_length = htonl(name_length);
        if (!xsend(&cmd, 5) || !xsend(t->name, name_length) || !handle_messages(0)) { return; }
    }

    // finally, wait up to 100 ms for the server to settle
    if (!handle_messages(100000)) {
        return;
    }

    // that's it, we're connected!
    crocket_current_state |= CROCKET_STATE_CONNECTED | CROCKET_EVENT_CONNECT;
}

#else // CROCKET_PLAYER_ONLY

#define reconnect()

#endif // CROCKET_PLAYER_ONLY



///////////////////////////////////////////////////////////////////////////////
///// CORE API                                                            /////
///////////////////////////////////////////////////////////////////////////////

int crocket_init(const char* save_file, const void* data, float rpm) {
#ifndef CROCKET_PLAYER_ONLY
    char* host;
#endif
#ifdef _WIN32
    static int wsa_initialized = 0;
    if (!wsa_initialized) {
        WSADATA dummy;
        WSAStartup(MAKEWORD(2, 2), &dummy);
        wsa_initialized = 1;
    }
#endif

    // prepare all variables
    crocket_done();
    crocket_timescale = rpm / 60.0f;
#ifndef CROCKET_PLAYER_ONLY
    crocket_current_state = 0;
    crocket_current_row = -1;
    crocket_save_file = save_file ? strdup(save_file) : NULL;
    crocket_mode = CROCKET_MODE_CLIENT;

    // set the server address
    host = getenv("CROCKET_SERVER");
    if (host) {
        host = strdup(host);  // we're gonna modify the value, so copy it
    }
    if (host) {
        struct addrinfo hints, *res = NULL;
        char *port = strchr(host, ':');
        if (port) { *port++ = '\0'; }
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        if (!getaddrinfo(host, port ? port : "1338", &hints, &res) && res) {
            memcpy(&crocket_server_address, res->ai_addr, sizeof(crocket_server_address));
        }
        else {
            crocket_mode = CROCKET_MODE_PLAYER;  // host not found, can't connect
        }
        if (res) { freeaddrinfo(res); }
    }
    else {
        // default address: localhost:1338
        crocket_server_address.sin_family = AF_INET;
        crocket_server_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        crocket_server_address.sin_port = htons(1338);
    }
    free(host);

    // try to initialize client mode; if that failed, switch to player mode
    reconnect();
    if (!(crocket_current_state & CROCKET_STATE_CONNECTED)) {
        crocket_set_mode(CROCKET_MODE_PLAYER);
#endif // CROCKET_PLAYER_ONLY

        // load track data from file
        void* loaded_data = NULL;
        if (save_file && save_file[0] && !data) {
            FILE *f = fopen(save_file, "rb");
            if (f) {
                size_t s;
                fseek(f, 0, SEEK_END);
                s = ftell(f);
                fseek(f, 0, SEEK_SET);
                loaded_data = malloc(s);
                if (loaded_data && (fread(loaded_data, 1, s, f) == s)) {
                    data = loaded_data;
                }
            }
        }

        // import track data (from file or provided buffer)
        load_data(data);
        free(loaded_data);        
#ifndef CROCKET_PLAYER_ONLY
    }
    return crocket_mode;
#else // CROCKET_PLAYER_ONLY
    crocket_current_state = CROCKET_STATE_PLAYING | CROCKET_EVENT_PLAY;
    return CROCKET_MODE_PLAYER;
#endif // CROCKET_PLAYER_ONLY
}

void crocket_done(void) {
    crocket_track_t* t;
#ifndef CROCKET_PLAYER_ONLY
    disconnect();
    free(crocket_save_file);
    crocket_save_file = NULL;
#endif // CROCKET_PLAYER_ONLY
    for (t = crocket_tracks;  t->name;  ++t) {
        free(t->keys);
        t->keys = NULL;
        t->nkeys = t->alloc = 0;
    }
}

int crocket_update(float *p_time) {
    const crocket_track_t* t;
    float row;
    int res;

    // convert time into rows
    if (!p_time) {
        return crocket_current_state;  // no time specified? then quit here
    }
    row = *p_time * crocket_timescale;
    if (row < 0.0f) { row = 0.0f; }

#ifndef CROCKET_PLAYER_ONLY
    // react on network, update state
    reconnect();
    handle_messages(0);

    // time update -- make the app time and crocket_current_row consistent
    if (crocket_current_state & CROCKET_EVENT_SEEK) {
        // seeking: server time to application time (converted into seconds)
        // (need to add a small offset to avoid rounding errors)
        row = crocket_current_row ? ((float)crocket_current_row + (1.0f / 65536)) : 0.0f;
        *p_time = row / crocket_timescale;
    }
    else {
        // not seeking: application time to server time (if a new row started)
        int new_row = (int)row;
        if (new_row != crocket_current_row) {
            #pragma pack(push, 1)
            struct _set_row_cmd {
                unsigned char cmd;
                unsigned int row;
            } cmd;
            #pragma pack(pop)
            cmd.cmd = 3;  // SET_ROW
            cmd.row = htonl(new_row);
            xsend(&cmd, 5);
            crocket_current_row = new_row;
        }
    }

    // handle save-to-file command
    if ((crocket_current_state & CROCKET_EVENT_SAVE)
    && crocket_save_file && crocket_save_file[0]) {
        int size = 0;
        void* data = crocket_get_data(&size);
        if (data && (size > 0)) {
            FILE *f = fopen(crocket_save_file, "wb");
            if (f) {
                (void) fwrite(data, 1, size, f);
                fclose(f);
            }
        }
        free(data);
    }
#endif // CROCKET_PLAYER_ONLY

    // sample current value for all tracks
    for (t = crocket_tracks;  t->name;  ++t) {
        *t->p_var = sample(t, row);
    }

    // done -- return state/event bitmask and clear the event part of it,
    // now that the events have been delivered to the application
    res = crocket_current_state;
    crocket_current_state &= CROCKET_STATE_CONNECTED | CROCKET_STATE_PLAYING;
    return res;
}

float crocket_get_value(const float* p_var, float time) {
    const crocket_track_t* t;
    for (t = crocket_tracks;  t->name;  ++t) {
        if (t->p_var == p_var) {
            return sample(t, time * crocket_timescale);
        }
    }
    return 0.0f;
}

void crocket_set_mode(int mode) {
#ifndef CROCKET_PLAYER_ONLY
    mode = !!mode;
    if (mode == crocket_mode) { return; }
    crocket_mode = mode;
    if (mode == CROCKET_MODE_PLAYER) {
        disconnect();
        crocket_current_state |= CROCKET_STATE_PLAYING | CROCKET_EVENT_PLAY;
    }
#else // CROCKET_PLAYER_ONLY
    (void) mode;
#endif // CROCKET_PLAYER_ONLY
}


///////////////////////////////////////////////////////////////////////////////
///// IMPORT / EXPORT                                                     /////
///////////////////////////////////////////////////////////////////////////////

//! \page ctf_format  CTF File Format
//! CTF files are binary files that contain all tracks in a single file.
//!
//! Integer values in the file are generally coded in LEB128 format
//! (see https://en.wikipedia.org/wiki/LEB128) with variable length.
//! Floating-point values are coded as native-endian IEEE754 single-precision
//! values (always 4 bytes). Strings are encoded as ASCII strings without
//! a termination byte, but prefixed with an LEB128 indicating the length.
//!
//! The file starts with a 16-byte signature consisting of three parts:
//! - the constant string "crocket<LF>" (8 bytes; "<LF>" is 10 decimal)
//! - the version number as floating-point value
//! - the constant string "<CR><LF><NUL><SUB>" (13, 10, 0, 26 decimal)
//! This signature has been chosen so that several kinds of transmission
//! and compatibility issues are detected:
//! - LF vs. CRLF (signature contains both)
//! - null byte (string terminator) and old DOS text file terminator
//! - version number in floating point to detect endianness issues
//!
//! After the signature, the following elements are encoded:
//! - LEB128 number of tracks (may be zero)
//! - for each track:
//!   - LEB128 track name length
//!   - STRING track name
//!   - LEB128 number of keys (may be zero)
//!   - for each key:
//!     - LEB128 number of empty rows between the last key and this one
//!              (for the first key, this is the row number)
//!     - FLOAT value
//!     - BYTE interpolation mode
//!
//! Empty tracks (i.e. tracks without any keys) may be omitted from the file.

#define CTF_FILE_HEADER_PART1  "crocket\n"
#define CTF_FILE_VERSION       1.0f
#define CTF_FILE_HEADER_PART3  "\r\n\0\x1a"
#define CTF_FILE_HEADER_LENGTH 16

#define MAX_LEB128_SIZE 5  //!< maximum size, in bytes, of a LEB128 value

#ifndef CROCKET_PLAYER_ONLY

inline unsigned char* put_data(unsigned char* pos, const char* data, int size) {
    memcpy(pos, data, size);
    return pos + size;
}
inline unsigned char* put_float(unsigned char* pos, float f) {
    union _val { float f; unsigned char b[4]; } conv;
    conv.f = f;
    memcpy(pos, conv.b, 4);
    return pos + 4;
}
inline unsigned char* put_leb128(unsigned char* pos, unsigned int val) {
    while (val >= 128) {
        *pos++ = ((unsigned char)val & 0x7F) | 0x80;
        val >>= 7;
    }
    *pos++ = (unsigned char)val;
    return pos;
}

void* crocket_get_data(int *p_size) {
    const crocket_track_t* t;
    const crocket_key_t* k;
    void* data;
    unsigned char* pos;
    unsigned int ref, size, key_count;

    // estimate maximum image size and count tracks
    size = CTF_FILE_HEADER_LENGTH + MAX_LEB128_SIZE;
    ref = 0;
    for (t = crocket_tracks;  t->name;  ++t) {
        size += (unsigned int)strlen(t->name) + 2 * MAX_LEB128_SIZE + t->nkeys * (MAX_LEB128_SIZE + 5);
        if (t->nkeys) { ++ref; }
    }

    // allocate data
    data = malloc(size);
    if (!data) { return NULL; }
    pos = data;

    // generate header
    pos = put_data(pos, CTF_FILE_HEADER_PART1, 8);
    pos = put_float(pos, CTF_FILE_VERSION);
    pos = put_data(pos, CTF_FILE_HEADER_PART3, 4);
    pos = put_leb128(pos, ref);

    // dump tracks
    for (t = crocket_tracks;  t->name;  ++t) {
        key_count = t->nkeys;
        if (!key_count) { continue; }
        ref = (unsigned int)strlen(t->name);
        pos = put_leb128(pos, ref);
        pos = put_data(pos, t->name, ref);
        pos = put_leb128(pos, key_count);
        ref = 0;
        for (k = t->keys;  key_count;  ++k, --key_count) {
            pos = put_leb128(pos, k->row - ref);
            pos = put_float(pos, k->value);
            *pos++ = k->interpol;
            ref = k->row + 1;
        }
    }

    // finished -- compute size
    size = (int)(pos - (unsigned char*)data);
    if (p_size) { *p_size = size; }
    return data;
}

#else // CROCKET_PLAYER_ONLY

void* crocket_get_data(int *p_size) {
    if (p_size) { *p_size = 0; }
    return NULL;
}

#endif // CROCKET_PLAYER_ONLY

//--//--//--//--//--//--//--//--//--//--//--//--//--//--//--//--//--//--//--//

inline const unsigned char* get_leb128(const unsigned char* pos, unsigned int *p_val) {
    unsigned int val = 0, shift = 0;
    for (shift = 0;  shift < 32;  shift += 7) {
        val |= (pos[0] & 0x7F) << shift;
        if (!(*pos++ & 0x80)) { break; }
    }
    *p_val = val;
    return pos;
}

static void load_data(const unsigned char* pos) {
    crocket_track_t* t;
    crocket_key_t *k, dummy_key;  // dummy key to read data into for unknown tracks
    unsigned int track_count, len, row;
    const float version = CTF_FILE_VERSION;

    // check header
    if (!pos) { return; }
    if (memcmp(&pos[ 0], CTF_FILE_HEADER_PART1, 8)
    ||  memcmp(&pos[ 8], &version, 4)
    ||  memcmp(&pos[12], CTF_FILE_HEADER_PART3, 4)) {
        return;
    }
    pos += 16;

    // iterate over tracks
    for (pos = get_leb128(pos, &track_count);  track_count;  --track_count) {
        // search for the proper track (or sentinel track at end of list if not found)
        pos = get_leb128(pos, &len);
        for (t = crocket_tracks;  t->name;  ++t) {
            if ((strlen(t->name) == len) && !memcmp(t->name, pos, len))
                { break; }
        }
        pos += len;

        // read track length, allocate memory for keys
        pos = get_leb128(pos, &len);
        t->nkeys = len;
        if (!len) { continue; }
        if (t->name) {
            free(t->keys);
            t->keys = k = malloc(len * sizeof(crocket_key_t));
            t->alloc = len;
            if (!k) { t->nkeys = t->alloc = 0; k = &dummy_key; }
        }
        else {
            k = &dummy_key;
        }

        // read and decode key data
        // (for unknown tracks, this only reads into dummy_key)
        row = 0;
        while (len--) {
            pos = get_leb128(pos, &k->row);
            memcpy(&k->value, pos, 4); pos += 4;
            k->interpol = *pos++;
            if (!t->name) { continue; }
            k->row += row;
            row = k->row + 1;
            ++k;
        }
    }
}
