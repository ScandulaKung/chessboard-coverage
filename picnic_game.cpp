#include "picnic_game.h"
#include <algorithm>
#include <iostream>
#include <ctime>
#include <climits>
#include <iomanip>
#include <fstream>
#include <sstream>
#include <string>
#include <queue>
#include <numeric>
#include <set>
#include <windows.h>

// 安全字符验证函数
bool isPrintableChar(char c) {
    return (c >= 32 && c <= 126);  // 可打印ASCII字符范围
}

// 安全输出函数
void safeOutputChar(char c) {
    if (!isPrintableChar(c)) {
        std::cout << "?";  // 不可打印字符用问号代替
    } else {
        std::cout << c;
    }
    std::cout.flush();
}

// 初始化静态成员变量
int PicnicGame::globalShapeIdCounter = 0;

void PicnicGame::loadConfigFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cout << "配置文件 " << filename << " 无法打开，使用默认配置" << std::endl;
        return;
    }
    
    std::string line;
    int loadedCount = 0;
    bool hasAngleConfig = false;
    
    while (std::getline(file, line)) {
        // 移除空白字符
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        // 跳过空行和注释行
        if (line.empty() || line[0] == '#' || line[0] == '{' || line[0] == '}') {
            continue;
        }
        
        // 检查是否是JSON格式文件
        if (line.find("\"") != std::string::npos) {
            file.seekg(0); // 重置到文件开头
            loadConfigFromJson(file);
            return;
        }
        
        // 解析简单格式：形状名称_角度 数量 或 形状名称 数量
        std::istringstream iss(line);
        std::string shapeKey;
        int count;
        
        if (iss >> shapeKey >> count) {
            // 检查是否包含角度信息
            size_t underscorePos = shapeKey.find('_');
            if (underscorePos != std::string::npos) {
                // 新格式：形状名称_角度
                std::string shapeName = shapeKey.substr(0, underscorePos);
                std::string angleStr = shapeKey.substr(underscorePos + 1);
                
                // 验证角度是否有效
                if (angleStr == "0" || angleStr == "90" || angleStr == "180" || angleStr == "270") {
                    shapeRotations[shapeKey] = count;
                    std::cout << "从配置文件加载: " << shapeKey << " = " << count << std::endl;
                    hasAngleConfig = true;
                    loadedCount++;
                } else {
                    std::cout << "警告：无效的角度 '" << angleStr << "' 在 '" << shapeKey << "'，已忽略" << std::endl;
                }
            } else {
                // 旧格式：仅形状名称（用于兼容性）
                if (shapeCounts.find(shapeKey) != shapeCounts.end()) {
                    shapeCounts[shapeKey] = count;
                    std::cout << "从配置文件加载（旧格式）: " << shapeKey << " = " << count << std::endl;
                    loadedCount++;
                } else {
                    std::cout << "警告：未知的形状名称 '" << shapeKey << "'，已忽略" << std::endl;
                }
            }
        }
    }
    
    file.close();
    
    if (hasAngleConfig) {
        std::cout << "配置文件加载完成（角度格式），共加载 " << loadedCount << " 个配置项" << std::endl;
    } else {
        std::cout << "配置文件加载完成（旧格式），共加载 " << loadedCount << " 个配置项" << std::endl;
    }
}

void PicnicGame::loadConfigFromJson(std::ifstream& file) {
    std::cout << "检测到JSON格式配置文件，正在解析..." << std::endl;
    
    std::string line;
    bool inTestCaseSection = false;
    bool inShapeRotations = false;
    bool inShapeCounts = false; // 兼容旧格式
    int loadedCount = 0;
    
    while (std::getline(file, line)) {
        // 移除空白字符
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        
        if (line.empty() || line[0] == '{' || line[0] == '}') {
            continue;
        }
        
        if (line.find("\"test_case_config\"") != std::string::npos) {
            inTestCaseSection = true;
            continue;
        }
        
        if (inTestCaseSection) {
            if (line.find("\"shape_rotations\"") != std::string::npos) {
                inShapeRotations = true;
                inShapeCounts = false;
                continue;
            } else if (line.find("\"shape_counts\"") != std::string::npos) {
                inShapeCounts = true;
                inShapeRotations = false;
                continue;
            } else if (line.find("}") != std::string::npos) {
                inShapeRotations = false;
                inShapeCounts = false;
                continue;
            }
        }
        
        if ((inShapeRotations || inShapeCounts) && line.find("\"") != std::string::npos) {
            // 解析JSON格式的形状名称和数量
            size_t nameStart = line.find("\"") + 1;
            size_t nameEnd = line.find("\"", nameStart);
            if (nameEnd != std::string::npos) {
                std::string shapeKey = line.substr(nameStart, nameEnd - nameStart);
                size_t colonPos = line.find(":", nameEnd);
                if (colonPos != std::string::npos) {
                    std::string countStr = line.substr(colonPos + 1);
                    // 移除逗号和空格
                    countStr.erase(std::remove(countStr.begin(), countStr.end(), ','), countStr.end());
                    countStr.erase(std::remove(countStr.begin(), countStr.end(), ' '), countStr.end());
                    
                    try {
                        int count = std::stoi(countStr);
                        
                        if (inShapeRotations) {
                            // 新格式：形状名称_角度
                            shapeRotations[shapeKey] = count;
                            std::cout << "从JSON配置文件加载（角度）: " << shapeKey << " = " << count << std::endl;
                        } else if (inShapeCounts) {
                            // 旧格式：仅形状名称（兼容性）
                            if (shapeCounts.find(shapeKey) != shapeCounts.end()) {
                                shapeCounts[shapeKey] = count;
                                std::cout << "从JSON配置文件加载（旧格式）: " << shapeKey << " = " << count << std::endl;
                            }
                        }
                        loadedCount++;
                    } catch (...) {
                        // 忽略解析错误
                    }
                }
            }
        }
    }
    
    if (!shapeRotations.empty()) {
        std::cout << "JSON配置文件解析完成（角度格式），共加载 " << loadedCount << " 个配置项" << std::endl;
    } else {
        std::cout << "JSON配置文件解析完成（旧格式），共加载 " << loadedCount << " 个配置项" << std::endl;
    }
}

PicnicGame::PicnicGame()
    : boardCols(6), boardRows(6), cellPixelSize(60), difficulty(PicnicDifficulty::Hard),
      isDragging(false), isDraggedOut(false), dragOffsetX(0), dragOffsetY(0),
      draggedOutRotation(0), draggingFromPanel(false),
      hasOriginalPosition(false), originalX(0), originalY(0), originalRotation(0) {
    initBoardStorage();
    applyDefaultShapesForDifficulty();
    initializeShapes();
}

void PicnicGame::initBoardStorage() {
    board.assign(boardRows, std::vector<std::shared_ptr<Shape>>(boardCols, nullptr));
    stones.assign(boardRows, std::vector<bool>(boardCols, false));
    placedShapes.clear();
}

bool PicnicGame::isStone(int x, int y) const {
    if (x < 0 || x >= boardCols || y < 0 || y >= boardRows)
        return false;
    return stones[y][x];
}

bool PicnicGame::isCellFree(int x, int y) const {
    if (x < 0 || x >= boardCols || y < 0 || y >= boardRows)
        return false;
    return board[y][x] == nullptr && !stones[y][x];
}

int PicnicGame::getStoneCount() const {
    int n = 0;
    for (int y = 0; y < boardRows; y++)
        for (int x = 0; x < boardCols; x++)
            if (stones[y][x]) n++;
    return n;
}

bool PicnicGame::setStone(int x, int y, bool on) {
    if (x < 0 || x >= boardCols || y < 0 || y >= boardRows)
        return false;
    if (on && board[y][x] != nullptr)
        return false;  // 已有图块的格子不能放石头
    stones[y][x] = on;
    return true;
}

bool PicnicGame::toggleStone(int x, int y) {
    if (x < 0 || x >= boardCols || y < 0 || y >= boardRows)
        return false;
    if (!stones[y][x] && board[y][x] != nullptr)
        return false;
    stones[y][x] = !stones[y][x];
    return stones[y][x];
}

void PicnicGame::clearStones() {
    for (int y = 0; y < boardRows; y++)
        for (int x = 0; x < boardCols; x++)
            stones[y][x] = false;
}

void PicnicGame::applyDefaultShapesForDifficulty() {
    shapeRotations.clear();
    shapeCounts.clear();
    switch (difficulty) {
    case PicnicDifficulty::Easy:
        // 初级 4×4 = 16 格
        shapeRotations = {
            {"2x2_0", 4}
        };
        break;
    case PicnicDifficulty::Medium:
        // 中级 4×6 = 24 格
        shapeRotations = {
            {"Cross_0", 1},      // 5
            {"Line5_90", 1},     // 5
            {"Line4_0", 1},      // 4
            {"L-shape_90", 1},   // 4
            {"Line3_90", 2}      // 6
        };
        break;
    case PicnicDifficulty::Hard:
    default:
        // 高级 6×6 = 36 格
        shapeRotations = {
            {"Small-L_90", 1},
            {"C-shape_0", 1},
            {"Line5_90", 1},
            {"Cross_0", 1},
            {"Line3_90", 2},
            {"Line4_0", 1},
            {"L-mirror_0", 1},
            {"L-shape_90", 1},
            {"1x1_0", 0}
        };
        break;
    }
}

