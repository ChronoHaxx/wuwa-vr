WUWA VR — DESKTOP LAUNCHER / PUBLIC BETA 1.0.9
Release: beta-2026-10-06-device-dispatch / VR build device-dispatch-20261006 / game 3.7

Run WuWa-VR-Setup.exe once to install for your Windows user, then open the
normal WuWa VR shortcut from the Start menu. No separate Python install is needed.
This small installer does not bundle the VR mod: first installation downloads
about 54 MB, so an internet connection is required.

01 · Game. WuWa VR keeps your saved choice. Use Change to select Kuro or Steam.
   For Steam, choose the detected installation or browse to Wuthering Waves.exe.
   The game opens through Steam. The owner confirmed startup/backend activity
   on this PC; other PCs and current headset use remain unverified.
   If another package is already running, the home screen offers Open existing launcher,
   Stop old launcher & use this version, or Retry connection. Switching requires
   the game, injector and recorder to be idle; it never force-kills them.
02 · Install VR. Check your installed/chosen VR version and its game compatibility.
   Expand Versions & updates only when needed. Confirm your game version and
   click the main Install VR button. The downloaded package is verified before
   installation. This does not start WuWa or switch OpenXR runtimes.
03 · Headset or simulator. Choose Use headset or Use bundled simulator. The current choice is
   marked. Close the game and injector before switching; accept Windows permission
   if requested. Start your headset connection software before headset play.
   Read the account-risk notice. The main button now says Launch in VR. Accept
   Windows permission. Steam opens the selected game through Steam; with Kuro,
   press Play in its launcher.

Returning players: use the main Launch in VR button with saved choices.
Windows permission and, for Kuro, Play in its launcher may still be required.
Updates: Step 02 > Versions & updates > Check updates. Update launcher downloads
and verifies the desktop app update; it does not restart the app. When ready,
close the game and injector, stop recording and finish other operations, then
choose Restart to update and confirm. Installing or updating the desktop app
preserves your selected VR package. In Step 02 choose beta-2026-10-06-device-dispatch
and install it, or select it if already installed, to use device-dispatch-20261006
with the device-verified dispatch repair and current recovery helpers. From 1.0.2–1.0.8, update/restart the app first, then explicitly
select and install the device-dispatch package. Check updates does not select it for you.
If the old app blocks updating, close it and use the new Setup from the website.
This does not guarantee that a stalled worker has stopped; review remaining processes.
Settings, backups, logs and recordings are preserved.
Do not delete the WuWa VR Manager or WuWa VR Launcher folders in LocalAppData.
There are no Setup/Record/Help tabs. The footer's Developer tools link opens the
original web tools for recording, playtests and advanced settings for this exact
package, with the same settings, recordings and debug tools.
Recording supports SteamVR and the bundled simulator; no audio or VDXR capture.

Troubleshooting: controller check, launch-readiness results, copy diagnostics,
repair and recovery. Back returns to the three-step home screen.
Controller check reads all four XInput slots, HidHide and known input helpers;
Refresh reruns it and Copy report copies a sanitized summary. It works without
the web helper. No hiding rules, driver settings or running apps are changed.
A visible controller is not proof that the game receives its buttons.
Failures expand Details beside the persistent status.
Troubleshooting > Stuck launcher processes > Find stuck launcher processes scans
names, PIDs, roles, start times, paths and eligibility, requesting elevation if needed.
No process is selected automatically. Select a verified launcher helper/startup
worker, choose Stop selected, and confirm the listed targets. It does not stop
the game, Steam, headset/runtime or injector processes, or kill by name/process tree.
Runtime/profile changes and active/unknown recording state remain protected. Inconclusive
processes may need manual handling; copy diagnostics and review them first.
Review the per-process results, then choose Retry connection when ready.
Recovery does not reconnect or launch the game automatically.
The readiness check may report that the profile still needs applying. It does not
certify headset output. Nothing is uploaded automatically.

Troubleshooting > Repair & recovery retains version rollback, original-settings restoration,
reset to mod defaults, logs and guarded removal. Repair acts on the version
selected there. Close WuWa and its injector before recovery changes.
To undo this VR update, close the game and injector, stop recording, then use
Troubleshooting > Repair & recovery > Use previous installed version to the previous installed package.
That changes the VR package, not the desktop app. Keep the earlier package;
its known renderer-startup problem can return on the affected PC.
Repair remains available if the installed helper is missing or damaged.
If an update fails after stopping the helper, the home screen offers Retry connection.

