# glim-dense

**GLIM 稠密点云输出 · plugin + 下游工具链文档**

本仓库是 Vistara 头盔稠密激光重建 + 全景上色工程的**上游起点**：提供一个 GLIM extension plugin，让 GLIM 在正常跑的同时**把每个 scan 的原始点云（含 per-point 时间、强度、本帧 IMU-rate 轨迹）落盘**。下游由 hera-desktop 内两个 operator 消费这些 raw dump 生成稠密几何 PLY 和 RGB 稠密点云。

---

## 本仓库职责

```
┌──────────────────────────────────────────────────────────────┐
│  GLIM r0.22 (阿里云原镜像)                                    │
│   + libglim_dense_dump.so (本仓库 plugin)   ← **本仓库核心**  │
│                                                               │
│  运行时: LD_PRELOAD plugin, GLIM 每 scan 触发 callback         │
│   dump <stamp>.bin (raw xyz + rel_t + intensity + imu_rate)   │
│   dump t_imu_lidar.txt                                        │
└──────────────────────────────────────────────────────────────┘
                              │ raw_frames/  +  traj_lidar.txt
                              ▼ (hera-desktop 消费)
┌──────────────────────────────────────────────────────────────┐
│  glim-dense-cloud operator (C++, PCL + GLIM CloudDeskewing)   │
│    → 几何稠密 PLY (XYZINormal, 带法线)                        │
│  panorama-colorize operator (C++, 上面 + OpenCV)              │
│    → RGB 稠密 PLY (XYZRGB)                                    │
│                                                               │
│  下游 operator 源码在 xreal_metaloc/desktop-app/operators/    │
│  (不在本仓库，但 Dockerfile 与说明见 DOCKER_IMAGES.md)         │
└──────────────────────────────────────────────────────────────┘
```

**本仓库交付的东西**：
- `glim_dense_dump/` — plugin 源码 + CMakeLists.txt + Dockerfile + build_image.sh
- `DOCKER_IMAGES.md` — 三张镜像（glim-runner / glim-dense-cloud / panorama-colorize）的构建链路和一键重编命令
- `PANORAMA_COLORIZE.md` — 用户视角的端到端操作手册

**本仓库不提供**：
- GLIM 本身的源码（见 koide3/glim 官方）
- hera-desktop 和下游 operator 的实现（见 `xreal_metaloc/desktop-app/`）

---

## Plugin 行为

- 不改 GLIM 源码，走 `SubMappingCallbacks::on_insert_frame` 扩展点
- `LD_PRELOAD` + 静态构造函数自动注册 callback
- 环境变量 `GLIM_DUMP_RAW_FRAMES_DIR=<dir>` 启用；没设就是 noop

每 scan 产出一个 `.bin`（binary、little-endian）：
```
Header (24 B):
  float64 base_stamp
  uint32  n_points
  uint32  flags             # bit 0 = has_intensity, bit 1 = has_imu_rate
  uint32  n_imu_samples
  uint32  reserved
Point records (n_points × 16 B 或 20 B):
  float32 x, y, z
  float32 rel_time          (相对 base_stamp 秒)
  float32 intensity         (若 flags&1)
IMU-rate 轨迹 (n_imu × 36 B):
  float64 t                 (host 绝对秒)
  float32 tx, ty, tz        (T_world_imu)
  float32 qx, qy, qz, qw
```

一次性落 `t_imu_lidar.txt`（dump dir 下）：
```
# T_imu_lidar  (p_imu = T * p_lidar)
# tx ty tz qx qy qz qw
<frame->T_lidar_imu.inverse() 的 TUM 形>
```

---

## 快速使用

### 构建 plugin + 镜像

```bash
cd glim_dense_dump
./build_image.sh
# 产物 crpi-.../glim-runner:r0.22 (= upstream + plugin + ENTRYPOINT 复位)
```

**前提**：首次用必须先 `docker tag r0.22 r0.22-upstream` 备份一次原始镜像。详见 `DOCKER_IMAGES.md`。

### 独立命令行验证 plugin

```bash
mkdir -p /tmp/out/map
docker run --rm \
  -v /path/to/X.hera:/data/input.hera:ro \
  -v /tmp/out:/output:rw \
  -e GLIM_DUMP_RAW_FRAMES_DIR=/output/map/raw_frames \
  --entrypoint bash \
  crpi-wzvoh0tsm7bwb22w.cn-shanghai.personal.cr.aliyuncs.com/glim/glim-runner:r0.22 \
  -c '. /opt/ros/humble/setup.sh && \
      LD_PRELOAD=/opt/glim_offline/lib/libglim_dense_dump.so \
      glim_offline /data/input.hera -c /opt/glim_offline/share/glim/config -o /output/map --window 0.1'

ls /tmp/out/map/raw_frames | wc -l     # 期望几百个 .bin + 1 个 t_imu_lidar.txt
```

---

## 下游怎么用（简要）

**hera-desktop 装了后**，UI 两张卡片直接消费 plugin 的产物：

1. **「激光重建出点云 (稠密)」** → glim-recon-dense + glim-dense-cloud → 几何稠密 `dense_cloud.ply`
2. **「激光重建点云 RGB 着色」** → panorama-colorize → `colored_cloud.ply`

详细操作见 `PANORAMA_COLORIZE.md`。

---

## 相关仓库

- **GLIM 官方**：`koide3/glim` — 本仓库 plugin 需要它的 header + libglim.so（通过镜像间接依赖）
- **本地 clone**：`/home/yuancaimaiyi/qiudao_0512/sync/codeup/glim/`（checkout v1.2.1 对齐镜像内 commit，仅供 diff / 读源码，不参与 build）
- **hera-desktop**：`/home/yuancaimaiyi/xreal_metaloc/desktop-app/` — 消费 plugin 产物的 UI + operator 平台

---

## 文档索引

| 文件 | 作用 |
|---|---|
| `README.md` | 本文件 — 仓库总览 |
| `DOCKER_IMAGES.md` | 三张镜像的构建链路、依赖图、一键重编命令、关键陷阱 |
| `PANORAMA_COLORIZE.md` | 用户端到端操作手册（gyro bridge → GLIM 稠密 → 全景抽帧 → RGB 上色）|
| `glim_dense_dump/Dockerfile` | plugin 构建 Dockerfile |
| `glim_dense_dump/build_image.sh` | 一键构建 + 备份校验 wrapper |
| `glim_dense_dump/src/glim_dense_dump.cpp` | plugin 源码（~150 行 C++）|
| `glim_dense_dump/CMakeLists.txt` | 本地编译 CMake（供开发时 `cmake-build`）|

---

## License

MIT (见 `LICENSE`)
