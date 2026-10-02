# vpx-bcp-controller

The original C#/.NET controller is in `vpx-bcp-controller/`.

The standalone native VPX plugin project is in [native/](native/README.md), with
CMake builds for Windows, Linux and macOS, GLF-compatible scripting bindings,
automated tests and a cross-platform CI workflow. Use its BcpDrainMessages helper
to support both the native plugin and C# controller in an existing table.
