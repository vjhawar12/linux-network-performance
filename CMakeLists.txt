# 1. Specify the minimum required CMake version
cmake_minimum_required(VERSION 3.23)

# 2. Define the project name and language
project(MyProject LANGUAGES CXX)

# 3. Set the C++ standard)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 4. Define the executable target and its source files
add_executable(
    linux_network_performance
    main.cpp
    02_af_packet/app.cpp
    02_af_packet/helpers.cpp
    02_af_packet/packet_generator.cpp
    02_af_packet/packet_receiver.cpp
)

# 5. Attach header directories directly to the target
target_include_directories(linux_network_performance PRIVATE include)
