Amps: CatpitAmp, RedPawAmp, FrostPawAmp, AmberPawAmp

### Fixed
- **A window that crashed on a computer with no graphics driver.** In a virtual machine, a Remote Desktop
  session or on a build server, Windows hands a window the system's own OpenGL, which runs no shaders; the
  cockpit's sky and the rovers' and the helicopter's view called it all the same, and the plug-in crashed as
  its window opened. There they now take the CPU's picture, as they do where a shader will not build, and say
  so in the family's log.