Advanced portable fallback: download WuWa-VR-Launcher.zip from the same release,
extract the whole ZIP and open WuWa VR Launcher.exe. Keep its app and python
folders together. This uses the older web interface and does not self-update
the desktop app. Release downloads and previous versions:
https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-device-dispatch
https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-steam

Steam and game graphics settings · 6 October public beta
App 1.0.9 / VR build device-dispatch-20261006 / game 3.7.
Choose Steam or Kuro in step 01; saved choices are preserved. The VR package
keeps the game’s graphics choices instead of forcing low/medium values. A
one-time cleanup removes only recognized generated overrides, preserving custom
edits and backups. The accepted timing correction remains separate.
The affected Windows 11 Steam PC still started flat with 1.0.8. Its logs positively identify a DX12 device, but the callback handoff is never entered after switching probes. This candidate keeps the callback that received frames and lets DX12 own it after the DX11 probe retires, while requiring a verified device and command queue. It retains the earlier startup and recovery repairs. The report does not establish an AMD, driver or Windows-version cause. The backend build and focused callback-dispatch tests passed. Successful startup on the affected PC and physical-headset acceptance remain unverified.
Visible startup stages, cancellation, Close launcher only, guarded process recovery
and Prepare uninstall remain available. Simulator selection and prerequisite
checks are retained. Existing graphics choices and accepted view fixes are kept.
Cinematic framing defaults on. The owner confirmed the simulator replay on
private screen-comfort-r1; sampled frames in the latest 68-second recording show
matching letterbox heights. Headset comfort remains pending. This retains the
game's authored letterbox, not an unrestricted immersive cinematic camera.
Accepted foliage, far indirect-lighting, main Resonators reflection, ultimate
camera and headset 2D-brightness corrections are retained.
Long scene/dialogue stalls, HUD-aspect refresh and moving flat-menu backgrounds
remain open. Manual mono theatre is a menu workaround, not a loading-stall fix.
The remote Windows 11 Steam retest and this build’s headset acceptance remain
pending. Epic game injection is also unverified.

OPTIONAL NPC RIM WORKAROUND: press L3 + R3 to open UEVR, then choose
VR > WuWa Controls > Suppress mismatched NPC rim lighting. Default off; your
chosen setting can be saved. It removes the toon-depth rim effect from every
character using it, including intended rim lighting on nearby characters.
Two repeated simulator comparisons in one stationary NPC scene reduced the extra
right-eye contour at zero rim width; it returned after restoring the prior value.
This supports a visual workaround, not a repair of the underlying stereo shader.
Physical headset acceptance and results across other characters remain pending.

Turn the option off to restore the captured prior value. Entering 2D screen mode
or leaving active native VR also restores it while the option still owns the value;
returning to eligible normal VR reapplies suppression if the option remains on.
Read the status below the option: failed restoration is not a successful rollback.
External changes are preserved. If the status requests a retry, turn the option
off, then on; it retries restoration before applying again. If testing, compare
the visual tradeoff and restoration as one group. TEST THIS.txt covers the current
release checks; previous features need only relevant regression checks.

Horizontal and Vertical Projection defaults remain Raw / default; saved choices
are unchanged. The separate timed projection comparison stays inactive and cannot
start while rim suppression would confound it. Neither it nor this workaround
establishes the underlying NPC-lighting cause. Weapon/Echo submenu reflections
remain open. Cutscene limits and existing controls are described below.

Xbox remains the default gameplay input. F7 or Xbox L3 + LT toggles the portal;
release both controls before repeating. Optional Hand / finger demo is under
WuWa Controls, off each launch. It draws an optical skeleton over the scene; no
grabbing, collisions or gesture gameplay. Supported optical OpenXR tracking is
required. Quest Pro + Virtual Desktop physical validation remains pending.

