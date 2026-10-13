// glim_dense_dump: GLIM extension plugin — per-scan raw dump including
// per-point time, intensity, and the IMU-rate trajectory covering the scan.
// Downstream can call GLIM's own CloudDeskewing to deskew at IMU-rate fidelity.
//
// Loaded via:  LD_PRELOAD=/opt/glim_offline/lib/libglim_dense_dump.so
// Enabled by:  GLIM_DUMP_RAW_FRAMES_DIR=/path/to/dir
//
// Per-scan file  <base_stamp>.bin  (little-endian):
//   Header (32 bytes):
//     float64 base_stamp              // scan reference time (abs)
//     uint32  n_points
//     uint32  flags                   // bit 0 = has_intensity, bit 1 = has_imu_rate
//     uint32  n_imu_samples           // IMU trajectory sample count (0 if none)
//     uint32  _reserved
//   Point records (n_points):
//     float32 x,y,z  rel_t  [intensity]      (16 or 20 bytes)
//   IMU-rate samples (n_imu_samples), T_world_imu per sample:
//     float64 t  float32 tx,ty,tz,qx,qy,qz,qw     (36 bytes)
//
// Once-per-run file  t_imu_lidar.txt  in the dump dir:
//   tx ty tz qx qy qz qw           (TUM, T_imu_lidar — camera-to-lidar style)

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <sys/stat.h>

#include <Eigen/Geometry>
#include <spdlog/spdlog.h>

#include <glim/util/extension_module.hpp>
#include <glim/mapping/callbacks.hpp>
#include <glim/odometry/estimation_frame.hpp>
#include <glim/preprocess/preprocessed_frame.hpp>

namespace glim {

class DenseDumpModule : public ExtensionModule {
public:
  DenseDumpModule() {
    const char* env = std::getenv("GLIM_DUMP_RAW_FRAMES_DIR");
    if (!env || !*env) {
      spdlog::info("[dense_dump] GLIM_DUMP_RAW_FRAMES_DIR not set, module inactive");
      return;
    }
    dump_dir_ = env;
    mkdir(dump_dir_.c_str(), 0755);
    spdlog::info("[dense_dump] active (raw+time+intensity+imu_rate) → {}", dump_dir_);

    using std::placeholders::_1;
    SubMappingCallbacks::on_insert_frame.add(std::bind(&DenseDumpModule::on_insert_frame, this, _1));
  }

  ~DenseDumpModule() override = default;

private:
  void on_insert_frame(const EstimationFrame::ConstPtr& frame) {
    if (dump_dir_.empty() || !frame || !frame->raw_frame) {
      return;
    }
    const auto& raw = *frame->raw_frame;
    const int n = static_cast<int>(raw.points.size());
    if (n == 0) {
      return;
    }
    const bool have_time = static_cast<int>(raw.times.size()) == n;
    const bool have_int  = static_cast<int>(raw.intensities.size()) == n;
    const int  n_imu     = static_cast<int>(frame->imu_rate_trajectory.cols());

    // One-time: write T_imu_lidar (constant across frames) to a shared file.
    bool expected = false;
    if (t_imu_lidar_written_.compare_exchange_strong(expected, true)) {
      const Eigen::Isometry3d T_il = frame->T_lidar_imu.inverse();
      const Eigen::Vector3d t = T_il.translation();
      const Eigen::Quaterniond q(T_il.linear());
      std::ofstream ofs(dump_dir_ + "/t_imu_lidar.txt");
      ofs << "# T_imu_lidar  (p_imu = T * p_lidar)\n";
      ofs << "# tx ty tz qx qy qz qw\n";
      ofs << t.x() << ' ' << t.y() << ' ' << t.z() << ' '
          << q.x() << ' ' << q.y() << ' ' << q.z() << ' ' << q.w() << '\n';
    }

    char path[1024];
    std::snprintf(path, sizeof(path), "%s/%.9f.bin", dump_dir_.c_str(), raw.stamp);

    std::lock_guard<std::mutex> lk(mu_);
    std::ofstream ofs(path, std::ios::binary);
    if (!ofs) {
      spdlog::warn("[dense_dump] failed to open {}", path);
      return;
    }

    // Header
    const double base_stamp = raw.stamp;
    const uint32_t np = static_cast<uint32_t>(n);
    const uint32_t flags = (have_int ? 1u : 0u) | (n_imu > 0 ? 2u : 0u);
    const uint32_t ni = static_cast<uint32_t>(n_imu);
    const uint32_t reserved = 0;
    ofs.write(reinterpret_cast<const char*>(&base_stamp), 8);
    ofs.write(reinterpret_cast<const char*>(&np),         4);
    ofs.write(reinterpret_cast<const char*>(&flags),      4);
    ofs.write(reinterpret_cast<const char*>(&ni),         4);
    ofs.write(reinterpret_cast<const char*>(&reserved),   4);

    // Points
    for (int i = 0; i < n; i++) {
      const float xyz[3] = {
        static_cast<float>(raw.points[i].x()),
        static_cast<float>(raw.points[i].y()),
        static_cast<float>(raw.points[i].z())
      };
      const float rt = have_time ? static_cast<float>(raw.times[i]) : 0.0f;
      ofs.write(reinterpret_cast<const char*>(xyz), 12);
      ofs.write(reinterpret_cast<const char*>(&rt), 4);
      if (have_int) {
        const float it = static_cast<float>(raw.intensities[i]);
        ofs.write(reinterpret_cast<const char*>(&it), 4);
      }
    }

    // IMU-rate trajectory  (8xN: t, tx, ty, tz, qx, qy, qz, qw)
    for (int i = 0; i < n_imu; i++) {
      const double t = frame->imu_rate_trajectory(0, i);
      const float tx = static_cast<float>(frame->imu_rate_trajectory(1, i));
      const float ty = static_cast<float>(frame->imu_rate_trajectory(2, i));
      const float tz = static_cast<float>(frame->imu_rate_trajectory(3, i));
      const float qx = static_cast<float>(frame->imu_rate_trajectory(4, i));
      const float qy = static_cast<float>(frame->imu_rate_trajectory(5, i));
      const float qz = static_cast<float>(frame->imu_rate_trajectory(6, i));
      const float qw = static_cast<float>(frame->imu_rate_trajectory(7, i));
      ofs.write(reinterpret_cast<const char*>(&t),  8);
      ofs.write(reinterpret_cast<const char*>(&tx), 4);
      ofs.write(reinterpret_cast<const char*>(&ty), 4);
      ofs.write(reinterpret_cast<const char*>(&tz), 4);
      ofs.write(reinterpret_cast<const char*>(&qx), 4);
      ofs.write(reinterpret_cast<const char*>(&qy), 4);
      ofs.write(reinterpret_cast<const char*>(&qz), 4);
      ofs.write(reinterpret_cast<const char*>(&qw), 4);
    }
  }

  std::string dump_dir_;
  std::mutex mu_;
  std::atomic<bool> t_imu_lidar_written_{false};
};

}  // namespace glim

extern "C" glim::ExtensionModule* create_extension_module() {
  return new glim::DenseDumpModule();
}

namespace {
struct AutoLoader {
  AutoLoader() { module.reset(new glim::DenseDumpModule()); }
  std::unique_ptr<glim::DenseDumpModule> module;
};
static AutoLoader _auto_loader;
}
