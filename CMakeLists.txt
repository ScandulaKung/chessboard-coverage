cmake_minimum_required(VERSION 3.16)
project(ColorBottleGame VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置Qt路径（如果需要）
# set(CMAKE_PREFIX_PATH "C:/Qt/6.10.1/msvc2022_64" CACHE PATH "Qt installation path")

# 查找Qt6（如果找不到则尝试Qt5）
# Multimedia 模块是可选的（用于音效）
find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Widgets Svg)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Widgets Svg)
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS Multimedia)
set(QT_MULTIMEDIA_FOUND ${Qt${QT_VERSION_MAJOR}Multimedia_FOUND})

# 设置自动处理Qt的MOC、UIC、RCC
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# 添加可执行文件
set(APP_SOURCES
    main.cpp
    color_bottle_game.cpp
    nonogram_game.cpp
    hanoi_game.cpp
    snake_game.cpp
    shape.cpp
    picnic_game.cpp
    app_resources.qrc
)
if(WIN32)
    list(APPEND APP_SOURCES app_icon.rc)
endif()

add_executable(${PROJECT_NAME} ${APP_SOURCES})

# 链接Qt库
target_link_libraries(${PROJECT_NAME} 
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Svg
)

# 如果找到 Multimedia 模块，则链接它
if(QT_MULTIMEDIA_FOUND)
    target_link_libraries(${PROJECT_NAME} Qt${QT_VERSION_MAJOR}::Multimedia)
    target_compile_definitions(${PROJECT_NAME} PRIVATE QT_MULTIMEDIA_AVAILABLE)
    message(STATUS "Qt Multimedia module found - sound effects enabled")
else()
    message(STATUS "Qt Multimedia module not found - sound effects disabled")
endif()

# 设置源文件编码为 UTF-8（解决中文注释的编码警告）
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /utf-8)
    message(STATUS "Setting UTF-8 encoding for source files")
endif()

# Windows特定设置
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE TRUE  # 隐藏控制台窗口
    )
endif()

message(STATUS "Qt version: ${Qt${QT_VERSION_MAJOR}_VERSION}")
message(STATUS "Qt libraries: Qt${QT_VERSION_MAJOR}::Core Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Svg")
if(QT_MULTIMEDIA_FOUND)
    message(STATUS "Qt Multimedia: Available")
else()
    message(STATUS "Qt Multimedia: Not available")
endif()


