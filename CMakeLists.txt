cmake_minimum_required(VERSION 3.10)
project(PuzzleGame)

# Set CMake policies to suppress warnings
if(POLICY CMP0074)
    cmake_policy(SET CMP0074 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置SFML路径（如果未设置，尝试默认路径）
if(NOT DEFINED SFML_ROOT)
    set(SFML_ROOT "C:/SFML-2_5_1" CACHE PATH "SFML安装路径")
endif()

# 如果SFML_ROOT设置了，也设置SFML_DIR
if(SFML_ROOT)
    set(SFML_DIR "${SFML_ROOT}/lib/cmake/SFML" CACHE PATH "SFML CMake配置路径")
endif()

# 查找SFML
find_package(SFML 2.5 COMPONENTS system window graphics REQUIRED)

# 如果找不到，输出错误信息
if(NOT SFML_FOUND)
    message(FATAL_ERROR 
        "找不到SFML库！\n"
        "请设置SFML_ROOT变量指向SFML安装目录，例如：\n"
        "  cmake .. -DSFML_ROOT=C:/SFML-2_5_1\n"
        "或修改CMakeLists.txt中的默认路径。"
    )
endif()

# 添加可执行文件
# Windows: 如果存在字体资源文件，添加到可执行文件
if(WIN32 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/font_resource.rc" AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/arial.ttf")
    add_executable(puzzle_game_gui puzzle_game_gui.cpp font_resource.rc)
    # 设置资源文件属性
    set_source_files_properties(font_resource.rc PROPERTIES LANGUAGE RC)
else()
    add_executable(puzzle_game_gui puzzle_game_gui.cpp)
endif()

# 链接SFML库
# SFML 2.5.1使用小写目标名称：sfml-system, sfml-window, sfml-graphics
# 新版本可能使用命名空间：SFML::System, SFML::Window, SFML::Graphics
if(TARGET SFML::System AND TARGET SFML::Window AND TARGET SFML::Graphics)
    # 使用命名空间目标（SFML 2.6+）
    target_link_libraries(puzzle_game_gui 
        SFML::System 
        SFML::Window 
        SFML::Graphics
    )
elseif(TARGET sfml-system AND TARGET sfml-window AND TARGET sfml-graphics)
    # 使用小写目标（SFML 2.5.1）
    target_link_libraries(puzzle_game_gui 
        sfml-graphics
    )
    # sfml-graphics会自动链接sfml-window和sfml-system
else()
    # 传统链接方式（兼容旧版本SFML）
    target_link_libraries(puzzle_game_gui 
        ${SFML_LIBRARIES}
    )
    target_include_directories(puzzle_game_gui PRIVATE ${SFML_INCLUDE_DIR})
    message(WARNING "使用传统方式链接SFML，建议使用支持现代CMake目标的SFML版本")
endif()

# Windows特定设置
if(WIN32)
    # 设置源文件和执行字符集为UTF-8（支持中文字符串）
    if(MSVC)
        target_compile_options(puzzle_game_gui PRIVATE /utf-8)
    endif()
    # 注意：使用main()作为入口点，所以不设置WIN32_EXECUTABLE
    # 如果需要无控制台窗口的GUI程序，可以设置WIN32_EXECUTABLE TRUE
    # 但需要将main()改为WinMain()或使用#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
endif()