void PicnicGame::setDifficulty(PicnicDifficulty d) {
    difficulty = d;
    switch (d) {
    case PicnicDifficulty::Easy:
        boardCols = 4;
        boardRows = 4;
        break;
    case PicnicDifficulty::Medium:
        boardCols = 6;
        boardRows = 4;
        break;
    case PicnicDifficulty::Hard:
    default:
        boardCols = 6;
        boardRows = 6;
        break;
    }
    initBoardStorage();
    applyDefaultShapesForDifficulty();
    rebuildShapes();
    resetGame();
}

PicnicGame::PicnicGame(const std::string& configFile)
    : boardCols(6), boardRows(6), cellPixelSize(60), difficulty(PicnicDifficulty::Hard),
      isDragging(false), isDraggedOut(false), dragOffsetX(0), dragOffsetY(0),
      draggedOutRotation(0), draggingFromPanel(false),
      hasOriginalPosition(false), originalX(0), originalY(0), originalRotation(0) {
    initBoardStorage();
    shapeRotations.clear();
    shapeCounts.clear();
    loadConfigFromFile(configFile);
    if (shapeRotations.empty()) {
        std::cout << "未加载到配置，使用默认配置" << std::endl;
        applyDefaultShapesForDifficulty();
    }
    initializeShapes();
}

void PicnicGame::initializeShapes() {
    availableShapes.clear();
    
    // 定义基础图块集合（与Python版本一致）
    std::vector<std::tuple<std::string, std::vector<std::vector<bool>>, char, Color>> baseShapes = {
        {"3x3", {{true, true, true}, {true, true, true}, {true, true, true}}, '3', Color(255, 0, 0)},
        {"Big-L", {{true, false, false}, {true, false, false}, {true, true, true}}, 'L', Color(0, 0, 255)},
        {"2x4", {{true, true, true, true}, {true, true, true, true}}, 'R', Color(128, 64, 0)},
        {"2x3", {{true, true, true}, {true, true, true}}, 'F', Color(139, 69, 19)},
        {"Line4", {{true, true, true, true}}, 'I', Color(0, 80, 80)},
        {"Cross", {{false, true, false}, {true, true, true}, {false, true, false}}, 'X', Color(0, 80, 0)},
        {"2x2", {{true, true}, {true, true}}, '2', Color(139, 0, 0)},
        {"L-shape", {{true, false}, {true, false}, {true, true}}, 'L', Color(0, 128, 128)},
        {"L-mirror", {{false, true}, {false, true}, {true, true}}, 'J', Color(128, 0, 128)},
        {"Small-L", {{true, false}, {true, true}}, 'l', Color(139, 69, 69)},
        {"T-shape", {{true, true, true}, {false, true, false}}, 'T', Color(100, 80, 0)}, // OXO / XXX
        {"Z-shape", {{true, true, false}, {false, true, true}}, 'Z', Color(0, 0, 128)},
        {"Z-mirror", {{false, true, true}, {true, true, false}}, 'N', Color(128, 0, 128)},
        {"Line3", {{true, true, true}}, 'S', Color(128, 0, 0)},
        {"Line2", {{true, true}}, 'D', Color(0, 128, 0)}, // XX
        {"1x1", {{true}}, '1', Color(0, 0, 128)},
        // C-shape: XX / XO / XX
        {"C-shape", {{true, true}, {true, false}, {true, true}}, 'B', Color(255, 165, 0)},
        {"Line5", {{true, true, true, true, true}}, 'V', Color(148, 0, 211)},
        // W-shape: XOO / XXO / OXX
        {"W-shape", {{true, false, false}, {true, true, false}, {false, true, true}}, 'W', Color(128, 128, 64)},
        // Big-T: XXX / OXO / OXO
        {"Big-T", {{true, true, true}, {false, true, false}, {false, true, false}}, 'Y', Color(100, 100, 50)},
        // Wide-L: XO / XX / XX
        {"Wide-L", {{true, false}, {true, true}, {true, true}}, 'K', Color(100, 80, 60)},
        // wideL-mirror: OX / XX / XX
        {"wideL-mirror", {{false, true}, {true, true}, {true, true}}, 'M', Color(160, 100, 70)},
        // longL: XO / XO / XO / XX
        {"longL", {{true, false}, {true, false}, {true, false}, {true, true}}, 'G', Color(70, 130, 180)},
        // longL-mirror: OX / OX / OX / XX
        {"longL-mirror", {{false, true}, {false, true}, {false, true}, {true, true}}, 'P', Color(100, 149, 237)},
        // alloL: XO / XO / XX / XO
        {"alloL", {{true, false}, {true, false}, {true, true}, {true, false}}, 'H', Color(210, 105, 30)},
        // alloL-mirror: OX / OX / XX / OX
        {"alloL-mirror", {{false, true}, {false, true}, {true, true}, {false, true}}, 'Q', Color(222, 140, 60)},
        // longZ: XXXO / OOXX
        {"longZ", {{true, true, true, false}, {false, false, true, true}}, 'U', Color(72, 61, 139)},
        // longZ-mirror: OXXX / XXOO
        {"longZ-mirror", {{false, true, true, true}, {true, true, false, false}}, 'E', Color(106, 90, 205)},
        // Big-Z: XXO / OXO / OXX
        {"Big-Z", {{true, true, false}, {false, true, false}, {false, true, true}}, 'I', Color(47, 79, 79)},
        // Big-Z-mirror: OXX / OXO / XXO（与 Big-Z 左右镜像）
        {"Big-Z-mirror", {{false, true, true}, {false, true, false}, {true, true, false}}, 'O', Color(70, 90, 90)}
    };
    
    // 使用全局唯一ID分配器
    
    // 首先检查是否有角度配置
    if (!shapeRotations.empty()) {
        std::cout << "使用角度配置初始化图块..." << std::endl;
        std::cout.flush();
        
        // 新格式：按角度配置初始化
        for (const auto& [name, pattern, symbol, color] : baseShapes) {
            // 检查这个形状的所有角度配置
            for (int angle = 0; angle < 360; angle += 90) {
                std::string key = name + "_" + std::to_string(angle);
                auto it = shapeRotations.find(key);
                
                if (it != shapeRotations.end() && it->second > 0) {
                    int count = it->second;
                    int rotation = angle / 90;
                    
                    if (count > 1) {
                        std::cout << "准备创建 " << count << " 个 " << name << " 图块..." << std::endl;
                        std::cout.flush();
                    }
                    
                    for (int i = 0; i < count; i++) {
                        std::cout << "正在创建第 " << (i + 1) << "/" << count << " 个 " << name << "..." << std::endl;
                        std::cout.flush();
                        
                        // 创建带角度标识的唯一符号
                        char uniqueSymbol;
                        if (count > 1) {
                            // 修复：安全的数字符号计算，避免字符溢出
                            int num = (i + 1) % 10;
                            uniqueSymbol = '0' + num;  // 只用0-9，避免复杂逻辑
                        } else if (rotation > 0) {
                            // 安全的角度符号表示，避免非ASCII字符
                            int angleIndex = (rotation / 90) - 1;
                            uniqueSymbol = 'A' + (angleIndex % 26);  // A-Z循环，防止超出范围
                        } else {
                            uniqueSymbol = symbol;
                        }
                        
                        // 确保生成的符号是安全的
                        if (!isPrintableChar(uniqueSymbol)) {
                            uniqueSymbol = '?';  // 默认安全字符
                        }
                        
                        auto shape = std::make_shared<Shape>(name, pattern, uniqueSymbol, color, getNextShapeId());
                        availableShapes.push_back(shape);
                        panelRotations[shape->getId()] = rotation;
                        
                        // 安全输出：先输出基本信息
                        std::cout << "创建 " << name << " 数量" << count << " 角度" << angle << " 符号 ";
                        safeOutputChar(uniqueSymbol);
                        std::cout << " ID=" << shape->getId() << std::endl;
                        std::cout.flush();
                    }
                }
            }
        }
    } else {
        std::cout << "使用旧格式初始化图块..." << std::endl;
        std::cout.flush();
        
        // 旧格式：按形状配置初始化，默认0度
        for (const auto& [name, pattern, symbol, color] : baseShapes) {
            int count = shapeCounts[name];
            for (int i = 0; i < count; i++) {
                char uniqueSymbol = (count > 1) ? ('A' + (symbol - 'A' + i) % 26) : symbol;
                auto shape = std::make_shared<Shape>(name, pattern, uniqueSymbol, color, getNextShapeId());
                availableShapes.push_back(shape);
                panelRotations[shape->getId()] = 0;
            }
        }
    }
    
    std::cout << "总共初始化了 " << availableShapes.size() << " 个图块" << std::endl;
    std::cout.flush();
    std::cout << "=== 初始化完成 ===" << std::endl;
    std::cout.flush();
}

