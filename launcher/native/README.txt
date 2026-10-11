WUWA VR — 1.2.0 BETA / GAME 3.7
Release: beta-1-2-0 | VR build: vr-menu-1-2-0

Run WuWa-VR-Setup.exe, then open WuWa VR from the Start menu.
First VR installation needs internet; no separate Python install is needed.

01 Game: choose your Steam or Kuro installation. For Steam, select
   Wuthering Waves.exe if browsing and sign into Steam first.
02 WuWa VR: accept the account-risk notice and install 1.2.0 beta.
03 Headset or simulator: start your headset software and select a valid
   OpenXR runtime, or explicitly choose Use bundled simulator.
   Select Launch in VR. Steam opens through Steam; for Kuro, press Play.

Launch requires the current installed package's simulator or a valid headset
runtime. An older package's simulator or a missing/broken runtime blocks launch.
Close game and injector before switching. Updating never switches automatically.

The launcher app and VR package have separate update status. Update the app,
then deliberately select/install the new VR package in step 02. Checking for
updates preserves your selection, including a rollback. Settings, backups,
recordings and logs are kept. Older packages remain available for rollback.

L3 + R3 opens the WuWa VR menu (laser, gamepad or mouse); UEVR's full
settings: Menu > All settings (classic UEVR menu). Hold both triggers first,
then click R3 for the next view (full VR, stereo screen, mono theatre), or
hold R3 for 0.8 seconds for full VR. Release all controls before repeating.
PlayStation setup, input limitations and the complete controls are in PlayerGuide.html.
Automatic cinema is experimental, off by default, and untested with real
prerendered movies. Scene stalls, HUD reset and some rendering issues remain.

Use Troubleshooting to copy diagnostics, inspect stuck workers, repair or
prepare uninstall. Stop waiting does not close the game. Stopping verified
launcher workers requires explicit selection and confirmation. Game, Steam,
headset/runtime and injector processes are excluded. Close launcher only does
not claim that background work stopped. Developer tools holds recording.

The owner played this release in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill) through private tests: the new VR menu with the laser and a gamepad, the view shortcuts, full-VR close-ups that follow the camera without getting stuck, blur in both eyes, from the start of a story scene, the menu in front of the stereo screen, a gamepad taking over at once from a put-down controller, the controls cards and the translated menu and VR controllers and a Reality Runner treadmill working together. The last test's additions (treadmill runs in the VR menu, the Controllers page, ambient occlusion off, the L3 card pinned low) and recording in 5-minute parts still need a headset check. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot, and in full VR the HUD can stay squashed until ESC.

The desktop launcher is MIT open source. Mod source changes are public;
upstream and community components retain their own licences. Read LICENSE.txt
and THIRD-PARTY-NOTICES.txt. VirusTotal reports are for exact file hashes and
are not safety or account-compatibility certificates.

Unofficial injection may trigger anti-cheat or account restrictions.
Not approved by Kuro Games. Do not disable security software.
Website: https://chronohaxx.github.io/wuwa-vr/
Source: https://github.com/ChronoHaxx/wuwa-vr

鸣潮 VR — 1.2.0 测试版 / 游戏 3.7

运行安装程序后，从开始菜单打开 WuWa VR。首次安装 VR 包需要网络，无需另装 Python。
01 选择 Steam 或库洛游戏；02 阅读风险提示并安装 1.2.0 beta；
03 启动头显软件并选择有效运行时，或明确选择内置模拟器，然后启动 VR。
Steam 通过 Steam 启动；库洛用户还需在库洛启动器点击开始游戏。

启动要求当前已选安装包的模拟器或有效的头显运行时。旧包模拟器、缺失或损坏的
运行时会阻止启动。切换前关闭游戏及注入器；更新应用不会自动更改运行时。
启动器应用和 VR 包分别显示版本与更新状态。更新应用后，在第 02 步明确选择
并安装新 VR 包。检查更新不会覆盖当前选择或主动回退。用户数据会保留。

L3 + R3 打开鸣潮 VR 菜单（激光、手柄或鼠标）；UEVR 完整设置：Menu > All settings (classic UEVR menu)。
先按住两个扳机，再点击 R3 切换下一视图（完整 VR、立体屏幕、单目影院），或按住 R3 0.8 秒回到完整 VR。
每次操作后松开全部按键。PlayStation 配置和完整限制见 PlayerGuide.html。
自动影院为实验功能，默认关闭，真正的预渲染影片仍未验证。加载停顿、HUD
刷新和部分渲染问题仍未解决。

故障排查提供复制诊断、查看卡住任务、修复和准备卸载。停止等待不会关闭游戏。
只允许明确选择并确认停止已验证的启动器任务；游戏、Steam、头显运行时及
注入器不在停止目标内。仅关闭启动器窗口不表示后台任务已停止。
开发工具保存录像和高级诊断功能。

开发者已在头显中（Valve Index 手柄配 Bigscreen Beyond；Meta Quest Pro 配 Reality Runner 跑步机）通过多轮内部测试试玩本版本：新的 VR 菜单（激光与手柄）、视图快捷键、跟随镜头且不再卡住的完整 VR 特写、双眼模糊（剧情场景一开始即生效）、立体屏幕前的菜单位置、放下 VR 手柄后游戏手柄立即接管、控件卡片与翻译后的菜单、VR 手柄与 Reality Runner 跑步机同时使用。最后一次测试新增的内容（VR 菜单中的跑步记录、“手柄”页面、关闭环境光遮蔽、L3 卡片固定在下方）、按 5 分钟分段录像仍待头显确认。“移动菜单”时 VR 摇杆暂时无法调节距离（请用“菜单距离”）。许多对话特写仍比 2D 镜头更远；完整 VR 下 HUD 可能保持压扁，直到打开并关闭 ESC。
启动器采用 MIT 开源许可；模组改动源代码公开，上游组件保留各自许可。
病毒扫描报告针对精确文件哈希，不保证安全或账号兼容性。
非官方注入可能导致反作弊或账号限制，未经库洛批准。不要禁用安全软件。
