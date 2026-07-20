# GitHub 上传清单

源码副本目录：

```text
/home/sthao/OKXTER/LobXChange
```

## 上传前检查

1. 阅读 `README.md`、`docs/BUILD_CN.md` 和 `docs/USAGE_CN.md`。
2. 确认 `third_party/limit-order-book/UPSTREAM.md` 中记录的上游许可证信息。
3. 为 LobXChange 自身选择并添加 `LICENSE`。未添加许可证时，GitHub 上默认不是开源授权。
4. 确认仓库中没有 build 目录、实验输出、凭据或本机配置。

## 初始化仓库

```bash
cd /home/sthao/OKXTER/LobXChange

git init
git add .
git commit -m "Initial LobXChange release"
git branch -M main
git remote add origin https://github.com/<user>/<repo>.git
git push -u origin main
```

如果 GitHub 仓库已包含 README 或其他初始提交，先确认远端内容，再决定是否拉取合并；不要直接强制覆盖。

## 本地最终验证

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j 4
ctest --test-dir build --output-on-failure
```

仓库包含 `.github/workflows/cmake.yml`。推送后 GitHub Actions 会在 Ubuntu 上执行同类 Release 构建和默认测试。
