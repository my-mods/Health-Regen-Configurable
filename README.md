# Health Regeneration

Adds 1.5% health regeneration per second outside combat in both human and vampire form, in one package. The rate scales with current maximum human health or total vampire blood-bar capacity. Existing regeneration bonuses and multipliers still apply.

Recovery uses the game's native calculations and respects recoverable health. Vampires stop at the current recoverable segment ceiling; permanently lost segments still require the normal game mechanics to restore them.

Healing arrives in one-second ticks, allowing meaningful gains to trigger Quiet Dawn's health reveal. A final partial tick can be smaller than its reveal threshold. Quiet Dawn's separate low-health setting can keep the HUD visible even after regeneration stops.

The added rates have no periodic execution of their own. The existing native effects check once per second and calculate zero healing when recoverable health is full. This removes the separate unconditional healing modifiers; the game's native checks remain active.

## Requirements

The Blood of Dawnwalker, Steam build 25232147 (UE 5.5.4). This is a cooked-asset mod and needs no UE4SS, DLL, script loader, or settings menu. Quiet Dawn is optional.

## Install and replace

1. In Vortex, disable/remove **HumanHealthRegen**, **VampireHealthRegen**, and the earlier **Vampire Health Regen - Segment Guard** replacement if installed. Deploy so their old container files are removed.
2. Install `Health-Regeneration.zip` through Vortex, enable it, and deploy. This package uses the game-root installer and contains explicit game-relative paths.
3. Fully restart the game and load a save. Keep only this combined regeneration package enabled.

To update, replace this same Vortex entry with the rebuilt ZIP and deploy. To uninstall, disable/remove this entry in Vortex, deploy, then restart the game. The package contains no Quiet Dawn files, replaces no personal INI, and does not edit save files.

## Configuration and compatibility

The added base rate is fixed at 1.5% per second. Each form has its own rate modifier, inactive during combat, death, invulnerability, and the other form. Each modifier is limited to one stack per target. There are no configuration files.

The following stock assets are overridden:

- `/Game/_Dawnwalker/Combat/Effects/GE_BloodHealthRestorationDecrease`
- `/Game/_Dawnwalker/Player/Effects/GE_BloodRegen`
- `/Game/_Dawnwalker/Player/Effects/GE_PlayerHealthRegen`

The package also supplies `/Game/_Dawnwalker/Player/Effects/GE_VampireSegmentGuardRate` and `/Game/_Dawnwalker/Player/Effects/GE_HealthRegenerationHumanRate`.

Other mods changing these effects conflict, even if Vortex shows different container filenames. Disable other human or vampire regen variants; choosing a filename conflict winner is insufficient for overlapping cooked assets. Recheck compatibility after game updates.

The vampire restoration-decrease calculation remains stock. Its native regeneration uses a one-second cadence in combat as well, although this mod's added rate is inactive there. Human regeneration uses the out-of-combat eligibility of HumanHealthRegen, the native maximum-health/permanent-damage clamp, and a rate based on live maximum health.

## Source and rebuilding

`src/` contains the five complete cooked-asset JSON sources. `SOURCE.json` records their hashes, the stock game build, and the calculations used. Open or convert each source with UAssetGUI/UAssetAPI compatible with v2.0.0x (3228c1e), preserving its `/Game/` path under `Dawnwalker/Content/` and targeting UE 5.5. Place the matching game's `scriptobjects.bin` beside the `Dawnwalker` directory. Convert the directory using `retoc to-zen --version UE5_5` into `zzz_HealthRegeneration_P.utoc`, retaining its matching `.ucas` and `.pak` files. Keep the package layout and metadata in `package/` when assembling the ZIP.

## Credits

The game and stock assets belong to Rebel Wolves and their respective rights holders. KumasanGames' Human Health Regen and Vampire Health Regen inspired the 1.5% out-of-combat behavior. This implementation is rebuilt from stock game assets and does not redistribute those mods' edited payloads.
