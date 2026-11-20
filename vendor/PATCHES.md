# vendor/tiny386 的改动清单

导入：`git archive a6a78937a09203da7cd915ce998893169cce922d` 的子集（文件列表见 THIRD_PARTY.md），其余文件没有导入。
上游 README.md 没有导入（内容见上游仓库）。

除下表外文件与上游逐字节相同。改动都用 `MONITA_X86`（本项目，host 和固件都定义）/ `MONITA_FW`（只在固件）宏隔离，
不定义这两个宏时行为与上游一致。逐条改动以 git 历史为准（`git log -p -- vendor/tiny386`）。

| 文件 | 改动 | 原因 |
|---|---|---|
| `pc.c` 第 219、426 行 | 行尾加注释 `/* public-safe-ignore */` | 公开脱敏检查把 SB16 端口号 0x221 误报成词表里的数字 |
