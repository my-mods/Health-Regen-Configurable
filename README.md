# Vampire Health Regen - Segment Guard

Adds 1.5% of total vampire blood-bar capacity per second outside combat, using the game's own regeneration calculation. Recovery stops at the current recoverable ceiling; permanently lost blood segments still require the normal game mechanics to restore them.

Healing arrives in one-second ticks, allowing meaningful gains to trigger Quiet Dawn's health reveal. A final partial tick can be smaller than its reveal threshold. Existing regeneration bonuses and multipliers still apply.

The added rate has no periodic execution of its own. The existing native blood-regeneration effect checks once per second and calculates zero healing at the recoverable ceiling. That native check still exists; this mod does not promise to suspend the game's entire regeneration system. It removes the separate, unconditional blood-gain modifier responsible for repeated attempts at the ceiling.

## Requirements

The Blood of Dawnwalker, Steam build 25232147 (UE 5.5.4). This is a cooked-asset mod and needs no UE4SS, DLL, script loader, or settings menu. Quiet Dawn is optional.

## Install and replace

1. In Vortex, disable/remove the existing **VampireHealthRegen 1.5 Percent OutOfCombat** entry and deploy so its three old container files are removed.
2. Install `Vampire-Health-Regen-Segment-Guard.zip` through Vortex, enable it, and deploy. This package uses the game-root installer and contains explicit game-relative paths.
3. Fully restart the game and load a save. Do not keep another vampire-regeneration package enabled alongside this replacement.

Human Health Regen can remain enabled: it changes a separate effect. This package contains no Quiet Dawn files and replaces no personal INI.

To update, replace this same Vortex entry with the rebuilt ZIP and deploy. To uninstall, disable/remove this entry in Vortex, deploy, then restart the game. Save files are not edited by the package.

## Configuration and compatibility

The added base rate is fixed at 1.5% per second, calculated from both live segment count and live blood per segment. It is inactive during combat, human form, death, and invulnerability. Game regeneration bonuses remain independent. There are no configuration files.

The following stock assets are overridden:

- `/Game/_Dawnwalker/Combat/Effects/GE_BloodHealthRestorationDecrease`
- `/Game/_Dawnwalker/Player/Effects/GE_BloodRegen`

The package also supplies `/Game/_Dawnwalker/Player/Effects/GE_VampireSegmentGuardRate`. Other mods changing either stock effect conflict, even if Vortex shows different container filenames. Disable other vampire regen variants; choosing a filename conflict winner is insufficient for overlapping cooked assets. Recheck compatibility after game updates.

The existing restoration-decrease calculation remains stock. The new rate effect is limited to one stack per target, so repeated applications cannot multiply the added rate. Native regeneration uses a one-second cadence in combat as well, although this mod's added rate is inactive there.

## Source and rebuilding

`src/` contains the three complete cooked-asset JSON sources. `SOURCE.json` records their hashes, the stock game build, and the calculation used. Open or convert each source with UAssetGUI/UAssetAPI compatible with v2.0.0x (3228c1e), preserving its `/Game/` path under `Dawnwalker/Content/` and targeting UE 5.5. Place the matching game's `scriptobjects.bin` beside the `Dawnwalker` directory. Convert the directory using `retoc to-zen --version UE5_5` into `zzz_VampireSegmentGuard_P.utoc`, retaining its matching `.ucas` and `.pak` files. Keep the package layout and metadata in `package/` when assembling the ZIP.

## Credits

The game and stock assets belong to Rebel Wolves and their respective rights holders. KumasanGames' Vampire Health Regen inspired this replacement's 1.5% out-of-combat behavior. This implementation is rebuilt from stock game assets and does not redistribute that mod's edited payload.
