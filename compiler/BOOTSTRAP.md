# Stage2 Bootstrap Contract

`compiler/` 现在是 Jiang 的唯一继续演进编译器主线。

从 `v0.2.0` 开始，Stage2 的发布语义固定为：

- `v0.2.0` 是第一个正式 Stage2 bootstrap release
- 后续 `main` 上的 Stage2 主线应默认由“上一版正式 release 的 `stage2c`”引导构建
- `stage1c` 仅保留为冷启动与灾备兜底路径，不再是日常主构建入口

## Bootstrap Policy

- minimum bootstrap compiler: `v0.2.0`
- preferred bootstrap compiler: previous stable Stage2 release
- emergency fallback: `STAGE2_ALLOW_STAGE1_FALLBACK=1`

## Recommended Flow

1. 下载上一版正式 release 的 `stage2c`
2. 设置 `STAGE2_BOOTSTRAP_STAGE2=/path/to/stage2c`
3. 运行：

```bash
bash ./script/build_stage2.sh
```

## Release Gate

正式 Stage2 release 至少应满足：

- `bash ./script/build_stage2.sh`
- `bash ./script/stage2_selfhost_smoke.sh`
- `bash ./script/stage2_complete_smoke.sh`

## Current Transition

`v0.2.0` 的意义不是“又一个可运行 tag”，而是：

- 第一个可作为后续 Stage2 主线 bootstrap anchor 的正式版本
- 允许 `compiler/` 后续源码逐步切换到 Stage2 语法
- 明确把“发布版 Stage2 -> 开发版 Stage2”固定为主 bootstrap 路径