bool PicnicGame::canPlaceShape(std::shared_ptr<Shape> shape, int x, int y, int rotation) {
    auto rotatedPattern = getRotatedPattern(shape, rotation);
    
    if (rotatedPattern.empty()) return false;
    
    int patternHeight = rotatedPattern.size();
    int patternWidth = rotatedPattern[0].size();
    
    // 检查边界
    if (x < 0 || y < 0 || x + patternWidth > boardCols || y + patternHeight > boardRows) {
        return false;
    }
    
    // 检查重叠（图块或石头）
    for (int i = 0; i < patternHeight; i++) {
        for (int j = 0; j < patternWidth; j++) {
            if (rotatedPattern[i][j] &&
                (board[y + i][x + j] != nullptr || stones[y + i][x + j])) {
                return false;
            }
        }
    }
    
    return true;
}

bool PicnicGame::canPlaceShapeConst(std::shared_ptr<Shape> shape, int x, int y, int rotation) const {
    auto rotatedPattern = getRotatedPattern(shape, rotation);
    
    if (rotatedPattern.empty()) return false;
    
    int patternHeight = rotatedPattern.size();
    int patternWidth = rotatedPattern[0].size();
    
    // 检查边界
    if (x < 0 || y < 0 || x + patternWidth > boardCols || y + patternHeight > boardRows) {
        return false;
    }
    
    // 检查重叠（图块或石头）
    for (int i = 0; i < patternHeight; i++) {
        for (int j = 0; j < patternWidth; j++) {
            if (rotatedPattern[i][j] &&
                (board[y + i][x + j] != nullptr || stones[y + i][x + j])) {
                return false;
            }
        }
    }
    
    return true;
}

bool PicnicGame::placeShape(std::shared_ptr<Shape> shape, int x, int y, int rotation) {
    if (!canPlaceShape(shape, x, y, rotation)) {
        return false;
    }
    
    // 在回溯算法中，确保这个图块没有被重复放置
    // 先清理这个图块的任何现有状态
    int shapeId = shape->getId();
    placedShapes.erase(
        std::remove_if(placedShapes.begin(), placedShapes.end(),
            [shapeId](const PlacedShape& ps) {
                return ps.shapeId == shapeId;
            }),
        placedShapes.end());
    
    // 清理棋盘上的这个图块
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            if (board[i][j] == shape) {
                board[i][j] = nullptr;
            }
        }
    }
    
    auto rotatedPattern = getRotatedPattern(shape, rotation);
    int patternHeight = rotatedPattern.size();
    int patternWidth = rotatedPattern[0].size();
    
    // 放置图块
    for (int i = 0; i < patternHeight; i++) {
        for (int j = 0; j < patternWidth; j++) {
            if (rotatedPattern[i][j]) {
                board[y + i][x + j] = shape;
            }
        }
    }
    
    // 记录已放置的图块
    placedShapes.emplace_back(shape, x, y, rotation);
    return true;
}

void PicnicGame::removeShapeAt(int x, int y) {
    if (x < 0 || x >= boardCols || y < 0 || y >= boardRows) return;
    
    auto shapeToRemove = board[y][x];
    if (!shapeToRemove) return;
    
    int shapeId = shapeToRemove->getId();
    
    // 清空游戏板上的图块
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            if (board[i][j] == shapeToRemove) {
                board[i][j] = nullptr;
            }
        }
    }
    
    // 从已放置列表中移除 - 使用shapeId字段进行匹配
    placedShapes.erase(
        std::remove_if(placedShapes.begin(), placedShapes.end(),
            [shapeId](const PlacedShape& ps) {
                return ps.shapeId == shapeId;
            }),
        placedShapes.end());
}

void PicnicGame::removeShapeById(int shapeId) {
    // 清空游戏板上的图块
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            if (board[i][j] && board[i][j]->getId() == shapeId) {
                board[i][j] = nullptr;
            }
        }
    }
    
    // 从已放置列表中移除
    placedShapes.erase(
        std::remove_if(placedShapes.begin(), placedShapes.end(),
            [shapeId](const PlacedShape& ps) {
                return ps.shapeId == shapeId;
            }),
        placedShapes.end());
}

bool PicnicGame::rotateSelectedShape() {
    if (!selectedShape) {
        return false;
    }
    
    // 如果是拖出状态，只更新旋转角度
    if (isDraggedOut) {
        draggedOutRotation = (draggedOutRotation + 1) % 4;
        return true;
    }
    
    // 查找已放置的形状信息
    PlacedShape* placedInfo = findPlacedShape(selectedShape->getId());
    if (!placedInfo) {
        return false;
    }
    
    int oldX = placedInfo->x;
    int oldY = placedInfo->y;
    int oldRot = placedInfo->rotation;
    int newRotation = (oldRot + 1) % 4;
    
    // 临时移除当前形状
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            if (board[i][j] == selectedShape) {
                board[i][j] = nullptr;
            }
        }
    }
    
    // 尝试放置新旋转
    if (canPlaceShape(selectedShape, oldX, oldY, newRotation)) {
        placeShape(selectedShape, oldX, oldY, newRotation);
        return true;
    } else {
        // 如果不能旋转，恢复原状
        placeShape(selectedShape, oldX, oldY, oldRot);
        return false;
    }
}

void PicnicGame::startDrag(std::shared_ptr<Shape> shape, int mouseX, int mouseY, bool fromPanel) {
    selectedShape = shape;
    isDragging = true;
    draggingFromPanel = fromPanel;
    hasOriginalPosition = false;
    
    // 查找已放置的形状信息
    PlacedShape* placedInfo = nullptr;
    if (!fromPanel) {
        for (auto& ps : placedShapes) {
            if (ps.shape->getId() == shape->getId()) {
                placedInfo = &ps;
                originalX = ps.x;
                originalY = ps.y;
                originalRotation = ps.rotation;
                draggedOutRotation = ps.rotation; // 保持原有的旋转角度
                hasOriginalPosition = true;
                break;
            }
        }
    } else {
        // 从面板拖拽，保持面板中的旋转角度
        draggedOutRotation = getPanelRotation(shape->getId());
    }
    
    // 计算图块相对于鼠标的偏移量
    auto pattern = shape->getRotatedPattern(draggedOutRotation);
    if (!pattern.empty()) {
        int shapeWidth = static_cast<int>(pattern[0].size()) * cellPixelSize / 2;
        int shapeHeight = static_cast<int>(pattern.size()) * cellPixelSize / 2;
        dragOffsetX = mouseX - shapeWidth / 2;
        dragOffsetY = mouseY - shapeHeight / 2;
    } else {
        dragOffsetX = 0;
        dragOffsetY = 0;
    }
    
    isDraggedOut = true; // 拖拽出去的标志
}

void PicnicGame::updateDrag(int mouseX, int mouseY) {
    if (!isDragging) return;
    // 这里可以添加拖拽过程中的逻辑
}

bool PicnicGame::endDrag(int mouseX, int mouseY) {
    if (!isDragging) return false;
    
    bool success = false;
    
    // 检查是否在游戏板内
    if (mouseX >= 0 && mouseX < boardCols * cellPixelSize &&
        mouseY >= 0 && mouseY < boardRows * cellPixelSize) {
        
        // 计算精确放置位置（与拖拽预览位置一致）
        auto pattern = selectedShape->getRotatedPattern(draggedOutRotation);
        int shapeWidth = static_cast<int>(pattern[0].size()) * cellPixelSize;
        int shapeHeight = static_cast<int>(pattern.size()) * cellPixelSize;
        
        float preciseX = (float)(mouseX - shapeWidth / 2) / (float)cellPixelSize;
        float preciseY = (float)(mouseY - shapeHeight / 2) / (float)cellPixelSize;
        
        int finalX = (int)round(preciseX);
        int finalY = (int)round(preciseY);
        
        finalX = (std::max)(0, (std::min)(finalX, boardCols - 1));
        finalY = (std::max)(0, (std::min)(finalY, boardRows - 1));
        
        if (draggingFromPanel) {
            // 从面板拖拽到游戏板
            if (canPlaceShape(selectedShape, finalX, finalY, draggedOutRotation)) {
                placeShape(selectedShape, finalX, finalY, draggedOutRotation);
                success = true;
            }
        } else {
            // 重新放置已存在的图块
            // 先移除原来的位置（如果存在）
            for (int i = 0; i < boardRows; i++) {
                for (int j = 0; j < boardCols; j++) {
                    if (board[i][j] == selectedShape) {
                        board[i][j] = nullptr;
                    }
                }
            }
            
            if (canPlaceShape(selectedShape, finalX, finalY, draggedOutRotation)) {
                placeShape(selectedShape, finalX, finalY, draggedOutRotation);
                success = true;
            }
        }
    }
    
    // 如果放置失败且有原始位置，恢复原始位置
    if (!success && hasOriginalPosition && !draggingFromPanel) {
        // 清除任何可能的残留状态
        for (int i = 0; i < boardRows; i++) {
            for (int j = 0; j < boardCols; j++) {
                if (board[i][j] == selectedShape) {
                    board[i][j] = nullptr;
                }
            }
        }
        
        // 恢复到原始位置
        placeShape(selectedShape, originalX, originalY, originalRotation);
    }
    
    // 清理拖拽状态（但不执行恢复，因为上面已经处理了）
    selectedShape = nullptr;
    isDragging = false;
    isDraggedOut = false;
    draggingFromPanel = false;
    draggedOutRotation = 0;
    hasOriginalPosition = false;
    return success;
}

