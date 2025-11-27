cmake_minimum_required(VERSION 3.10)

project(HaoBServer VERSION 1.0 LANGUAGES CXX)

# 包含头文件目录
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    # 如果有其他 include 目录，比如 lock/、CGImysql/，也加上
    # ${CMAKE_SOURCE_DIR}/lock
    # ${CMAKE_SOURCE_DIR}/CGImysql
)

add_executable(HaoBServer src/main.cpp src/connection.cpp src/webserver.cpp)