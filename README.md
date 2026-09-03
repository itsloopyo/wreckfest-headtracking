# Wreckfest Head Tracking

![Wreckfest running with this mod](https://raw.githubusercontent.com/itsloopyo/wreckfest-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Wreckfest that moves the camera with your head while your wheel or controller keeps steering, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **6DOF tracking** - yaw, pitch and roll plus positional lean, peek and duck
- **Works with any OpenTrack-compatible source** - webcam, phone app, or anything else that sends the OpenTrack UDP protocol

## Requirements

- [Wreckfest on Steam](https://store.steampowered.com/app/228380/Wreckfest/), the `Wreckfest_x64.exe` built 2023-09-19
- A tracking source that sends the OpenTrack UDP protocol, such as [OpenTrack](https://github.com/opentrack/opentrack) with a webcam
- Windows 10 or 11, 64-bit

## Installation

1. Download `WreckfestHeadTracking-v<version>-installer.zip` from the [releases page](https://github.com/itsloopyo/wreckfest-headtracking/releases).
2. Extract it anywhere.
3. Double-click `install.cmd`.
4. Configure OpenTrack to output UDP to `127.0.0.1` port `4242`.
5. Launch the game.

The installer puts two files next to `Wreckfest_x64.exe`: `WreckfestHeadTracking.asi` (the mod) and `version.dll` (the bundled Ultimate ASI Loader, which the game already imports so the loader is picked up on start).

Success looks like a `HeadTracking.ini` and a `HeadTracking.log` appearing in the game folder after the first launch, with the log reading `[build] activated profile steam-win64-20230919` and a `[camera] hooked ViewManager::Update at ... and Camera::UpdateViewMatrix at ...` line.

If the installer cannot find your game, point it at the folder yourself, either with an environment variable:

```powershell
$env:WRECKFEST_PATH = "D:\Games\Wreckfest"
.\install.cmd
```

or by passing the path as an argument:

```powershell
.\install.cmd "D:\Games\Wreckfest"
```

### Manual Installation

Copy `plugins\WreckfestHeadTracking.asi` and `vendor\ultimate-asi-loader\dinput8.dll` out of the ZIP into the folder holding `Wreckfest_x64.exe`, renaming the loader to `version.dll`.

## Setting Up OpenTrack

1. Set **Input** to whatever tracker you use.
2. Set **Output** to `UDP over network`, host `127.0.0.1`, port `4242`.
3. Press **Start**.
4. Center with OpenTrack's own Center bind while sitting the way you drive.

### VR Headset Setup

1. Connect the headset over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on UDP `127.0.0.1` port `4242`.

### Webcam Setup

Set OpenTrack's **Input** to `Neuralnet tracker`. It tracks your face from a plain webcam, with no markers, clips or IR hardware to fit.

### Phone App Setup

Your app has to send the OpenTrack UDP protocol, either from the phone or through a companion program on the PC. Many phone trackers use something else, so check that first.

There are two ways to wire it up:

- **Straight to the game.** Point the app at your PC's local IP address, port `4242`.
- **Through OpenTrack.** Set OpenTrack's **Input** to your app, then follow [Setting Up OpenTrack](#setting-up-opentrack).

Try it straight to the game first. Hold your head still and watch: if the view shakes or creeps, your app is sending a rough feed, and putting OpenTrack in the middle will clean it up. Use OpenTrack anyway if you want its curves.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It smooths on the phone, so it goes straight to the game.

## Controls

| Action              | Nav cluster | Chord          |
|---------------------|-------------|----------------|
| Toggle tracking     | `End`       | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up`   | `Ctrl+Shift+G` |

Both columns fire the same action, so use whichever your keyboard has. All four keys are remappable in `[Hotkeys]`.

`Page Up` cycles three positions in order: rotation and position, then rotation only, then position only, then back.

Your head turns the view about the camera's own up axis, so it stays glued to the car through a bank, a barrel roll or a landing on the roof.

Centering is done in the tracker: OpenTrack's Center bind, the center button in your phone app, or SteamVR's reset.

## Configuration

`HeadTracking.ini` is written to the game folder, next to `Wreckfest_x64.exe`, on first run and read at startup. Edit it and restart the game.

```ini
[Network]
; Port the mod listens on, 1024 to 65535. Must match the tracker's output port.
UdpPort=4242

[General]
; Whether tracking is on when the game starts.
EnableOnStartup=1

[Hotkeys]
; Windows virtual key codes, in hex. A value the mod cannot bind leaves that
; action on its previous key and says so in the log.
ToggleKey=0x23
CycleModeKey=0x21
ChordToggleKey=0x59
ChordCycleModeKey=0x47

[Rotation]
; Sensitivities multiply the tracker's angles; the inverts flip an axis.
YawSensitivity=1.0
PitchSensitivity=1.0
RollSensitivity=1.0
InvertYaw=0
InvertPitch=0
InvertRoll=0
; Smoothing runs 0.0 (none) to 1.0 (heavy) and covers rotation and position
; alike. Which of the two applies is picked per connection from where the
; tracker sends from: loopback gets LocalSmoothing, anything else gets
; RemoteSmoothing.
LocalSmoothing=0.0
RemoteSmoothing=0.15

[Position]
Enabled=1
SensitivityX=1.0
SensitivityY=1.0
SensitivityZ=1.0
InvertX=0
InvertY=0
InvertZ=0
; Limits are metres, and cap how far the camera moves from where the game put
; it. Forward and backward Z differ on purpose, so leaning in has more range
; than pulling back.
LimitX=0.30
LimitY=0.20
LimitZ=0.40
LimitZBack=0.10
```

The mod picks between the two smoothing values by where the packets came from, and it goes by address rather than by machine. Only `127.0.0.1` counts as local. A phone on your WiFi gets `RemoteSmoothing`, which is what you want, but so does OpenTrack running on this same PC if you have pointed it at your PC's own network address. Send to `127.0.0.1` to get `LocalSmoothing`.

## Troubleshooting

Read `HeadTracking.log`, next to `Wreckfest_x64.exe`. It records the game folder, the build profile it matched or refused, the config it loaded, the camera update it hooked, and the first head pose that reached the camera.

**Mod not loading:**

- No log file at all means the loader is not being picked up. Check that `version.dll` and `WreckfestHeadTracking.asi` are both in the folder holding `Wreckfest_x64.exe`.
- If the log says the mod stayed dormant, your `Wreckfest_x64.exe` is not a build this mod has a profile for. The mod fingerprints the running exe (TimeDateStamp, SizeOfImage and CheckSum) and installs no hooks unless it matches. The log line says whether your build is newer or older than the ones it knows; a newer one needs a mod update.

**No tracking response:**

- Check the log for `head pose reached the camera hook`. If it is absent the packets are not arriving: confirm the tracker's output host and port match `UdpPort`, and that no firewall rule is dropping them.
- If it is present, press `End` (or `Ctrl+Shift+Y`). Tracking may be toggled off.
- Only one process can hold UDP 4242. If OpenTrack or another game with a head tracking mod already has it, the log reads `Failed to bind UDP port 4242 (another app is listening on it -- OpenTrack, or another game) -- retrying every 500ms until it is free`, and it keeps retrying twice a second, repeating a `Still waiting` line every 30 seconds. Close the other app and the mod picks the port up within about half a second. No restart needed.
- No movement in the garage, in a menu, or with the game paused is the mod holding off on purpose. Head tracking follows only while an in-race camera owns the view, the engine reports a live race session, and the game is not paused. Opening the pause menu or alt-tabbing away stops it; closing the menu starts it again from where your head is then. Online races are not treated differently from local ones. The log line starting `[state] view is` says which of the three the mod is seeing.

**Jittery or unstable tracking:**

- Raise `RemoteSmoothing` if the tracker is on another device, or `LocalSmoothing` if it is on this PC. Both are frame-rate independent.
- If a phone app is sending direct, route it through OpenTrack so its filters can clean up the feed.

**Wrong rotation axis:**

- Per-axis inversion lives in `[Rotation]`, but fix a mirrored axis in your tracker's profile first so every game behaves the same way.

**Edits to `HeadTracking.ini` do nothing:** the file is read once at startup, so restart the game. If a single value is being ignored, the log names it and says what it used instead.

## Updating

Download the new release and run `install.cmd` again. Your `HeadTracking.ini` is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway. Your `HeadTracking.ini` is left alone either way.

## Building from Source

Needs [pixi](https://pixi.sh) and Visual Studio with the C++ toolchain.

```powershell
git clone --recursive https://github.com/itsloopyo/wreckfest-headtracking.git
cd wreckfest-headtracking
pixi run build      # WreckfestHeadTracking.asi
pixi run test       # unit tests
pixi run package    # the release ZIP
```

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

The loader it ships and the libraries compiled into the `.asi` keep their own licenses, listed in [THIRD-PARTY-NOTICES.md](THIRD-PARTY-NOTICES.md). Copies of all of them travel in the release ZIP, under `licenses/` and beside the vendored loader.

## Credits

- Wreckfest by [Bugbear Entertainment](https://www.bugbeargames.com/), published by THQ Nordic
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by ThirteenAG
- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu
- [OpenTrack](https://github.com/opentrack/opentrack)
- [cameraunlock-core](https://github.com/itsloopyo/cameraunlock-core), the shared head tracking pipeline behind these mods

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Bugbear Entertainment or THQ Nordic. Use at your own risk.