void PicnicGame::cancelDrag() {
    // 如果是从游戏板拖拽且放置失败，需要恢复原始位置
    if (!draggingFromPanel && hasOriginalPosition && selectedShape) {
        // 清除当前可能的残留状态
        for (int i = 0; i < boardRows; i++) {
            for (int j = 0; j < boardCols; j++) {
                if (board[i][j] == selectedShape) {
                    board[i][j] = nullptr;
                }
            }
        }
        
        // 恢复到原始位置
        placeShape(selectedShape, originalX, originalY, originalRotation);
    }
    
    selectedShape = nullptr;
    isDragging = false;
    isDraggedOut = false;
    draggingFromPanel = false;
    draggedOutRotation = 0;
    hasOriginalPosition = false;
}

bool PicnicGame::isGameWon() const {
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            if (board[i][j] == nullptr && !stones[i][j]) {
                return false;
            }
        }
    }
    return true;
}

void PicnicGame::resetGame() {
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            board[i][j] = nullptr;
        }
    }
    placedShapes.clear();
    // 石头保留，不随重置清除
}

std::shared_ptr<Shape> PicnicGame::getBoardCell(int x, int y) const {
    if (x < 0 || x >= boardCols || y < 0 || y >= boardRows) {
        return nullptr;
    }
    return board[y][x];
}

std::vector<std::vector<bool>> PicnicGame::getRotatedPattern(std::shared_ptr<Shape> shape, int rotation) const {
    return shape->getRotatedPattern(rotation);
}

PlacedShape* PicnicGame::findPlacedShape(int shapeId) {
    for (auto& ps : placedShapes) {
        if (ps.shape->getId() == shapeId) {
            return &ps;
        }
    }
    return nullptr;
}

int PicnicGame::getPanelRotation(int shapeId) const {
    auto it = panelRotations.find(shapeId);
    return (it != panelRotations.end()) ? it->second : 0;
}

void PicnicGame::setPanelRotation(int shapeId, int rotation) {
    panelRotations[shapeId] = rotation % 4;
}

int PicnicGame::incrementPanelRotation(int shapeId) {
    int current = getPanelRotation(shapeId);
    int newRotation = (current + 1) % 4;
    setPanelRotation(shapeId, newRotation);
    return newRotation;
}

bool PicnicGame::isShapeUsed(std::shared_ptr<Shape> shape) const {
    int shapeId = shape->getId();
    for (const auto& ps : placedShapes) {
        if (ps.shapeId == shapeId) {
            return true;
        }
    }
    return false;
}

std::vector<std::shared_ptr<Shape>> PicnicGame::getAvailableShapesForPanel() const {
    std::vector<std::shared_ptr<Shape>> availableForPanel;
    for (const auto& shape : availableShapes) {
        bool used = false;
        int shapeId = shape->getId();
        
        // 检查这个图块是否在已放置列表中
        for (const auto& ps : placedShapes) {
            if (ps.shapeId == shapeId) {
                used = true;
                break;
            }
        }
        
        if (!used) {
            availableForPanel.push_back(shape);
        }
    }
    return availableForPanel;
}

std::shared_ptr<Shape> PicnicGame::getShapeByName(const std::string& name) const {
    // 创建基础图块集合用于查找
    std::vector<std::tuple<std::string, std::vector<std::vector<bool>>, char, Color>> baseShapes = {
        {"3x3", {{true, true, true}, {true, true, true}, {true, true, true}}, '3', Color(255, 0, 0)},
        {"Big-L", {{true, false, false}, {true, false, false}, {true, true, true}}, 'L', Color(0, 0, 255)},
        {"2x4", {{true, true, true, true}, {true, true, true, true}}, 'R', Color(128, 64, 0)},
        {"2x3", {{true, true, true}, {true, true, true}}, 'F', Color(139, 69, 19)},
        {"Line4", {{true, true, true, true}}, 'I', Color(0, 80, 80)},
        {"Cross", {{false, true, false}, {true, true, true}, {false, true, false}}, 'X', Color(0, 80, 0)},
        {"2x2", {{true, true}, {true, true}}, '2', Color(139, 0, 0)},
        {"L-shape", {{true, false}, {true, false}, {true, true}}, 'L', Color(0, 128, 128)},
        {"L-mirror", {{false, true}, {false, true}, {true, true}}, 'J', Color(128, 0, 128)},
        {"Small-L", {{true, false}, {true, true}}, 'l', Color(139, 69, 69)},
        {"T-shape", {{true, true, true}, {false, true, false}}, 'T', Color(100, 80, 0)},
        {"Z-shape", {{true, true, false}, {false, true, true}}, 'Z', Color(0, 0, 128)},
        {"Z-mirror", {{false, true, true}, {true, true, false}}, 'N', Color(128, 0, 128)},
        {"Line3", {{true, true, true}}, 'S', Color(128, 0, 0)},
        {"Line2", {{true, true}}, 'D', Color(0, 128, 0)},
        {"1x1", {{true}}, '1', Color(0, 0, 128)},
        {"C-shape", {{true, true}, {true, false}, {true, true}}, 'B', Color(255, 165, 0)},
        {"Line5", {{true, true, true, true, true}}, 'V', Color(148, 0, 211)},
        {"W-shape", {{true, false, false}, {true, true, false}, {false, true, true}}, 'W', Color(128, 128, 64)},
        {"Big-T", {{true, true, true}, {false, true, false}, {false, true, false}}, 'Y', Color(100, 100, 50)},
        {"Wide-L", {{true, false}, {true, true}, {true, true}}, 'K', Color(100, 80, 60)},
        {"wideL-mirror", {{false, true}, {true, true}, {true, true}}, 'M', Color(160, 100, 70)},
        {"longL", {{true, false}, {true, false}, {true, false}, {true, true}}, 'G', Color(70, 130, 180)},
        {"longL-mirror", {{false, true}, {false, true}, {false, true}, {true, true}}, 'P', Color(100, 149, 237)},
        {"alloL", {{true, false}, {true, false}, {true, true}, {true, false}}, 'H', Color(210, 105, 30)},
        {"alloL-mirror", {{false, true}, {false, true}, {true, true}, {false, true}}, 'Q', Color(222, 140, 60)},
        {"longZ", {{true, true, true, false}, {false, false, true, true}}, 'U', Color(72, 61, 139)},
        {"longZ-mirror", {{false, true, true, true}, {true, true, false, false}}, 'E', Color(106, 90, 205)},
        {"Big-Z", {{true, true, false}, {false, true, false}, {false, true, true}}, 'I', Color(47, 79, 79)},
        {"Big-Z-mirror", {{false, true, true}, {false, true, false}, {true, true, false}}, 'O', Color(70, 90, 90)}
    };
    
    for (const auto& [shapeName, pattern, symbol, color] : baseShapes) {
        if (shapeName == name) {
            return std::make_shared<Shape>(shapeName, pattern, symbol, color, -1);
        }
    }
    
    return nullptr;
}

