# Xbox controller reference

This reference describes the gameplay bindings in the current public builds:
**Camera candidate + trigger controls · 28 Sep 16:27** (experimental) and
**Stereo, menus and languages · 26 Sep 22:42 BST** (older beta). Neither
changes the bindings first checked against the **Stereo and camera candidate ·
26 Sep 20:11 BST**: the 22:42 beta restricted hidden-UI warnings to detected
menus and made Polar fly the supplied freecam default, and the 28 Sep 16:27
build added developer trigger-input tooling with automatic input off. Earlier
candidates contain different subsets of these features. The owner-tested
**22:37 Camera + acro checkpoint** differs in a few places, listed at the end
of this page. These
bindings were checked against the Lua and native shortcut sheet, not assumed
from the original profile. Physical gamepad passthrough bypasses mod shortcuts.

**L3 / R3:** click the left / right stick. **View:** two squares. **Menu:** three
lines. Use gameplay shortcuts with UEVR settings closed. Release controls when
entering/leaving modes and after returning from Alt-Tab.

## Everyday

| Input | Action |
| --- | --- |
| L3 + R3 | Open/close UEVR settings |
| L3 + B | Hide/show game UI, including game menus |
| L3 + A | Recenter view and enabled portal; position reset is a separate option |
| L3 + View | First person on/off |
| L3 + RB | Game/fixed camera |
| Double R3 | Freecam on/off |
| Double L3 | Windows screenshot (Win + Print Screen) |
| L3 + Menu | Show/hide the illustrated shortcut sheet |
| L3 + D-pad Left / Right | Previous/next sheet page while the sheet is open |
| L3 + D-pad Up | Automatic sheet page |
| L3 + D-pad Down | In first person: toggle full animation follow / previous motion choice |
| L3 + LB, then release | Toggle HUD/mouse adjustment; Xbox mouse shortcuts must be enabled |
| LB + R3 | Original utility assist: V; hold 0.8 seconds for the Tab wheel |
| Hold RB | Full speed when optional Polar walk is enabled |

**LB + Y is not rebound by these shortcuts.** Flight/grapple selection is the
game's behavior. A visible target highlight alone has not proved first-person
aim and the game's targeting agree.

## HUD/mouse adjustment

Enable **Xbox mouse shortcuts** in **VR → WuWa Controls**, close UEVR and press
**L3 + LB once**, then release. Repeat to exit. This latches the mode so you do
not need to hold a bumper. The mode starts off at each launch.

| While the mode is on | Action |
| --- | --- |
| Left stick | Cursor |
| A / B | Click / back |
| X + left stick | Scroll |
| D-pad | Pan the map |
| LT / RT | HUD nearer / farther |
| LB / RB | HUD down / up |
| L3 + LB | Leave adjustment |
| L3 + R3 | Open UEVR settings |

The game does not receive ordinary pad input during adjustment. First-person
placement remains; freecam motion pauses. **Game time continues.** With the mode
off, menus keep their normal gamepad controls by default. The legacy automatic
mouse-in-menus option is separate and starts off.

The 26 September candidates show an amber **HUD / MOUSE MODE
ON** notice even when the shortcut sheet is hidden. The **17:30 Camera, HUD and
privacy** candidate keeps the sheet visible during mouse mode, with the recovery
hint inside it. Its automatic page chooses page 03. Use **L3 + LB** to exit,
or **WuWa Controls → Exit mouse mode now**. That button also disables legacy
automatic mouse-in-menus. Release all controls before resuming play.

The heading stays **HUD / MOUSE MODE**. When the optional legacy setting
activates the cursor for a detected menu, a smaller note says **Activated
automatically in this menu: cursor only**. HUD distance/height controls are
available after entering manual adjustment with L3 + LB.

With game UI hidden, a red recovery hint reminds you that game menus are hidden
too. Detected menus get a larger warning; some WuWa menus do not expose the
cursor flag, so the smaller hint remains available. **L3 + B** restores the UI.

## First person and fixed camera

| Input | Action |
| --- | --- |
| L3 + Y / X | Raise/lower the first-person or fixed camera |
| LB + LT / RT | Fixed camera farther/closer |
| L3 + View | Leave/enter first person |
| L3 + RB | Leave/enter fixed camera |

In the new **First person** section, choose keep the full mesh, hide the character or
hide supported head bones while retaining the body. Animated head/neck following
and the late-position refresh are separate options. Unsupported rigs can fall
back; this is not guaranteed universal head tracking.

**Full animation rotation → Use game view while either stick moves** keeps
the preferred stick override, returning to animation after 0.4 seconds neutral.
**Smooth transition between animation and game view** blends that transition
over 0.05–0.75 seconds. The 17:30 profile enables it at 0.2 seconds; older saved
profiles retain their choice. It also blends reported movement-mode transitions,
such as takeoff/landing, when the character exposes that signal. It does not
continuously smooth animation or headset tracking. Position updates immediately.
The pitch-only legacy option is under **Aiming and right-stick pitch**.
Full animation can still disagree with the game's aim; smoothing is not an
aim-alignment fix.

