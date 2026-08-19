cmake_minimum_required(VERSION 3.16)
project(orderbook_env_bindings CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(pybind11 CONFIG REQUIRED)

pybind11_add_module(orderbook_env bindings.cpp ../src/flat_order_book.cpp)
target_include_directories(orderbook_env PRIVATE ../include)
