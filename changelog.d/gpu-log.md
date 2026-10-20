Amps: CatpitAmp, RedPawAmp, FrostPawAmp, AmberPawAmp

### Added
- **When the view out of the window cannot start on your graphics card, the amp now writes down why.** The
  windscreen falls back to the 1993 landscape and the cockpit's sky to its stars on their own, as before;
  now each time it happens a line goes into `gpu.log` in Darwin's Cat's folder — on the Mac
  `~/Library/Application Support/Darwin's Cat/gpu.log`, on Windows `%APPDATA%\Darwin's Cat\gpu.log`, on Linux
  `~/.config/Darwin's Cat/gpu.log` — with the date and time, the amp, the format and the host it was loaded
  in, the graphics card's own name for itself, and the reason in its driver's words. Send it to us with a
  report. The file stays small: past 256 KB it keeps only its newest half.