bool PicnicGame::solveFillEntireBoard() {
    resetGame();
    
    std::cout << "\n=== 开始自动求解 ===" << std::endl;
    std::cout.flush(); // 强制立即输出
    std::cout << "=== 简化求解算法 ===" << std::endl;
    std::cout.flush();
    std::cout << "开始求解过程..." << std::endl;
    std::cout.flush(); // 再次确保输出
    
    auto startTime = clock();
    
    // 构建求解用的图块列表
    std::vector<std::shared_ptr<Shape>> solvingShapes;
    std::map<std::string, int> requiredCount;
    
    // 统计配置中的确切数量
    if (!shapeRotations.empty()) {
        for (const auto& [key, count] : shapeRotations) {
            if (count <= 0) continue;
            size_t underscorePos = key.find('_');
            if (underscorePos != std::string::npos) {
                std::string shapeName = key.substr(0, underscorePos);
                requiredCount[shapeName] += count;
            }
        }
    }
    
    // 直接从availableShapes中筛选符合配置的图块
    for (const auto& shape : availableShapes) {
        std::string name = shape->getName();
        if (requiredCount.find(name) != requiredCount.end() && requiredCount[name] > 0) {
            solvingShapes.push_back(shape);
            requiredCount[name]--;
        }
    }
    
    std::cout << "求解图块总数: " << solvingShapes.size() << std::endl;
    
    int totalArea = 0;
    for (const auto& shape : solvingShapes) {
        totalArea += shape->getArea();
        std::cout << "- " << shape->getName() << " (ID:" << shape->getId() 
                  << ", 面积:" << shape->getArea() << ", 角度:" 
                  << (getPanelRotation(shape->getId()) * 90) << "°)" << std::endl;
    }
    
    const int playableArea = getPlayableArea();
    std::cout << "求解总面积: " << totalArea << " (可放格: " << playableArea
              << ", 棋盘: " << (boardCols * boardRows)
              << ", 石头: " << getStoneCount() << ")" << std::endl;
    
    if (totalArea != playableArea) {
        std::cout << "❌ 面积不匹配，无法完全填满棋盘!" << std::endl;
        return false;
    }
    
    std::cout << "✅ 验证通过，开始求解..." << std::endl;
    
    // 打开调试日志文件
    std::ofstream debugFile("debug_log.txt", std::ios::app);
    if (!debugFile.is_open()) {
        std::cout << "⚠️ 无法打开调试日志文件 debug_log.txt" << std::endl;
    } else {
        debugFile << "\n===== 开始求解 =====" << std::endl;
        debugFile << "时间: " << std::time(nullptr) << std::endl;
        debugFile << "图块总数: " << solvingShapes.size() << std::endl;
    }
    
    int attempts = 0;
    bool solutionFound = false;
    std::vector<bool> used(solvingShapes.size(), false);
    
    // 调试输出辅助函数
    auto debugPrint = [&](const std::string& message) {
        std::cout << message << std::endl;
        std::cout.flush(); // 确保输出立即显示
        if (debugFile.is_open()) {
            debugFile << message << std::endl;
        }
    };
    
    // 文件专用调试输出（无深度限制）
    auto debugFileOnly = [&](const std::string& message) {
        if (debugFile.is_open()) {
            debugFile << message << std::endl;
        }
    };
    
    // 棋盘状态记录函数
    auto logBoardState = [&](const std::string& shapeName, int x, int y, int depth) {
        if (debugFile.is_open()) {
            debugFile << "\n🗺 [深度" << depth << "] " << shapeName << "放置在(" << x << "," << y << ")后棋盘状态:" << std::endl;
            debugFile << "   ";
            for (int boardY = 0; boardY < boardRows; boardY++) {
                for (int boardX = 0; boardX < boardCols; boardX++) {
                    if (board[boardY][boardX] == nullptr) {
                        debugFile << "□";
                    } else {
                        debugFile << "■";
                    }
                }
                if (boardY < boardRows - 1) {
                    debugFile << std::endl << "   ";
                }
            }
            
            // 计算空余格子数
            int emptyCount = 0;
            for (int boardY = 0; boardY < boardRows; boardY++) {
                for (int boardX = 0; boardX < boardCols; boardX++) {
                    if (board[boardY][boardX] == nullptr) emptyCount++;
                }
            }
            debugFile << " (空余: " << emptyCount << "格)" << std::endl;
        }
    };
    
    // 关键修复：预先计算固定的全局优先级排序，确保每个深度对应固定图块
    std::vector<std::tuple<int, int, int>> globalShapeIndices; // {priority, area, index}
    for (int i = 0; i < solvingShapes.size(); i++) {
        int priority = 0;
        // Cross图块最高优先级
        if (solvingShapes[i]->getName() == "Cross") {
            priority = 1000;
        } else {
            // 其他图块按面积排序，大图块优先
            priority = solvingShapes[i]->getArea();
        }
        globalShapeIndices.push_back({priority, solvingShapes[i]->getArea(), i});
    }
    std::sort(globalShapeIndices.rbegin(), globalShapeIndices.rend()); // 降序排列，优先级高的在前
    
    // 调试：输出全局优先级排序
    std::stringstream globalOrder;
    globalOrder << "🎯 全局图块优先级顺序: ";
    for (auto [priority, area, idx] : globalShapeIndices) {
        globalOrder << solvingShapes[idx]->getName() << "(P:" << priority << ") ";
    }
    globalOrder << std::endl;
    debugPrint(globalOrder.str());
    
    // 简化但充分的回溯算法
    std::function<bool(int)> backtrack = [&](int placedCount) -> bool {
        if (placedCount == solvingShapes.size()) {
            if (isBoardCompletelyFilled()) {
                std::cout << "✅ 找到完整解！" << std::endl;
                return true;
            } else {
                std::cout << "⚠️ 所有图块已放置但棋盘未完全填满" << std::endl;
                return false;
            }
        }
        
        // 早期无解检测：检查剩余空间是否合理
        std::vector<int> remainingShapeAreas;
        for (int i = 0; i < solvingShapes.size(); i++) {
            if (!used[i]) {
                remainingShapeAreas.push_back(solvingShapes[i]->getArea());
            }
        }
        std::sort(remainingShapeAreas.rbegin(), remainingShapeAreas.rend()); // 降序排列
        
        int emptyBoardCells = 0;
        std::vector<std::pair<int, int>> emptyPositions;
        for (int y = 0; y < boardRows; y++) {
            for (int x = 0; x < boardCols; x++) {
                if (isCellFree(x, y)) {
                    emptyBoardCells++;
                    emptyPositions.emplace_back(x, y);
                }
            }
        }
        
        // 检查总面积匹配
        int totalRemainingArea = 0;
        for (int area : remainingShapeAreas) totalRemainingArea += area;
        
        if (totalRemainingArea != emptyBoardCells) {
            if (placedCount <= 3) {
                std::cout << "⚠️ 面积不匹配：剩余图块总面积" << totalRemainingArea 
                         << " != 棋盘空余" << emptyBoardCells << "格" << std::endl;
            }
            return false; // 立即回溯
        }
        
        // 连通性检查：确保最大剩余区域能容纳最大的剩余图块
        std::vector<std::vector<std::pair<int, int>>> regions;
        findConnectedRegions(regions);
        
        if (!regions.empty()) {
            int maxRegionSize = 0;
            int minRemainingShape = remainingShapeAreas.empty() ? 0 : *std::min_element(remainingShapeAreas.begin(), remainingShapeAreas.end());
            
            // 找出所有连通区域的大小
            std::vector<int> regionSizes;
            for (const auto& region : regions) {
                regionSizes.push_back(region.size());
                maxRegionSize = (std::max)(maxRegionSize, (int)region.size());
            }
            
            // 检查1: 最大连通区域必须能容纳最大剩余图块
            int maxRemainingShape = remainingShapeAreas.empty() ? 0 : remainingShapeAreas[0];
            if (maxRegionSize < maxRemainingShape) {
                if (placedCount <= 3) {
                    std::cout << "⚠️ 空间碎片化：最大连通区域" << maxRegionSize 
                             << "格 < 最大剩余图块" << maxRemainingShape << "格" << std::endl;
                    std::cout << "   连通区域数量: " << regions.size() 
                             << ", 区域大小: [";
                    for (size_t i = 0; i < regionSizes.size(); i++) {
                        if (i > 0) std::cout << ", ";
                        std::cout << regionSizes[i];
                    }
                    std::cout << "]" << std::endl;
                }
                return false; // 立即回溯
            }
            
            // 检查2: 任何封闭区域必须能容纳最小剩余图块（关键优化！）
            for (size_t i = 0; i < regions.size(); i++) {
                if (regionSizes[i] < minRemainingShape && regionSizes[i] > 0) {
                    if (placedCount <= 3) {
                        std::cout << "⚠️ 封闭区域检测：发现" << regionSizes[i] << "格的封闭区域，"
                                 << "但最小剩余图块需要" << minRemainingShape << "格" << std::endl;
                        std::cout << "   这个封闭区域无法使用任何剩余图块，求解必定失败" << std::endl;
                        std::cout << "   所有区域大小: [";
                        for (size_t j = 0; j < regionSizes.size(); j++) {
                            if (j > 0) std::cout << ", ";
                            std::cout << regionSizes[j];
                        }
                        std::cout << "], 剩余图块面积: [";
                        for (size_t j = 0; j < remainingShapeAreas.size(); j++) {
                            if (j > 0) std::cout << ", ";
                            std::cout << remainingShapeAreas[j];
                        }
                        std::cout << "]" << std::endl;
                    }
                    return false; // 立即回溯，避免在无解的布局上浪费时间
                }
            }
        }
        
        // 调试：跟踪递归深度
        if (placedCount <= 5) {
            std::stringstream depthMsg;
            depthMsg << "📍 [深度" << placedCount << "] 开始回溯搜索，已放置 " << placedCount << "/" << solvingShapes.size() << " 个图块";
            debugPrint(depthMsg.str());
        }
        
        // 调试：显示当前棋盘空余空间
        if (placedCount <= 2) {
            std::cout << "🗺 当前棋盘状态:   ";
            for (int y = 0; y < boardRows; y++) {
                for (int x = 0; x < boardCols; x++) {
                    if (board[y][x] == nullptr) {
                        std::cout << "□";
                    } else {
                        std::cout << "■";
                    }
                }
                if (y < boardRows - 1) std::cout << std::endl << "                   ";
            }
            std::cout << " (空余: " << emptyBoardCells << "格)" << std::endl;
        }
        
        // 关键修复：使用全局固定的优先级排序，确保每个深度对应固定图块
        // 找到当前深度应该使用的图块 - 深度直接对应全局优先级排序中的位置
        int currentShapeIndex = -1;
        
        if (placedCount < globalShapeIndices.size()) {
            // 直接从全局优先级排序中获取对应深度的图块
            auto [priority, area, idx] = globalShapeIndices[placedCount];
            
            // 检查这个图块是否已被使用
            if (!used[idx]) {
                currentShapeIndex = idx;
            } else {
                // 如果该图块已被使用（理论上不应该发生），找到下一个未使用的
                for (int i = placedCount + 1; i < globalShapeIndices.size(); i++) {
                    auto [nextPriority, nextArea, nextIdx] = globalShapeIndices[i];
                    if (!used[nextIdx]) {
                        currentShapeIndex = nextIdx;
                        break;
                    }
                }
            }
        }
        
        if (currentShapeIndex == -1) {
            std::cout << "❌ [深度" << placedCount << "] 无法找到对应的图块" << std::endl;
            return false;
        }
        
        // 调试：验证深度映射是否正确
        if (placedCount < globalShapeIndices.size()) {
            auto [expectedPriority, expectedArea, expectedIdx] = globalShapeIndices[placedCount];
            if (currentShapeIndex != expectedIdx) {
                std::cout << "⚠️ [深度" << placedCount << "] 映射异常：预期索引" << expectedIdx 
                         << "，实际索引" << currentShapeIndex << std::endl;
            }
        }
        
        auto currentShape = solvingShapes[currentShapeIndex];
        
        // 调试：显示当前深度使用的图块
        if (placedCount <= 5) {
            std::stringstream shapeMsg;
            shapeMsg << "🎯 [深度" << placedCount << "] 使用图块: " << currentShape->getName() << " (ID:" << currentShape->getId() << ")";
            debugPrint(shapeMsg.str());
        }
        // 文件记录所有深度的图块信息
        std::stringstream shapeFileMsg;
        shapeFileMsg << "🎯 [深度" << placedCount << "] 使用图块: " << currentShape->getName() << " (ID:" << currentShape->getId() << ")";
        debugFileOnly(shapeFileMsg.str());
        
        // 尝试当前深度对应的图块在所有位置
        // 只使用配置给定的角度，禁止其他角度
        int fixedRotation = getPanelRotation(currentShape->getId());
        auto pattern = currentShape->getRotatedPattern(fixedRotation);
        
        // 尝试所有可能的放置位置
        int maxX = boardCols - static_cast<int>(pattern[0].size());
        int maxY = boardRows - static_cast<int>(pattern.size());
            
            // 调试：显示当前尝试的图块
            if (placedCount <= 2) {
                std::cout << "🔶 尝视图块: " << currentShape->getName() << " (ID:" << currentShape->getId() << ")" << std::endl;
            }
            
        // 对于调试，输出位置范围信息
        if (currentShape->getName() == "Cross" || placedCount <= 5) {
            std::stringstream rangeMsg;
            rangeMsg << "📍 " << currentShape->getName() << "图块(" << pattern.size() << "x" << pattern[0].size() << ")可尝试位置范围: (0,0) 到 (" << maxX << "," << maxY << "), 共" << ((maxX+1)*(maxY+1)) << "个位置";
            debugPrint(rangeMsg.str());
        }
        // 文件记录所有深度的位置范围信息
        std::stringstream rangeFileMsg;
        rangeFileMsg << "📍 [深度" << placedCount << "] " << currentShape->getName() << "图块(" << pattern.size() << "x" << pattern[0].size() << ")可尝试位置范围: (0,0) 到 (" << maxX << "," << maxY << "), 共" << ((maxX+1)*(maxY+1)) << "个位置";
        debugFileOnly(rangeFileMsg.str());
            
            int successfulPlacements = 0; // 统计成功放置次数
            
            // 尝试该图块的所有位置
            for (int y = 0; y <= maxY; y++) {
                for (int x = 0; x <= maxX; x++) {
                    
                    attempts++;
                    
                    if (attempts % 10 == 0) {
                        auto currentTime = clock();
                        double elapsed = double(currentTime - startTime) / CLOCKS_PER_SEC;
                        //std::cout << "尝试 " << attempts << " 次，已放置 " << placedCount 
                                 // << "/" << solvingShapes.size() << " 个图块，耗时 " 
                                 // << std::fixed << std::setprecision(2) << elapsed << "s" << std::endl;
                        
                        if (solveUpdateCallback) {
                            solveUpdateCallback();
                        }
                        
                        // 防止无限运行，设置时间限制
                        if (elapsed > 600.0) {
                            std::cout << "⏰ 求解时间超过600秒，终止求解" << std::endl;
                            return false;
                        }
                    }
                    
                    if (canPlaceShape(currentShape, x, y, fixedRotation)) {
                        successfulPlacements++;
                        // 对于Cross，输出每次成功放置的位置
                        if (currentShape->getName() == "Cross") {
                            std::cout << "Cross尝试放置在位置 (" << x << "," << y << ") - 成功！" << std::endl;
                        } else if (placedCount <= 2) {
                            std::cout << currentShape->getName() << "尝试放置在位置 (" << x << "," << y << ") - 成功！" << std::endl;
                        }
                        
                        used[currentShapeIndex] = true;
                        placeShape(currentShape, x, y, fixedRotation);
                        
                        // 记录棋盘状态（深度0-4）
                        logBoardState(currentShape->getName(), x, y, placedCount);
                        
                        // 显示递归状态
                        if (placedCount <= 5) {
                            std::stringstream placeMsg;
                            placeMsg << "🔽 [深度" << placedCount << "] " << currentShape->getName() << "在(" << x << "," << y << ")放置成功，递归到深度" << (placedCount + 1) << "求解剩余" << (solvingShapes.size() - placedCount - 1) << "个图块";
                            debugPrint(placeMsg.str());
                        }
                        
                        // 立即更新GUI显示新放置的图块
                        if (solveUpdateCallback) {
                            solveUpdateCallback();
                        }
                        
                        bool recursiveResult = backtrack(placedCount + 1);
                        
                        // 显示递归结果（控制台有限制，文件无限制）
                        if (placedCount <= 5) {
                            std::stringstream returnMsg;
                            returnMsg << "🔼 [深度" << placedCount << "] " << currentShape->getName() << "在(" << x << "," << y << ")递归返回，结果: " << (recursiveResult ? "成功" : "失败");
                            debugPrint(returnMsg.str());
                        }
                        // 文件记录所有深度
                        std::stringstream returnFileMsg;
                        returnFileMsg << "🔼 [深度" << placedCount << "] " << currentShape->getName() << "在(" << x << "," << y << ")递归返回，结果: " << (recursiveResult ? "成功" : "失败");
                        debugFileOnly(returnFileMsg.str());
                        
                        if (recursiveResult) {
                            return true; // 找到解，直接返回
                        }
                        
                        // 输出回溯信息（控制台有限制，文件无限制）
                        if (placedCount <= 5) {
                            std::stringstream backMsg;
                            backMsg << "↩️ [深度" << placedCount << "] " << currentShape->getName() << "在位置 (" << x << "," << y << ") 回溯失败，继续尝试下一位置";
                            debugPrint(backMsg.str());
                        }
                        // 文件记录所有深度
                        std::stringstream backFileMsg;
                        backFileMsg << "↩️ [深度" << placedCount << "] " << currentShape->getName() << "在位置 (" << x << "," << y << ") 回溯失败，继续尝试下一位置";
                        debugFileOnly(backFileMsg.str());
                        
                        // 回溯时使用统一的清理方法，确保状态同步
                        removeShapeById(currentShape->getId());
                        
                        used[currentShapeIndex] = false;
                        
                        // 记录清理后的棋盘状态（所有深度）
                        logBoardState(currentShape->getName() + "(清理后)", x, y, placedCount);
                        
                        // 立即更新GUI显示图块被移除
                        if (solveUpdateCallback) {
                            solveUpdateCallback();
                        }
                        
                        // 显示清理完成状态（控制台有限制，文件无限制）
                        if (placedCount <= 5) {
                            std::stringstream cleanMsg;
                            cleanMsg << "🧹 [深度" << placedCount << "] " << currentShape->getName() << "清理完成，准备尝试下一个位置...";
                            debugPrint(cleanMsg.str());
                        }
                        // 文件记录所有深度
                        std::stringstream cleanFileMsg;
                        cleanFileMsg << "🧹 [深度" << placedCount << "] " << currentShape->getName() << "清理完成，准备尝试下一个位置...";
                        debugFileOnly(cleanFileMsg.str());
                    }
                }
            }
            
            // 调试：显示这个图块的放置尝试结果（控制台有限制，文件无限制）
            if (placedCount <= 5) {
                std::stringstream completeMsg;
                completeMsg << "🏁 [深度" << placedCount << "] " << currentShape->getName() << "所有位置尝试完成，successfulPlacements=" << successfulPlacements << "，无解，返回 false";
                debugPrint(completeMsg.str());
            }
            // 文件记录所有深度
            std::stringstream completeFileMsg;
            completeFileMsg << "🏁 [深度" << placedCount << "] " << currentShape->getName() << "所有位置尝试完成，successfulPlacements=" << successfulPlacements << "，无解，返回 false";
            debugFileOnly(completeFileMsg.str());
            
            // 如果成功放置次数为0，记录一个特殊的棋盘状态，显示为何无法放置
            if (successfulPlacements == 0 && placedCount >= 5) {
                std::stringstream noPlacementMsg;
                noPlacementMsg << "⚠️ [深度" << placedCount << "] " << currentShape->getName() << "无法找到任何有效位置！";
                debugFileOnly(noPlacementMsg.str());
                
                // 记录当前棋盘状态，帮助分析为何无法放置
                std::stringstream boardHeader;
                boardHeader << "🗺 [深度" << placedCount << "] " << currentShape->getName() << "无法放置时的棋盘状态:";
                debugFileOnly(boardHeader.str());
                debugFileOnly("   ");
                for (int boardY = 0; boardY < boardRows; boardY++) {
                    for (int boardX = 0; boardX < boardCols; boardX++) {
                        if (board[boardY][boardX] == nullptr) {
                            debugFile << "□";
                        } else {
                            debugFile << "■";
                        }
                    }
                    if (boardY < boardRows - 1) {
                        debugFile << "\n   ";
                    }
                }
                
                int emptyCount = 0;
                for (int boardY = 0; boardY < boardRows; boardY++) {
                    for (int boardX = 0; boardX < boardCols; boardX++) {
                        if (board[boardY][boardX] == nullptr) emptyCount++;
                    }
                }
                std::stringstream emptyInfo;
                emptyInfo << " (空余: " << emptyCount << "格)\n";
                debugFileOnly(emptyInfo.str());
            }
            
            // 当前深度对应的图块所有位置都尝试失败，返回false
            return false;
    };
    
    solutionFound = backtrack(0);
    
    auto endTime = clock();
    double duration = double(endTime - startTime) / CLOCKS_PER_SEC;
    
    std::cout << "\n=== 求解结果 ===" << std::endl;
    std::cout << "求解成功: " << (solutionFound ? "✅ 是" : "❌ 否") << std::endl;
    std::cout << "总尝试次数: " << attempts << std::endl;
    std::cout << "总用时: " << std::fixed << std::setprecision(3) << duration << " 秒" << std::endl;
    
    // 关闭调试日志文件
    if (debugFile.is_open()) {
        debugFile << "\n=== 求解结果 ===" << std::endl;
        debugFile << "求解成功: " << (solutionFound ? "✅ 是" : "❌ 否") << std::endl;
        debugFile << "总尝试次数: " << attempts << std::endl;
        debugFile << "总用时: " << std::fixed << std::setprecision(3) << duration << " 秒" << std::endl;
        debugFile << "===== 求解结束 =====\n" << std::endl;
        debugFile.close();
    }
    
    if (solutionFound) {
        // 求解成功后进行状态同步，确保棋盘和placedShapes一致
        syncBoardWithPlacedShapes();
        
        std::cout << "\n最终配置验证:" << std::endl;
        std::cout << "已放置图块数: " << placedShapes.size() << "/" << solvingShapes.size() << std::endl;
        
        std::map<std::string, int> usedCountByName;
        std::map<int, bool> usedShapeIds;
        
        for (const auto& ps : placedShapes) {
            usedCountByName[ps.shape->getName()]++;
            usedShapeIds[ps.shapeId] = true;
            std::cout << "  " << ps.shape->getName() << "(ID:" << ps.shapeId << ") 在 (" << ps.x << "," << ps.y 
                     << ") 旋转" << (ps.rotation * 90) << "°" << std::endl;
        }
        
        std::cout << "\n配置数量验证:" << std::endl;
        bool configMatch = true;
        
        for (const auto& [name, required] : requiredCount) {
            int actual = usedCountByName[name];
            std::cout << name << ": 配置要求 " << required << ", 实际使用 " << actual;
            if (required != actual) {
                std::cout << " ❌ 配置不匹配!";
                configMatch = false;
            } else {
                std::cout << " ✅ 完全匹配";
            }
            std::cout << std::endl;
        }
        
        // 额外验证：检查棋盘是否真的完全填满
        bool boardFilled = isBoardCompletelyFilled();
        std::cout << "\n棋盘填满状态: " << (boardFilled ? "✅ 完全填满" : "❌ 未完全填满") << std::endl;
        
        if (!configMatch) {
            std::cout << "❌ 配置验证失败!" << std::endl;
            return false;
        }
        
        if (!boardFilled) {
            std::cout << "❌ 棋盘未完全填满!" << std::endl;
            return false;
        }
        
        std::cout << "✅ 所有验证通过!" << std::endl;
    }
    
    if (solveUpdateCallback) {
        solveUpdateCallback();
    }
    
    // 以棋盘实际填满状态为准，而不是算法的solutionFound标志
    bool boardFilled = isBoardCompletelyFilled();
    bool finalSuccess = boardFilled;  // 只要棋盘填满就算成功
    
    std::cout << "\n🔍 函数返回诊断:" << std::endl;
    std::cout << "solutionFound: " << (solutionFound ? "true" : "false") << std::endl;
    std::cout << "棋盘实际填满: " << (boardFilled ? "true" : "false") << std::endl;
    std::cout << "最终返回值: " << (finalSuccess ? "true" : "false") << std::endl;
    std::cout << "📋 判断逻辑: 以棋盘实际填满状态为准" << std::endl;
    
    return finalSuccess;
}

