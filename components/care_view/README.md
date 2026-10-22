# care_view

家长在家长端短暂查看设备摄像头的实时画面。

## Contract

平台通过既有的、双向 TLS 的设备命令通道下发两条命令：

| 命令 | payload | 回执 `result_code` |
| --- | --- | --- |
| `care_view_start` | `session_id`、`expires_at`（RFC3339）、`child_id`（可选） | `viewing_started` / `camera_unavailable` / `busy` / `expired` |
| `care_view_stop` | `session_id` | `guardian_closed` |

回执走既有的 `device.command_ack`。不属于本模块的命令原样忽略，因此可以与其他功能共用同一条命令流。

## 儿童安全约束

这些规则由 `src/care_view_core.c` 的状态机强制执行，不依赖调用方自觉：

1. **不可静默开启。** 摄像头、屏幕或喇叭任一不可用时，开始请求被直接拒绝
   （`camera_unavailable`）。没有可见提示或可闻提示就开摄像头，等于偷拍。
2. **确认后才出帧。** 即使开始请求被接受，会话仍停留在 `starting`，必须先播提示音、
   确认屏幕提示已显示，才进入 `viewing`。只有 `viewing` 允许采集。
3. **孩子一键关闭。** 查看中任意按键立即结束，回执 `local_closed`，按键仍交给其他处理器。
4. **设备自管时限。** 平台给的截止时间会被限幅到 `CONFIG_CARE_VIEW_MAX_SESSION_SECONDS`，
   由本机定时器执行；即使停止命令丢失，摄像头也不会一直开着。
5. **单会话。** 已有会话时新的开始请求返回 `busy`，不抢占。
6. **不落盘。** 帧不进 flash、不进 SD 卡、不进 NVS，日志只有 `session_id` 与状态名。

## Configuration

Kconfig 开关 `FEATURE_CARE_VIEW`，默认 `n`，且依赖相机、屏幕、喇叭、按键、命令通道与设备身份
能力同时存在。可选参数：

- `CARE_VIEW_MAX_SECONDS`（默认 180，上限 300）
- `CARE_VIEW_FRAME_INTERVAL_MS`
- `CARE_VIEW_MAX_FRAME_BYTES`
- `CARE_VIEW_MAX_SESSION_SECONDS`

## Tests

```powershell
powershell -ExecutionPolicy Bypass -File tools/run_host_tests.ps1
```

`care_view_core` 覆盖：无提示拒绝开始、开始不等于采集、确认后才允许采集、抢占被拒绝、
截止时间限幅、到点自动结束、重复结束幂等、过期开始被拒绝、计数器按会话清零。

设备构建：

```powershell
pio run -e esp32-s3-n16r8
```

## Remove safely

删除 `components/care_view/`，同时移除 `src/main.c` 中三处 `CONFIG_FEATURE_CARE_VIEW`
分支（按键分发、模块注册、轮询定时器）与 `src/CMakeLists.txt` 中的 `app_requires` 分支。
`tools/verify_removable_modules.ps1` 会在副本中删除本模块并重新构建以确认这一点。
