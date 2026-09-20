# Git 使用约定（课程版）

> 目标：留下真实、可追溯的研发过程，而不是为了“提交次数”刷记录。

## 1. 初始化

在项目根目录：

```bash
git init
git config user.name "你的姓名"
git config user.email "你的邮箱"
git status
```

课程指导手册明确建议先检查状态和差异，不要在完全不了解内容时习惯性 `git add .`。

## 2. 每次提交前的最短流程

```bash
git status
git diff
```

确认修改后，只暂存这一批相关文件：

```bash
git add <file1> <file2>
git diff --cached
```

确认无误后：

```bash
git commit -m "feat(ui): add startup title"
```

## 3. 推荐提交类型

课程手册建议的常用类型：

- `feat`：新功能
- `fix`：修复
- `docs`：文档
- `test`：测试
- `refactor`：重构
- `chore`：工程/杂项

推荐格式：

```text
类型(模块): 本次变化
```

示例：

```text
chore(repo): initialize project structure
docs(course): add course requirement notes
feat(ui): customize startup screen
feat(input): handle key press event
fix(ui): avoid duplicate input event
test(input): record repeated key press verification
```

## 4. 什么叫“好提交”

一个提交最好做到：

- 只解决一个相关问题；
- 修改前能说清目标；
- 修改后有验证；
- 别人能看懂为什么改；
- 必要时可以定位到修改前后的版本。

## 5. 开发日志和 Git 要对应

开发日志可以这样写：

```text
目标：让 KEY1 切换菜单项
修改：新增 input handler
验证：连续按 20 次，菜单每次移动一项，无重复触发
对应提交：abc1234 feat(input): add KEY1 menu navigation
```

这样老师能沿着：

```text
需求 -> 修改 -> 验证 -> commit
```

一路追溯。

## 6. 不建议的操作

初学阶段避免把这些当“修复一切”的按钮：

```bash
git reset --hard
git push --force
rm -rf .git
```

课程手册建议：已提交历史需要撤销时优先考虑 `git revert`，这样过程仍然可追溯。

## 7. 第一次推荐提交顺序

```text
chore(repo): initialize HapticPod project

docs(course): add course notes and development templates

feat(board): bring up course starter on Huangshan board

docs(devlog): record first build and flash verification

feat(ui): customize startup text for HapticPod
```

不要为了凑数量硬拆提交。只有实际完成后再提交。

## 8. `.gitignore`

构建产物、缓存、临时日志和密钥不要进入 Git。

但不要在不知道课程工程具体输出目录的情况下随便忽略一大片目录。第一次构建后根据 `git status` 再补充 `.gitignore`。
