## 1.1.0

- Apply regeneration rates, combat regeneration and vampire segment recovery changes during play.
- Fix a crash report appearing when quitting the game normally.

# Changelog

## Unreleased

- Allow executable-file differences outside the native calculation code and layout used for vampire segment restoration.
- Report the native code or table address when the segment helper cannot initialize.

- Fixed a crash report appearing when quitting the game normally.

## 1.0.2

- Fixed healing not starting after loading a save.
- Fixed vampire segment restoration stopping before full health.

## 1.0.1

- Regeneration now waits until your save has finished loading.

## 1.0.0

- Fix regeneration settings stopping during effect cleanup, which left both human and vampire healing rates at zero.
- Fix a crash while loading the vampire segment-recovery effect by correcting its serialized modifier boundaries.
- Combine human and vampire health regeneration in one Health Regen - Configurable package.
- Add 1.5% of live maximum human health per second outside combat through the native recoverable-health calculation.
- Route added vampire regeneration through the game's recoverable-health ceiling check.
- Deliver the added 1.5% base rate in one-second ticks, scaled by live segment count and size.
- Preserve permanent blood damage and suppress the added rate during combat.
- Limit the persistent rate effect to one stack per target.
- Add separate human and vampire rate controls in Mod Settings Menu: 0% to 5%, with 0.25% steps through 2% and 0.5% steps above 2%.
- Add Combat regeneration and Restore vampire segments toggles, both Off by default.
- Apply settings after save loading, with finite setup retries and no idle Lua worker or settings polling.
- Restore lost vampire segments only when enabled, without invoking feeding or mutation-charge replenishment effects.
- Use the shared save-load settings flow and diagnostics; preserve the settings snapshot during ordinary player replacement.