MANUAL SCREEN MODES: fully hold LT + RT first, then click R3 for mono theatre
(the same scene and HUD for both eyes), or hold L3 for 0.8 seconds for a stereo
screen with scene depth. Release all controls before repeating; close UEVR and
HUD/mouse adjustment. The shortcuts also work in dialogue.
After returning, try Reset HUD aspect in WuWa Controls if the HUD is squashed,
and read its result: the refresh may be unavailable. Opening/closing ESC has
helped when the game allows it. Saved HUD size/position is unchanged.
Automatic cinema is experimental and defaults off. It did not activate for the
latest reported in-engine scene; a prerendered movie has not been tested. Keep
the manual shortcut available instead of relying on automatic switching.

OPTIONAL WALKING: VR > WuWa Controls > VR controllers for walking (optional) >
Walking input. This OpenXR layout starts Off each launch. VR controllers only
supplies slot 0: left stick moves, right stick looks. VR + treadmill / Xbox
slot 0-3 merges with your explicitly selected connected slot; use launcher
Troubleshooting > Controller check, then confirm which slot is your device.
Slot numbers alone cannot identify RealityRunner. Merge preserves its entire
left-stick movement, even when neutral. VR buttons and right-stick look are
added; a physical right stick moved beyond its deadzone keeps both look axes.
In the UEVR menu, the VR left stick navigates even in merge mode; treadmill
movement does not scroll the menu.

Quest A/B/X/Y keep their printed labels. Triggers = LT/RT, grips = LB/RB,
stick clicks = L3/R3. Left Menu = Start; left grip + Menu = View (consumes LB).
Both stick clicks open/close UEVR. First person remains a separate existing
choice; no camera or aim settings are changed. No Quest system-button or D-pad
mapping is added; this is a sightseeing layout, not a complete combat layout.
Wake both controllers, focus the game, release VR buttons/grips/triggers and
center the sticks to arm. Repeat after focus loss, reconnect, mode changes or
opening/closing UEVR. Physical gamepad passthrough or a conflicting slot filter
blocks VR input. No driver, HidHide, RealityRunner or system settings are changed.

Rollback the walking layout with Off (normal input). Off restores the existing
ControllersAllowed behavior: if that setting was already ON, the legacy VR
controller mapping resumes. It does not disable that pre-existing mapping.
If testing this beta, previous features need only relevant regression checks;
TEST THIS.txt covers this release, not all earlier acceptance.

Optional diorama: VR > WuWa Controls > Diorama mode (optional) > Miniature world
(this launch only). It temporarily uses the maximum 10x world scale, not ten times
your normal scale. Turn off to use the current normal VR_WorldScale, including
deliberate manual edits; the toggle does not overwrite it. Starts OFF each launch,
configuration reload and runtime reinitialization. Native Stereo only; AFR disables
the option. Portal on/off both work. Head translation is magnified; use the existing
Recenter action if displaced. Hold L3, then fully squeeze RT to toggle diorama; keep LT released and release L3 and both triggers before repeating. Close UEVR/game menus and leave HUD adjustment first. L3 + LT still toggles the portal. No new keyboard key.

Comfort Lua offers temporary saved Menu HUD placement and temporary visibility
for hidden UI. Both start off and restore owned settings on leaving a recognized
menu; manual edits take precedence. This is not automatic cinematic detection.

Developer tools > Developer playtests saves build-bound results and notes locally.
A matching in-game checklist avoids taking the headset off for basic verdicts.
Voice notes need an explicit microphone choice and confirmation every time;
local transcription needs an existing whisper-cli/model after finishing the
session and closing the game. No microphone starts on launch or installation.

Installed Guide/Controls/Feedback pages open locally in English or Simplified
Chinese. Before installation (or if absent), the online English guide is used.
If an update check fails, read the displayed error and retry with an internet
connection. A cached list does not establish that your version is the latest.
Background launcher checks do not establish headset acceptance.

Guide: https://chronohaxx.github.io/wuwa-vr/guide.html
Feedback: https://github.com/ChronoHaxx/wuwa-vr/issues
Optional support: https://ko-fi.com/chronohax

鸣潮 VR — 桌面启动器 / 公开测试版 1.0.9
发布版本 beta-2026-10-06-device-dispatch／VR 构建 device-dispatch-20261006／游戏 3.7

运行 WuWa-VR-Setup.exe，为当前 Windows 用户安装，然后从开始菜单打开
普通 WuWa VR 快捷方式。无需另行安装 Python。小型安装程序不包含 VR 模组；
首次安装 VR 需要联网下载约 54 MB。

