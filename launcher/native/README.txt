WUWA VR — 1.2.3 BETA / GAME 3.7
Release: beta-1-2-3 | VR build: ui-layout-1-2-3

Run WuWa-VR-Setup.exe, then open WuWa VR from the Start menu.
First VR installation needs internet; no separate Python install is needed.

01 Game: choose your Steam or Kuro installation. For Steam, select
   Wuthering Waves.exe if browsing and sign into Steam first.
02 WuWa VR: accept the account-risk notice and install 1.2.3 beta.
03 Headset or simulator: start your headset software and select a valid
   OpenXR runtime, or explicitly choose Use bundled simulator.
   Select Launch in VR. Steam opens through Steam; for Kuro, press Play.

Launch requires the current installed package's simulator or a valid headset
runtime. An older package's simulator or a missing/broken runtime blocks launch.
Close game and injector before switching. Updating never switches automatically.

The launcher app and VR package have separate update status. Update the app;
when it starts with the game closed, it installs a newer VR package for the
same game version by itself, and Back to ... in step 02 returns to the one you
had. Test builds are never replaced. Settings, backups, recordings and logs are
kept. Every published version is in Troubleshooting > Versions & updates.

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

The owner played 1.2.0 in the headset (Valve Index controllers with a Bigscreen Beyond; a Meta Quest Pro with a Reality Runner treadmill), 1.2.1 in the headset with the Kuro launcher copy of the game, and an earlier 1.2.3 test build in the headset (SteamVR) with the Kuro launcher copy. The final 1.2.3 test builds were tested by the owner on the OpenXR simulator with both the Steam copy and the Kuro launcher copy: menus, the mouse and the controller cursor in the 16:9 and taller layouts, story pictures and story scenes, the character screen and the translations; on the Steam copy the UI redirect starts and the stereo fixes pass their code checks. The 1.2.3 release build has not been played in a headset yet. In Move menu the VR thumbstick does not change the distance yet (use Menu distance). Many dialogue close-ups still sit wider than the 2D shot.

The desktop launcher is MIT open source. Mod source changes are public;
upstream and community components retain their own licences. Read LICENSE.txt
and THIRD-PARTY-NOTICES.txt. VirusTotal reports are for exact file hashes and
are not safety or account-compatibility certificates.

Unofficial injection may trigger anti-cheat or account restrictions.
Not approved by Kuro Games. Do not disable security software.
Website: https://chronohaxx.github.io/wuwa-vr/
Source: https://github.com/ChronoHaxx/wuwa-vr

鸣潮 VR — 1.2.3 测试版 / 游戏 3.7

运行安装程序后，从开始菜单打开 WuWa VR。首次安装 VR 包需要网络，无需另装 Python。
01 选择 Steam 或库洛游戏；02 阅读风险提示并安装 1.2.3 beta；
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

开发者已在头显中（Valve Index 手柄配 Bigscreen Beyond；Meta Quest Pro 配 Reality Runner 跑步机）试玩 1.2.0，用库洛官方启动器版本的游戏在头显中试玩了 1.2.1，并用库洛版在头显（SteamVR）中试玩了较早的 1.2.3 测试版。最终的 1.2.3 测试版已由开发者在 OpenXR 模拟器中分别用 Steam 版和库洛官方启动器版测试：16:9 和较高布局下的菜单、鼠标和手柄光标，剧情图片和剧情场景，角色界面以及翻译；在 Steam 版上 UI 重定向能启动，立体修复通过代码校验。1.2.3 正式版尚未在头显中试玩。“移动菜单”时 VR 摇杆暂时无法调节距离（请用“菜单距离”）。许多对话特写仍比 2D 镜头更远。
启动器采用 MIT 开源许可；模组改动源代码公开，上游组件保留各自许可。
病毒扫描报告针对精确文件哈希，不保证安全或账号兼容性。
非官方注入可能导致反作弊或账号限制，未经库洛批准。不要禁用安全软件。
