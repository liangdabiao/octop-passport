cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# 公众发布版本号（开机日志与 OTA 版本显示；发版时同步更新）。
set(PROJECT_VER "1.0.0")

project(FoloToy-AI-Passport)
