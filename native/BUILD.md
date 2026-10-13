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

The helper uses the C++ mod lifecycle, LuaMadeSimple stack interface, reflection and object-array APIs declared by the pinned SDK. It does not inspect the host DLL file, fork name or version at runtime. CMake pins RE-UE4SS `97b7e501c19d8b2b7c662feee73aaa0dc1f0a4d1` and UEPseudo `eb40a05f49509bdeb1ac39287032b60af585cca8`. `Framecore2b.def` retains its historical name and supplies only the imported symbols; it does not restrict the loader. Changed required game code/layouts still stop native setup with the failing address, and object identity, parameter layout and binding probes remain in place. Tested host provenance: Framecore 2b `fb1839ee91f71f83d508d44a2763a15ac1bb0c5fb4e504ac0fcfca64376a054a`; Vercadi RC6 `795c3732dd4dde53d1c9210d93ae8191b983031cd275e7e1cf93fef69fad6deb`. These are evidence fingerprints, not an allowlist.

When segment restoration is enabled, the helper gives only the mod's two calculation class defaults a private native virtual table. The table's calculation entry returns the required permanent-damage reduction or blood recovery. The game's shared table is unchanged. Both assets inherit GameplayModMagnitudeCalculation, whose native default returns zero; an absent helper cannot apply the inherited blood regeneration value to permanent damage.

The reference calculation layout was established on Dawnwalker 1.0.5 / Steam build 25232147, EXE SHA-256 `cb9b7d7bd88a6754c0a9c08318aa64d5013ddfd92d5badcae84e1b4ea980dcfc`. This hash is provenance only. Setup finds the reflected native base calculation default and `CalculateBaseMagnitude`, verifies the owned defaults share that base table, and traces the wrapper's actual owner/vptr/slot-90/float-result route. It checks scalar float return metadata and readable executable storage at the replaced slot. The intentionally overridden base body is not forwarded or compared; nonzero returns, helper calls and instrumentation there are not dependencies. Equivalent encodings are accepted; unrelated helper implementations, cleanup and untouched virtual targets are not compared.

The private table retains the remaining readable PE-section storage, rather than assuming the base has exactly 92 entries or copying later unrelated image sections. Custom/non-PE storage falls back to its readable allocation suffix. This preserves future added slots without treating their identities as dependencies. Allocation/readability boundaries are established before any slot read or copy. Every published clone is immutable and retained until shutdown. At stop/rebind, changed original storage or extent creates a fresh snapshot; unchanged snapshots are reused. This preserves untouched updates and added slots without invalidating older reachable allocations. On the reference executable the section suffix is 39,138,320 bytes, shared by both defaults. This deliberately conservative fallback has a material one-time allocation/copy cost; it is not a 92-pointer allocation. A valid MSVC RTTI COL prefix and a following COL/new-table prefix can safely narrow extent without inspecting unused function identities; otherwise the conservative storage bound is retained. This executable has no verified COL prefix at the base table, so the fallback remains39,138,320 bytes. The original cold demand-paged resize/copy measured292.355ms; a later warmed-file run measured29.3089ms first and7.75?7.95ms subsequent copies. These are offline setup timings, not gameplay/FPS evidence. Setup rejects incompatible owners/results, unreadable required storage or missing/unverifiable required dispatch. It does not reject a changed original table or skipped-original implementation. It probes both actual virtual calls before applying the effect; ordinary regeneration does not require this setup to succeed. Function signatures use the Windows x64 ABI: float result, calculation object and a const effect-spec pointer. The spec is not read or retained; its contents and marshal-helper internals are not requirements.

The engine applies permanent-damage recovery followed by blood recovery. Calculation callbacks only return numbers; they never change health while building or recalculating a gameplay-effect specification. Each callback uses three cached native float getters and bounded identity checks. It has no Lua call, object search, settings read, reflection traversal, allocation or timer. Full health and inactive sessions return zero. Cleanup removes the owned effect and restores the two original object tables. The immutable private table stays allocated for the helper's lifetime so deletion callbacks cannot release a table still in use.

Object identities retain an address, index and existing serial. A deletion listener invalidates the binding before address reuse, including objects whose serial is zero. It does not construct weak/soft references or allocate serial numbers. An atomic interest mask rejects unrelated deletions before locking. Game-thread execution revalidates indexed identities before reading objects. Pause immediately disables calculation. DLL/Lua hot reload is unsupported.

Logging records a single execution snapshot after setup, plus aggregate calculation counts and elapsed time on cleanup, only when `debugLogging=1`; disabled instrumentation does not measure each callback. Offline tests of the calculations and lifecycle do not establish in-game frame times or full engine compatibility.

UE4SS and fmt license notices are included under `LICENSES/`. Zydis and Zycore are statically linked for structural instruction decoding, with their notices included. ImGui and ImGuiColorTextEdit headers are transitively required by the SDK; their implementations are not linked.

## Live settings API

The object-array shutdown callback disables calculation, unregisters its deletion listener and clears owned identities. It does not restore object tables or access dying game objects. Later cleanup can run again without removing the listener twice.

`_HRNativeConfigure(expectedBloodAddress, rate, logging)` runs on the game thread. It accepts a finite rate in (0, 0.05], verifies the existing bound blood owner and indexed identity, then changes only rate and diagnostics. An inactive or paused binding returns false and cannot be resumed by configuration. It performs no discovery, effect application or rebinding. Lua removes the segment effect before stopping a binding and retains the strict loading gate for both setup and cleanup.

Mod Setting Menu 1.0.6+ requires `HookProcessConsoleExec=1` in the loader profile. The mod archive does not supply that global INI.
