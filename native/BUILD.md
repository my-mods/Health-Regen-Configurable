# Building the native regeneration helper

Use Windows x64, Visual Studio 2022's MSVC v143 C++ tools, the Windows 10/11 SDK, Git, and CMake 3.25 or newer. Build Release with the shared C runtime (`/MD`). The reference compiler is MSVC 19.44.35228 with Windows SDK 10.0.26100.0.

The UE adaptation headers require GitHub access through a linked Epic Games account. They are not included in the repository or archive.

```bat
git clone https://github.com/UE4SS-RE/RE-UE4SS.git RE-UE4SS
git -C RE-UE4SS checkout 97b7e501c19d8b2b7c662feee73aaa0dc1f0a4d1
git -C RE-UE4SS submodule update --init deps/first/Unreal
cmake -S native -B .local/native-build -A x64 -DUE4SS_SDK=C:/path/to/RE-UE4SS
cmake --build .local/native-build --config Release
```

Run CMake from the Health Regen - Configurable repository. It retrieves pinned public header dependencies. The output is `.local/native-build/Release/main.dll` with the Visual Studio generator, or `.local/native-build/main.dll` with Ninja. Copy the build into the source package at `Dawnwalker/Binaries/Win64/ue4ss/Mods/HealthRegeneration/dlls/main.dll`, assemble the ZIP, and install it through Vortex.

## Supported runtime and design

This helper targets Framecore 2b, UE4SS DLL SHA-256 `fb1839ee91f71f83d508d44a2763a15ac1bb0c5fb4e504ac0fcfca64376a054a`. It checks that hash once before exposing its private Lua functions. CMake pins RE-UE4SS `97b7e501c19d8b2b7c662feee73aaa0dc1f0a4d1` and UEPseudo `eb40a05f49509bdeb1ac39287032b60af585cca8`. `Framecore2b.def` generates an import library for the existing loader; it does not replace the loader DLL.

When segment restoration is enabled, the helper gives only the mod's two calculation class defaults a private native virtual table. The table's calculation entry returns the required permanent-damage reduction or blood recovery. The game's shared table is unchanged. Both assets inherit GameplayModMagnitudeCalculation, whose native default returns zero; an absent helper cannot apply the inherited blood regeneration value to permanent damage.

This path additionally requires Dawnwalker build 25232147, EXE SHA-256 `cb9b7d7bd88a6754c0a9c08318aa64d5013ddfd92d5badcae84e1b4ea980dcfc`. The helper verifies both executable and loader hashes before exposing its Lua API. For that executable, the base table is at image RVA `0x76c77f0`, has 92 entries, and calculation slot 90 points to the zero-return implementation at RVA `0x1271ab0`. Setup rejects another table or slot and probes both actual virtual calls before enabling the effect. Function signatures use the Windows x64 ABI: float result, calculation object and a const effect-spec pointer. The spec is not read or retained.

The engine applies permanent-damage recovery followed by blood recovery. Calculation callbacks only return numbers; they never change health while building or recalculating a gameplay-effect specification. Each callback uses three cached native float getters and bounded identity checks. It has no Lua call, object search, settings read, reflection traversal, allocation or timer. Full health and inactive sessions return zero. Cleanup removes the owned effect and restores the two original object tables. The immutable private table stays allocated for the helper's lifetime so deletion callbacks cannot release a table still in use.

Object identities retain an address, index and existing serial. A deletion listener invalidates the binding before address reuse, including objects whose serial is zero. It does not construct weak/soft references or allocate serial numbers. An atomic interest mask rejects unrelated deletions before locking. Game-thread execution revalidates indexed identities before reading objects. Pause immediately disables calculation. DLL/Lua hot reload is unsupported.

Logging records a single execution snapshot after setup, plus aggregate calculation counts and elapsed time on cleanup, only when `debugLogging=1`; disabled instrumentation does not measure each callback. Offline tests of the calculations and lifecycle do not establish in-game frame times or full engine compatibility.

UE4SS and fmt license notices are included under `LICENSES/`. ImGui, ImGuiColorTextEdit and Zydis/Zycore headers are transitively required by the SDK; their implementations are not linked into the DLL.
