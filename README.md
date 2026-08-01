# Game Collection / 游戏合集

**版本：2.0.0**

基于 **C++17 + Qt（Widgets / Svg）** 的桌面小游戏合集。顶栏可切换五种玩法：猜瓶子、棋盘填色、汉诺塔、贪吃蛇、野餐日。窗口按设计分辨率 1400×900 等比缩放，支持调整大小。

## 关键字

`Qt` · `C++17` · `CMake` · `游戏合集` · `猜瓶子` · `颜色匹配` · `Mastermind` · `Nonogram` · `数织` · `汉诺塔` · `贪吃蛇` · `野餐日` · `拼图` · `贴图` · `可缩放 UI` · `Windows`

## 包含游戏

| 菜单 | 玩法概要 |
|------|----------|
| 1、猜瓶子 | 15 关颜色位置匹配；刷子色板与瓶子贴图；尝试历史可分列展示 |
| 2.棋盘填色 | Nonogram / 数织，含编辑与自动求解 |
| 3.汉诺塔 | 可调圆盘数，支持自动求解动画 |
| 4.贪吃蛇 | 经典蛇玩法（可选 Multimedia 音效） |
| 5.野餐日 | 可变棋盘难度、石头、图块贴图、求解记录与穷举 |

### 猜瓶子关卡规则（摘要）

- **1–5 关**：瓶数 2→6，尝试次数 = 瓶数 + 1，色板颜色数 = 瓶数  
- **6–10 关**：瓶数 2→6，尝试次数 = 瓶数，色板颜色数 = 瓶数  
- **11–15 关**：瓶数 2→6，尝试次数 = 瓶数，色板颜色数 = 瓶数 + 1（含干扰色）  
- 通关第 15 关后再继续 → 回到第 1 关  

## 版本说明

| 版本 | 说明 |
|------|------|
| **2.0.0** | 多游戏合集形态；猜瓶子 15 关三段难度；色板两列锁定与减色策略；可缩放窗口；野餐日求解记录优化等 |
| 1.0.0 | 早期单游戏 / 基础骨架（历史） |

版本号与 `CMakeLists.txt` 中 `project(... VERSION ...)` 保持一致。

## 编译要求

- C++17 或更高  
- CMake 3.16 或更高  
- Qt 5.12+（推荐 Qt 6.x；需 **Core / Widgets / Svg**，Multimedia 可选）

### Windows 安装 Qt

1. 下载：https://www.qt.io/download  
2. 安装 MSVC 或 MinGW 套件  
3. 将 Qt `bin` 加入 PATH，或设置 `CMAKE_PREFIX_PATH`

### Linux

```bash
# Ubuntu/Debian
sudo apt-get install qt6-base-dev qt6-base-dev-tools libqt6svg6-dev

# Fedora
sudo dnf install qt6-qtbase-devel qt6-qtsvg-devel
```

### macOS

```bash
brew install qt
```

## 编译步骤

### 使用脚本（推荐，Windows）

```bash
build.bat
```

或：

```powershell
.\build.ps1
```

### 手动 CMake（Visual Studio）

```bash
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build . --config Release
```

### Linux / macOS

```bash
mkdir build && cd build
cmake ..
cmake --build . -j
```

## 运行

```bash
# Windows
.\build\Release\ColorBottleGame.exe

# Linux/macOS
./build/ColorBottleGame
```

动态链接 Qt 时，请将对应 DLL / 插件与可执行文件放在一起，或使用 `windeployqt`（`build.bat` 会尝试部署）。

## 猜瓶子操作

1. 点击右侧 **Color Palette** 刷子选色，颜色填入当前尝试行最左侧未上色瓶  
2. 未匹配瓶全部上色后自动比对  
3. 匹配瓶保留；错误位显示飞溅，点击提示后进入下一轮  
4. 全部匹配后点击进入下一关  

## 技术要点

- Qt Widgets + `QPainter` 自绘界面  
- 资源经 `app_resources.qrc` 嵌入（图标、刷子、瓶子贴图等）  
- 野餐日求解记录可延迟加载，避免启动卡顿  
- 设计分辨率坐标 + 等比缩放，鼠标事件映射到设计坐标系  

## 许可证

本项目仅供学习和娱乐使用。