bool PicnicGame::isBoardCompletelyFilled() const {
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            if (board[i][j] == nullptr && !stones[i][j]) {
                return false;
            }
        }
    }
    return true;
}

int PicnicGame::enumerateFillCombinations(
    const std::function<void(const std::map<std::string, int>&)>& onCombination,
    const std::function<bool()>& shouldStop,
    const std::function<void(int attempts, int found)>& onProgress)
{
    struct Variant {
        std::string name;
        int rotation;  // 0..3
        std::vector<std::vector<bool>> pattern;
        int area = 0;
    };

    auto patternsEqual = [](const std::vector<std::vector<bool>>& a,
                            const std::vector<std::vector<bool>>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (a[i] != b[i]) return false;
        }
        return true;
    };

    std::vector<Variant> variants;
    for (const std::string& name : getAllShapeNames()) {
        auto proto = getShapeByName(name);
        if (!proto) continue;
        for (int rot = 0; rot < 4; ++rot) {
            auto pat = proto->getRotatedPattern(rot);
            if (pat.empty() || pat[0].empty()) continue;
            bool dup = false;
            for (const auto& v : variants) {
                if (v.name == name && patternsEqual(v.pattern, pat)) {
                    dup = true;
                    break;
                }
            }
            if (dup) continue;
            Variant v;
            v.name = name;
            v.rotation = rot;
            v.pattern = std::move(pat);
            for (const auto& row : v.pattern)
                for (bool cell : row)
                    if (cell) v.area++;
            variants.push_back(std::move(v));
        }
    }

    std::sort(variants.begin(), variants.end(),
              [](const Variant& a, const Variant& b) {
                  if (a.area != b.area) return a.area > b.area;
                  if (a.name != b.name) return a.name < b.name;
                  return a.rotation < b.rotation;
              });

    std::vector<std::vector<bool>> occ(boardRows, std::vector<bool>(boardCols, false));
    for (int y = 0; y < boardRows; ++y)
        for (int x = 0; x < boardCols; ++x)
            if (stones[y][x])
                occ[y][x] = true;

    int emptyCount = 0;
    for (int y = 0; y < boardRows; ++y)
        for (int x = 0; x < boardCols; ++x)
            if (!occ[y][x]) emptyCount++;

    if (emptyCount <= 0)
        return 0;

    std::map<std::string, int> currentCombo;
    std::set<std::map<std::string, int>> uniqueCombos;
    int used1x1 = 0;
    int usedLine2 = 0;
    int attempts = 0;
    int found = 0;

    std::function<void()> dfs;
    dfs = [&]() {
        if (shouldStop && shouldStop())
            return;

        int cx = -1, cy = -1;
        for (int y = 0; y < boardRows && cx < 0; ++y) {
            for (int x = 0; x < boardCols; ++x) {
                if (!occ[y][x]) {
                    cx = x;
                    cy = y;
                    break;
                }
            }
        }

        if (cx < 0) {
            if (uniqueCombos.insert(currentCombo).second) {
                ++found;
                if (onCombination)
                    onCombination(currentCombo);
            }
            return;
        }

        for (const Variant& v : variants) {
            if (shouldStop && shouldStop())
                return;
            if (v.name == "1x1" && used1x1 >= 2)
                continue;
            if (v.name == "Line2" && usedLine2 >= 2)
                continue;

            const int ph = static_cast<int>(v.pattern.size());
            const int pw = static_cast<int>(v.pattern[0].size());

            for (int ly = 0; ly < ph; ++ly) {
                for (int lx = 0; lx < pw; ++lx) {
                    if (!v.pattern[ly][lx])
                        continue;
                    const int ox = cx - lx;
                    const int oy = cy - ly;
                    if (ox < 0 || oy < 0 || ox + pw > boardCols || oy + ph > boardRows)
                        continue;

                    bool fits = true;
                    for (int i = 0; i < ph && fits; ++i) {
                        for (int j = 0; j < pw; ++j) {
                            if (!v.pattern[i][j])
                                continue;
                            if (occ[oy + i][ox + j]) {
                                fits = false;
                                break;
                            }
                        }
                    }
                    if (!fits)
                        continue;

                    ++attempts;
                    if (onProgress && (attempts % 5000 == 0))
                        onProgress(attempts, found);

                    for (int i = 0; i < ph; ++i)
                        for (int j = 0; j < pw; ++j)
                            if (v.pattern[i][j])
                                occ[oy + i][ox + j] = true;

                    const std::string key = v.name + "_" + std::to_string(v.rotation * 90);
                    currentCombo[key] += 1;
                    if (v.name == "1x1") ++used1x1;
                    if (v.name == "Line2") ++usedLine2;

                    dfs();

                    if (v.name == "1x1") --used1x1;
                    if (v.name == "Line2") --usedLine2;
                    if (--currentCombo[key] <= 0)
                        currentCombo.erase(key);

                    for (int i = 0; i < ph; ++i)
                        for (int j = 0; j < pw; ++j)
                            if (v.pattern[i][j])
                                occ[oy + i][ox + j] = false;
                }
            }
        }
    };

    dfs();
    if (onProgress)
        onProgress(attempts, found);
    return found;
}

