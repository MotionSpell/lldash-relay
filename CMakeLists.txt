cmake_minimum_required(VERSION 3.15)

project(sub VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Determine the host system and set the appropriate source file
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(HOST "gnu")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Windows")
    set(HOST "mingw")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(HOST "darwin")
else()
    message(FATAL_ERROR "Unsupported host system: ${CMAKE_SYSTEM_NAME}")
endif()


# Change some compiler options that are dependent on the target platform.
# xxxjack some are actually dependent on the toolchain...
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    add_compile_options(-Wall -Wextra -Werror -fvisibility=default -fvisibility-inlines-hidden -Wno-deprecated-declarations)
    set(TCP_SERVER_SRC src/tcp_server_windows.cpp)
    # Debug-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g3)
    endif()

    # Release-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-w -DNDEBUG)
    endif()
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Darwin")
    add_compile_options(-Wall -Wextra -Werror -fvisibility=default -fvisibility-inlines-hidden -Wno-deprecated-declarations)
    set(TCP_SERVER_SRC src/tcp_server_gnu.cpp)
    # Debug-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g3)
    endif()

    # Release-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-w -DNDEBUG)
    endif()
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
    add_compile_options(-Wall -Wextra -Werror -fvisibility=default -fvisibility-inlines-hidden -Wno-deprecated-declarations)
    set(TCP_SERVER_SRC src/tcp_server_gnu.cpp)
    # Debug-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-g3)
    endif()

    # Release-specific options
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-w -DNDEBUG)
    endif()
else()
    message(FATAL_ERROR " Unknown CMAKE_SYSTEM_NAME ${CMAKE_SYSTEM_NAME}")
endif()

find_package(OpenSSL REQUIRED)


add_executable(evanescent
    src/main.cpp
    src/tls.cpp
    ${TCP_SERVER_SRC}
)
target_link_libraries(evanescent PRIVATE OpenSSL::SSL)
# target_link_libraries(evanescent PRIVATE OpenSSL::Crypto)
if (${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    target_link_libraries(evanescent PRIVATE ws2_32)
endif()

signals_install_app(evanescent)
