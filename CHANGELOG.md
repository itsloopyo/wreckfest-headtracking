# Changelog

## [Unreleased]

### Added

- The Xbox Game Pass / Microsoft Store copy of the game is supported. It is a
  different build from the Steam one - older, and with all its own addresses -
  so it gets its own build profile and both stores now work from the same mod
  binary. The dormant-build log line no longer tells a Game Pass player their
  game needs updating when it is a build the mod simply has no profile for.
- A setting set to `default` in `CameraUnlock.ini` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it, and neither do earlier versions of this mod. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.
- `Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.
- When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that.

### Changed

- Settings move to `CameraUnlock.ini`, next to `Wreckfest_x64.exe`. Earlier versions of the mod kept these settings in `HeadTracking.ini`, in the same folder. The first time this version starts and finds no `CameraUnlock.ini`, it reads your settings from `HeadTracking.ini` and writes them into `CameraUnlock.ini`. It never changes `HeadTracking.ini`, and does not read it again while `CameraUnlock.ini` exists.
- A first start with no `HeadTracking.ini` no longer writes one. It creates `CameraUnlock.ini` instead.
- A setting that the defaults the README shows set to `default` is written as `default` when the value imported for it equals its default at that start, which is the value `Defaults.ini` gives it, or the built-in value where `Defaults.ini` gives none. It then follows `Defaults.ini`. Every other setting is written with the value imported for it.
- `RotationEnabled` and `PositionEnabled` are one setting here, the tracking mode, so both are written as `default` or neither is.
- Comments, and keys the mod never read, are not carried over. Nor is this, where your old file had it:
  - A sensitivity or axis inversion you changed from its default. Set these in your tracker instead.
- An older version of the mod reads `HeadTracking.ini` and never reads `CameraUnlock.ini`, so a setting you change after updating is not in `HeadTracking.ini`.
- Deleting only `CameraUnlock.ini` makes the next start read `HeadTracking.ini` again. To go back to the defaults, replace everything in `CameraUnlock.ini` with the defaults the README shows. Every setting they set to `default` then follows `Defaults.ini`.
- Hotkeys are written as key names, and each hotkey lists every key that triggers it, the Ctrl+Shift chord included: `ToggleKey=End, Ctrl+Shift+Y`. `[Hotkeys] ToggleKey` and `ChordToggleKey`, virtual key codes, are imported together into `ToggleKey`, and `CycleModeKey` and `ChordCycleModeKey` into `CycleTrackingModeKey`, each as the plain key and Ctrl+Shift with the chord key.
- The tracking mode (Page Up / Ctrl+Shift+G) is saved to `CameraUnlock.ini` when you change it, and the game starts in the mode you left it in. Turning tracking on or off (End / Ctrl+Shift+Y) is still not saved; the game starts with head tracking on or off as `EnableOnStartup` says.
- The keys are renamed to the names every head tracking mod on `CameraUnlock.ini` uses: `LocalSmoothing` and `RemoteSmoothing` move from `[Rotation]` to `[Smoothing]`, and the lean limits are `PositionLimitX`, `PositionLimitZ` and `PositionLimitZBack`. `LimitY` set both vertical limits, so it becomes `PositionLimitY` (up) and `PositionLimitYDown` (down), both imported from it. `[Position] Enabled`, which chose the mode tracking started in, is imported as that mode: `Enabled=0` starts in rotation only, as it did.
- `pixi run install`, `pixi run uninstall` and `pixi run check-fingerprint` act
  on every Wreckfest install on the machine rather than the first one detection
  returns, so a Steam and a Game Pass copy side by side stay in step.

### Removed

- The sensitivity and axis inversion settings, `[Rotation] YawSensitivity`, `PitchSensitivity`, `RollSensitivity`, `InvertYaw`, `InvertPitch` and `InvertRoll`, and `[Position] SensitivityX`, `SensitivityY`, `SensitivityZ`, `InvertX`, `InvertY` and `InvertZ`. Set these in your tracker app instead.
- With these settings at their shipped defaults the camera moves as it did before.

## [0.1.0] - 2026-09-03

- Head tracking now reaches the rendered frame. The pinned camera function was
  the view matrix history push, not the camera update, so the head pose lived
  only for the duration of that call and reached nothing but the frame's motion
  vectors - a smear across a view that never moved. The pose is now composed in
  before the view matrix is derived and left in place for the rest of the frame,
  with a second hook on the view manager's camera update taking it back out
  before the engine interpolates from it.
- Yaw and pitch corrected at the engine boundary, checked against a running
  game.
- Head tracking is now off outside a race. The gameplay gate compared two view
  manager fields that were never equal, so it read gameplay everywhere including
  the main menu. It now identifies the active camera controller by its vtable
  and requires a live race session on top of it - Wreckfest renders its main
  menu through the same in-game car camera a race does, so the camera alone
  cannot tell them apart.
- Head tracking now stops while the game is paused. The pause menu draws the
  same cockpit through the same in-race camera and the race session stays live
  across it, so neither of the gate's existing inputs could see it and the head
  kept moving the camera behind the menu. The gate now also reads the engine's
  own global pause byte, which the pause menu and a window that loses focus both
  raise through the engine's SetPaused. The view snaps to the game's camera when
  the menu opens and back to the tracked one when it closes; the pose pipeline
  keeps advancing throughout, so it resumes from where your head is then rather
  than from where it was when you paused. A build whose pause offsets are not in
  the profile, or whose chain cannot be read, keeps following through the pause
  menu and says so in the log rather than going dormant.
- Head tracking now follows in online races as well as local ones. The gate
  used to call the engine's bgMultiplayerGameGet() through the machine context
  and shut for anything it could not positively read as a local session, which
  cost every online player the mod. The mod composes the head pose into the
  camera transform for the frame being drawn and takes it back out before the
  engine interpolates from it, so car control, physics and everything sent over
  the wire read the camera the game computed - there is nothing for a server or
  another player to see. The whole session check is gone with it, and with it
  the mod's only indirect call into an engine function and the two pinned
  offsets (`machine_context_ptr_rva`, `machine_context_is_multiplayer`) that a
  patch could move. The gate is now the view state and the race session flag,
  both plain data reads.
