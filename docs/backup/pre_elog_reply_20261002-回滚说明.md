# 回滚说明：ecli 接 elog（方案一：分层）

- 备份时间：2026-10-02
- 备份对象：`feat/derive-args-traits` @ `78df443`（feat(sandbox): 让 matchit 在 sandbox 里直接露一手）
- 标签：`pre-elog-reply-20261002`
- 完整备份：`docs/backup/pre_elog_reply_20261002.bundle`（`git bundle --all`：全部分支与标签）
- 开发分支：`feat/derive-args-traits`（改动都在这里）
- 远端：**未推送**，等你确认

## 回滚步骤

### 1) 只丢改动、保留分支（最常用）
```bash
git switch feat/derive-args-traits
git reset --hard pre-elog-reply-20261002
```

### 2) 仓库被搞坏时，从 bundle 完整恢复
```bash
git clone docs/backup/pre_elog_reply_20261002.bundle restored
git -C restored log --oneline -1        # 应为 78df443
```

或把 bundle 当远端拉回现有仓库：
```bash
git fetch docs/backup/pre_elog_reply_20261002.bundle 'refs/tags/*:refs/tags/*'
git fetch docs/backup/pre_elog_reply_20261002.bundle feat/derive-args-traits:rescue-branch
```

## 校验备份是否完好
```bash
git bundle verify docs/backup/pre_elog_reply_20261002.bundle
```

## 这次改了什么（回滚后应该消失的东西）
- 新文件：`ecli/elog_reply.hpp`（命令回复接到 elog 的 sink）、
  `tests/ecli_elog_check.cpp`（11 项）、反例 `tests/ecli_compile_fail_elog_temp.cpp`
- 改动：`sandbox/main.cpp`（输出改道）、`tests/run_check.ps1`（新增 ecli × elog 步骤与反例）、
  `README.md` / `docs/EFMT-使用手册.md` / `sandbox/README.md`（文档）
- **行为变化**：`sandbox --check` 的每一行现在带 `[info] [文件:行 函数]` 前缀（原来是裸文本）；
  REPL 的提示符与命令回复仍是原样字节。**ecli 库本身行为未变** —— 新头是可选层，
  不 include 就是零开销（只解析的固件体积实测一模一样）。

## 备注
- `run_check.ps1` 是 **带 BOM 的 UTF-8**：用会丢 BOM 的编辑器改它，PowerShell 5.1 会按 GBK
  解码中文注释并报语法错（改完确认前三字节是 `EF BB BF`）。
- 工作区里 `tests/out/`（探针与编译产物）不受版本管理，回滚不影响它们。
