//! \file crocket.h
//! \brief alternative Rocket client API

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

#ifndef _CROCKET_H_
#define _CROCKET_H_

#ifdef __cplusplus
extern "C" {
#endif

// add definitions for the managed variables
#define var(s,n) extern float s;
#include "crocket_vars.h"
#undef var

//! initialize the Rocket client
//! \param save_file  name of a file to load and save track data from;
//!                   if NULL, the application must handle CROCKET_EVENT_SAVE
//!                   itself
//! \param data  a track data dump; if NULL, data is loaded from save_file
//! \param rpm   the demo's speed in rows per minute for timestamp conversion
//!              (i.e. beats per minute * rows per beat);
//!              use CROCKET_TIME_IN_ROWS to work directly with rows
//!              instead of seconds
//! \returns whether running in player or client mode
//! \note track_data is *not* checked for consistency. The data is expected
//!       to originate from a trusted source without transmission errors.
//! \note If the server could not be reached, the application falls back into
//!       player mode and will never try to reach the server again. In other
//!       words, the server must already run when crocket_init() is called.
extern int crocket_init(const char* save_file, const void* data, float rpm);
// possible return values of crocket_init():
#define CROCKET_MODE_PLAYER 0  //!< player ("standalone") mode
#define CROCKET_MODE_CLIENT 1  //!< client ("slave") mode
// "neutral" value for rpm parameter
#define CROCKET_TIME_IN_ROWS (60.0f)  //!< don't convert seconds into rows

//! uninitialize everything
extern void crocket_done(void);

//! update the library state and all variables
//! \param p_time  pointer to a variable containing the current time in seconds,
//!                or (if CROCKET_TIME_IN_ROWS is used) in rows;
//!                if the server ordered a seek, this value is updated and
//!                CROCKET_EVENT_SEEK is signalled
//! \returns a bitfield describing the current state and flags for events
//!          that occurred during this update
//! \note This function *must* be called multiple times a second, i.e. with
//!       every frame.
extern int crocket_update(float *p_time);
// bitfield assignments of crocket_update():
#define CROCKET_STATE_PLAYING    (1 << 0)  //!< playback is currently running
#define CROCKET_STATE_CONNECTED  (1 << 1)  //!< currently connected to a server
#define CROCKET_EVENT_STOP       (1 << 2)  //!< the application shall pause
#define CROCKET_EVENT_PLAY       (1 << 3)  //!< the application shall continue playback
#define CROCKET_EVENT_SEEK       (1 << 4)  //!< the update resulted in a seek
#define CROCKET_EVENT_CONNECT    (1 << 5)  //!< a new server connection has been established
#define CROCKET_EVENT_DISCONNECT (1 << 6)  //!< the server has disconnected
#define CROCKET_EVENT_SAVE       (1 << 7)  //!< the server requested saving the file
#define CROCKET_EVENT_ACTION(n)  (1 << (8 + (n)))  //!< user-defined action number n

//! get the value of a specific variable at a specific time
//! \param p_var  pointer to the variable to check
//! \param time   the time to query (in seconds or rows)
//! \returns the requested value
extern float crocket_get_value(const float* p_var, float time);

//! switch between client and player mode at runtime
//! \param mode  CROCKET_MODE_PLAYER to disconnect from the server and
//!              continue running in player mode;
//!              CROCKET_MODE_CLIENT to reconnect to the server
extern void crocket_set_mode(int mode);

//! produce a CTF (Crocket Compact Track Format) dump of the track data
//! \param p_size  pointer to a variable that shall receive the size,
//!                in bytes, of the produced data
//! \returns a pointer to the produced track data,
//!          to be free()'d by the application
//! \note always returns NULL and zero size in CROCKET_PLAYER_ONLY mode
extern void* crocket_get_data(int *p_size);

#ifdef __cplusplus
}
#endif

#endif // _CROCKET_H_
