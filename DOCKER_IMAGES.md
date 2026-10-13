# Vistara 稠密点云 + 全景上色 · Docker 镜像构建

本文档记录 **3 个镜像** 的构建流程、依赖关系、Dockerfile 位置。

```
┌─────────────────────────────────────────────────────────────┐
│  glim-runner:r0.22-upstream  (原始 GLIM r0.22, 备份用)      │
└─────────────────────────────────────────────────────────────┘
                              │ FROM
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  glim-runner:r0.22                                          │
│  = 原始 + libglim_dense_dump.so                              │
│  Dockerfile: codeup/glim_dense_dump/Dockerfile              │
└─────────────────────────────────────────────────────────────┘
                              │ FROM
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  glim-dense-cloud:0.2                                       │
│  = r0.22 + libpcl-dev + build_dense_cloud (C++)             │
│  Dockerfile: desktop-app/operators/glim-dense-cloud/Dockerfile │
└─────────────────────────────────────────────────────────────┘
                              │ FROM
                              ▼
┌─────────────────────────────────────────────────────────────┐
│  panorama-colorize:0.1                                      │
│  = dense-cloud:0.2 + libopencv-dev + panorama_colorize (C++)│
│  Dockerfile: desktop-app/operators/panorama-colorize/Dockerfile │
└─────────────────────────────────────────────────────────────┘
```

---

## 一次性备份原镜像（第一次构建前必做）

```bash
# r0.22 是从阿里云 registry 拉的原始镜像. 备份它, 后续 Dockerfile 从 upstream 衍生.
docker tag \
  crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-runner:r0.22 \
  crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-runner:r0.22-upstream
```

---

## 构建顺序

### 1. glim-runner:r0.22 (plugin 版)

```bash
cd /home/yuancaimaiyi/qiudao_0512/sync/codeup/glim_dense_dump
./build_image.sh
# 产物 crpi-.../glim-runner:r0.22  (= upstream + plugin + ENTRYPOINT 复位)
```

**产物包含**：
- 全套 GLIM r0.22 (base)
- `/opt/glim_offline/lib/libglim_dense_dump.so` (plugin)
- ENTRYPOINT 复位为 `[]`（给 hera-desktop runner 用 `sh -c <cmd>` 启动）

**plugin 行为**：
```
LD_PRELOAD=/opt/glim_offline/lib/libglim_dense_dump.so  启用
GLIM_DUMP_RAW_FRAMES_DIR=<dir>                           指定 dump 目录
→ GLIM 每 scan 落盘 raw points + per-point time + intensity + imu_rate 轨迹
→ 一次性落 t_imu_lidar.txt 到同目录
```

---

### 2. glim-dense-cloud:0.2 (几何稠密版)

```bash
cd /home/yuancaimaiyi/xreal_metaloc/desktop-app/operators/glim-dense-cloud
./build.sh
# 产物 crpi-.../glim-dense-cloud:0.2
```

**产物包含**：
- 继承 glim-runner:r0.22
- `apt libpcl-dev build-essential cmake`
- `/opt/scripts/build_dense_cloud` (C++ 二进制)

**build_dense_cloud 流程**：
```
for each raw_frames/<stamp>.bin:
    读 raw 点 + imu_rate
    GLIM CloudDeskewing::deskew (IMU-rate)
    T_wl = SLERP(traj_lidar, base_stamp)
    push PCL<XYZI> world 系
PCL VoxelGrid 下采
PCL StatisticalOutlierRemoval 去飞点 (MeanK=30, StddevMul=1.5)
PCL NormalEstimationOMP (K=30)
→ PLY (XYZINormal) with normals
```

---

### 3. panorama-colorize:0.1 (RGB 全景上色)

```bash
cd /home/yuancaimaiyi/xreal_metaloc/desktop-app/operators/panorama-colorize
./build.sh
# 产物 crpi-.../panorama-colorize:0.1
```

**产物包含**：
- 继承 glim-dense-cloud:0.2 (传递 glim-runner 和 PCL)
- `apt libopencv-dev`
- `/opt/scripts/panorama_colorize` (C++ 二进制)
- `/opt/scripts/colorize_entry.sh` (自动找 sibling pano/map)

**panorama_colorize 流程 (v10 终版)**：
```
Round 1 几何聚合:
    GLIM CloudDeskewing + T_wl → world
    PCL VoxelGrid (1cm)
Round 2 上色:
    每 ds 点 bin 到 5cm 立方格
    bin centroid → kNN K=12 panos → view-angle score (W_DIST=2, W_LAT=1) rerank
    按 pano 分组批处理 (每 pano 一次 imread)
    Mid360 本体 cone (20°) 精准 mask (从 T_lidar_camera 平移反算方向)
    + lat_cutoff (10°) 过滤 Insta 极处拼接接缝
    equirect project + bilinear sample
→ PLY (XYZRGB)
```

---

## 一键重编（三张图依次重建）

```bash
set -e
cd /home/yuancaimaiyi/qiudao_0512/sync/codeup/glim_dense_dump && ./build_image.sh
cd /home/yuancaimaiyi/xreal_metaloc/desktop-app/operators/glim-dense-cloud && ./build.sh
cd /home/yuancaimaiyi/xreal_metaloc/desktop-app/operators/panorama-colorize && ./build.sh
```

---

## 验证

```bash
# plugin 字符串在 r0.22 里
docker run --rm --entrypoint bash \
  crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-runner:r0.22 \
  -c 'strings /opt/glim_offline/lib/libglim_dense_dump.so | grep raw+time+intensity'

# dense_cloud 二进制在 glim-dense-cloud 里
docker run --rm --entrypoint bash \
  crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-dense-cloud:0.2 \
  -c 'ldd /opt/scripts/build_dense_cloud | grep -E "glim|pcl" | head -5'

# colorize 二进制在 panorama-colorize 里
docker run --rm --entrypoint bash \
  crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/panorama-colorize:0.1 \
  -c 'ldd /opt/scripts/panorama_colorize | grep -E "glim|opencv" | head -5'
```

---

## 关键陷阱

### A. 不要用 `docker commit` 替代 Dockerfile

之前我们曾用 `docker commit` 把 plugin 塞进 r0.22：
```bash
# ❌ 这个做法会把 --entrypoint 固化到 r0.22
docker run --name foo --entrypoint bash <image> -c "cp ..."
docker commit foo <image>:r0.22
# 结果 ENTRYPOINT=["bash"] 固化, 下游 sh -c <cmd> 变成 "sh: cannot execute binary file"
```

**正解**：用 Dockerfile FROM + 显式 `ENTRYPOINT []`。

### B. r0.22-upstream 必须备份

Dockerfile 里 `FROM ...:r0.22-upstream`。首次构建前**必须** `docker tag r0.22 r0.22-upstream`，否则：
- 没有 r0.22-upstream → 自己 FROM 自己 → build 失败
- 或者 r0.22 已被改坏 → upstream 也坏 → 无法恢复

### C. PCL header 名字

PCL 1.12 (Ubuntu 22.04) 的 Normal Estimation OMP header 是 `pcl/features/normal_3d_omp.h`，不是 `normal_estimation_omp.h`（有的教程写错）。

---

## 相关文档

- `codeup/helmet/PANORAMA_COLORIZE.md` — 用户视角端到端操作手册
- `xreal_metaloc/desktop-app/BUILD.md` — hera-desktop 本身的编译/打包
