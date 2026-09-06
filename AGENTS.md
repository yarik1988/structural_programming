# AGENTS.md — notes for AI coding agents

Teaching repo (C++, university course assignments). Windows 11, git bash shell.
Everything here was verified by actually running the code, not assumed.

## Locating built executables

**The rule:** exes are NOT in the build root. Ninja writes each target next to a
mirror of its source directory:

```
cmake-build-debug/<source-dir-of-the-target>/<TARGET>.exe
```

Example: target `GRAPH` is defined in `semester_1/graphics_freeglut/CMakeLists.txt`
→ `cmake-build-debug/semester_1/graphics_freeglut/GRAPH.exe`.

Fastest lookup (works even if you only know the target name):

```bash
find cmake-build-debug -name "<TARGET>.exe" -not -path "*/CMakeFiles/*" -not -path "*/_deps/*"
```

GUI targets of interest:

| Target | exe | Window title | GUI lib |
|---|---|---|---|
| `GRAPH` | `cmake-build-debug/semester_1/graphics_freeglut/GRAPH.exe` | `Graph application` | freeglut + ImGui |
| `FLOODFILL` | `.../graphics_freeglut/FLOODFILL.exe` | (freeglut) | freeglut |
| `DYN_PICT` | `.../graphics_freeglut/DYN_PICT.exe` | (freeglut) | freeglut |
| `TRIANGLE_SQUARE`, `INTERVAL_INTERSECTION` | same dir | (freeglut) | freeglut |
| `Graph_SFML`, `GAME_LIFE`, `3D`, others | `cmake-build-debug/semester_1/graphics/` | `Graph application` (Graph_SFML) | SFML |

Non-GUI exes (Arrays, Combinatorics, Strings, Number Theory, Lists, Database…)
follow the same mirror rule. Other build dirs (`cmake-build-relwithdebinfo/` etc.,
if present) are separate CLion profiles with identical layout.

`semester_1/graphics/` has its own nested `AGENTS.md` with SFML/ImGui-SFML-specific
knowledge (resetGLStates rendering pitfall, DLL import / bash-launch quirks,
Graph_SFML test notes) — read it before working in that folder.

## Building

CLion-bundled toolchain (there is no system-wide cmake/MinGW on PATH):

- cmake: `C:\Program Files\JetBrains\CLion 2025.2.1\bin\cmake\win\x64\bin\cmake`
- g++:   `C:\Program Files\JetBrains\CLion 2025.2.1\bin\mingw\bin\g++` (GCC 15.2.0)
- ninja: `C:\Program Files\JetBrains\CLion 2025.2.1\bin\ninja\win\x64\ninja.exe`

Existing build tree: `cmake-build-debug/` (Ninja generator, single config).

```bash
# rebuild one target (auto-reconfigures if CMakeLists changed)
cmake --build cmake-build-debug --target GRAPH

# full reconfigure from scratch (FetchContent downloads freeglut v3.6.0 + imgui into _deps/, needs network)
cmake -S . -B cmake-build-debug -G Ninja \
  -DCMAKE_MAKE_PROGRAM="C:/Program Files/JetBrains/CLion 2025.2.1/bin/ninja/win/x64/ninja.exe" \
  -DCMAKE_CXX_COMPILER="C:/Program Files/JetBrains/CLion 2025.2.1/bin/mingw/bin/g++.exe"
```

freeglut apps are statically linked (`FREEGLUT_STATIC`, `-static-libgcc
-static-libstdc++`) — the exes run standalone, no DLLs on PATH needed.
Never add bare `-static` to linker flags: breaks with GCC 15.2 (see
`semester_1/graphics_freeglut/CMakeLists.txt` comment).
New source directories are auto-added: root `CMakeLists.txt` file(GLOB)s every
subdir of `semester_1`/`semester_2` — each dir just needs its own CMakeLists.txt.

## Running / automating GUI apps — win-gui MCP server

A local MCP server in `.claude/mcp/win_gui_server.py`, registered project-wide
in `.mcp.json` (loaded after a session restart + one-time approval). Gives
tools `mcp__win-gui__*`:

- `launch_app(exe_path, wait_for_title=...)` — starts exe (console window
  suppressed via CREATE_NO_WINDOW), waits for a visible window, tracks pid
