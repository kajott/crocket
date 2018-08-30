# CRocket
**an alternative Rocket sync tracker client**

CRocket is an implementation of the client part of the [Rocket](https://rocket.github.io/) protocol. (If you don't know what this is or never have used Rocket before, you're likely not part of the target audience of this project.)

If offers the following features:

- implemented in plain C (C99)
  - compatible with 32/64-bit architectures that support unaligned data access and have IEEE754 floating-point support (i.e. x86/x86_64 and ARMv7/v8 is fine)
  - cross-platform (tested on Windows and GNU/Linux so far)
- just two files (one `.h` and `.c` each), less than 1000 lines in total (including comments)
  - can be easily transformed into a header-only library, if that's your thing
- very streamlined API
  - no need to query each variable manually with `sync_get_track` everytime it's used
  - sync variables are really just variables, updated (almost) automatically
  - central "registry" of sync tracks declares all variables
  - no callbacks
- API-side timestamps in seconds instead of rows
- automatic switching between client and player mode
- can be compiled in "player-only" mode
  - eliminates all client and editing code under the hood
  - but API stays 100% the same
- custom compact track format (CTF)
  - all tracks in one file (instead of dozens of small files)
  - no issues with special characters in track names



## Tutorial

Put `crocket.h` and `crocket.c` somewhere into your project and create a new file `crocket_vars.h` that declares all tracks and their variables, like this:

```c
var(cvCamPosX, "camera:pos.x")
var(cvCamPosY, "camera:pos.y")
var(cvCamPosZ, "camera:pos.z")
var(cvFlash,   "flash")
```

(Note the absence of any `#include` stuff and -- most importantly -- include guards and semicolons.)

This sets up four variables with their names in C code and the corresponding track names, which may possibly have special names to use the grouping features of common Rocket editors.

The variables are simply variables of type `float`, there's nothing special about them. You can get access to them in your code by including `crocket.h` or just declaring them as `extern float`.

To initialize the client or player, use `crocket_init` and specify the name of the track data file to load or save and the tempo of the music in "rows per minute", i.e. the product from beats per minute and rows per beat:

```c
crocket_init("sync.ctf", NULL, 125 * 8);
```

This function tries to connect to a Rocket server. If it succeeds, it will run in "client mode" and get all track data from the server (i.e. the tracker GUI). If the Rocket server could not be reached, it falls back to "player mode", loads track data from the specified file or from memory (that's what the `NULL` parameter in the example call would be used for) and plays this. `crocket_init` returns a value that specifies which mode it uses, but normally you don't need it.

The most important function is `crocket_update`, which must be called once every frame with a pointer(!) to the current time (in seconds) as a `float` value:

```c
int res = crocket_update(&currentTime);
```

`crocket_update` handles all the network communication stuff, updates all registered variables and returns a bitmask that contains (among others) bits that notify about special events:

- `CROCKET_EVENT_PLAY` is signalled when the application shall start playback; from that point on, the time parameter passed to `crocket_update` is expected to increase with every frame
- `CROCKET_EVENT_STOP` is signalled when the application shall stop playback; from that point on, the time parameter passed to `crocket_update` is expected to stay constant across frames
- `CROCKET_EVENT_SEEK` is signalled when the time variable passed to `crocket_update` has been modified; if playback was running during the seek, the application is expected to continue playback from there

A full example can look like this (assuming a suitable `audioPlayer` implementation that has play/pause controls and a seek operation that works in both of these states):

```c
crocket_init("sync.crf", NULL, 125 * 8);
audioPlayer.initialize("music.mp3");  // important: must start in "paused" mode
while (true) {
    float t = audioPlayer.getCurrentPlaybackPosition();
    int res = crocket_update(&t);
    if (res & CROCKET_EVENT_STOP) audioPlayer.pause();
    if (res & CROCKET_EVENT_SEEK) audioPlayer.seek(t);
    if (res & CROCKET_EVENT_PLAY) audioPlayer.play();
    renderFrame();
}
```



## API Reference

Please refer to the Doxygen-style comments in `crocket.h` for a detailed list of all API calls, parameters and enumerations.



## Specific Topics


### Time Translation

The Rocket protocol itself only manages time in tracker rows. However, this is usually an inconvenient representation to work with in an application, as the timing and audio APIs is has to deal with are usually based on seconds. To get rid of this, the speed of the soundtrack can be specified once in `crocket_init` and the time values going into and coming out of `crocket_update` are automatically converted from seconds into rows and vice-versa.

If such a translation is not desired, the value `CROCKET_TIME_IN_ROWS` can be specified in the call to `crocket_init`.


### Player vs. Client Mode

If a conection to a server could be made in `crocket_init`, *client mode* is engaged, in which the library acts purely as a slave to the server's editor GUI. Track data is **not** loaded from disk or the provided pointer, only from the server.

If a server connection could **not** be established in `crocket_init`, *player mode* is used. Track data is loaded from disk or the provided pointer. The first call to `crocket_update` reports a `CROCKET_EVENT_PLAY` event.

The application doesn't really need to care about modes; all it needs to do is start up in the "paused" state with a time of zero, and `crocket_init` and `crocket_update` will take care of the rest.


### Track Data Loading and Saving

Track data is loaded in `crocket_init` and saved whenever the "save data" command is received in client mode (in most editors, this can be sent with Ctrl+E).

If the `track_data` parameter to `crocket_init` is not `NULL`, track data will be loaded from memory at the specified address, regardless of the value of `save_file`.

If `save_file` is specified (i.e. neither `NULL` nor an empty string), the specified file name is used to load track data in player mode (unless `track_data` is specified) and save it back to disk on user request. In this case, the servers's "remote save" command is handled transparently in the library.

If `save_file` is not specified, track data can only be loaded from the `track_data` parameter; if this is unspecified too, player mode won't work in a meaningful manner, as all tracks will be empty. Saving can't be handled by the library itself either, but the application can react on `CROCKET_EVENT_SAVE` and use the `crocket_get_track_data` function to get an in-memory dump of the track file data and implement some other means of data persistence.

A custom binary data format ("CTF" - Compact Track Format) is used for the load and save operations. This is a simple, but rather compact representation of the track data, using variable-length integers and delta coding for timestamps. All tracks are combined into a single data stream. Track data of the Rocket reference implementation is not supported.

Please note that the CTF loader is **not** resilient against accidentally or deliberately corrupted data; only a short signature check is performed to reject obvious non-CTF files. Other than that, the loader will happily overflow buffers if anything doesn't check out, so it's important to only ever feed it verified data from a trusted source.

CTF files are essentially machine-, platform- and architecture-neutral, except for endianness. Loading a CTF file on a big-endian machine that was saved on a little-endian machine or vice-versa will fail the file signature check.

A detailed description of the CTF format can be found as a comment block in `crocket.c`.


### Connect and Reconnect Behavior

The timeout for connecting to a server is just 20 milliseconds. This is sufficient for servers running on localhost or another computer in the same local network. With the short timeout, detection of an unavailable server is much faster; in other words, the demo start up quicker when no server is reachable.

By default, `crocket_init` connect to a server running on the same machine (`localhost`) at Rocket's default port (1338). This can be overridden by specifying a different IP address or host name and (optionally) a different port in the `CROCKET_SERVER` environment variable (e.g. `CROCKET_SERVER=192.168.1.23:4567`).

If the connection is interrupted while running in client mode, this is detected and `CROCKET_EVENT_DISCONNECT` is signalled. Client mode will **not** be left automatically; instead, a reconnection attempt is made during every future frame. This slows down things **a lot** because in this scenario, 20 milliseconds of waiting is a lot and the operating system might add a couple hundred milliseconds on top of that too, but this way, it's at least possible to reconnect with a server if it crashed, for example.
If you want the client to automatically switch to player mode in such a scenario instead, you can do this by reacting on `CROCKET_EVENT_DISCONNECT` and switching into player mode with `crocket_set_mode`:

```c
if (res & CROCKET_EVENT_DISCONNECT) {
    crocket_set_mode(CROCKET_MODE_PLAYER);
}
```

After switching to player mode, no reconnect attempts will be made, until switching back to server mode again. `CROCKET_EVENT_PLAY` is generated when switching from client mode to player mode in paused state.


### Timed Variable Queries and Low-Level API

In addition to the automatic updates that are done by `crocket_update`, the `crocket_get_value` function can be used to query the value of a specific variable at an arbitrary time.

Some of the internal data structures of crocket are exposed via a low-level API that provides direct read access to the track and keyframe data. This allows more complex queries than the normal `crocket_uodate` and `crocket_get_value` function can provide.


### Player-Only Mode

If the preprocessor define `CROCKET_PLAYER_ONLY` is set during compilation, everything related to client mode and file saving is omitted from the compiled code. This results in the following behavioral changes:

- `crocket_init` doesn't try to establish a server connection, it always starts up in player mode and returns `CROCKET_MODE_PLAYER`
- `crocket_set_mode` is a no-op, i.e. switching to client mode is not possible
- `crocket_get_track_data` always returns a `NULL` pointer and a size of zero

Note that the API remains exactly the same, even if compiled with `CROCKET_PLAYER_ONLY`.
