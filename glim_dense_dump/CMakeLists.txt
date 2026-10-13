cmake_minimum_required(VERSION 3.5.2)
project(glim_dense_dump)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

find_package(glim REQUIRED)
find_package(gtsam_points REQUIRED)
find_package(spdlog REQUIRED)

add_library(glim_dense_dump SHARED
  src/glim_dense_dump.cpp
)
target_link_libraries(glim_dense_dump
  glim::glim
  gtsam_points::gtsam_points
  spdlog::spdlog
)
