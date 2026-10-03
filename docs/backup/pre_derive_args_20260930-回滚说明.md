# 回滚说明：E_FMT_DERIVE 接口改造（v1.9）

- 备份时间：2026-09-30
- 备份对象：`main` @ `f90ba43`（fix(derive): STRICT 守卫误杀迭代器容器成员…）
- 标签：`pre-derive-args-20260930`
- 完整备份：`docs/backup/pre_derive_args_20260930.bundle`（`git bundle --all`：含全部分支与标签，11 个 ref）
- 开发分支：`feat/derive-args-traits`（改造都在这里）
- 远端：**未推送**（origin = github.com/ruixuezhao890/efmt-elog.git），等你确认

## 回滚步骤

### 1) 只丢改动、保留分支（最常用）
```bash
git switch main
git branch -D feat/derive-args-traits
```

### 2) 在开发分支上撤到备份点
```bash
git switch feat/derive-args-traits
git reset --hard pre-derive-args-20260930
```

### 3) 仓库被搞坏时，从 bundle 完整恢复
```bash
git clone docs/backup/pre_derive_args_20260930.bundle restored
git -C restored log --oneline -1        # 应为 f90ba43
```

或把 bundle 当远端拉回现有仓库：
```bash
git fetch docs/backup/pre_derive_args_20260930.bundle 'refs/tags/*:refs/tags/*'
git fetch docs/backup/pre_derive_args_20260930.bundle main:rescue-main
```

## 校验备份是否完好
```bash
git bundle verify docs/backup/pre_derive_args_20260930.bundle
```
输出应包含 `refs/heads/main`、`refs/heads/feat/derive-args-traits`、
`refs/tags/pre-derive-args-20260930` 等 11 个 ref。

## 备注
- 备份用 `--all`，可完整重建当时的分支与标签；
- 工作区里 `tests/out/`（探针与编译产物）与 `testsinclude/`（疑似误建的拷贝目录）都不受版本管理，回滚不影响它们。
