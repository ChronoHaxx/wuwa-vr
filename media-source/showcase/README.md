# Showcase videos

Record long, play normally, and let the editor cut the good bits.

## 1. Record (OBS)

- **WuWa controls > Recording > Steady desktop view** (on by default in builds from
  2 Oct). The game window then shows a calm, full-resolution view of one eye: head shake is
  smoothed out from the exact head pose each frame used. Stick turns and the HUD layer are
  not moved, and the headset image is unchanged. **Steadiness** sets the smoothing in seconds
  (0.35 by default; higher is calmer and lags further behind fast head turns).
- Set WuWa's window to your monitor's resolution (2560×1440 here). OBS: the **recording**
  scene (Game Capture of WuWa, game audio), NVENC, CQP 18–20, 60 fps, MKV. **F9** starts
  and stops.
- Hide the HUD (**L3 + B**) for scenery; keep it for combat if you like. Dead time (menus,
  loading, standing still) is cut automatically, so just play.

Old side-by-side recordings from the launcher recorder work too: add `--crop sbs-left`.
They are 1280 px per eye, have no audio and include the lens edge.

## 2. Edit

Python 3.10+ with `numpy` and `opencv-python`; ffmpeg with NVENC on `PATH`.

```powershell
python showcase.py analyze "E:\OBS Videos\2026-10-02 20-00-00.mkv"
```

Writes `showcase-work\` next to the clip: `analysis-*.json` (motion, sound, dead time) and
contact sheets (`sheets\*.jpg`: a thumbnail every few seconds, a bar for "how lively", dim
for dead time). `sheet CLIP FROM TO --every 1` makes a close-up sheet of one stretch.

Then either pick the shots yourself (or ask Claude to read the sheets and write the edit),
or let it choose:

```powershell
python showcase.py auto "E:\OBS Videos\showcase-work\analysis-*.json" --length 75
python showcase.py render "E:\OBS Videos\showcase-work\edit.json"
python showcase.py render "E:\OBS Videos\showcase-work\edit.json" --vertical
```

Render writes `edit-<date>-<time>.mp4` (2560×1440, H.264, -14 LUFS for YouTube) and a
`.preview.jpg` strip. `--vertical` makes 1080×1920 for Shorts. Segments are cached in
`render-cache\`, so changing captions or order re-renders in seconds.

## Edit list

```json
{
  "title": "WuWa VR", "subtitle": "Wuthering Waves in VR",
  "outro": "Free on PC VR", "outro_sub": "chronohaxx.github.io/wuwa-vr",
  "music": "C:/Music/track.mp3", "music_volume": 0.8, "game_volume": 0.5,
  "transition": 0.5,
  "segments": [
    {"clip": "E:/OBS Videos/2026-10-02 20-00-00.mkv", "in": 312.0, "out": 318.5,
     "caption": "Both eyes match, even far away"},
    {"clip": "E:/OBS Videos/2026-10-02 20-00-00.mkv", "in": 845.2, "out": 851.0,
     "speed": 0.5},
    {"card": "Combat", "sub": "Full speed, native stereo", "duration": 2.5}
  ]
}
```

- `in`/`out` are seconds in the clip; `speed` 0.5–2 (0.5 = slow motion).
- `caption` fades in at the bottom left; `card` items are full-screen text over a blurred
  frame of the neighbouring shot. `title`/`outro` are cards added at the start and end.
- `music` is optional (use music you may publish; the game's soundtrack can draw copyright
  claims). With music, game audio drops to `game_volume`.
- `stabilize` per segment: `off` (default; footage from the steady desktop view needs
  nothing), `light`, `normal`, `strong`, `lock`. These use ffmpeg's vidstab, which tracks
  the picture. Test it per shot: on WuWa's effects-heavy footage it made the shake worse
  about as often as better.
- `crop`: `sbs-left`/`sbs-right` or any ffmpeg `crop=w:h:x:y`.

## Exercise runs: route card and timelapse (`route.py`)

For treadmill sessions (Reality Runner or similar). Before recording, start
`launcher\dev\wuwa_route_log.py` (logs position and head rotation 30 times a second). In
the game, **WuWa controls > Recording > Start run** after OBS starts and **End run** before
it stops: each press is logged and flashes a magenta square in the game window, which lines
the video up with the log to within a frame.

```powershell
python route.py load                      # parse the session's logs (newest game session)
python route.py summary --run 3 --real-km 1.43 --real-time 32:48 --calories 160
python route.py sync "E:\OBS Videos\2026-10-02 20-43-58.mkv"
python route.py timelapse "E:\OBS Videos\2026-10-02 20-43-58.mkv" --run 3 --stills 4
python route.py timelapse "E:\OBS Videos\2026-10-02 20-43-58.mkv" --run 3 --real-km 1.43 --real-time 32:48 --calories 160
```

- `summary` writes a Strava-style card (map, distance, moving time, speed, climb; the
  treadmill's own numbers when given) to `%LOCALAPPDATA%\WuWa VR Launcher\routes\`.
- `timelapse` writes `<video> timelapse run<N>.mp4` next to the video: summary card, about
  75 s of run (`--length`), the card again with the treadmill numbers. Frames are spaced
  mostly by distance, so standing still passes quickly, and UEVR-menu time is skipped.
- Stabilisation uses the logged head rotation, not the picture: each frame is rotated to a
  smoothed path, and the 1.4× crop (`--zoom`) hides the edges. Near each frame time it also
  picks the moment the head was closest to that path. `--fov` is the recording's
  horizontal field of view (104° measured for the desktop view of this headset).
- WuWa's on-screen User ID is painted out; `--show-uid` keeps it. `--stills 4` saves four
  sample frames instead of the video, for a quick check.
