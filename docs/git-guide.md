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

先检查状态和差异，不要在不了解内容时习惯性执行 `git add .`。

## 2. 每次提交前的强制流程

```bash
git status
git diff
```

确认范围后，只暂存相关文件：

```bash
git add <file1> <file2>
git diff --cached
```

提交前必须确认暂存区只包含一个逻辑变更，并检查没有构建产物、临时日志、密钥或 Token。人工开发者按课程流程自行提交；自动化 Agent 只有用户明确要求时才 commit 或 push。自动化 Agent 禁止擅自 force push、reset 或 rebase。

## 3. 强制提交规范

标题必须使用以下格式，且英文简短描述：

```text
type(scope): short English description

变更内容：
- …

变更原因：（可选）
- …

验证结果：
- …

已知限制：（可选）
- …

关联信息：（可选）
- …
```

必填栏目是“变更内容”和“验证结果”；“变更原因”“已知限制”“关联信息”按需填写。第三方或教师代码必须在正文中注明来源，验证必须真实，未验证必须明确写“未验证”。

允许的提交类型：

- `feat`：新功能
- `fix`：修复
- `docs`：文档
- `test`：测试
- `refactor`：重构
- `chore`：工程/杂项
- `build`：构建或依赖
- `style`：格式或样式

标题示例：

```text
docs(course): add course requirement notes
feat(ui): customize startup screen
build(sdk): update local build instructions
```

## 4. 编写提交正文和执行提交

推荐使用编辑器填写完整正文：

```bash
git commit
```

也可以使用多次 `-m`，但必须保留空行和完整栏目：

```bash
git commit -m "feat(ui): customize startup screen" \
  -m "变更内容：
- 将启动页标题改为 HapticPod

验证结果：
- 主机静态检查已完成
- 目标编译、烧录和实机验证：未验证"
```

不能只写 `git commit -m "..."` 而省略必填正文。

至少区分以下验证层级：

- 主机测试：脚本、单元测试或文档检查在开发机执行。
- 目标编译：针对黄山派目标成功构建固件。
- 烧录：固件实际写入开发板。
- 实机验证：板上启动并按预期运行。

因此不能只写“测试通过”。未执行的层级必须逐项写“未验证”。例如：目标编译成功不代表已经烧录，更不代表实机验证成功。

## 5. 完整示例

### 5.1 教师脚手架导入

```text
chore(scaffold): import teacher-provided peripheral lab

变更内容：
- 导入课程教师提供的 peripheral_lab 示例/脚手架
- 保留教师代码来源边界，不宣称为原创

变更原因：
- 建立课程开发基线，并让后续个人修改可与教师快照区分。

验证结果：
- 文件范围和 Git diff 已检查
- 目标编译、烧录和实机验证：未验证

关联信息：
- 来源：课程教师提供的快照，未提供版本或 commit
```

### 5.2 UI 功能

```text
feat(ui): customize startup title

变更内容：
- 将启动页面标题改为 HapticPod

变更原因：
- 建立首次可观察修改，验证源码到实机的最小开发闭环。

验证结果：
- 目标编译：未验证
- 烧录：未验证
- 实机验证：未验证
```

### 5.3 纯文档

```text
docs(project): add development checklist

变更内容：
- 新增第一周开发清单和验证记录模板

变更原因：
- 统一第一周任务和验证记录方式。

验证结果：
- 已检查 Markdown 结构和链接文本
- 目标编译、烧录和实机验证：不适用
```

## 6. 开发日志和 Git 对应

开发日志可以这样写：

```text
目标：让 KEY1 切换菜单项
修改：新增 input handler
验证：连续按 20 次，菜单每次移动一项，无重复触发
对应提交：abc1234 feat(input): add KEY1 menu navigation
```

这样可以沿着“需求 → 修改 → 验证 → commit”追溯。

## 7. 不建议的危险操作

初学阶段不要把这些当“修复一切”的按钮：

```bash
git reset --hard
git push --force
rm -rf .git
```

已提交历史需要撤销时，优先考虑 `git revert`，保持过程可追溯。

## 8. 第一次推荐提交顺序

```text
chore(repo): initialize HapticPod project

chore(scaffold): import teacher-provided peripheral lab

docs(course): add course notes and development templates

docs(devlog): record first build and flash verification

feat(ui): customize startup text for HapticPod
```

不要为了凑数量硬拆提交；只有实际完成后再提交。

## 9. `.gitignore`

构建产物、缓存、临时日志和密钥不要进入 Git。但不要在不知道课程工程具体输出目录的情况下随便忽略一大片目录；第一次构建后根据 `git status` 再补充 `.gitignore`。
