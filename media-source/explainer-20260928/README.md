# 60-second WuWa VR explainer

Designed and rendered by Claude Opus 5.5 (one bounded session); Codex reviewed
the contact sheet and guide, verified decoding, and adapted source paths here.
Silent: visible captions, captions.srt and transcript.md. No copyrighted music.
Earlier gameplay footage is labelled; diagrams are explanatory, not GPU captures.

Windows: Python with Pillow and numpy, FFmpeg/FFprobe, installed Segoe UI fonts.
Set FFBIN to your FFmpeg bin folder or put its executables on PATH.
From this directory: `python src/render.py`, then `python src/verify.py`.
Outputs go to out/. Optional WUWA_EXPLAINER_CLIP overrides the repository's
site/media/feature-first-person.mp4. Font files are not redistributed.
Codex corrected and re-rendered the video on 28 Sep after the owner confirmed
the ultimate camera fix in headset testing. The original design remains credited
to Claude Opus 5.5; no new Claude session was used for this correction.
