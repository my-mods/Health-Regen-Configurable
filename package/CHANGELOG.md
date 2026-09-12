# Changelog

## Unreleased

- Combine human and vampire health regeneration in one Health Regeneration package.
- Add 1.5% of live maximum human health per second outside combat through the native recoverable-health calculation.
- Route added vampire regeneration through the game's recoverable-health ceiling check.
- Deliver the added 1.5% base rate in one-second ticks, scaled by live segment count and size.
- Preserve permanent blood damage and suppress the added rate during combat.
- Limit the persistent rate effect to one stack per target.
