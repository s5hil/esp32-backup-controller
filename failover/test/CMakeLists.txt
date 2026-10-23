cmake_minimum_required(VERSION 3.14)
project(failover_tests CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()
add_executable(leader_fsm_test leader_fsm_test.cpp ../leader_fsm.cpp)
target_include_directories(leader_fsm_test PRIVATE ..)
target_link_libraries(leader_fsm_test GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(leader_fsm_test)