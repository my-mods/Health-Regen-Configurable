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

The helper uses the pinned UE4SS lifecycle, LuaMadeSimple, reflection and object-array interfaces. It does not gate on executable hashes, version labels or loader names. The historical Framecore2b.def filename supplies imports without restricting the host.

Segment restoration finds the reflected GameplayModMagnitudeCalculation base default, verifies the two owned calculation defaults share its table, and validates the native CalculateBaseMagnitude wrapper's owner, slot 90 and scalar float result. Only the selected slot needs readable, aligned storage and an executable original target. The original implementation is forwarded for unrelated objects, so executable storage is required; its instructions and other table entries are not compared. Both assets retain their neutral native parent.

MagnitudeBinding replaces one pointer atomically while retaining the complete original table in place. A small Windows x64 tail-jump thunk carries an immutable routing record. Only the two live owned calculations on the game thread receive custom values. All other calls retain their original context, effect-spec argument and float result, including background calls and calls that fetched the thunk before cleanup. No table extent, RTTI boundary or cloned table is needed. Additional virtual slots remain untouched.

Cleanup clears the callback under a recursive invocation lock, then restores the slot only if it still contains the owned thunk. A competing replacement is never overwritten. Routing records and executable pages remain valid for late calls; the dispatcher module is pinned until process exit. Rebinding the same slot and original target reuses one record and one 4 KiB code page. New target identities retain separate records. Ordinary regeneration remains independent of segment setup. DLL hot unloading is intentionally unsupported.

The engine applies permanent-damage recovery followed by blood recovery. Custom calculations only return numbers; they do not apply effects or change attributes. The callbacks use cached getters and checked object identities, without Lua, settings I/O, object searches, allocation or timers. The loading/application gate and pre-activation real virtual-call probe remain in place. Pause and object deletion disable custom calculations immediately. Shutdown releases the static slot and removes the deletion listener without dereferencing dying UObjects.

Debug alone records calculation counts and timing. Offline dispatch, lifecycle and arithmetic tests do not establish live game behavior or frame-time performance. Zydis and Zycore provide structural instruction decoding; their licenses, UE4SS and fmt notices are included in LICENSES. ImGui and ImGuiColorTextEdit headers are SDK dependencies, with no linked implementations.

## Live settings API

`_HRNativeConfigure(expectedBloodAddress, rate, logging)` accepts a finite rate in (0, 0.05] on the game thread. It requires the active indexed blood owner and changes only the rate and diagnostics. It does not discover objects, apply effects or reactivate paused bindings. Lua removes the segment effect before stopping its binding and retains the strict loading gate.

Mod Setting Menu 1.0.6+ requires `HookProcessConsoleExec=1` in the loader profile. The archive does not supply that global INI.