01 · 游戏。保留已保存的选择；点击“更改”可选择库洛或 Steam。
   Steam 版可选已检测的安装，或浏览到 Wuthering Waves.exe；通过 Steam 启动。
   作者已在本机确认 Steam 启动成功、VR 后端运行；其他电脑及头显体验仍待验证。
   如旧版启动器仍在运行，可打开旧版、停止旧版并使用本版，或重试连接。
   切换前需关闭游戏及注入器并结束录制；不会强制结束它们。
02 · 安装 VR。检查已安装／已选 VR 版本和游戏兼容版本；需要时展开“版本与更新”。
   确认游戏版本后点击底部主按钮“安装 VR”。下载的安装包会先经过验证，
   不会启动游戏，也不会自动切换 OpenXR 运行时。
03 · 头显或模拟器。选择头显或内置模拟器，当前选项会标注。
   切换前请关闭游戏和注入器，并在需要时同意 Windows 授权。
   阅读账号风险提示；主按钮会变成“以 VR 模式启动”。同意 Windows 授权后，
   Steam 版通过 Steam 启动；库洛版需在其启动器中点击开始。使用头显前请启动连接软件。

之后使用已保存的选项，点击“以 VR 模式启动”即可进入启动流程。
仍可能需要 Windows 授权；库洛版需在其启动器内点击开始。
更新：第 02 步 > 版本与更新 > 检查更新。“更新启动器”下载并校验桌面应用更新，
此时不会重启。更新就绪后，关闭游戏和注入器、停止录制并完成其他操作，
再选择“重启并更新”并确认。安装或更新桌面应用会保留当前选择的 VR 包。
在第 02 步选择 beta-2026-10-06-device-dispatch 并安装（已安装时直接选用），
即可使用 device-dispatch-20261006 后端。旧版（包括 1.0.8）须先更新并重启应用，
再明确选择并安装新 VR 包；检查更新不会自动改选。如旧应用阻止更新，请关闭应用并运行网站的新 Setup。
这不能保证卡住的启动任务已停止；仍需检查残留进程。设置、备份、日志和录像会保留；
不要删除 LocalAppData 中的 WuWa VR Manager 或 WuWa VR Launcher 文件夹。
不再提供“设置／录制／帮助”标签页。底部“开发工具”链接打开同一安装包的
原版网页工具，共用设置、录像、测试和调试功能。录制支持 SteamVR 和内置模拟器；
暂不支持音频和 VDXR。

底部“故障排查”提供手柄检查、启动检查、诊断及修复恢复；“返回”回到首页。
故障排查 > 卡住的启动器进程 > 查找卡住的启动器进程，扫描名称、PID、角色、
启动时间、路径和是否允许停止，需要时请求管理员权限，默认不勾选任何进程。
只选择已验证归属的启动器辅助程序或启动任务，点击 Stop selected（停止所选进程），
核对并确认列表。不会结束游戏、Steam、头显／运行时或注入器，也不按名称或进程树终止。
查看逐项结果，准备好后再点击“重试连接”；不会自动重连或启动游戏。
运行时／配置修改、正在录制或无法确认的录制状态仍受保护。无法判断的进程可能需要手动处理；
请先复制诊断并检查。
手柄检查只读取四个 XInput 插槽、HidHide 和常见输入辅助程序；可刷新及复制脱敏报告。
不依赖网页辅助程序，不更改隐藏规则、驱动或运行中的程序。
检测到手柄不代表已验证游戏收到按键。
操作结果在底部显示，错误自动展开详情。启动检查不代表头显画面已验证。
修复恢复保留回退、恢复模组前设置、重置模组设置和日志。操作前关闭游戏及注入器。
要撤回本次 VR 更新，请先关闭游戏及注入器并停止录制，再在故障排查 > 修复与恢复
选择“使用上一个已安装版本”。此操作只回退 VR 包，不回退桌面应用。请保留旧包；
受影响电脑回退后可能重新出现旧版的渲染器启动问题。
已安装的辅助程序损坏或缺失时仍可修复；连接中断后可在首页重试。
进阶备用方式：从同一发布页下载 WuWa-VR-Launcher.zip，完整解压后打开
WuWa VR Launcher.exe，保持 app 和 python 文件夹完整。这是旧版网页界面，
不会自动更新桌面应用。发布页及旧版本链接见上方英文部分。

