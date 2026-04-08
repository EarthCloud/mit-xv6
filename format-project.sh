#!/bin/bash

# 获取项目根目录（脚本所在目录）
ROOT_DIR=$(cd "$(dirname "$0")" && pwd)
cd "$ROOT_DIR"

echo "🚀 开始格式化 xv6 项目源代码..."

# 1. 检查 clang-format 是否安装
if ! command -v clang-format &> /dev/null; then
    echo "❌ 错误: 未找到 clang-format。请运行 'sudo apt install clang-format' 安装。"
    exit 1
fi

# 2. 定义排除目录（例如 .git, .cache, 编译器生成的目录）
EXCLUDE_DIRS="-not -path '*/.*' -not -path './__pycache__/*'"

# 3. 查找并格式化所有 .c 和 .h 文件
# 使用 -i (in-place) 直接修改文件
echo "📝 正在美化 .c 和 .h 文件..."
find . -type f \( -name "*.c" -o -name "*.h" \) $EXCLUDE_DIRS -exec clang-format -i {} +

# 4. 可选：清理 clangd 索引缓存
# 这能解决你之前遇到的“索引不更新”或“报错关键字不消失”的问题
read -p "❓ 是否同时清理 clangd 缓存以刷新索引? (y/n): " confirm
if [[ $confirm == [yY] || $confirm == [yY][eE][sS] ]]; then
    if [ -d ".cache/clangd" ]; then
        rm -rf .cache/clangd
        echo "🧹 已清理 .cache/clangd/index"
    else
        echo "✨ 未发现 clangd 缓存，跳过清理。"
    fi
fi

echo "✅ 任务完成！"
echo "💡 提示：你可以运行 'git status' 查看受影响的文件，建议单独提交这些样式修改。"
