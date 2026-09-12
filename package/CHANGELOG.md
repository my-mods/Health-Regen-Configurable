# Changelog

## Unreleased

- Combine human and vampire health regeneration in one Health Regeneration package.
- Add 1.5% of live maximum human health per second outside combat through the native recoverable-health calculation.
- Route added vampire regeneration through the game's recoverable-health ceiling check.
- Deliver the added 1.5% base rate in one-second ticks, scaled by live segment count and size.
- Preserve permanent blood damage and suppress the added rate during combat.
- Limit the persistent rate effect to one stack per target.
- Add separate human and vampire rate controls in Mod Settings Menu: 0% to 5%, with 0.25% steps through 2% and 0.5% steps above 2%.
- Add Combat regeneration and Restore vampire segments toggles, both Off by default.
- Apply settings after save loading, with finite setup retries and no idle Lua worker or settings polling.
- Restore lost vampire segments only when enabled, without invoking feeding or mutation-charge replenishment effects.
