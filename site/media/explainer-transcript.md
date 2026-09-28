# WuWa VR explained in 60 seconds: transcript

Silent video (no audio track). All information is carried by the burned-in captions below and the on-screen diagrams. The only footage is a short, labelled first-person clip from an earlier build (20–32 s), reused from the project website.

## 00–08 s · What it is

- `00:00` WuWa VR: an unofficial, free fan mod that plays Wuthering Waves on a PC VR headset.
- `00:04` It turns the game's single flat camera into two eye views with head tracking.

## 08–20 s · How it works

- `00:08` The game renders a normal Unreal Engine camera, world and UI.
- `00:12` UEVR plus our native hooks redirect the scene and the real game UI. Lua scripts handle camera, body and controls.
- `00:16` A VR runtime (OpenXR) receives two eyes: the same moment, from slightly different viewpoints.

## 20–32 s · What improved

- `00:20` 19–26 Sep: real game UI in VR, first person, head, body and shadow improvements.
- `00:24` 27 Sep: turning Native Stereo Fix on repaired character materials (owner-reported).
- `00:28` 28 Sep: the owner confirmed the ultimate camera fix in headset testing for the cases tested.

## 32–45 s · What's still broken

- `00:32` Still open: one tree animates in both eyes up close…
- `00:36` …but freezes in the left eye far away, even with zero eye separation and equal projection scale.
- `00:41` Still open: reflection submenus and some lighting and shadows.

## 45–55 s · What's next

- `00:45` Next: name the actual failing asset, then compare what each eye's draw really receives.
- `00:50` Then make the smallest proven fix, and test everything together in one grouped pass.

## 55–60 s · Project status

- `00:55` Free and experimental. Source and beginner guide: github.com/ChronoHaxx/wuwa-vr
