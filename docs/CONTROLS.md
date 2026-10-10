# WuWa VR controls

Reference for **1.1.0**, package **beta-1-1-0**, VR build
**player-ready-1-1-0**, targeting **WuWa 3.7**. The launcher app and VR package
update separately; check both identities when reporting a problem.

Xbox/XInput remains the usual gameplay path. **L3 / R3** mean click the left /
right stick; **View** is the two-squares button and **Menu** is the three-lines
button. Use gameplay shortcuts with UEVR settings closed. Release controls
between gestures and after switching focus. **Insert** is the keyboard recovery
path for opening UEVR. Physical gamepad passthrough bypasses mod shortcuts.

Startup on the affected Windows 11 Steam PC with the simulator was confirmed by
the user on **1.0.10**. This does not establish **1.1.0 PlayStation, headset or
optional-feature acceptance**; those checks remain pending. Historical build
results are recorded in [release notes](https://github.com/ChronoHaxx/wuwa-vr/releases).
The [bilingual player guide](../launcher/native/PlayerGuide.html) covers setup,
updates, runtime selection, recording and recovery.

## PlayStation controllers (experimental)

A working **Steam Input / Xbox-XInput mapping** uses the bindings below. In
1.1.0, an experimental direct reader also accepts Sony **DualShock 4 v1/v2** and
**DualSense / DualSense Edge** USB or Bluetooth reports. It becomes eligible
after two seconds without a successful raw XInput pad poll. **Any active XInput
pad takes priority**, including a different connected controller. The mod does
not install a driver or write controller output reports.

| PlayStation | Xbox name used below |
| --- | --- |
| Cross / Circle / Square / Triangle | A / B / X / Y |
| L1 / R1 | LB / RB |
| L2 / R2 | LT / RT |
| Left / right stick click | L3 / R3 |
| Share (PS4) / Create (PS5) | View / Back |
| Options | Menu / Start |

For example, **L2 + R2 first, then R3** toggles mono theatre; **L3 + Share/Create**
toggles first person, and **L3 + Options** opens the shortcut sheet. Release all
controls between gestures and after reconnects or source changes. Read the UEVR
shortcut-panel status for the active input source or pause reason.

**Direct input cannot consume a shortcut from WuWa's own native HID input.**
The game may react to the same buttons. Prefer the working XInput route when
you need the mod to consume a chord. The direct reader pauses with physical
gamepad passthrough or an explicit XInput slot filter. No touchpad/PS-button
shortcut, rumble, adaptive-trigger or light controls are added. A third-party
pad or unusual report format is not implied to work.
Direct input can open/close UEVR with L3 + R3, but does not add menu navigation;
use keyboard/mouse, mapped XInput or existing VR menu controls. Physical PS4/PS5 acceptance
is pending; simulated reports do not establish USB/Bluetooth device acceptance.

## Everyday

| Input | Action |
| --- | --- |
| L3 + R3 | Open/close UEVR settings |
| Fully hold LT + RT first, then click R3 | Toggle mono theatre: one scene and HUD shown identically to both eyes; useful for problematic menus/cinematics |
| Fully hold LT + RT first, then hold L3 for 0.8 seconds | Toggle stereoscopic screen, retaining scene depth; release all controls before repeating. Works in dialogue, with UEVR and adjustment closed |
| L3 + B | Hide/show game UI, including game menus |
| L3 + A | Recenter view and enabled portal; position reset is a separate option |
| Hold L3, then fully squeeze LT; or F7 | Toggle the portal; release controls before repeating |
| Hold L3, then fully squeeze RT | Toggle the temporary 10× diorama; release controls before repeating |
| L3 + View | First person on/off |
| L3 + RB | Game/fixed camera |
| Double R3 | Freecam on/off |
| Double L3 | Windows screenshot (Win + Print Screen) |
| L3 + Menu | Show/hide the illustrated shortcut sheet |
| L3 + D-pad Left / Right | Previous/next sheet page while the sheet is open |
| L3 + D-pad Up | Automatic sheet page |
| L3 + D-pad Down | In first person: toggle full animation follow / previous motion choice |
| L3 + LB, then release | Toggle HUD/mouse adjustment; controller mouse shortcuts must be enabled |
| LB + R3 | Original utility assist: V; hold 0.8 seconds for the Tab wheel |
| Hold RB | Full speed when optional Polar walk is enabled |

The two screen shortcuts are manual. Release all controls between toggles, with
UEVR and HUD/mouse adjustment closed. Automatic cinema is experimental, **off
by default**, and did not activate for the latest reported in-engine scene;
prerendered movie switching is untested. Do not rely on it to switch for you.

If returning from a screen view squashes the HUD, try **WuWa Controls → Reset
HUD aspect** and read its status. It can remain unavailable. Opening and closing
ESC has helped when the game allows it. Manual mono theatre can also combine a
troublesome flat menu and its moving background on one panel.

**LB + Y is not rebound by these shortcuts.** Flight/grapple selection is the
game's behavior. A visible target highlight alone has not proved first-person
aim and the game's targeting agree.

## Optional Quest controllers for walking

Quest controllers can act as an Xbox pad through OpenXR. This is a
sightseeing layout, not a tested combat layout. They start **off** each launch,
so only your Xbox pad (or treadmill) works until you turn them on. First person
is a separate choice under **View modes**; this does not change camera or aim
settings.

**Turn on or off:** hold the **left Menu** button for 1 second (a buzz
confirms: long for on, short for off), or tick **VR → WuWa Controls → VR
controllers for walking (optional) → VR controllers on (this launch)**. A quick
press of left Menu is still Start. Controllers without a usable Menu button,
such as Valve Index (SteamVR keeps its system button), turn them on by holding
**both stick clicks for 1 second** while they are off; once on, both stick
clicks open UEVR settings, where the checkbox turns them off. The hold gestures
are unavailable while UEVR's separate **Enable motion-controller input** is on;
use the checkbox then.

**Sharing with Xbox / treadmill** (remembered):

- **Both together** (default): both work at once. Buttons combine, triggers
  take the stronger press, and each stick follows whichever controller pushes
  it further, so treadmill walking keeps working while VR looks around.
- **Last used wins**: whichever you pressed or pushed last controls the game;
  the other is ignored until you use it. Holding a stick or button does not
  switch.
- **VR only (Xbox ignored)**: only VR controllers work while on.

**Controller slot** (remembered): the Windows XInput slot that VR shares.
Slot 0 suits most setups. For a treadmill, choose its slot; **Launcher →
Troubleshooting → Controller check** reports the slots in use.

| Quest control | Xbox input |
| --- | --- |
| Right A / B; left X / Y | A / B / X / Y, matching the printed labels |
| Left / right stick | Left / right stick |
| Click left / right stick | L3 / R3 |
| Left / right trigger | LT / RT |
| Left / right grip | LB / RB |
| Left Menu (quick press) | Start / Menu |
| Left grip + left Menu (quick press) | View / Back; consumes LB |
| Left Menu (hold 1 s) | Turn VR controllers on or off |
| Both stick clicks (hold 1 s, while off) | Turn VR controllers on (for controllers without Menu) |
| Both stick clicks | L3 + R3: open/close UEVR settings |

No Quest system-button or D-pad mapping is added. With menus and HUD/mouse
adjustment closed, existing **hold L3, then squeeze LT** portal and **hold L3,
then squeeze RT** diorama shortcuts remain. First-person, portal and diorama
rules above still apply.

While on, the selected slot always reports a connected pad, so the game keeps
reading it even when the controllers sleep, the headset comes off or no Xbox is
plugged in. If the game is not reading the slot yet, the mod asks it to check
for controllers again every 2 seconds. While UEVR settings are open, every
controller steers them at once. A VR button or stick that is already held
when VR input starts, wakes up, or when UEVR opens or closes, is ignored until
you let go of it; everything else works straight away. **Physical gamepad
passthrough** or a conflicting **XInput controller slot** filter blocks VR
input. This mode does not change drivers, HidHide, RealityRunner, runtime or
device settings.

**Headset and RealityRunner acceptance is pending.** Turning VR controllers off
restores your existing `ControllersAllowed` behavior. If that setting was
already on, the legacy VR controller mapping resumes.

## HUD/mouse adjustment

Enable **controller mouse shortcuts** in **VR → WuWa Controls → Controller shortcuts**, close UEVR and press
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

The **HUD / MOUSE MODE** notice and shortcut sheet show the current mode;
the automatic sheet page uses page 03. Use **L3 + LB** or
**WuWa Controls → Exit mouse mode now** to exit. The button also disables legacy
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

In **VR → WuWa Controls → First person**, choose keep the full mesh, hide the character or
hide supported head bones while retaining the body. Animated head/neck following
and the late-position refresh are separate options. Unsupported rigs can fall
back; this is not guaranteed universal head tracking.

**Full animation rotation → Use game view while either stick moves** keeps
the preferred stick override, returning to animation after 0.4 seconds neutral.
**Smooth transition between animation and game view** blends that transition
over 0.05–0.75 seconds. The supplied profile enables it at 0.2 seconds; saved
custom choices remain authoritative. It also blends reported movement-mode transitions,
such as takeoff/landing, when the character exposes that signal. It does not
continuously smooth animation or headset tracking. Position updates immediately.
The pitch-only legacy option is under **Aiming and right-stick pitch**.
Full animation can still disagree with the game's aim; smoothing is not an
aim-alignment fix.

Returning to animation starts from the last displayed game view. Full animation
needs **Game** aim with **Decoupled Pitch off**. If these
conflict, a warning appears beside First person motion and game-view rotation is
used. **Use game aim for full animation** explicitly restores compatible settings;
it does not make the game's grapple targeting follow your headset.

**Hide head bones; full shadow copy** is the supplied default. It
rechecks visibility and retries delayed source meshes automatically after a
character swap. A failure leaves the ordinary head-hiding mode available;
the status reports active shadow copies and pending retries.

For vertical aim, **Use right-stick pitch** disables the horizon lock and selects
game aim. **Try headset aim** selects UEVR Head aim with player control rotation.
These are global UEVR aim settings and change only when selected. Grapple/wing
behavior still needs comparison in the game.

## Optional temporary menu layout

In the Comfort Lua panel, save a **Menu** layout that you find comfortable,
then enable **Use saved Menu HUD temporarily**. Close UEVR settings and reopen
the game menu. When the existing native menu signal recognises it, the mod
temporarily uses that placement, then restores the prior placement on return
to gameplay. **Temporarily show hidden UI in game menus** can separately reveal
a HUD you deliberately hid. Both options start off each Lua session.

Your manual changes take precedence. Unknown menu signals, focus changes and
manual adjustment suspend the override. A missing saved layout is reported;
the mod does not choose a new one for you. Menu recognition is incomplete and
is not a cinematic or subtitle detector. These options do not alter the game
camera, stereo passes or portal dimensions.

## Optional diorama mode

With UEVR settings and game menus closed, **hold L3, then fully squeeze RT**
to turn diorama on or off. Release all controls before repeating. Keep LT
released: **L3 + LT** still toggles the portal, and **F7** remains its keyboard
shortcut. Leave HUD/mouse adjustment before using the diorama shortcut.
Physical gamepad passthrough bypasses these controller shortcuts.

The menu control remains **VR → WuWa Controls → Diorama mode (optional) →
Miniature world (this launch only)**. Diorama temporarily uses the maximum **10× world scale** for
a miniature view, with the portal on or off. It requires Native Stereo; AFR
disables it. No new keyboard binding is added.

Turning it off returns to the current normal `VR_WorldScale`. The toggle never
overwrites that value; intentional normal-scale slider or preset edits remain
when you turn it off. It starts off each launch, configuration reload and runtime
reinitialization. Head translations also scale up; use existing **Recenter
(L3 + A)** if displaced. Camera offsets, portal dimensions, HUD and renderer
settings are unchanged. Physical stereo, comfort and return-to-normal acceptance
remain pending.

## Optional optical hand demo

Under **WuWa Controls → Hand / finger demo (optional)**, enable **Show optical hands
(this launch only)**. This draws a coloured hand/finger skeleton in both eyes,
over the game, with no scene occlusion, grabbing, collisions or gesture input.
Xbox gameplay remains unchanged. It is a demonstration, not a replacement
for the game controller; it starts off every launch.

It requires a runtime that supplies optical hand joints and their source,
such as a supported Virtual Desktop OpenXR configuration. Controller-based
simulated fingers are rejected. If support is missing, the control explains
why it is unavailable. Tracking loss hides the affected hand; stale data is
not held in place. Quest Pro + Virtual Desktop physical validation is pending.

## Developer playtest notes

Open the **Developer tools** footer link in the desktop launcher, then **Developer
playtests** in the web tools. Start one local session for the selected build.
Every result begins **Not tested**; only your explicit verdict changes it.
The backend menu's **Developer playtest checklist** can record results and
notes, link the launcher's latest recording, and finish that active session.
It does not start recording or a microphone automatically.

Voice notes are optional: choose a microphone and confirm each recording in
the web page. Local transcription requires an existing whisper-cli and model,
a finished session and idle game/injector. Original audio stays local, and
transcription does not assign pass/fail. Export the session report and keep
the build identity and recording timestamp with a reproducible issue.

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
