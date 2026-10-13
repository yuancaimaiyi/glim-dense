#!/bin/sh
# 构建 glim-runner:r0.22（在 r0.22-upstream 之上套 plugin）
#
# 前提：
#   已经执行过 docker tag ...:r0.22 ...:r0.22-upstream 一次（备份原镜像）
#   否则此 build 会把当前 r0.22 当"基镜像"，循环引用自己
#
set -e
cd "$(dirname "$0")"

IMAGE_BASE="crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-runner"
UPSTREAM="${IMAGE_BASE}:r0.22-upstream"
TARGET="${IMAGE_BASE}:r0.22"

# 安全检查: 必须有 upstream 备份
if ! docker image inspect "$UPSTREAM" > /dev/null 2>&1; then
    echo "[error] missing $UPSTREAM" >&2
    echo "[hint] 先备份当前 r0.22:" >&2
    echo "       docker tag $TARGET $UPSTREAM" >&2
    exit 1
fi

docker build -t "$TARGET" .
echo "built $TARGET"

# 校验 patch 已进镜像
docker run --rm --entrypoint bash "$TARGET" -c \
    'strings /opt/glim_offline/lib/libglim_dense_dump.so | grep -c "raw+time+intensity" && \
     echo verified plugin'
