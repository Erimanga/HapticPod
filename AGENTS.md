# HapticPod 仓库协作约定

## 项目基线

- SiFli SDK：v2.5.1
- 开发板：`sf32lb52-lchspi-ulp`
- RT-Thread
- LVGL v9

## 教师脚手架边界

`peripheral_lab/` 是教师提供的示例/脚手架。保留其来源边界，不宣称为原创；不要无理由大规模重构，也不要编辑 build 生成文件。对脚手架的修改必须记录修改内容并完成相应验证。

## 验证纪律

明确区分主机测试、目标编译、烧录和实机验证。未执行的层级必须写明“未验证”；编译通过不能替代烧录或实机通过。

## Git 规则

- 只有用户明确要求时才 commit 或 push。
- 禁止擅自 push、force push、reset 或 rebase。
- 一次提交只包含一个逻辑变更；提交前检查 `git status`、`git diff` 和 `git diff --cached`。
- 提交标题必须是 `type(scope): short English description`。允许的 type：`feat`、`fix`、`docs`、`test`、`refactor`、`chore`、`build`、`style`。
- 每个提交必须有中文正文，至少包含“变更内容”和“验证结果”；按需写“变更原因”“已知限制”“关联信息”。第三方或教师代码必须注明来源；验证必须真实，未验证必须明确说明。

不要提交构建产物、临时日志、密钥或 Token。
