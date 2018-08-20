# GNU Rocket Protocol Description

> **Note:** If you don't know what GNU Rocket is or what it's good for, this document won't be useful to you. If you just want to use GNU Rocket, it won't be useful either. However, if you're planning to implement a new GNU Rocket client and don't want to spend the hour it takes to study the source code and reverse-engineer the protocol, you're at the right spot here.

Client = the demo
Server = the editor

All track data is managed in the **client**. The server is basically just a GUI.

After the client establishes a TCP connection with the server on port 1338, it sends the `CLIENT_GREET` message, which is the 19-byte string "hello, synctracker!" (without the quotes, without trailing newline or null bytes). The server is supposed to answer with `SERVER_GREET`: "hello, demo!" (12 bytes; again without quotes and terminating bytes).

After that, messages are exchanged between client and server in both directions. All messages start with a command byte. All multi-byte values are sent in
big-endian byte order.

Typically, the client will first send a few `GET_TRACK` requests to the server and expects it to answer with a (possibly large) number of `SET_KEY` messages.
The order of the `GET_TRACK` requests also establishes the track-name-to-index mapping that is used throughout the remaining communication. Track indices start counting from zero.


## The `SET_KEY` command (Server to Client)
Requests the client to add or update a keyframe.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 0 (`SET_KEY` command) |
|      1 | UINT32  | track index |
|      5 | UINT32  | row number |
|      9 | FLOAT32 | the keyframe's value |
|     13 | UINT8   | interpolation type (described below) |
total message size: 14 bytes


## The `DELETE_KEY` command (Server to Client)
Requests the client to delete a keyframe.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 1 (`DELETE_KEY` command) |
|      1 | UINT32  | track index |
|      5 | UINT32  | row number |
total message size: 9 bytes


## The `GET_TRACK` command (Client to Server)
Requests the data of a whole track from the server.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 2 (`GET_TRACK` command) |
|      1 | UINT32  | length of the track name |
|      5 | string  | the raw track name (without any terminating bytes) |
total message size: 5 bytes + track name length

> **Note:** Older versions of the protocol (including the one that ships as part of the "0.9" release version) transmit an additional UINT32 before the track name length (i.e. at offset 1). This is the explicit index of the track.


## The `SET_ROW` command (Server to Client)
Requests the client to jump to a specific position.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 3 (`SET_ROW` command) |
|      1 | UINT32  | desired row number |
total message size: 5 bytes


## The `SET_ROW` command (Client to Server)
Informs the server about the client's current playback position.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 3 (`SET_ROW` command) |
|      1 | UINT32  | current row number |
total message size: 5 bytes


## The `PAUSE` command (Server to Client)
Informs the client that it shall pause or resume playback.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 4 (`PAUSE` command) |
|      1 | UINT8   |  nonzero for "pause", zero for "resume playback" |
total message size: 2 bytes


## The `SAVE_TRACKS` command (Server to Client)
Informs the client that it shall save the track data to disk.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 5 (`SAVE_TRACKS` command) |
total message size: 1 byte


## The `ACTION` command (Server to Client)
Informs the client about an arbitrary event, e.g. a debug keypress.
> **Warning:** This is a proprietary extension from mog/TRBL.

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT8   | fixed value 6 (`ACTION` command) |
|      1 | UINT32  | current row number |
total message size: 5 bytes


## Interpolation Modes

| Mode | Name         | Description                       | Pseudocode |
| ---- | ------------ | --------------------------------- | ---------- |
|    0 | `KEY_STEP`   | stay constant until next keyframe | `x = a` |
|    1 | `KEY_LINEAR` | linear interpolation              | `x = lerp(a, b, t)` |
|    2 | `KEY_SMOOTH` | smoothstep interpolation          | `x = lerp(a, b, 3*t^2 - 2*t^3)` |
|    3 | `KEY_RAMP`   | "ramp-up" interpolation           | `x = lerp(a, b, t^2)` |


# GNU Rocket Track File Format Description

The `.track` files simply contain multiple 9-byte records describing each keyframe in the following format:

| Offset | Type    | Description |
| ------ | ------- | ----------- |
|      0 | UINT32  | row number |
|      4 | FLOAT32 | the keyframe's value |
|      8 | UINT8   | interpolation type (described above) |

In contrast to the network protocol, the 32-bit values are stored in the host's native byte order (i.e. little-endian on x86 and ARM), not big-endian.