- `screenshot_window(window=<title-substring|hwnd>)` — PNG path + geometry;
  view the PNG with the Read tool. Uses `PrintWindow(PW_RENDERFULLCONTENT)` so
  background/unfocused windows capture fine (GL content included);
  BitBlt fallback if the shot comes back black
- `click(x, y, window=..., space="window")` — coordinates are **in the same
  pixel space as the screenshot** (client_offset_in_window is returned by
  screenshot_window). `method="sendinput"` (default) moves the real cursor and
  activates the window; `method="postmessage"` clicks without stealing focus —
  works for freeglut/ImGui apps
- `click_sequence([...], window=...)` — several clicks in ONE call (saves a
  model round-trip per click): `[{"x":158,"y":431},{"x":458,"y":231,"button":"right"}]`,
  optional per-click `"double": true`; `inter_click_ms` default 150 (min 50 so
  same-spot pairs aren't read as double-clicks); aborts and reports if the
  window moves mid-sequence; `out_path` captures a screenshot after the last
  click. Use for pre-planned geometric sequences (click-N-points demos);
  stay with single `click` when the next point depends on what's on screen
- `drag(x1,y1,x2,y2, ...)` — e.g. move a graph vertex; `key("enter")`,
  `activate_window`, `list_windows`, `close_window` (WM_CLOSE, force-kills
  after 2 s)

Verified workflow (GRAPH): launch → screenshot → click the ImGui button
"Create ordinary graph" at screenshot coords ≈ (154, 174) → screenshot shows
the graph → close_window. Note `imgui.ini` next to the exe persists ImGui
window positions, so toolbar coordinates are stable between runs.

Freeglut windows: client area is offset inside the captured image by the
frame (GRAPH: client 800x800 inside an 816x839 capture, offset [8, 31]) —
use `space="window"` + screenshot coords and the server does the math.

## Driving the running CLion — clion MCP server

`.claude/mcp/clion_bridge.py`, registered in `.mcp.json` as `clion`. Gives 54
`mcp__clion__*` tools straight from the IDE you already have open:
`get_file_problems` / `get_compiler_info` (CLion's real inspections),
`get_run_configurations` / `execute_run_configuration`, `apply_patch`,
`search_symbol` / `search_regex`, `git_status`, the whole `xdebug_*` family
(breakpoints, stack, frame values, registers, memory, disassemble), plus DB tools.

**Why a bridge and not a plain url:** CLion's MCP server binds *Windows*
loopback only (`127.0.0.1:64362`). WSL2 here runs in NAT mode
(`172.24.144.x`), so WSL's `127.0.0.1` is a different network stack and an
`sse`/`streamable-http` entry pointing at `127.0.0.1` can never connect. The
bridge is a stdio MCP server run through interop by `python.exe`, so it executes
on the Windows side where that address resolves, and relays JSON-RPC over
`POST /stream`. A `type: stdio` entry running `clion64.exe stdioMcpServer` also
fails (`-32000`): the launcher hands off to the already-running IDE and exits,
closing the pipe. CLion's own `0.0.0.0` port (63247) is firewall-blocked from
WSL, so port-forwarding isn't a shortcut either.

**Env vars do NOT cross the WSL→Windows interop boundary** unless the name is
listed in `WSLENV` — an `"env"` block in `.mcp.json` silently arrives empty in a
`python.exe` server. argv always crosses, so configure via flags:
`--project-path`, `--url`, `--port`, `--timeout`, `--debug` (env is fallback
only, for running it natively on Windows). This bit any future stdio server
launched through interop, not just this one.

Notes: requires CLion open on the project (it's a client of the live IDE, not a
standalone service). Port defaults to 64362; if that's dead the bridge
enumerates `clion64.exe`'s loopback ports and probes for the MCP one, so a
port change self-heals. Debug with
`python.exe -u .claude/mcp/clion_bridge.py --debug` and feed it JSON-RPC lines
on stdin. Only JSON-RPC may ever reach stdout — log to stderr.

## Environment

- Python: `C:\Program Files\Python312\python.exe` (3.12), with `mcp`, `pywin32`,
  `Pillow`, `opencv-python`, `numpy` installed (user site-packages)
- `.claude/` is gitignored (server lives there, local only);
  `.mcp.json` is untracked — commit it or gitignore it, owner's choice
- Getting a window's pid needs `GetWindowThreadProcessId(hwnd, byref(DWORD))`
  — passing `None` returns the *thread* id (bug fixed in the server, don't
  reintroduce it)
