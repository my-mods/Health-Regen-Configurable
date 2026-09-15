# Health Regen - Configurable

Human and vampire health regeneration in one configurable package. Both forms default to **1.5% per second**, outside combat. Vampires stop at the current recoverable segment ceiling by default.

Regeneration arrives in one-second ticks. A 1.5% gain can trigger Quiet Dawn's health reveal; a final partial tick or a rate at or below its threshold may not. Quiet Dawn's independent low-health rule can also keep the HUD visible.

## Requirements

- The Blood of Dawnwalker for PC, with the native calculation layout supported by the bundled helper. Reference build: **1.0.5 / Steam 25232147**, UE **5.5.4**.
- [**UE4SS for BoD — Framecore 2b**](https://www.nexusmods.com/thebloodofdawnwalker/mods/283). The bundled native helper targets that loader version.
- Required: [Mod Setting Menu 1.0.6 or later](https://www.nexusmods.com/thebloodofdawnwalker/mods/271).
- Quiet Dawn is optional.

The Lua scripts, native helper, and cooked assets belong to this single mod. No separate human/vampire regeneration mod is required.

## Installation

- Vortex: Install `Health-Regen-Configurable.zip` through Vortex, enable it and deploy.
- Manual: Copy the archive's `Dawnwalker` folder into `E:\SteamLibrary\steamapps\common\The Blood of Dawnwalker`, preserving the folder structure.

## Configuration

Mod Setting Menu 1.0.6 or later is required. Its callback bridge also requires `HookProcessConsoleExec = 1` in `UE4SS-settings.ini`. Manage that loader setting through your Vortex loader configuration; this archive contains no replacement global UE4SS INI.

Settings are prepared when the game starts and are available from the main menu before the first save. Press **Apply** to save and update the active game. Changes made while loading are retained for the next valid player. Restore and Discard leave saved settings unchanged; Reset takes effect after Apply.

Human rate, vampire rate, combat rules and segment mode update independently. Rate or Logging changes in segment mode configure the existing owner without rebinding or refreshing its effect. Segment-mode transitions retain the removal, binding and guarded application sequence. Loading pauses both setup and cleanup.

Logging is the final, sole diagnostic control. It changes immediately; verbose logging is Off by default. Logs are written to `Dawnwalker/Binaries/Win64/ue4ss/UE4SS.log`. Settings are never polled.

| Setting | Choices | Default |
| --- | --- | --- |
| Vampire regeneration per second | 0%, 0.25%, 0.5%, 0.75%, 1%, 1.25%, 1.5%, 1.75%, 2%, 2.5%, 3%, 3.5%, 4%, 4.5%, 5% | 1.5% |
| Human regeneration per second | Same choices | 1.5% |
| Combat regeneration | Off: outside combat only. On: also during combat. | Off |
| Restore vampire segments | Off: respect the recoverable segment ceiling. On: restore lost segments toward full health. | Off |
| Logging | Write setup and aggregate diagnostics to `ue4ss/UE4SS.log`. | Off |

Required: [Mod Setting Menu 1.0.6 or later](https://www.nexusmods.com/thebloodofdawnwalker/mods/271). Open Mod Settings and press Apply to save and update gameplay.

To edit the settings file directly, launch the game once to generate `Dawnwalker/Binaries/Win64/ue4ss/Mods/HealthRegeneration/settings.ini` inside the game folder, then close the game and edit that file:

```ini
[Settings]
vampireRegenPercent = 1.5
humanRegenPercent = 1.5
combatRegen = 0
restoreVampireSegments = 0
debugLogging = 0
```

The rate keys use the percentage numbers listed above: `1.5` means 1.5% per second. Toggles use `0` for Off and `1` for On. Save the file, restart the game and load a save to apply the changes.

Zero disables this mod's added regeneration for that form. Existing game regeneration and bonuses are separate. Death and invulnerability block the added regeneration; segment restoration does not revive a dead player.

## Recovery behavior

Human regeneration adds the selected percentage of live maximum health to the native rate. The game applies its regeneration multiplier and clamps recovery to maximum health minus permanent damage.

With **Restore vampire segments Off**, the selected base rate scales with live segment count and size and uses the game's native regeneration multiplier. At the recoverable ceiling, its native calculation returns zero. Losing health or restoring a segment through normal gameplay makes recovery possible again. This mode never changes permanent blood damage.

With **Restore vampire segments On**, the added regeneration restores the selected percentage of current total blood capacity per second, capped at full health. Each tick reduces only the permanent blood damage needed for that tick's recovery. It uses health attribute modifiers and does not invoke feeding, overdrinking, or mutation-charge replenishment effects. Existing game regeneration bonuses continue separately.

Regeneration starts once the loading screen closes. Lost vampire segments are restored only when **Restore vampire segments** is On.

## Source and rebuilding

`src/` contains eight cooked-asset JSON sources, the Lua scripts, and the menu definition. `SOURCE.json` records the game and source fingerprints. Shared Lua modules are pinned by `ue4ss-common.lock.json`; their license and native dependency notices are under `LICENSES/`.

Convert each JSON with UAssetGUI/UAssetAPI compatible with v2.0.0x (3228c1e), preserving its `/Game/` path under `Dawnwalker/Content/` and targeting UE 5.5. Place the matching game's `scriptobjects.bin` beside that directory, then use `retoc to-zen --version UE5_5` to create `zzz_HealthRegeneration_P.utoc` and its matching `.ucas` and `.pak` files. Build the DLL using [native/BUILD.md](native/BUILD.md).

In the package, place the containers under `Dawnwalker/Content/Paks/~mods/`. Place the Lua files in `Dawnwalker/Binaries/Win64/ue4ss/Mods/HealthRegeneration/Scripts/`, the DLL in that mod's `dlls/main.dll`, and `mod_settings.ini` plus an empty `enabled.txt` in the mod folder. Retain the root metadata, layout note, licenses and Vortex overrides from `package/`; do not add a personal settings file to the ZIP.

## Credits

The game and stock assets belong to Rebel Wolves and their respective rights holders. The idea was inspired by KumasanGames' [Human Health Regen](https://www.nexusmods.com/thebloodofdawnwalker/mods/387) and [Vampire Health Regen](https://www.nexusmods.com/thebloodofdawnwalker/mods/391). This implementation was completely rebuilt from stock game assets with its own configuration and runtime integration; neither original mod's edited payloads are included.

The native integration uses UE4SS, its engine adaptation headers, and Framecore's exported hooks. The included notices cover these dependencies and fmt headers.