Steam 与游戏画质设置 · 10 月 6 日公开测试版
应用 1.0.9／VR 构建 device-dispatch-20261006／游戏 3.7。
第 01 步可选择 Steam 或库洛版本，保留已保存选择。VR 包保留游戏自身画质选择，
不强制低／中画质；一次性清理仅移除已识别的旧生成覆盖项，保留自定义修改与备份。
已接受的时序修正独立保留。受影响的 Windows 11 Steam 电脑在 1.0.8 中仍显示平面画面。日志已明确识别 DX12 设备，但切换探测后从未进入回调交接。本候选版保留确实收到画面的回调，在 DX11 探测退出后由 DX12 管理，并要求验证设备和命令队列。此前的启动与恢复修复继续保留。现有报告不能确定 AMD、驱动或 Windows 版本是原因。后端编译及回调分派专项测试已通过。受影响电脑的启动和实体头显体验仍未验证。
保留启动阶段、取消提示、仅关闭启动器窗口、受保护的进程恢复与准备卸载。
模拟器选择、依赖检查、已有画质选择及已接受的视图修正继续保留。
过场构图修正默认开启。作者已确认私测 screen-comfort-r1 的模拟器回放改善；
最新 68 秒录像抽样中双眼黑边高度一致，头显舒适度仍待验证。
此修正保留游戏原有黑边，不是可自由环顾的沉浸式过场镜头。
保留此前已接受的植被、远处间接光照、共鸣者主页面反射、大招镜头与头显二维亮度修正。
场景／对话加载停顿、HUD 比例刷新与平面菜单背景移动仍未解决。
手动单目影院可作为菜单临时方案，但不是已确认的加载停顿修复。
远程 Windows 11 Steam 复测及本版头显验收仍待完成；Epic 注入也未验证。

可选 NPC 边缘光方案：按 L3 + R3 打开 UEVR，在 VR > WuWa Controls 中选择
“抑制不匹配的 NPC 边缘光”（Suppress mismatched NPC rim lighting）。默认关闭，
所选状态可以保存。它会移除所有使用该效果的角色的卡通深度边缘光，包括近处角色
原本应有的边缘光，并非只影响远处 NPC。
同一固定 NPC 场景的两轮模拟器对比中，将边缘宽度设为零会减弱右眼额外轮廓，
恢复原值后轮廓重新出现。这支持临时抑制方案，不代表修复了底层立体着色原因；
头显实测及其他角色、场景仍待验证。

关闭选项会恢复之前捕获的数值。进入二维屏幕或原生 VR 不再活动时，也会在仍由
本选项持有该数值的情况下恢复；若选项保持开启，返回适用的普通 VR 后会再次抑制。
请读取选项下方状态，恢复失败不能视为回退成功。外部修改不会被覆盖。
若状态提示重试，先关闭再开启，系统会先重试恢复原值，再重新应用。
之后如选择测试，仅检查相关可选功能；TEST THIS.txt 列出本次发布检查，不替代全部早期验收。

水平／垂直投影默认仍为 Raw / default，不改写已保存选择。独立的限时投影对比
默认不运行；边缘光抑制会干扰对比时不允许启动。两者均不能证明 NPC 光照的底层原因。
武器／声骸子菜单反射仍未解决；过场限制及已有操作见下文。
Xbox 仍是默认输入。F7 或 Xbox L3 + LT 切换空间窗口，重复前松开两者。
WuWa Controls 中的 Hand / finger demo 每次启动默认关闭，需要支持光学裸手数据的
OpenXR 运行时；只显示骨架，不含抓取、碰撞或手势玩法。Quest Pro + Virtual Desktop
的实际对齐与延迟仍待验证。

