# Independent review of GUIDE.md (28 Sep 2026)

Scope: read GUIDE.md against `foliage-evidence.md` and `projection-evidence.md` in this folder. Nothing outside this folder was edited or run. Claims I could not check against these two files (Indath's timing suggestion, the two VF/shader candidates, the 13:18 package) are marked as such rather than called wrong.

## Verdict

The guide is accurate and appropriately hedged. Its evidence claims about the far tree and projection match both evidence files, and it clearly says the 16:28 package is tooling, not a rendering fix. The timeline + code map + next-experiments structure works for a beginner. The issues below are mostly about clarity. Only #1 and #2 could lead a tester to a wrong conclusion.

## Problems worth fixing

1. **The package label suggests a camera fix without saying it is unaccepted.** Line 10 tells testers to select "Camera candidate + trigger controls · 28 Sep 16:27", and line 15 only says the package "does not contain a new foliage repair". The morning's same-draw camera-base candidate (line 49) is a render-affecting change, and it has not been accepted with a real ultimate recording. Append to line 15:
   > "It also carries the 28 Sep morning same-draw camera-base candidate aimed at ultimate-transition desync; that candidate is **not yet accepted** because no actual ultimate has been recorded with it."

2. **The rollback advice points to a package the guide never introduces.** Line 144 says "Keep the 13:18 package or older public beta for rollback", but no 13:18 package appears anywhere else. Either add a timeline row that says what 13:18 contains, or replace it with:
   > "Keep the older 26 Sep public beta (included in this ZIP) for rollback."

3. **The 16:27 vs 16:28 times look like a typo.** Line 7 says "16:28 BST package" and line 10 says "· 16:27". If one time is the build label and the other is the package time, say so once:
   > "(the launcher lists the build as 16:27; the ZIP was packaged at 16:28)".

4. **The timeline is out of order at the end.** The zero-IPD retreat row (line 52, "28 Sep afternoon") comes after the 16:00–16:28 row. The retreat evidence file is time-stamped 16:28, and the guide describes the 16:28 tooling as prepared for the *next* tests. Move the retreat row above the 16:00–16:28 row, or give it a clock time.

5. **The zero-IPD row is slightly more certain than its evidence.** Line 78 says "retained CPU eye positions coincided". The evidence says these came from four-call scopes marked `duplicate_eye:true, valid:false`, and that strict two-call pairing was never proven. Suggested wording:
   > "Sampled CPU offset outputs coincided (399/399 rows; four-call scopes, strict eye-pair association unproven)."

6. **The projection row leaves out a trap and a missing input.** For line 80, add: "Auxiliary pass-2 views (different M11 0.848, no off-axis) exist; grouping by pass number alone manufactures a false FOV difference. No per-view LOD distance factor was recorded either." This stops the next person from repeating the false positive described in `projection-evidence.md`.

## Minor

- Line 11: "Select your headset runtime… Start your headset software first" puts the steps in the wrong order. Starting the headset software belongs before step 2.
- Line 66 (the two VF/shader representation candidates) and line 47 (Indath) cannot be checked against the two supplied evidence files. The wording is already hedged, so no change is needed. A pointer to the source evidence file would help.
- Line 88: "full-width target versus a custom eye-width target" is accurate. Adding "(2W×H vs W×H, `FFakeStereoRenderingHook.cpp` ~6649/7702)" would make it actionable.

## Confirmed consistent (no change needed)

- The same tree moves in both eyes up close, then goes left-static / right-moving far away, while CPU offsets are zero throughout the retreat. The neighboring tree moves in both eyes. (foliage-evidence)
- The main-eye M00/M11 are equal, with mirrored M20. Viewport dimensions and temporal-LOD inputs are unrecorded. No global FOV/LOD fix is justified. (projection-evidence)
- NSF material repair is described as a user report with limited scope. The reflection work is described as a one-page improvement. The input and inventory tools are described as not yet accepted live.
- The next-work order (name the asset → join it to a draw → measure the missing inputs → smallest fix → one grouped acceptance pass) follows from the evidence gaps.
