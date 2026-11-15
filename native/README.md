# Native VPX BCP plugin

Standalone C++20 replacement for the C# BCP controller, built with CMake for
Windows, Linux and macOS. Uses the VPX plugin SDK headers from an external
vpinball checkout; no .NET, COM registration, JSON library or VPX link library is
required. The C# project remains available separately.

## VPX compatibility (read before installing)

SDK baseline: `vpinball/vpinball` revision
`eb9ae26c30e2475e865e41ba89bc6f271b173bef`. These plugin APIs are still evolving.

The host must provide `MsgPluginAPI` version 1 and the compatible script API.
VPX introduced the versioned dispatch table in commit
`595d1fd5e3b886645d6e5c8e0a1624dc9733b516` (September 5, 2026). Older,
unversioned hosts are incompatible. The displayed version `10.8.1` alone does
not identify a compatible build; compare the full VPX build/commit with a known
working installation or use a build matching the SDK baseline above.

The plugin now checks the message API before calling host functions and writes
an incompatibility diagnostic to stderr when it refuses initialization. This
does not make an older host compatible, and its plugin UI may still show BCP
as enabled. Earlier BCP binaries could crash in `MsgPluginManager::SubscribeMsg`
from `BCPPluginLoad` on such a host because the function-pointer offsets differ.

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
| `RegisterDisplay(name, width, height, id, isDmd)` | Publish a named GMC frame stream as a VPX display; `id` is unique across BCP connections |
| `ClearDisplays()` | Withdraw this connection's display sources |

Messages expose `Command`, `GetValue(key)` and `RawMessage`.
`GetValue("RawMessage")` also returns the unmodified line without CR/LF.
Keys and command names are case-insensitive; values retain case and type prefixes
such as `bool:true`, `int:1` and `NoneType:`. Missing keys return an empty string.
Outgoing JSON is passed through, and incoming `json=` payloads are kept intact.
Binary BCP bodies use the header's `bytes=N` length, so pixel values containing
newlines or NULs do not corrupt the following messages. Binary bodies are not
exposed by the scripting message object; `rgb_dmd_frame` is handled internally.
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

## Display Godot/GMC inside VPX 10.8.1

Use GMC's built-in `MPFDMDDisplay` for frame output; an extra sender script is
not required. For the complete Godot project and VPX table migration steps,
including GLF wrapper setup and the optional alternative sender, see
[Godot display migration](docs/godot-display-migration.md).

The same BCP connection can supply VPX's native display API. No window title,
window capture, additional port, or separate receiver plugin is required.
The native plugin receives GMC's existing binary `rgb_dmd_frame` messages and
advertises registered outputs through `ControllerPlugin.h`.

1. Enable this BCP plugin in VPX. Rebuild it against the SDK matching your VPX
   build, as described above.
2. On the **existing media controller object**, register the output once during
   table initialization. The name is the GMC display node's name (case-sensitive),
   and the width/height must equal the resolution of the frames GMC sends:

   ```vbscript
   ' Example only: replace name and dimensions with your GMC display settings.
   m_bcpController.RegisterDisplay "DefaultDisplay", 256, 64, 0, True
   ```

   This can run before or after `Connect`. In a GLF table, add the call inside
   `GlfVpxBcpController.init`, where `m_bcpController` is the native object, or
   use the forwarding methods in `examples/GlfVpxBcpController.vbs`. Reuse the
   media connection on 5050; the GLF monitor on 5051 is a separate connection.
3. In the flasher properties, choose **DMD** for `isDmd=True`, or **Display**
   for `isDmd=False` (LCD-style artwork). Set **Source** to:

   ```text
   ctrl://BCP/display?id=0
   ```

   The final number is the registered id. DMD mode adds VPX's dot appearance;
   Display mode suits artwork whose appearance is already rendered by Godot.
   Remove any script assignments that force the flasher into the other mode or
   write its legacy `DMDPixels`/`DMDColoredPixels` directly.
4. Enable binary output in Godot. A plain `MPFDisplay` does not send frames.
   The bundled GMC `MPFDMDDisplay` already does: use it on the display node and
   set its `resolution` to the registered dimensions and `pixel_order` to RGB.
   Its `rgb_dmd_frame` header contains only `name` and `bytes`, which is why
   resolution must be registered in VPX. It captures its containing viewport;
   give a display its own viewport/window when showing multiple displays.
   Do not add the optional sender alongside `MPFDMDDisplay` for the same output.

