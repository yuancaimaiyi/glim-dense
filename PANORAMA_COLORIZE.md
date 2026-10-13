# 全景 RGB 着色稠密点云 · 使用手册

## 目标

从 Vistara 头盔（Insta360 X4 + Livox Mid360）采集的 `.hera + .insv + session.json` 出发，端到端生成**带 RGB 的稠密 PLY 点云**：

```
.hera / .insv  ──┬──→ GLIM 稠密激光重建   ──→  traj_lidar + raw_frames (世界系)
                 │
                 └──→ 全景抽帧 + 位姿赋值 ──→  frames/<host_ns>.jpg + prior_poses.txt
                                               │
     ────────────────────────────────────────→ RGB 着色          ──→  colored_cloud.ply
```

本文档描述 **第 3 步及之后** 的后续操作：你已经采集到 `.hera + .insv`，并且外参 `T_lidar_camera.txt` 已经出厂标定好。

---

## 前置条件

- hera-desktop 已安装（见 `/home/yuancaimaiyi/xreal_metaloc/desktop-app/README.md`）
- `.hera` 会话旁有：`.insv`、`.session.json`、`<stem>_sync_offset.txt`、`T_lidar_camera.txt`
- 推荐布置：所有文件都放在同一目录（`$SESSION`）

示例目录（以 0928-FULL 数据集为例）：

```
/home/yuancaimaiyi/feima_data/0928-FULL/
├── 20260928150504_Lioos_office.hera
├── 20260928150504_Lioos_office.insv
├── 20260928150504_Lioos_office.session.json
├── 20260928150504_Lioos_office_sync_offset.txt
├── panorama.mp4                                  # Insta SDK / panorama_stitch_gpu 拼出
└── T_lidar_camera.txt                            # 出厂标定
```

---

## 一次性准备 (gyro bridge)

**只在每次新采集后跑一次**，把全景视频第 0 帧的 host 时间锚点写进 `session.json`：

```bash
python3 /home/yuancaimaiyi/qiudao_0512/sync/codeup/helmet/patch_session_with_gyro_bridge.py \
  --hera    "$SESSION/20260928150504_Lioos_office.hera" \
  --insv    "$SESSION/20260928150504_Lioos_office.insv" \
  --session "$SESSION/20260928150504_Lioos_office.session.json"
```

执行完 `session.json` 多一个 `frame_0_host_ns` 字段。校验：

- `offset std < 10 ms` ✓
- `frame_0 - record_start = +200 ~ +1500 ms` ✓
- `[out] patched → ...` ✓

---

## 步骤 1 · 跑 GLIM 稠密激光重建 (hera-desktop)

**UI 卡片**：`激光重建出点云 (稠密)`

**输入**：选 `.hera` 文件
**输出**：`$SESSION/<timestamp>-reconstruct_pointcloud_dense/step_recon/map/`

关键参数（UI 面板里确认或改）：

| 参数 | 推荐值 | 为何 |
|---|---|---|
| `T_lidar_imu` 平移 | `[0, 0, 0]` | Vistara 实测用 identity 比出厂 nominal 更稳 |
| `imu_acc_noise` | `0.02` | IMU 预测比纯 LiDAR 配准更准，收紧权重 |
| `keyframe_strategy` | `DISPLACEMENT` | 走路场景按位移触发，稠密覆盖好 |
| `keyframe_interval_trans` | `0.5` | 每 50 cm 加一帧，相对精度高 |
| `preprocess_downsample_target` | `0` | 关掉下采，保留全部点 |
| `preprocess_downsample_rate` | `1.0` | 同上 |

**产物**：

```
step_recon/map/
├── traj_lidar.txt          # 闭环后位姿，第一列是 host 绝对秒
├── raw_frames/
│   ├── <stamp>.bin         # 每 scan 一个（原始点 + per-point time + intensity + imu_rate 轨迹）
│   └── t_imu_lidar.txt     # 由 glim_dense_dump plugin 落盘，供下游 deskew
├── 000000/                 # GLIM submap 数据（keyframe 融合用）
└── graph.bin               # GTSAM 因子图
```

**step_dense/dense_cloud.ply**：几何稠密灰度云（intensity 显色，**带法线 normals**），用来**先验证几何对齐**（CloudCompare 开着看一眼墙、画框、CCTag 是否方正）。

**几何版 pipeline 内置两步后处理**（2026-10-13 加）：
- Statistical Outlier Removal (K=30, StddevMul=1.5)：去除飞点，墙面变整洁
- Normal Estimation OMP (K=30)：PLY 带法线字段，供下游 vismap / 可视化着色用

**判据**：GLIM 日志里 `IMU prediction is not good` 这类 warning **应该是 0 次**。若多次出现，意味着 `T_lidar_imu` 或 `imu_acc_noise` 要调。

---

## 步骤 2 · 全景抽帧 + 位姿赋值 (命令行)

拿到 traj_lidar.txt 后，命令行跑 `panorama_to_prior_poses.py`：

