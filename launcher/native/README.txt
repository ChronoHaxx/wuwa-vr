WUWA VR — DESKTOP LAUNCHER / PUBLIC BETA 1.0.0
Release: beta-2026-10-04-launcher / backend npc-rim-20261004 / game 3.7

Run WuWa-VR-Setup.exe once to install for your Windows user, then open the
normal WuWa VR shortcut from the Start menu. No separate Python install is needed.
This small installer does not bundle the VR mod: first installation downloads
about 54 MB, so an internet connection is required.

01 · Game. WuWa VR finds your saved/default official launcher. Use Change only if needed.
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
   Windows permission, then press Play in the game's own launcher.

Returning players: use the main Launch in VR button with saved choices.
Windows permission and Play in the official game launcher may still be required.
Updates: Step 02 > Versions & updates > Check updates. Choose Update launcher
when offered; close the game and stop recording first. The launcher updates only
while idle and restarts afterward. To update the VR mod, select its newer version
in Step 02 and install. Settings, backups, logs and recordings are preserved.
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
The readiness check may report that the profile still needs applying. It does not
certify headset output. Nothing is uploaded automatically.

Troubleshooting > Repair & recovery retains version rollback, original-settings restoration,
reset to mod defaults, logs and guarded removal. Repair acts on the version
selected there. Close WuWa and its injector before recovery changes.
Repair remains available if the installed helper is missing or damaged.
If an update fails after stopping the helper, the home screen offers Retry connection.

Advanced portable fallback: download WuWa-VR-Launcher.zip from the same release,
extract the whole ZIP and open WuWa VR Launcher.exe. Keep its app and python
folders together. This uses the older web interface and does not self-update
the desktop app. Release downloads and previous versions:
https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-04-launcher
https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-01-1817

NPC rim lighting workaround · 4 Oct public beta
Desktop app 1.0.0 uses backend npc-rim-20261004. The owner confirmed headset
2D brightness and ultimate-camera improvements in earlier builds; this beta
retains those corrections. The new NPC rim toggle still needs headset checking.
Fresh-PC installation and Steam/Epic game injection remain unverified. Release
test results belong in the release receipt; this guide does not establish a pass.

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
off, then on; it retries restoration before applying again. See TEST THIS.txt for
one grouped check when you choose to test; previous features need only regression.

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

2D screen: fully hold LT and RT first, then hold L3 for 0.8 seconds. Release all
three before repeating; close UEVR and HUD/mouse adjustment. Dialogue is supported.
After leaving 2D, if HUD proportions are wrong, use VR > WuWa Controls > Reset HUD
aspect and read its result. It does not send ESC or change saved HUD size/position.
Cutscenes can still lose the right-eye scene or flicker. Manual stereoscopic 2D
has been useful to the player; automatic cutscene switching is not provided.

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
If testing this beta, previous features need only the relevant regression
checks in TEST THIS.txt, not a complete repeat of earlier acceptance.

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

鸣潮 VR — 桌面启动器 / 公开测试版 1.0.0
发布版本 beta-2026-10-04-launcher／后端 npc-rim-20261004／游戏 3.7

运行 WuWa-VR-Setup.exe，为当前 Windows 用户安装，然后从开始菜单打开
普通 WuWa VR 快捷方式。无需另行安装 Python。小型安装程序不包含 VR 模组；
首次安装 VR 需要联网下载约 54 MB。

01 · 游戏。自动查找已保存或默认位置的官方启动器。仅在需要时点击“更改”。
   如旧版启动器仍在运行，可打开旧版、停止旧版并使用本版，或重试连接。
   切换前需关闭游戏及注入器并结束录制；不会强制结束它们。
02 · 安装 VR。检查已安装／已选 VR 版本和游戏兼容版本；需要时展开“版本与更新”。
   确认游戏版本后点击底部主按钮“安装 VR”。下载的安装包会先经过验证，
   不会启动游戏，也不会自动切换 OpenXR 运行时。
