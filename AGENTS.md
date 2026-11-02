# AGENTS.md -- Plugin Loader

`version.dll` for Motor Town: it loads `plugins\mt_plugin_*.dll` and gives them one shared
way into the game. README.md holds the rules and the api.

## Build and test

`build.bat` (MSVC 2022 x64) writes `out\version.dll` and runs `tools\ini_test.cpp`, the check
for the ini merge, the one part of the loader with no game in the loop.

## Rules

- `include/mt_plugin_api.h` is the contract. Fields are only appended and `MT_API_VERSION`
  goes up, so older plugins keep working.
- The loader owns the ProcessEvent hook and engine discovery, once. Plugins never do either.
- Layouts are detected, not assumed: an engine or game update should change a log line,
  not crash.
- `[Mappings] Export=1` in `mt_loader.ini` writes `Mappings.usmap` about 10 seconds after
  the game's systems load, so a crash at startup means no dump.
- Every plugin repo carries a copy of `include/`. Changing the api means updating those.
