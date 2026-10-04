# Xbox controller reference

These bindings describe public beta
[beta-2026-10-04-launcher](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-04-launcher),
backend **npc-rim-20261004**, for game **3.7**. They were checked against the Lua
and native shortcut sheet. Portal, diorama, the deliberate 2D-screen hold below
and **VR → WuWa Controls → Reset HUD aspect** are included; HUD reset does not
need the game ESC menu. Optional experimental features still need physical
headset/gamepad acceptance. See the release receipt for package verification.

Historical evidence: backend compilation and synthetic input checks passed for
the **4 October controller-shortcut source revision**. Portal and diorama
shortcuts were absent from the published 1 October ZIP; the later
**screen-mode-20261004** candidate introduced the 2D hold and HUD reset.
Earlier build differences are recorded at the end of this page. Physical
gamepad passthrough bypasses mod shortcuts.

**L3 / R3:** click the left / right stick. **View:** two squares. **Menu:** three
lines. Use gameplay shortcuts with UEVR settings closed. Release controls when
entering/leaving modes and after returning from Alt-Tab.

## Everyday

| Input | Action |
| --- | --- |
| L3 + R3 | Open/close UEVR settings |
| Hold both triggers fully, then hold L3 for 0.8 seconds | Toggle 2D screen mode; release all three before repeating. Works in dialogue, with UEVR and adjustment closed |
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
| L3 + LB, then release | Toggle HUD/mouse adjustment; Xbox mouse shortcuts must be enabled |
| LB + R3 | Original utility assist: V; hold 0.8 seconds for the Tab wheel |
| Hold RB | Full speed when optional Polar walk is enabled |

**LB + Y is not rebound by these shortcuts.** Flight/grapple selection is the
game's behavior. A visible target highlight alone has not proved first-person
aim and the game's targeting agree.

## Optional Quest controllers for walking

Open **VR → WuWa Controls → VR controllers for walking (optional) → Walking
input**. This is an optional OpenXR sightseeing layout, not a tested combat
layout. It starts **Off (normal input)** each launch. First person remains a
separate choice in the existing **First person** section; enabling walking
does not change camera or aim settings, or fix rendering issues.

- **VR controllers only:** emulates Xbox input on **slot 0**. Left stick moves;
  right stick looks. Use the merge mode instead when keeping treadmill input.
- **VR + treadmill / Xbox slot 0–3:** choose the connected slot used by your
  device. **Launcher → Troubleshooting → Controller check** reports Windows
  XInput slots, but a slot number alone does not identify RealityRunner.
  Movement stays entirely on the selected device's left stick, including when
  it is neutral; the VR left stick does not replace it. VR buttons are added,
  and the VR right stick looks unless the device's right stick is moved beyond
  its deadzone, when that device keeps control of both look axes.

| Quest control | Xbox input |
| --- | --- |
| Right A / B; left X / Y | A / B / X / Y, matching the printed labels |
| Left / right stick | Left / right stick; left ignored in merge mode |
| Click left / right stick | L3 / R3 |
| Left / right trigger | LT / RT |
| Left / right grip | LB / RB |
| Left Menu | Start / Menu |
| Left grip + left Menu | View / Back; consumes LB |
| Both stick clicks | L3 + R3: open/close UEVR settings |

No Quest system-button or D-pad mapping is added. With menus and HUD/mouse
adjustment closed, existing **hold L3, then squeeze LT** portal and **hold L3,
then squeeze RT** diorama shortcuts remain; release all controls before
repeating. First-person, portal and diorama rules above still apply.

Wake both controllers and focus the game. Release the VR buttons, grips and
triggers, and center both sticks before input arms. Do this again after a
focus change, reconnect, mode change, or opening/closing UEVR. Merge also needs
the selected Windows slot to remain connected. **Physical gamepad passthrough**
or a conflicting **XInput controller slot** filter blocks VR input. This mode
does not change drivers, HidHide, RealityRunner, runtime or device settings.

**Headset and RealityRunner acceptance is pending.** Use the grouped checks
in [the public-beta checklist](https://github.com/ChronoHaxx/wuwa-vr/blob/main/launcher/native/TEST%20THIS.txt), including
held-input recovery and return to **Off (normal input)**. Off removes this
optional mapping and restores the normal input path; it does not repair an
existing Windows controller-visibility problem.

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
settings are unchanged. The source keeps one scale for both eyes within a draw;
physical stereo, comfort and return-to-normal acceptance remain pending.

At the next grouped check, confirm that a held squeeze toggles only once and
that neither release order, returning from Alt-Tab nor closing UEVR replays it.
Compare both eyes and slow head movement through off/on/off with the portal
enabled and disabled, then confirm a deliberate
normal-scale edit survives exit. Record explicit results; no build or physical
acceptance is implied by these instructions.

**简体中文：** 关闭 UEVR 设置及游戏菜单、退出 HUD／鼠标调整后，**按住 L3，再将 RT
按到底**，即可切换微缩视角。再次操作前松开所有按键，并保持 LT 松开；**L3 + LT**
及 **F7** 仍切换空间窗口。手柄直通会绕过这些手柄快捷键。新快捷键属于 10 月 4 日的
源代码修订；后端编译与模拟输入检查已通过，头显／手柄验证仍待完成。

也可在 **VR → WuWa Controls → Diorama mode (optional) → Miniature world
(this launch only)** 切换。临时使用最高 **10 倍世界比例**，空间窗口开关均可；仅支持
Native Stereo，AFR 会禁用，没有新增键盘快捷键。关闭后恢复当前普通
比例；主动修改普通比例或预设会保留。每次启动、重载配置或运行时重新初始化后关闭。
头部平移也会放大，偏移时使用现有 **L3 + A 重新居中**。相机偏移、窗口、HUD 和渲染
设置保持原样。双眼、舒适度、开关往返及普通比例修改后的恢复均待实际头显验证。

## Optional optical hand demo

Under **WuWa Controls → Hand / finger demo**, enable **Show optical hands
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

The current public beta includes the newer controls described above. Earlier
component checks and accepted rendering fixes do not establish physical-headset
acceptance for every optional feature; record untested controls as **Not tested**.
