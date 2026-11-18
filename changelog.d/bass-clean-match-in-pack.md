Amps: OrbitBass, RedGrooveBass, DeepGrooveBass

### Changed
- **BLEND is matched from the first block.** The bass packs (Ampeg SGT-DI, Two Notes ReVolt Bass Clean, Dirt and
  Drive) carry each capture's clean match in their manifest, fitted once when the pack is built, so an amp that loads
  one plays its clean low end in phase at once instead of fitting every capture first, about a tenth of a second
  each. The matches are the ones the amp fitted at load before: the sound is the same. A pack that does not carry
  them, a player's own, is still fitted at load, as before.

### Dependencies
- **felitronics-guitar-core v0.13.0** (from v0.12.0) and **namz v5.2.0** with it: the pack's own clean match.