```bash
STEM=20260928150504_Lioos_office
MAP=$SESSION/<timestamp>-reconstruct_pointcloud_dense/step_recon/map

python3 /home/yuancaimaiyi/qiudao_0512/sync/codeup/helmet/panorama_to_prior_poses.py \
  --panorama       "$SESSION/panorama.mp4" \
  --session        "$SESSION/${STEM}.session.json" \
  --traj           "$MAP/traj_lidar.txt" \
  --offset         "$SESSION/${STEM}_sync_offset.txt" \
  --extrinsic_file "$SESSION/T_lidar_camera.txt" \
  --target_fps     30 \
  --out_dir        "$SESSION/${STEM}_pano"
```

**产物（必须在 `<stem>_pano/` 命名下，着色卡片按约定找）**：

```
$SESSION/20260928150504_Lioos_office_pano/
├── frames/
│   ├── 1790579108841495040.jpg  # 文件名 = host_ns
│   └── ...
├── prior_poses.txt              # TUM: image_name tx ty tz qx qy qz qw
└── frame_timestamps.csv
```

**target_fps 建议值**：

| fps | 用途 | scan↔pano 时间差 |
|---|---|---|
| 3  | 测试调试 | ±167 ms (走路 17cm 偏差) |
| 15 | 中等密度 | ±33 ms (3 cm 偏差) |
| **30** | **稠密上色推荐** | ±17 ms (1.7 cm 偏差) |
| 60 | 非常稠密 | ±8 ms | （Insta 原生 29.97fps，> 30 无额外收益）

---

## 步骤 3 · RGB 着色 (hera-desktop)

**UI 卡片**：`激光重建点云 RGB 着色`

**输入**：选同一个 `.hera` 文件（不需要再指定其它路径）
**自动行为**：
- 从 `.hera` 父目录找最新的 `*-reconstruct_pointcloud_dense/step_recon/map/`（步骤 1 产物）
- 从 `.hera` 父目录找 `<stem>_pano/`（步骤 2 产物）
- 两者结合后产出 `colored_cloud.ply`

**产物**：`$SESSION/<timestamp>-reconstruct_colored_pointcloud/step_colorize/colored_cloud.ply`

### 可调参数 (UI 面板)

| 参数 | 默认 | 说明 |
|---|---|---|
| `voxel` | `0.01` | 最终 voxel 下采样 (m)。0.01 很稠密 (~600 万 pts, ~90MB)；0.02 中等 (~200 万 pts) |
| `max_depth` | `30.0` | 最大上色距离 (m)。远点全景分辨率不够，skip 掉 |
| `lat_cutoff_deg` | `10.0` | 全景极区过滤角。只过滤最极端的 Insta 拼接接缝 |

### 算法层后处理（代码写死）

1. **Mid360 本体 cone mask**（**20°** 立体锥，根据 `T_lidar_camera` 自动算方向）—— 避免地面被染成 Mid360 球体 + 电缆的颜色（辐射花瓣伪影）。
2. **空间 bin 一致性**（5cm 立方格）—— 每格内所有点共用同一 pano 候选列表，防画框边缘碎裂。
3. **view-angle rerank**（`W_DIST=2, W_LAT=1`, kNN K=12）—— 优先挑能近正视该点的 pano，而非只按距离最近。
4. **Statistical Outlier Removal**（K=30, StddevMul=1.5）—— 去飞点让墙面干净，与几何版一致。多 2-3 分钟运行时间。

### 算法层终点（**不要再加遮挡过滤**）

2026-10-13 验证：v11 "per-pano depth-map 遮挡过滤" 实测无感知收益但多 10 分钟。此后彩色版**算法已到物理精度上限**（见下）。

### 剩余错位的物理根因（算法改不动）

| 现象 | 根因 | 工程层 |
|---|---|---|
| 墙面不同 scan 的 RGB 对不齐 | T_lidar_camera 外参残差 (dvlc 典型 1-3 cm) | 重标定 |
| AprilTag 板子色块偏/模糊 | 外参 + 走路时相机边缘畸变 | 重标定 |
| 画框彩色扭曲（几何方正） | 不同 pano pixel 偏移累积 | — |
| 远处纹理糊 | 全景分辨率 + Insta rolling shutter 15-30 ms | 硬件 |

**要再上一档**只有：重标 `T_lidar_camera` / 换 global shutter 相机 / 做 rolling shutter 建模（工程大）。

---

## 验证产物

CloudCompare 打开 `colored_cloud.ply`：

| 检查项 | 期望 | 若不对则 |
|---|---|---|
| **地面** | 没有辐射状黑/黄花瓣 | lat_cutoff 调大到 20 或 Mid360 cone 调大到 45 |
| **墙面纹理** | 清晰可辨 | 几何重影问题 → 回步骤 1 调 GLIM；/ 或 target_fps 调大 |
| **AprilTag/CCTag** | 格子可见 | 外参 `T_lidar_camera` 精度不够 → 重标 |
| **画框边缘** | 保持矩形 | bin 边界硬切换 → voxel 调更小；或接受当前精度 |
| **颜色接缝** | 相邻 pano 过渡区的颜色跳变不严重 | 可接受；要彻底消需加 feather blending |

