# True Directional Movement API (vendored)

`TrueDirectionalMovementAPI.h` is True Directional Movement's modder API header, copied from
https://github.com/ersh1/TrueDirectionalMovement (`src/TrueDirectionalMovementAPI.h`, commit `57b913ae17fe`), as its
header invites ("Copy this file into your own project if you wish to use this API").

License: GNU GPL v3.0 (`COPYING`) with the modding and linking exceptions in `EXCEPTIONS`, both copied unchanged from
the same repository. Community Behaviors is GPL-3.0 as well.

Modified: `RequestPluginAPI` calls `GetModuleHandleA` instead of `GetModuleHandle`, so the narrow module name compiles
in Community Behaviors' UNICODE build. The change is marked in the file.
