Amps: CatpitAmp, RedPawAmp

### Changed
- **The INPUT lever's zero is called the input's zero now, as in every amp of the family**: INPUT'S ZERO,
  in the settings menu and in the INPUT column's menu, the same values and the same -9 dB unless you chose
  another.
- **The zero is in force with the window closed.** An instance whose window was never opened played its
  input 9 dB hotter than one whose window was; now every instance plays it the same. The host's Input Trim
  is the lever's place now, 0 dB at its middle, and the lever goes 24 dB either way of the zero. Choosing
  another zero moves the sound and leaves the lever where it is.
- Sessions and presets from 0.8.0 open playing exactly what they played, every register with them; only
  the lever may stand a little apart from where it stood, since its travel is now even about the zero. An
  automation lane of Input Trim recorded in 0.8.0 is the host's own and is not changed: its values are read
  as the lever's place now, 9 dB under what they played at the -9 dB zero.
