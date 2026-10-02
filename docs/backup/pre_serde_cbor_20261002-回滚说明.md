# 回滚说明：eserde 多格式（CBOR）与能力门禁（v1.10）

- 备份时间：2026-10-02
- 备份对象：`feat/derive-args-traits` @ `6631ffd`（feat(eserde): JSON 序列化 / 反序列化）
- 标签：`pre-serde-cbor-20261002`
- 完整备份：`docs/backup/pre_serde_cbor_20261002.bundle`（`git bundle --all`：全部分支与标签，13 个 ref）
- 开发分支：`feat/derive-args-traits`（改动都在这里）
- 远端：**未推送**，等你确认

## 回滚步骤

### 1) 只丢改动、保留分支（最常用）
```bash
git switch feat/derive-args-traits
git reset --hard pre-serde-cbor-20261002
```

### 2) 仓库被搞坏时，从 bundle 完整恢复
```bash
git clone docs/backup/pre_serde_cbor_20261002.bundle restored
git -C restored log --oneline -1        # 应为 6631ffd
```

或把 bundle 当远端拉回现有仓库：
```bash
git fetch docs/backup/pre_serde_cbor_20261002.bundle 'refs/tags/*:refs/tags/*'
git fetch docs/backup/pre_serde_cbor_20261002.bundle feat/derive-args-traits:rescue-branch
```

## 校验备份是否完好
```bash
git bundle verify docs/backup/pre_serde_cbor_20261002.bundle
```
输出应包含 13 个 ref（`refs/heads/main`、`refs/heads/feat/derive-args-traits`、
`refs/tags/pre-serde-cbor-20261002` 等）。

## 这次改了什么（回滚后应该消失的东西）
- 新文件：`eserde/traits.hpp`、`eserde/cbor.hpp`、`eserde/eserde.hpp`、
  `tests/eserde_cbor_check.cpp`、`tests/eserde_cbor_etl_check.cpp`、
  `tests/eserde_compile_fail_caps.cpp`、`tests/eserde_compile_fail_caps_read.cpp`
- 改动：`eserde/serde.hpp`（新增 `field_key` / `field_skipped`）、`eserde/json.hpp`
  （形状判定改为引用 traits、键名策略走基座、新增能力断言）、`tests/eserde_json_check.cpp` /
  `tests/eserde_json_etl_check.cpp`（补能力标签）、`tests/run_check.ps1`（CBOR 步骤 + 两条反例）、
  `README.md` / `docs/EFMT-使用手册.md` / `efmt/core/README.md`
- **行为变化**：没有能力标签的类型现在不能（反）序列化（编译期报错）。回滚后此门禁消失。

## 备注
- `run_check.ps1` 是 **带 BOM 的 UTF-8**：用会丢 BOM 的编辑器改它，PowerShell 5.1 会按
  GBK 解码中文注释并报语法错（`tests/out/_add_bom.ps1` 是补 BOM 的小脚本）。
- 工作区里 `tests/out/`（探针与编译产物）与 `testsinclude/`（疑似误建的拷贝目录）不受版本管理，
  回滚不影响它们。
