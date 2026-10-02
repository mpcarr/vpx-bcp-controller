# Native VPX BCP plugin

Standalone C++20 replacement for the C# BCP controller, built with CMake for
Windows, Linux and macOS. Uses the VPX plugin SDK headers from an external
vpinball checkout; no .NET, COM registration, JSON library or VPX link library is
required. The C# project remains available separately.

## VPX compatibility (read before installing)

SDK baseline: `vpinball/vpinball` revision
`eb9ae26c30e2475e865e41ba89bc6f271b173bef`. These plugin APIs are still evolving.

The plugin exposes `ReadMessage()` to return one message at a time. No VPX source
patch is required. The supplied GLF table expects an array, so add the helper from
`examples/stock-vpx-adapter.vbs` and change the `GetMessages` assignment in both
GLF wrappers to:

```vb
GetMessages = BcpDrainMessages(m_bcpController)
```

The helper drains native `ReadMessage()` results into a VBScript object array.
It also supports the C# controller: if `ReadMessage` is unavailable, it falls
back to C# `GetMessages()`. Other errors are propagated rather than triggering
fallback. The native plugin does not expose `GetMessages()` or register any
array types, avoiding VPX's current limitation on native object-array results.

Enable BCP in VPX's plugin settings. The plugin registers
`vpx_bcp_controller.VpxBcpController` as a legacy override and `BCP.Controller`
as an explicit alias. New scripts can use `CreatePluginObject("BCP.Controller")`.
The legacy override applies to VPX's literal `CreateObject("...")` rewriting,
not arbitrary dynamic construction of ProgIDs.

## Build

Requirements: CMake 3.24+, C++20 compiler, VPX checkout; Python 3 for tests.
Use a Visual Studio developer terminal on Windows. With a sibling `vpinball`
checkout, the default SDK path works; otherwise set `VPX_SOURCE_DIR` explicitly.

```sh
cmake -S native -B native/build -DVPX_SOURCE_DIR=/path/to/vpinball -DCMAKE_BUILD_TYPE=Release
cmake --build native/build --config Release --parallel
ctest --test-dir native/build -C Release --output-on-failure
cmake --install native/build --config Release --prefix native/dist
```

On Windows select an x64 generator (`-A x64` for Visual Studio). On macOS a
universal build uses `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"`. Linux x64 and
AArch64 builds are made on their respective architectures. A shared library must
match the VPX process architecture. This project targets desktop platforms.

Copy `native/dist/plugins/bcp` to the VPX plugins directory. It contains
`plugin.cfg` and the platform library (`plugin-bcp64.dll`, `plugin-bcp.so`, or
`plugin-bcp.dylib`). The GitHub Actions workflow builds/tests Windows x64, Linux
x64/AArch64 and macOS universal packages; it does not publish releases.

## Script API

| Call | Behavior |
| --- | --- |
| `Connect(port, executable)` | Launch executable, then connect; empty executable means attach only |
| `Connect(port, godotExecutable, projectDirectory)` | Launch Godot with `--path projectDirectory`, then connect |
| `ConnectToBuild(port, executable)` | Same as two-argument Connect |
| `ConnectToDebug(port)` | Connect without launching |
| `Send(line)` | Queue one UTF-8 protocol line; append exactly one newline |
| `ReadMessage()` | Pop the oldest queued message, or return Nothing when empty |
| `Disconnect()` | Cancel connection, join worker and clear queues; safe repeatedly |
| `EnableLogging()` | Enable host logging for this object |
| `Connected` | Current connection status |
| `LastError` | Last asynchronous transport failure, empty after a new connection |

Messages expose `Command`, `GetValue(key)` and `RawMessage`.
`GetValue("RawMessage")` also returns the unmodified line without CR/LF.
Keys and command names are case-insensitive; values retain case and type prefixes
such as `bool:true`, `int:1` and `NoneType:`. Missing keys return an empty string.
Outgoing JSON is passed through, and incoming `json=` payloads are kept intact.
`GetArrayValue` is not implemented: it is not called by the supplied GLF table,
and Newtonsoft token arrays do not have a defined portable scripting contract.

Each object owns its connection and queues. Ports 5050 and 5051 can run together.
Connect starts a worker and returns before the connection completes. Early sends
are queued after the protocol hello. The legacy hello value `version=21` is
preserved pending verification against the target media controller. The plugin
passes incoming hello/trigger/monitor messages to GLF without consuming them or
sending the GLF reset/window-title sequence itself.

Connections use IPv4 loopback, three attempts separated by five seconds, bounded
nonblocking I/O, and cancellation. There is no automatic reconnect after a lost
established connection: call Connect again and let GLF resynchronize. Disconnect
discards pending application messages and attempts a newline-terminated goodbye
only when it cannot corrupt a partially sent line. Queue overflow is an explicit
error (16 MiB / 65,536 messages), not silent loss. Maximum line size is 16 MiB.
Workers never call VPX or script APIs; asynchronous errors are logged on polling
when enabled and are available through `LastError`.

Executables are launched without a command shell. Paths are resolved relative to
the host working directory; pass absolute paths for predictable behavior.
Windows supports `.lnk` fallback. Linux/macOS require a native executable (for a
macOS app bundle use its `Contents/MacOS/...` executable); Windows `.exe`/`.lnk`
paths in a table must be adapted for other systems. Launched processes are not
killed by Disconnect. The plugin stops networking at game end and unload; VPX
must release script objects before unloading their providing library.

## Verification

CTest covers protocol parsing, native bindings and lifecycle, and two loopback
peers exercising independent connections, early-send ordering, fragmented UTF-8,
large JSON frames, literal typed parameters and bounded shutdown.
Binding tests verify scalar message polling and that no native GetMessages or
array type is exposed. Native tests do not replace an end-to-end VPX + Godot/GLF
session, which is still required before a
release. Cross-platform builds require the corresponding CI jobs or local hosts.