手动屏幕模式：先按住 LT + RT，再点击 R3 切换单目影院（两眼相同场景与 HUD），
或按住 L3 0.8 秒切换保留场景深度的立体屏幕。重复前松开全部按键；
关闭 UEVR 与 HUD／鼠标调整，对话中也可使用。
返回后若 HUD 被压扁，可尝试 WuWa Controls 中的 Reset HUD aspect 并查看结果；
刷新仍可能不可用。游戏允许时打开再关闭 ESC 菜单曾帮助恢复，不改写保存的大小／位置。
自动影院是实验功能，默认关闭。最近报告的引擎内过场未触发自动切换，
真正的预渲染影片也未测试，请保留手动快捷键作为备用。
可选漫游：VR > WuWa Controls > VR 控制器漫游（可选）> 漫游输入。
需要 OpenXR，每次启动默认关闭。“仅 VR 控制器”在槽位 0 提供输入，左摇杆移动、右摇杆转动视角。
“VR + 跑步机／Xbox 槽位 0-3”合并明确选择的已连接槽位；在启动器“故障排查 > 手柄检查”
查看后，再确认设备所属槽位。槽位编号本身不能识别 RealityRunner。合并完整保留设备左摇杆输入，
包括静止状态；添加 VR 按键及右摇杆视角。实体右摇杆超过死区时，由它控制两轴视角。
UEVR 菜单中始终使用 VR 左摇杆导航，即使处于合并模式；跑步机移动不会滚动菜单。
Quest A/B/X/Y 与印字对应；扳机 = LT/RT，握持键 = LB/RB，按下摇杆 = L3/R3。
左 Menu = Start；左握持键 + Menu = View（不发送 LB）；同时按下两侧摇杆开关 UEVR。
第一人称仍为已有独立选项，不更改相机或瞄准设置；不映射 Quest 系统键或十字键，不是完整战斗布局。
唤醒两侧控制器，切回游戏焦点，松开 VR 按键／握持键／扳机并让摇杆回中以启用输入。
失焦恢复、重连、切换模式或开关 UEVR 后需重复。手柄直通或槽位筛选冲突会阻止 VR 输入。
不会更改驱动、HidHide、RealityRunner 或系统设置。
选择“关闭（常规输入）”撤销此漫游布局，恢复原有 ControllersAllowed 行为；如果该设置原已开启，
旧版 VR 控制器映射会恢复，关闭漫游并不会关闭那项既有映射。如选择测试本版，
已有功能仅需相关回归检查；TEST THIS.txt 列出本次发布检查，无需重复全部早期验收。
可选微缩模式：VR > WuWa Controls > Diorama mode (optional) > Miniature world
(this launch only)。临时使用最高 10 倍世界比例，不是在普通比例上再乘十倍。
关闭后使用当前普通 VR_WorldScale，包括期间主动修改的新值；开关不会改写它。
每次启动、重载配置及重新初始化运行时后关闭。需要 Native Stereo，AFR 会禁用。
空间窗口开／关均可；头部平移也会放大，视角偏移时使用现有 Recenter。关闭 UEVR／游戏菜单并退出 HUD 调整后，按住 L3，再将 RT 按到底切换微缩；保持 LT 松开，再次操作前松开 L3 及两侧扳机。L3 + LT 仍切换空间窗口；没有新增键盘按键。
Comfort Lua 可选择临时菜单 HUD 位置及显示已隐藏的界面；默认关闭，手动修改优先。
这不等于自动检测过场。开发网页中的测试记录可保存结果、备注和录像时间点。
语音备注每次需要选择麦克风并确认；本地转写需要已有模型，结束测试并关闭游戏后才能运行。
安装后的指南/操作/反馈页提供本地中英文版本；安装前或缺失时会打开在线英文指南。
后台检查不代表头显验收。中文界面不代表国服客户端或中国大陆下载连通性已验证。

Retained 1.0.4 recovery: old live workers are identified as stalled rather than shown as
a new launch. Cancellation is a request until acknowledged. Close refreshes
state; if it cannot stop the helper, an explicit Close launcher only confirmation
allows the window to exit without terminating background work. Recovery can
recognize verified older portable workers; unverified identities remain read-only.
Reinstalling preserves saved data and does not stop existing workers.

Uninstall: Troubleshooting > Prepare uninstall shows a confirmation and removes
verified downloads when idle. Use process recovery first for a blocked worker.
Windows Apps uninstall also runs bounded cleanup. Active OpenXR packages and
busy/unverified files are retained with reasons in WuWa VR Manager/uninstall-result.txt.
The report opens after uninstall if files remain. Settings, backups and recordings stay.