void PicnicGame::updateShapeCount(const std::string& shapeName, int count) {
    // 更新图块数量配置
    auto it = shapeCounts.find(shapeName);
    if (it != shapeCounts.end()) {
        it->second = count;
    }
}

void PicnicGame::updateShapeRotation(const std::string& shapeName, int angle, int count) {
    // 更新图块角度配置
    std::string key = shapeName + "_" + std::to_string(angle);
    
    if (count > 0) {
        shapeRotations[key] = count;
        std::cout << "更新角度配置: " << key << " = " << count << std::endl;
    } else {
        // 如果数量为0，移除该配置
        shapeRotations.erase(key);
        std::cout << "移除角度配置: " << key << std::endl;
    }
}

int PicnicGame::getShapeRotationCount(const std::string& shapeName, int angle) const {
    // 获取指定角度的数量
    if (!shapeRotations.empty()) {
        std::string key = shapeName + "_" + std::to_string(angle);
        auto it = shapeRotations.find(key);
        return (it != shapeRotations.end()) ? it->second : 0;
    }
    
    // 如果没有角度配置，返回0
    return 0;
}

int PicnicGame::getShapeCount(const std::string& shapeName) const {
    // 首先尝试从新的shapeRotations中计算（支持角度配置）
    if (!shapeRotations.empty()) {
        int totalCount = 0;
        // 检查所有角度配置
        for (int angle = 0; angle < 360; angle += 90) {
            std::string key = shapeName + "_" + std::to_string(angle);
            auto it = shapeRotations.find(key);
            if (it != shapeRotations.end()) {
                totalCount += it->second;
            }
        }
        return totalCount;
    }
    
    // 如果没有角度配置，使用旧的shapeCounts
    auto it = shapeCounts.find(shapeName);
    return (it != shapeCounts.end()) ? it->second : 0;
}

