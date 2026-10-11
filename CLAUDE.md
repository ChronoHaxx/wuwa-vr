# Notes for Claude

- Just get the work done in this project: no quiz or check questions, no "try it yourself"
  gating, no tutorials. Report briefly what changed, what was verified, and what is still open.
- Player-facing text is translated in all ten languages before a release. In the
  VR plugin, draw English through `wuwa_ui::` / `tr()` / VR menu rows / `N_()`,
  never raw `ImGui::Text`. Then extract, translate and merge as in
  `dev/i18n/README.md`, and finish with `python dev/i18n/check_all.py` passing.
