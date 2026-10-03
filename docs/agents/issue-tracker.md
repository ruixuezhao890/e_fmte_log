# Issue tracker: GitHub

本仓库的 issue 与 spec 存放在 GitHub Issues：`ruixuezhao890/e_fmte_log`。所有操作使用 `gh` CLI。

> **前置**：本机尚未安装 `gh`（`winget install --id GitHub.cli`），装好后执行 `gh auth login`。未完成这一步，相关技能会直接失败。

## 约定

- 新建 issue：`gh issue create --title "..." --body "..."`，多行正文用 heredoc。
- 读取 issue：`gh issue view <number> --comments`，用 `jq` 过滤评论并一并取 labels。
- 列出 issue：`gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'`，配合 `--label`、`--state` 过滤。
- 评论：`gh issue comment <number> --body "..."`
- 加/删标签：`gh issue edit <number> --add-label "..."` / `--remove-label "..."`
- 关闭：`gh issue close <number> --comment "..."`

仓库由 `git remote -v` 推断；在 clone 内运行时 `gh` 自动识别。

## PR 作为 triage 入口

**PR 作为请求入口：否。**（若本仓库把外部 PR 当特性请求，改成 `yes`；`/triage` 会读这个开关。）

设为 `yes` 时，PR 走与 issue 相同的标签与状态，命令换成 `gh pr view` / `gh pr diff` / `gh pr comment` / `gh pr edit --add-label` / `--remove-label` / `gh pr close`；列外部 PR 用 `gh pr list --state open --json number,title,body,labels,author,authorAssociation,comments`，只保留 `authorAssociation` 为 `CONTRIBUTOR` / `FIRST_TIME_CONTRIBUTOR` / `NONE` 的（去掉 `OWNER` / `MEMBER` / `COLLABORATOR`）。

GitHub 的 issue 与 PR 共享编号空间，裸 `#42` 可能是任意一种：先 `gh pr view 42`，失败再 `gh issue view 42`。

## 技能说「publish to the issue tracker」时

新建一个 GitHub issue。

## 技能说「fetch the relevant ticket」时

运行 `gh issue view <number> --comments`。

## Wayfinding 操作

供 `/wayfinder` 使用。**map** 是一个 issue，**子 issue** 是 ticket。

- **Map**：单个带 `wayfinder:map` 标签的 issue，正文放 Notes / Decisions-so-far / Fog。`gh issue create --label wayfinder:map`。
- **子 ticket**：用 GitHub sub-issue 关联到 map（`gh api` 的 sub-issues 端点）。未启用 sub-issue 时，把子项加进 map 正文的任务清单，并在子 issue 正文顶部写 `Part of #<map>`。标签 `wayfinder:<type>`（`research`/`prototype`/`grilling`/`task`）。领取后指派给执行的开发者。
- **阻塞**：用 GitHub 原生 issue dependencies（UI 可见的规范表示）：`gh api --method POST repos/<owner>/<repo>/issues/<child>/dependencies/blocked_by -F issue_id=<blocker-db-id>`，其中 `<blocker-db-id>` 是阻塞者的数字 **database id**（`gh api repos/<owner>/<repo>/issues/<n> --jq .id`），不是 `#number` 或 `node_id`。GitHub 用 `issue_dependencies_summary.blocked_by` 报告未关闭的阻塞者（真正的门）。不可用时退化为子 issue 正文顶部的 `Blocked by: #<n>, #<n>`。所有阻塞者关闭即解除阻塞。
- **Frontier 查询**：列出 map 下未关闭的子 issue，剔除有未关闭阻塞者或已有 assignee 的；按 map 顺序取第一个。
- **领取**：`gh issue edit <n> --add-assignee @me`（本次会话的第一次写操作）。
- **解决**：`gh issue comment <n> --body "<答案>"` → `gh issue close <n>` → 在 map 的 Decisions-so-far 追加一条上下文指针（要点 + 链接）。
