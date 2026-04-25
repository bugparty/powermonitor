#!/bin/bash
# 代码文件 UTF-8 BOM 统计与转换脚本
# 用法:
#   ./convert_to_utf8bom.sh          # 统计模式，显示文件编码状态
#   ./convert_to_utf8bom.sh --convert # 转换模式，将文件转换为 UTF-8 with BOM

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 脚本所在目录的父目录作为项目根目录
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# 要处理的文件扩展名
FILE_EXTENSIONS="cpp c h hpp"
CMAKE_FILES="CMakeLists.txt"

# 检查文件是否有 UTF-8 BOM
has_bom() {
    local file="$1"
    local bom=$(head -c 3 "$file" 2>/dev/null | xxd -p)
    [ "$bom" = "efbbbf" ]
}

# 获取文件编码
get_encoding() {
    local file="$1"
    file --mime-encoding "$file" 2>/dev/null | cut -d: -f2 | tr -d ' '
}

# 添加 UTF-8 BOM
add_bom() {
    local file="$1"
    printf '\xEF\xBB\xBF' | cat - "$file" > "$file.tmp" && mv "$file.tmp" "$file"
}

# 从其他编码转换到 UTF-8 with BOM
convert_to_utf8bom() {
    local file="$1"
    local encoding="$2"

    # 如果已经是 UTF-8 或 ASCII，直接添加 BOM
    if [ "$encoding" = "utf-8" ] || [ "$encoding" = "us-ascii" ]; then
        add_bom "$file"
    else
        # 使用 iconv 转换编码
        iconv -f "$encoding" -t UTF-8 "$file" > "$file.tmp"
        printf '\xEF\xBB\xBF' | cat - "$file.tmp" > "$file"
        rm -f "$file.tmp"
    fi
}

# 检查路径是否应该被排除
should_exclude() {
    local path="$1"
    case "$path" in
        */build/*|*/build_*/*|*/.git/*|*/_deps/*|*/cmake-build*/*|*/.vs/*|*/.vscode/*|*/third_party/*)
            return 0
            ;;
        *)
            return 1
            ;;
    esac
}

# 查找所有代码文件
find_code_files() {
    # 查找源代码文件
    for ext in $FILE_EXTENSIONS; do
        find "$PROJECT_ROOT" -type f -name "*.$ext" 2>/dev/null
    done

    # 查找 CMakeLists.txt
    find "$PROJECT_ROOT" -type f -name "$CMAKE_FILES" 2>/dev/null

    # 查找 .cmake 文件
    find "$PROJECT_ROOT" -type f -name "*.cmake" 2>/dev/null
}

# 统计模式
do_stats() {
    echo -e "${BLUE}=== 代码文件编码统计 ===${NC}"
    echo -e "项目目录: ${PROJECT_ROOT}\n"

    local total=0
    local with_bom=0
    local utf8_no_bom=0
    local ascii=0
    local other=0

    local files_no_bom=""
    local files_other=""

    while IFS= read -r file; do
        # 跳过排除的目录
        if should_exclude "$file"; then
            continue
        fi

        total=$((total + 1))
        encoding=$(get_encoding "$file")

        if has_bom "$file"; then
            with_bom=$((with_bom + 1))
        elif [ "$encoding" = "utf-8" ]; then
            utf8_no_bom=$((utf8_no_bom + 1))
            files_no_bom="$files_no_bom\n  $file"
        elif [ "$encoding" = "us-ascii" ]; then
            ascii=$((ascii + 1))
            files_no_bom="$files_no_bom\n  $file"
        else
            other=$((other + 1))
            files_other="$files_other\n  $file ($encoding)"
        fi
    done < <(find_code_files)

    echo -e "${GREEN}UTF-8 with BOM:${NC}    $with_bom"
    echo -e "${YELLOW}UTF-8 without BOM:${NC} $utf8_no_bom"
    echo -e "${YELLOW}ASCII (no BOM):${NC}    $ascii"
    echo -e "${RED}其他编码:${NC}          $other"
    echo -e "------------------------"
    echo -e "总计:               $total"

    if [ -n "$files_no_bom" ]; then
        echo -e "\n${YELLOW}需要添加 BOM 的文件:${NC}$files_no_bom"
    fi

    if [ -n "$files_other" ]; then
        echo -e "\n${RED}需要转换编码的文件:${NC}$files_other"
    fi

    local need_convert=$((utf8_no_bom + ascii + other))
    if [ $need_convert -gt 0 ]; then
        echo -e "\n${BLUE}提示: 运行 '$0 --convert' 转换所有文件到 UTF-8 with BOM${NC}"
    else
        echo -e "\n${GREEN}所有文件已经是 UTF-8 with BOM 编码${NC}"
    fi
}

# 转换模式
do_convert() {
    echo -e "${BLUE}=== 转换代码文件到 UTF-8 with BOM ===${NC}"
    echo -e "项目目录: ${PROJECT_ROOT}\n"

    local converted=0
    local skipped=0
    local failed=0

    while IFS= read -r file; do
        # 跳过排除的目录
        if should_exclude "$file"; then
            continue
        fi

        if has_bom "$file"; then
            skipped=$((skipped + 1))
            continue
        fi

        encoding=$(get_encoding "$file")

        if convert_to_utf8bom "$file" "$encoding" 2>/dev/null; then
            echo -e "${GREEN}已转换:${NC} $file"
            converted=$((converted + 1))
        else
            echo -e "${RED}失败:${NC} $file"
            failed=$((failed + 1))
        fi
    done < <(find_code_files)

    echo -e "\n------------------------"
    echo -e "${GREEN}已转换:${NC} $converted"
    echo -e "${BLUE}已跳过:${NC} $skipped (已有BOM)"
    if [ $failed -gt 0 ]; then
        echo -e "${RED}失败:${NC}   $failed"
    fi
}

# 主函数
main() {
    case "${1:-}" in
        --convert|-c)
            do_convert
            ;;
        --help|-h)
            echo "用法: $0 [选项]"
            echo ""
            echo "选项:"
            echo "  (无)        统计模式，显示文件编码状态"
            echo "  --convert   转换所有文件到 UTF-8 with BOM"
            echo "  --help      显示帮助信息"
            echo ""
            echo "排除的目录:"
            echo "  build*, cmake-build*, .git, _deps, .vs, .vscode, third_party"
            ;;
        "")
            do_stats
            ;;
        *)
            echo -e "${RED}未知选项: $1${NC}"
            echo "使用 --help 查看帮助"
            exit 1
            ;;
    esac
}

main "$@"
