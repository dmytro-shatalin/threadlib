cmake_minimum_required(VERSION 4.2)

project(threadlib VERSION 1.0.0 LANGUAGES CXX)

# Requirements

find_package(Threads REQUIRED)

# Variables

option(THREADLIB_BUILD_EXAMPLE "Whether to compile the example" OFF)
option(THREADLIB_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})

# Target

add_library(threadlib STATIC
        src/threadlib/thread_pool.cpp
)
add_library(threadlib::threadlib ALIAS threadlib)

target_sources(threadlib PUBLIC
        FILE_SET HEADERS
        BASE_DIRS include
        FILES include/threadlib/thread_pool.h include/threadlib/stl_queue.h
)
target_compile_features(threadlib PUBLIC
        cxx_std_23
)
target_link_libraries(threadlib PUBLIC
        Threads::Threads
)

if (THREADLIB_BUILD_EXAMPLE)
    add_executable(threadlib_example example/threadlib_pool_example.cpp)
    target_link_libraries(threadlib_example PRIVATE
            threadlib::threadlib
    )
    set_target_properties(threadlib_example PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin
    )
endif ()

if (THREADLIB_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    set(THREADLIB_CMAKE_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/threadlib")

    install(TARGETS threadlib
            EXPORT threadlibTargets
            FILE_SET HEADERS
    )
    install(EXPORT threadlibTargets
            NAMESPACE threadlib::
            DESTINATION "${THREADLIB_CMAKE_DIR}"
    )

    file(WRITE ${PROJECT_BINARY_DIR}/threadlibConfig.cmake
            "include(\"\${CMAKE_CURRENT_LIST_DIR}/threadlibTargets.cmake\")\n")
    write_basic_package_version_file(${PROJECT_BINARY_DIR}/threadlibConfigVersion.cmake
            COMPATIBILITY SameMajorVersion
            ARCH_INDEPENDENT
    )
    install(FILES
            ${PROJECT_BINARY_DIR}/threadlibConfig.cmake
            ${PROJECT_BINARY_DIR}/threadlibConfigVersion.cmake
            DESTINATION ${THREADLIB_CMAKE_DIR}
    )
endif ()