---

## 常见问题

### Q: 卡片 "激光重建点云 RGB 着色" 报错 "no reconstruct_pointcloud_dense output found"

说明步骤 1 没跑过。先在 hera-desktop 跑 "激光重建出点云 (稠密)" 卡片。

### Q: 卡片报错 "no sibling panorama directory found"

说明步骤 2 没跑或命名不对。确认 `$SESSION/<stem>_pano/` 存在且里面有 `frames/` 和 `prior_poses.txt`。

### Q: 跑完了但 PLY 全是黑/花瓣严重

- 看 `colorize_entry.sh` 的 log，确认用的是正确的 pano 目录
- 看 `prior_poses.txt` 第一行注释，确认 `T_world_camera` 公式正确
- 看 `T_lidar_camera.txt` 是否是正确的那个 (`extrinsic_sim.txt` 是仿真占位，别用)

### Q: 地面稀疏/有空洞

说明 `lat_cutoff_deg` 或 Mid360 cone 过严。调小 lat_cutoff（如 5）、把 Mid360 cone 从默认 35° 调小到 25°。注意同时花瓣可能回来。

### Q: 重影严重（墙两次观测对不上）

**不是着色问题**，是 GLIM SLAM 精度上限。回步骤 1：
- `T_lidar_imu = [0, 0, 0]` （确认已归零）
- `imu_acc_noise = 0.02`
- `keyframe_strategy = DISPLACEMENT, interval = 0.5`
- 看 log `IMU prediction is not good` 为 0

### Q: AprilTag 色块偏/画框扭曲

**不是着色算法问题**，是**外参 T_lidar_camera** 精度或 rolling shutter 残差。目前算法层无法改善。

---

## 整个流程命令汇总 (一次新采集)

```bash
SESSION=/home/yuancaimaiyi/feima_data/0928-FULL
STEM=20260928150504_Lioos_office

# 1. gyro bridge (一次性，每次新采集)
python3 /home/yuancaimaiyi/qiudao_0512/sync/codeup/helmet/patch_session_with_gyro_bridge.py \
  --hera "$SESSION/$STEM.hera" --insv "$SESSION/$STEM.insv" --session "$SESSION/$STEM.session.json"

# 2. GLIM 稠密重建 (走 hera-desktop UI 卡片 "激光重建出点云 (稠密)")
#    → 产物在 $SESSION/<timestamp>-reconstruct_pointcloud_dense/step_recon/map/

# 3. 全景抽帧 + 位姿赋值
MAP=$(ls -1dt "$SESSION"/*-reconstruct_pointcloud_dense/step_recon/map | head -1)
python3 /home/yuancaimaiyi/qiudao_0512/sync/codeup/helmet/panorama_to_prior_poses.py \
  --panorama       "$SESSION/panorama.mp4" \
  --session        "$SESSION/$STEM.session.json" \
  --traj           "$MAP/traj_lidar.txt" \
  --offset         "$SESSION/${STEM}_sync_offset.txt" \
  --extrinsic_file "$SESSION/T_lidar_camera.txt" \
  --target_fps     30 \
  --out_dir        "$SESSION/${STEM}_pano"

# 4. RGB 着色 (走 hera-desktop UI 卡片 "激光重建点云 RGB 着色")
#    → 产物在 $SESSION/<timestamp>-reconstruct_colored_pointcloud/step_colorize/colored_cloud.ply
```

---

## 相关代码位置

| 组件 | 路径 |
|---|---|
| `patch_session_with_gyro_bridge.py` | `/home/yuancaimaiyi/qiudao_0512/sync/codeup/helmet/` |
| `panorama_to_prior_poses.py`        | 同上 |
| GLIM plugin `libglim_dense_dump.so` | `/home/yuancaimaiyi/qiudao_0512/sync/codeup/glim_dense_dump/` (源码) → 镜像 `glim-runner:r0.22` 内 |
| `build_dense_cloud` (稠密几何版)     | `desktop-app/operators/glim-dense-cloud/src/build_dense_cloud.cpp` |
| `panorama_colorize` (着色版)         | `desktop-app/operators/panorama-colorize/src/panorama_colorize.cpp` |
| `colorize_entry.sh` (自动找 pano/map) | `desktop-app/operators/panorama-colorize/` |
| hera-desktop 稠密 workflow          | `desktop-app/workflows/reconstruct_pointcloud_dense.json` |
| hera-desktop 着色 workflow          | `desktop-app/workflows/reconstruct_colored_pointcloud.json` |
| **三张镜像构建流程**                 | `codeup/DOCKER_IMAGES.md` (含依赖图 + 一键重编 + 关键陷阱) |
| hera-desktop 编译/打包 .deb         | `xreal_metaloc/desktop-app/BUILD.md` |