The 20:11 candidate uses the current game-camera angle and matching eye offsets
after a game-view handover finishes. Returning to animation starts from the last
displayed game view. These repairs have component checks, with headset acceptance
pending. Full animation needs **Game** aim with **Decoupled Pitch off**. If these
conflict, a warning appears beside First person motion and game-view rotation is
used. **Use game aim for full animation** explicitly restores compatible settings;
it does not make the game's grapple targeting follow your headset.

**Hide head bones; full shadow copy** is the supplied default in the 17:30
candidate, following the owner's successful test of the 15:39 version. It
rechecks visibility and retries delayed source meshes automatically after a
character swap. A failure leaves the ordinary head-hiding mode available;
the status reports active shadow copies and pending retries.

For vertical aim, **Use right-stick pitch** disables the horizon lock and selects
game aim. **Try headset aim** selects UEVR Head aim with player control rotation.
These are global UEVR aim settings and change only when selected. Grapple/wing
behavior still needs comparison in the game.

## Recover your profile controls

Open **WuWa Controls → Restore profile settings**. **Restore supplied profile
controls** restores the selected build's original camera, first-person/freecam,
aim and HUD settings. **Restore controls from this launch** returns to the
settings loaded when this game started, including your saved custom choices.
**Undo last controls reset** restores the settings from before that reset.

Temporary HUD/mouse adjustment turns off in all three cases. Rendering choices,
Native Stereo Fix, runtime selection, recording and streamer privacy are outside this reset.
An older profile without `wuwa-profile-defaults.txt` can use launch recovery;
the supplied-profile button explains why it is unavailable. These controls are
separate from UEVR's factory reset. Settings persist through the usual UEVR save.

## Freecam and flight

Choose **Freecam movement style** in Camera customization, then **double R3**
to enter or exit. These move your viewpoint; your character remains in the game
and is not protected or paused.

| Mode | Movement |
| --- | --- |
| Polar fly | Left stick moves, right stick looks; LT rises, LB descends; RT boosts, double RT toggles turbo. |
| Hover drone | Left stick moves horizontally, right stick looks; LT rises, LB descends. Response controls drift. |
| Plane FPV | Right stick pitches/banks, left X yaws; RT/LT increase/decrease speed; LB brakes, RB boosts. |
| Acro drone (manual) | Low throttle + tap RB to arm; right stick rolls/pitches, left X yaws. RT is default throttle; left Y is an alternate setting. Center sticks stop rotation without auto-level. |

**Acro recovery:** hold LB to pause/disarm; **LB + RB** levels and stops. Tap RB
can disarm. Menus and focus loss disarm it. **Double R3** exits. Full acro roll
needs its own headset comfort check. Optional collision depends on the scene's
traceable geometry and cannot stop physical headset leaning through a wall.

## UEVR settings open

| Input | Action |
| --- | --- |
| D-pad / A / B | Navigate / activate / back |
| LB / RB | Change sidebar page |
| Left stick, with RT released | Scroll the focused panel |
| RT + left stick / right stick | Camera forward/side / height |
| RT + D-pad Left / Right | Choose RT shortcut page |
| RT page 1: B / Y / X | Reset camera offsets / recenter view / reset standing origin |
| RT page 2 or 3: X / Y / B | Load or save camera slots 0 / 1 / 2 |

Page 04 on the shortcut sheet is a reference to this **settings-open** context.
It is not a second set of gameplay bindings. If scrolling seems absent, release
RT and focus the panel. Insert remains a keyboard recovery path.

## Differences in the 22:37 Camera + acro checkpoint

The owner-tested build predates the later HUD-input, motion and recovery changes.
Some important differences are below; its own shortcut sheet/settings describe
the features available in that build.

| In the 22:37 build | Instead of |
| --- | --- |
| Mouse/HUD adjustment switches on by itself while a game menu shows the cursor, or while you hold LB + R3. Needs Xbox mouse shortcuts enabled; its saved settings have them **off**. | L3 + LB latched adjustment |
| In that mouse mode LT / RT move the HUD nearer/farther and LB / RB lower/raise it, so menu bumpers and triggers do not change tabs. | Native menu controls while adjustment is off |
| No red hidden-UI notice and no **Show game UI now** button; L3 + B still restores the UI. | Red notice and button |
| No late first-person position refresh and no **Use right-stick pitch** / **Try headset aim** buttons. | Optional camera and aim settings |
| No full-animation motion preset or L3 + D-pad Down motion toggle. | Later first-person motion presets and smooth handovers |
| No **Restore supplied profile controls** or **Undo last controls reset** buttons in UEVR. | Later in-game control recovery |

Choose the candidate if you want these newer controls; it has not yet been
tested in a headset.
