# Health Regen - Configurable 1.0.0

Configurable health regeneration for humans and vampires. Stops adding healing at full recoverable health or the current vampire segment limit, with optional combat and lost-segment recovery.

Separate human and vampire rates range from 0% to 5% per second, using 0.25% steps through 2% and 0.5% above 2%. Both default to 1.5%. Combat regeneration and lost vampire segment recovery are optional and default to Off.

Regeneration setup and cleanup wait for save loading to complete. The vampire segment-recovery effect is blocked from automatic application during asset loading and parent-effect refreshes.