std::vector<std::string> PicnicGame::getAllShapeNames() const {
    return {
        "3x3", "Big-L", "2x4", "2x3", "Line4", "Cross",
        "2x2", "L-shape", "L-mirror", "Small-L", "T-shape",
        "Z-shape", "Z-mirror", "Line3", "Line2", "1x1", "C-shape", "Line5", "W-shape", "Big-T", "Wide-L",
        "wideL-mirror", "longL", "longL-mirror", "alloL", "alloL-mirror", "longZ", "longZ-mirror",
        "Big-Z", "Big-Z-mirror"
    };
}

void PicnicGame::rebuildShapes() {
    // 清理所有旧ID：重置全局ID计数器
    resetGlobalShapeIdCounter();
    
    // 清理旧的panelRotations映射，因为ID会重新分配
    panelRotations.clear();
    std::cout << "panelRotations映射已清理" << std::endl;
    
    // 重新初始化图块，将分配新的唯一ID
    initializeShapes();
}

void PicnicGame::resetGlobalShapeIdCounter() {
    globalShapeIdCounter = 0;
    std::cout << "全局图块ID计数器已重置" << std::endl;
}

int PicnicGame::getNextShapeId() {
    return globalShapeIdCounter++;
}

void PicnicGame::syncBoardWithPlacedShapes() {
    // 清空棋盘
    for (int i = 0; i < boardRows; i++) {
        for (int j = 0; j < boardCols; j++) {
            board[i][j] = nullptr;
        }
    }
    
    // 根据placedShapes重新填充棋盘
    for (const auto& ps : placedShapes) {
        auto pattern = ps.shape->getRotatedPattern(ps.rotation);
        for (int i = 0; i < pattern.size(); i++) {
            for (int j = 0; j < pattern[0].size(); j++) {
                if (pattern[i][j]) {
                    int boardY = ps.y + i;
                    int boardX = ps.x + j;
                    if (boardY >= 0 && boardY < boardRows && boardX >= 0 && boardX < boardCols) {
                        board[boardY][boardX] = ps.shape;
                    }
                }
            }
        }
    }
    
    std::cout << "🔄 棋盘状态已同步，共 " << placedShapes.size() << " 个图块" << std::endl;
}

void PicnicGame::findConnectedRegions(std::vector<std::vector<std::pair<int, int>>>& regions) const {
    std::vector<std::vector<bool>> visited(boardRows, std::vector<bool>(boardCols, false));
    regions.clear();
    
    for (int y = 0; y < boardRows; y++) {
        for (int x = 0; x < boardCols; x++) {
            if (isCellFree(x, y) && !visited[y][x]) {
                std::vector<std::pair<int, int>> region;
                std::queue<std::pair<int, int>> q;
                q.push({x, y});
                visited[y][x] = true;
                
                while (!q.empty()) {
                    auto [cx, cy] = q.front();
                    q.pop();
                    region.push_back({cx, cy});
                    
                    // 四个方向的邻居
                    const int dx[] = {0, 1, 0, -1};
                    const int dy[] = {1, 0, -1, 0};
                    
                    for (int i = 0; i < 4; i++) {
                        int nx = cx + dx[i];
                        int ny = cy + dy[i];
                        
                        if (nx >= 0 && nx < boardCols && ny >= 0 && ny < boardRows &&
                            isCellFree(nx, ny) && !visited[ny][nx]) {
                            visited[ny][nx] = true;
                            q.push({nx, ny});
                        }
                    }
                }
                
                regions.push_back(region);
            }
        }
    }
}

bool PicnicGame::canFillAreaWithShapes(int area, const std::vector<int>& shapeAreas) const {
    if (area == 0) return true;
    if (shapeAreas.empty()) return false;
    
    // 使用动态规划判断是否能组合出目标面积
    std::vector<bool> dp(area + 1, false);
    dp[0] = true;
    
    for (int shapeArea : shapeAreas) {
        for (int i = area; i >= shapeArea; i--) {
            if (dp[i - shapeArea]) {
                dp[i] = true;
            }
        }
    }
    
    return dp[area];
}