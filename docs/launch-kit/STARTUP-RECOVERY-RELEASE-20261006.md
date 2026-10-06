# Startup-recovery release · 6 October 2026

Release preparation notes for **desktop app 1.0.3** and
**`beta-2026-10-06-recovery`**, game **3.7**. These notes do not by themselves
confirm publication or human acceptance; the final release receipt identifies
the published assets and hashes.

The VR payload includes updated startup/runtime helpers. Its renderer, injector
and build ID **`steam-20261006`** are unchanged from
[`beta-2026-10-06-steam`](https://github.com/ChronoHaxx/wuwa-vr/releases/tag/beta-2026-10-06-steam).
Existing graphics choices, controls, optional features and known rendering limits
remain. NPC rim suppression is still off by default; enabling it also removes
intended nearby rim lighting. No new renderer or gamma fix is claimed.

## Changes and evidence boundary

- Startup status follows the owned elevated worker through its handoff, retaining
  progress, failure and cancellation feedback rather than showing an indefinite
  spinner after the frontend exits.
- Explicit simulator selection supports a missing runtime registration and an old
  package's simulator path. Missing Visual C++ 2015–2022 x64 dependencies produce
  a useful message. Opening or updating the app does not change the runtime.
- Troubleshooting adds **Stuck launcher processes** for reviewing specific owned
  launcher helpers/startup workers before confirmed termination.

Startup/runtime changes have background test coverage. Release packaging and the
integrated process-recovery checks are recorded by the release owner separately;
this document claims no unrecorded pass. The previous **1.0.2 failed to start on a
remote Windows 11 Steam PC**. That PC's 1.0.3 retest, real UAC interaction and
current headset acceptance are **pending**. Previous owner Windows 10 launch and
renderer acceptance are historical evidence, not a pass for this remote PC.

## Update from installed 1.0.2

1. **02 → Versions & updates → Check updates → Update launcher** downloads the
   app update. When game, injector, recording and other operations are idle,
   choose **Restart to update** and confirm.
2. After reopening, explicitly select **beta-2026-10-06-recovery** in step 02 and
   install it. App updates preserve the selected VR package, and Check updates
   does not select the new one. Both parts are needed for these changes.
3. If the old app blocks updating, close it and run the new **WuWa-VR-Setup.exe**
   from the [website](https://chronohaxx.github.io/wuwa-vr/). This updates the app;
   it does not guarantee that a stalled worker has stopped. Review remaining
   processes before changing a package or runtime.

Settings, backups, logs and recordings remain in their existing data folders.
Do not replace installed files by hand. Keep the previous Steam beta for rollback.
The portable ZIP remains an advanced fallback using the older web interface; it
does not install or self-update the desktop app.

## Recover a stuck startup

Use **Stop waiting** when offered; it cancels startup waiting without killing the
game. Inspect **View details** and **Copy diagnostics** before retrying an unknown
result. Do not run another injector in parallel.

Open **Troubleshooting → Stuck launcher processes → Find stuck launcher
processes**. Scanning may ask for elevation but stops nothing. Review names,
PIDs, roles, start times, paths and eligibility. Nothing is selected automatically.
Select only an eligible verified helper/startup worker you intend to stop, choose
**Stop selected**, then confirm the exact listed targets. Identity and safety are
checked again before stopping; per-process results show success, exit or refusal.

WuWa, Steam, headset/runtime and injector processes are excluded; there is no
name-based or process-tree kill. Runtime/profile workers and active or unknown
recording state stay protected. Inconclusive processes may need manual handling.
Review the results, then choose **Retry connection** explicitly when ready.
Recovery neither reconnects nor launches the game automatically.

The short bilingual user checks are in `launcher/native/TEST THIS.txt`; full
controls and limits remain in the bundled PlayerGuide and README.

## 简体中文

这是桌面应用 **1.0.3**／**beta-2026-10-06-recovery** 的发布准备说明，适用于
游戏 3.7。最终发布状态、文件及校验值以发布回执为准。VR 包更新启动／运行时
辅助程序，渲染器、注入器和 **steam-20261006** 构建不变。NPC 轮廓光临时方案
仍默认关闭，开启后也会移除附近角色原有轮廓光；不宣称新增渲染或 gamma 修复。

本版改进启动阶段、错误／取消反馈，以及无运行时注册、旧包模拟器路径时的明确
选择，并提示缺失的 Visual C++ 2015–2022 x64 依赖。仅打开或更新应用不会切换
运行时。故障排查新增“卡住的启动器进程”，先核对归属再确认停止。

启动／运行时改动已有后台测试覆盖，最终打包和集成恢复检查由发布负责人另行记录。
此前 **1.0.2 在远程 Windows 11 Steam 电脑启动失败**；该电脑的 1.0.3 复测、
实际 Windows 授权和本版头显验收**仍待进行**，不能宣称问题已经解决。

从 1.0.2 更新：第 02 步 → 版本与更新 → 检查更新 → 更新启动器；全部空闲后
“重启并更新”。重新打开后，**明确选择 beta-2026-10-06-recovery 并安装**。
更新应用保留旧包选择，检查更新不会自动改选，必须完成两项更新。旧应用阻止更新时，
关闭后运行网站的新 Setup；它不能保证卡住的任务已停止。设置、备份、日志和录像
继续保留，不要手动覆盖已安装文件或删除数据目录；上一 Steam 测试版可留作回退。

需要恢复时先尝试“停止等待”，并查看详情／复制诊断；这不结束游戏，结果不明时
不要并行启动另一个注入器。打开**故障排查 → 卡住的启动器进程 → 查找卡住的
启动器进程**。扫描可能请求管理员权限，但不会停止进程，默认也不勾选。
核对名称、PID、角色、启动时间、路径和可停止状态，仅选择确实想停止且已验证归属、
允许停止的辅助程序／启动任务。点击**停止所选进程**，核对确切目标后确认，并查看
逐项结果。游戏、Steam、头显／运行时及注入器不属于终止目标，不按名称或进程树
终止。运行时／配置任务和正在录制或无法确认的录制状态仍受保护，无法判断的进程
可能需要手动处理。准备好后明确点击**重试连接**，不会自动重连或启动游戏。

## Website verification receipt

The scoped release-page builder completed with Node **v22.15.0** at
`C:/Program Files/nodejs/node.exe`. The existing loopback-only
`dev/i18n/check-launcher-release.cjs` passed **14 checks, zero errors** at
`2026-10-06T13:07:05.933Z`: EN/zh setup, 1440/390/320 px layout, current
installer/portable/previous-release links, local anchors, other-locale notices
and Chinese use without JavaScript. No release asset was fetched or installer run.

Receipt and six screenshots:
`E:/Coding/wuwa-vr/extracted/startup-recovery-20261006/site-review/receipt.json`.
The already-installed Chromium headless shell 1228 was selected with
`WUWA_CHROMIUM`; the default shell 1234 was absent. Nothing was downloaded.

## Final background verification

The final 1.0.3 build passed 95 native core tests, 10 controller groups,
17 updater groups, 25 recovery checks and 39 individual offscreen WPF checks.
The old offscreen harness footer count is stale; individual PASS records were counted.
Recovery verification includes native process-identity reads and stopping only a
newly spawned inert test child, with identity-change refusals. It did not accept UAC
or stop any existing user process. Fourteen headless website checks also passed.

The exact public payload installed successfully through the compiled native installer
into an isolated store. All 842 payload hashes and the bundled helper self-check
passed; the actual native bridge connected to the helper, read status, closed it,
and verified that its PID exited and receipt disappeared. Source inputs were checked
against the packaging receipt. No production install, game launch, injection, global
runtime change or headset acceptance was performed.

Final evidence: `extracted/startup-recovery-public-20261006/build4.json`,
`pack.json`, `verify-install.json`, `real-helper-result.json`, and release receipts.
