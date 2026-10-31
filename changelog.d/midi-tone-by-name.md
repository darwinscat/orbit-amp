Amps: FrostPawAmp, AmberPawAmp

### Fixed
- **MIDI CC 24, 25 and 26 turn BASS, MIDDLE and TREBLE, as the MIDI list says, on every channel.** They used to
  turn the first three tone knobs in order, so on a channel whose knobs run otherwise they missed: on FrostPawAmp's
  CLEAN CC 25 turned TREBLE and CC 26 nothing; on AmberPawAmp's CLEAN CC 24 turned TONE and BASS had no controller.
  Now each finds its knob by name; a channel without that knob (CLEAN's MIDDLE on FrostPawAmp) leaves the controller
  turning nothing.
