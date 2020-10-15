cmake_minimum_required(VERSION 2.8.3)
project(img_msgs_sync_repub)

if(NOT CMAKE_BUILD_TYPE)
    set( CMAKE_BUILD_TYPE "Release" )
endif()
message(WARNING "Build type: " ${CMAKE_BUILD_TYPE})

set(CMAKE_CXX_FLAGS "-std=c++11 -fPIC -Wall" )
SET(CMAKE_CXX_FLAGS_DEBUG   "-O0 -g -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(catkin REQUIRED COMPONENTS
    roscpp
    rospy
    std_msgs
    sensor_msgs
    cv_bridge
    image_transport
    rosbag
    tf
)

catkin_package()

include_directories(
    include
    ${catkin_INCLUDE_DIRS}
)

## Declare a C++ executable
add_executable(img_msgs_sync_repub src/img_msgs_sync_repub.cpp)
target_link_libraries(img_msgs_sync_repub ${catkin_LIBRARIES})