The receiver accepts tightly packed **RGB8** (3 bytes/pixel) and **RGBA8**
(4 bytes/pixel, alpha discarded), publishing sRGB RGB24 to VPX. If your GMC
version returns another image format, convert the image to `Image.FORMAT_RGB8`
before `get_data()`; HDR linear images also need conversion to sRGB. Output
pixels before a Godot dot shader when using VPX's DMD effect. Native resolution
and a modest update rate keep GPU readback and shared-connection traffic low.

Wire format (the pixel body is binary, not base64, and has no trailing newline):

```text
rgb_dmd_frame?name=DefaultDisplay&bytes=49152\n
<49152 RGB bytes for a 256 x 64 frame><next BCP header>\n
```

New custom senders must serialize all writes to a BCP socket: a header and its
binary body must remain together, including when other threads send events.
GMC variants should be checked for this if frames corrupt during simultaneous
events. Do not insert JSON or event lines into an in-flight pixel body.

Display frames bypass `ReadMessage` and the normal event queue. The receiver
keeps the latest complete frame for each registered name; outputs not registered
on that connection are consumed and discarded. A dimension/byte-count mismatch
sets `LastError` and stops that connection rather than publishing invalid pixels.
Malformed or oversized binary lengths likewise close the connection.

`Disconnect` and connection loss turn registered displays black, retaining their
configuration for a subsequent `Connect`. `ClearDisplays`, table end, and object
destruction withdraw the sources. Register outputs again for the next table.
Ids are nonnegative and unique across all BCP objects; duplicate ids/names are
rejected. Each connection supports up to 16 outputs. Dimensions are 1..4096,
with `width * height * 4` limited to 16 MiB. Source discovery and removal run on
the VPX API thread; immutable snapshots protect readers from partial frames.

Executables are launched without a command shell. Relative executable names and
paths are resolved beside the loaded `.vpx` file, using VPX's table information.
Absolute executable paths are used as supplied. If no table path is available,
lookup falls back to the host working directory. The plugin does not change the
host or child working directory. The separate Godot project argument retains its
existing behavior; use an absolute project path for predictable development launches.
You can pass a platform-neutral basename such as `DarkChaos_gmc`:

| Platform | Lookup order for a basename |
| --- | --- |
| Windows | `DarkChaos_gmc.exe`, exact name, `.lnk`, `.exe.lnk` |
| Linux | Exact name, then `.x86_64` on x64 or `.arm64` / `.aarch64` on ARM64 |
| macOS | `DarkChaos_gmc.app`, then exact name |

Explicit filenames are preserved (Windows still permits `.lnk` fallback).
macOS app bundles resolve their executable through bundle metadata, so the
internal executable may have a different name; both XML and binary plists are
handled by CoreFoundation. Linux/macOS binaries need executable permission.
Each system still requires its own Godot export; this does not run Windows
binaries on other platforms. Launched processes are not
killed by Disconnect. The plugin stops networking at game end and unload; VPX
must release script objects before unloading their providing library.

For the native plugin, use `bcpExeName = CGameName & "_gmc"`. Remove the GLF
startup wrapper's `FileSystemObject.FileExists(bcpExeName)` check: the basename
itself may not exist, and a macOS app is a directory. Let the plugin resolve and
validate the export instead:

```vb
Public Sub Glf_ConnectToBCPMediaController(args)
    If glf_production_mode Then
        Set bcpController = (New GlfVpxBcpController)(bcpPort, bcpExeName)
    Else
        Set bcpController = (New GlfVpxBcpController)(bcpPort, "")
    End If
End Sub
```

This basename lookup is a native-plugin feature. When using the older C#
controller, continue supplying its explicit Windows executable filename.

## Verification

CTest covers protocol parsing, native bindings and lifecycle, and two loopback
peers exercising independent connections, early-send ordering, fragmented UTF-8,
large JSON frames, literal typed parameters and bounded shutdown. Display tests
cover every split point of a binary frame, RGB/RGBA conversion, invalid lengths,
immutable concurrent snapshots, display discovery, duplicate ids, frame/event
interleaving, disconnect blanking and source removal lifetime.
Binding tests verify scalar message polling and that no native GetMessages or
array type is exposed. Native tests do not replace an end-to-end VPX + Godot/GLF
session, which is still required before a
release. Cross-platform builds require the corresponding CI jobs or local hosts.