03 · 头显或模拟器。选择头显或内置模拟器，当前选项会标注。
   切换前请关闭游戏和注入器，并在需要时同意 Windows 授权。
   阅读账号风险提示；主按钮会变成“以 VR 模式启动”。同意 Windows 授权后，
   在游戏官方启动器中点击开始。使用头显前请启动其连接软件。

之后使用已保存的选项，点击“以 VR 模式启动”即可进入启动流程。
仍可能需要 Windows 授权和在官方启动器内点击开始。
更新：第 02 步 > 版本与更新 > 检查更新。出现提示后选择“更新启动器”。
请先关闭游戏并停止录制；仅在空闲时更新，完成后应用会重启。
更新 VR 模组则在第 02 步选择较新版本并安装。设置、备份、日志和录像会保留；
不要删除 LocalAppData 中的 WuWa VR Manager 或 WuWa VR Launcher 文件夹。
不再提供“设置／录制／帮助”标签页。底部“开发工具”链接打开同一安装包的
原版网页工具，共用设置、录像、测试和调试功能。录制支持 SteamVR 和内置模拟器；
暂不支持音频和 VDXR。

底部“故障排查”提供手柄检查、启动检查、诊断及修复恢复；“返回”回到首页。
手柄检查只读取四个 XInput 插槽、HidHide 和常见输入辅助程序；可刷新及复制脱敏报告。
不依赖网页辅助程序，不更改隐藏规则、驱动或运行中的程序。
检测到手柄不代表已验证游戏收到按键。
操作结果在底部显示，错误自动展开详情。启动检查不代表头显画面已验证。
修复恢复保留回退、恢复模组前设置、重置模组设置和日志。操作前关闭游戏及注入器。
已安装的辅助程序损坏或缺失时仍可修复；连接中断后可在首页重试。
进阶备用方式：从同一发布页下载 WuWa-VR-Launcher.zip，完整解压后打开
WuWa VR Launcher.exe，保持 app 和 python 文件夹完整。这是旧版网页界面，
不会自动更新桌面应用。发布页及旧版本链接见上方英文部分。

NPC 边缘光临时方案 · 10 月 4 日公开测试版
桌面应用 1.0.0 使用后端 npc-rim-20261004。作者已在此前版本确认头显二维亮度
与大招镜头改善；本版保留这些修正。新增 NPC 边缘光开关仍待头显验证。
全新电脑安装和 Steam／Epic 游戏版本注入尚未确认。最终测试结果以发布记录为准；
指南本身不代表验收通过。

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
之后如选择测试，请按 TEST THIS.txt 做一组检查；已有功能只需相关回归。

水平／垂直投影默认仍为 Raw / default，不改写已保存选择。独立的限时投影对比
默认不运行；边缘光抑制会干扰对比时不允许启动。两者均不能证明 NPC 光照的底层原因。
武器／声骸子菜单反射仍未解决；过场限制及已有操作见下文。
Xbox 仍是默认输入。F7 或 Xbox L3 + LT 切换空间窗口，重复前松开两者。
WuWa Controls 中的 Hand / finger demo 每次启动默认关闭，需要支持光学裸手数据的
OpenXR 运行时；只显示骨架，不含抓取、碰撞或手势玩法。Quest Pro + Virtual Desktop
的实际对齐与延迟仍待验证。

二维屏幕：先将 LT 与 RT 按到底，再按住 L3 0.8 秒；重复前松开三者。
关闭 UEVR 与 HUD／鼠标调整，对话中也可使用。离开二维后如 HUD 比例异常，
在 VR > WuWa Controls 选择 Reset HUD aspect 并查看结果；不会发送 ESC 或修改保存的大小／位置。
过场仍可能出现右眼场景缺失／闪烁；玩家反馈手动立体二维模式有帮助，但不提供自动过场切换。
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
已有功能仅需 TEST THIS.txt 所列相关回归检查，无需重复全部早期验收。
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
