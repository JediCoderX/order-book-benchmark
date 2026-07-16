cmake_minimum_required(VERSION 3.16)
project(order_book_benchmark CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(orderbook_core
  src/feed_parser.cpp
  src/order_book.cpp
)
target_include_directories(orderbook_core PUBLIC include)
target_compile_options(orderbook_core PUBLIC
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
  $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

add_executable(orderbook_replay src/main.cpp)
target_link_libraries(orderbook_replay PRIVATE orderbook_core)

add_executable(orderbook_tests
  tests/test_main.cpp
  tests/test_feed_parser.cpp
  tests/test_order_book.cpp
)
target_include_directories(orderbook_tests PRIVATE tests)
target_link_libraries(orderbook_tests PRIVATE orderbook_core)

enable_testing()
add_test(NAME orderbook_tests COMMAND orderbook_tests)