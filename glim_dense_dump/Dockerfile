# glim_dense_dump: GLIM extension plugin
#
# 产物：crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-runner:r0.22
#   = r0.22-upstream  +  /opt/glim_offline/lib/libglim_dense_dump.so
#
# 使用:
#   docker run --rm \
#     -e GLIM_DUMP_RAW_FRAMES_DIR=/output/map/raw_frames \
#     ... \
#     --entrypoint bash <image> \
#     -c '. /opt/ros/humble/setup.sh && \
#         LD_PRELOAD=/opt/glim_offline/lib/libglim_dense_dump.so \
#         glim_offline /data/input.hera -c /glim/config -o /output/map'
#
# 前置: r0.22-upstream 镜像（原始 GLIM r0.22，手动 docker tag 备份一次）:
#   docker tag crpi-.../glim/glim-runner:r0.22 crpi-.../glim/glim-runner:r0.22-upstream

FROM crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-runner:r0.22-upstream

LABEL org.hera.image.id="glim-runner"
LABEL org.hera.image.version="r0.22"
LABEL org.hera.image.patch="glim_dense_dump plugin"

COPY src/        /tmp/glim_dense_dump/src/
COPY CMakeLists.txt /tmp/glim_dense_dump/CMakeLists.txt

RUN . /opt/ros/humble/setup.sh && \
    cd /tmp/glim_dense_dump && \
    mkdir -p build && cd build && \
    cmake .. -DCMAKE_PREFIX_PATH=/opt/glim_offline -DCMAKE_BUILD_TYPE=Release && \
    make -j && \
    cp libglim_dense_dump.so /opt/glim_offline/lib/ && \
    cd / && rm -rf /tmp/glim_dense_dump

# 显式复位 ENTRYPOINT/CMD/WORKDIR。基镜像 ENTRYPOINT=[] 空，
# hera-desktop runner 用 `sh -c <cmd>` 启动, 不能有 ENTRYPOINT 覆盖。
ENTRYPOINT []
CMD ["python3", "index.py"]
WORKDIR /app
