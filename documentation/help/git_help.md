# Git 使用指南 (Git Usage Guide)

本文档旨在指导如何在终端中管理代码更新并推送到远程仓库。

## 1. 基础流程 (Basic Workflow)

每次更新代码后，请遵循以下步骤：

### 第一步：查看状态
```bash
git status
```
确认哪些文件被修改、删除或新增。

### 第二步：添加更改
- 添加所有更改：
  ```bash
  git add .
  ```
- 添加特定文件：
  ```bash
  git add <file_path>
  ```

### 第三步：提交更改
```bash
git commit -m "你的提交信息 (例如: 修复了XXX漏洞, 更新了帮助文档)"
```

### 第四步：推送至远程仓库
```bash
git push
```
*如果这是第一次在当前分支推送，可能需要执行 `git push --set-upstream origin <branch_name>`*

---

## 2. 常见问题及解决方法 (Common Issues & Solutions)

### Q: 提示 "Rejected - non-fast-forward" (推送被拒绝)
**原因**：远程仓库有你本地没有的更新。
**解决方法**：
1. 先拉取远程更新：
   ```bash
   git pull origin <your_branch_name>
   ```
2. 如果有冲突，手动解决冲突文件，然后：
   ```bash
   git add <resolved_files>
   git commit -m "Merge remote changes"
   git push
   ```

### Q: 如何撤销 `git add`？
```bash
git restore --staged <file_path>
```

### Q: 如何放弃本地修改并同步远程？
```bash
git fetch origin
git reset --hard origin/<your_branch_name>
```
**警告**：这会删除所有未提交的本地更改！

### Q: 身份验证失败 (Authentication failed)
- 检查是否配置了 SSH Key。
- 如果使用 HTTPS，检查 Token 或 密码是否正确。

---

## 3. 最佳实践 (Best Practices)

1. **频繁提交**：不要等堆积了一周的代码才提交，尽量按功能点提交。
2. **写好 Commit Message**：简洁明了地描述你做了什么。
3. **推送前先拉取**：养成 `git pull` 的习惯，减少冲突几率。
