#include <SFML/Graphics.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <thread>
#include <mutex>
#include <map>
#include <memory>
#include <fstream>
#include <tuple>
#include <functional>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace std;
using namespace sf;

// 8x8游戏板
const int BOARD_SIZE = 8;
const int CELL_SIZE = 60;
const int WINDOW_WIDTH = 1800;
const int WINDOW_HEIGHT = 1000;
const int PREVIEW_CELL_SIZE = 30;
const int EDITOR_PREVIEW_SIZE = 200;

vector<vector<int>> board(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
vector<vector<int>> solutionBoard(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
bool solved = false;
bool showSolution = false;
mutex boardMutex;

// 求解计时器相关（全局变量，供drawBoard访问）
bool solving = false;  // 是否正在求解
bool solutionFound = false;  // 是否找到解
Clock solveTimer;  // 求解计时器
float solveTime = 0.0f;  // 求解时间（秒）
thread* solveThread = nullptr;  // 求解线程
bool solveTimeout = false;  // 求解超时标志
int solveCheckCount = 0;  // 求解调用计数器（用于超时检查）
float estimatedSolveTime = 120.0f;  // 预估求解时间（秒），默认120秒

// 图块数量编辑器相关
struct PieceCount {
    int pieceId;
    int count;
    int currentShapeIndex;
};

vector<PieceCount> pieceCounts;  // 每种图块的数量
bool showEditor = false;  // 是否显示编辑器
int selectedPieceType = -1;  // 选中的图块类型（用于编辑器）
int previewPieceType = -1;  // 预览的图块类型（用于编辑器示意图）

// 拖拽相关
struct DraggedPiece {
    int pieceId;
    int shapeIndex;
    int originalRow;
    int originalCol;
    bool isDragging;
    Vector2i dragOffset;
};

DraggedPiece draggedPiece = {-1, 0, -1, -1, false, {0, 0}};
bool isRotating = false;  // 是否正在旋转（鼠标右键按住时）

// 编辑器拖拽相关
struct EditorDrag {
    bool isDragging;
    Vector2i dragOffset;
    int editorX;
    int editorY;
};

EditorDrag editorDrag = {false, {0, 0}, 50, BOARD_SIZE * CELL_SIZE + 100};

// 图块纹理资源
struct PieceTexture {
    int pieceId;
    Texture texture;
    bool loaded;
};

vector<PieceTexture> pieceTextures;

// 图块结构
struct Piece {
    string name;
    vector<vector<pair<int, int>>> shapes;
    int id;
    Color color;
};

vector<Piece> pieces;
vector<pair<int, int>> piecePositions;

// 颜色数组
Color colors[] = {
    Color(255, 100, 100),   // 红色
    Color(100, 255, 100),   // 绿色
    Color(100, 100, 255),   // 蓝色
    Color(255, 255, 100),   // 黄色
    Color(255, 100, 255),   // 品红
    Color(100, 255, 255),   // 青色
    Color(255, 165, 0),     // 橙色
    Color(128, 0, 128),     // 紫色
    Color(255, 192, 203),   // 粉色
    Color(144, 238, 144),   // 浅绿
    Color(173, 216, 230),   // 浅蓝
    Color(255, 218, 185),   // 桃色
    Color(221, 160, 221),   // 梅色
    Color(152, 251, 152),   // 浅绿2
    Color(255, 228, 196),   // 米色
    Color(176, 224, 230)    // 粉蓝
};

// 旋转图块形状（90度顺时针）
vector<pair<int, int>> rotateShape(const vector<pair<int, int>>& shape) {
    vector<pair<int, int>> rotated;
    for (const auto& cell : shape) {
        // 90度顺时针旋转：(x, y) -> (y, -x)
        rotated.push_back({cell.second, -cell.first});
    }
    // 归一化到原点
    int minRow = rotated[0].first, minCol = rotated[0].second;
    for (const auto& cell : rotated) {
        minRow = min(minRow, cell.first);
        minCol = min(minCol, cell.second);
    }
    for (auto& cell : rotated) {
        cell.first -= minRow;
        cell.second -= minCol;
    }
    return rotated;
}

// 初始化所有图块定义
// 重要说明：图块形状定义中的坐标是相对于基准点（baseRow, baseCol）的偏移量
// - 基准点是图块的参考点，不一定是图块占据的第一个单元格
// - 如果形状在(0,0)位置为空（例如 cross 形状），基准点位置可以放置其他图块
// - 所有形状定义都应该归一化到至少有一个单元格的行或列为0，但不要求(0,0)位置必须被占据
// - 放置图块时，实际单元格位置 = (baseRow + cell.first, baseCol + cell.second)
void initializePieces() {
    pieces.clear();
    piecePositions.clear();
    
    int colorIndex = 0;
    
    // 1. 3x3 正方形
    pieces.push_back({
        "3x3",
        {{{0,0}, {0,1}, {0,2}, {1,0}, {1,1}, {1,2}, {2,0}, {2,1}, {2,2}}},
        1,
        colors[colorIndex++]
    });
    
    // 2. 3x3L (横向减少两格，从7格变为5格)
    pieces.push_back({
        "3x3L",
        {{{0,0}, {1,0}, {2,0}, {2,1}, {2,2}},
         {{0,0}, {0,1}, {0,2}, {1,0}, {2,0}},
         {{0,0}, {0,1}, {0,2}, {1,2}, {2,2}},
         {{0,2}, {1,2}, {2,0}, {2,1}, {2,2}}},
        2,
        colors[colorIndex++]
    });
    
    // 3. 2x4
    pieces.push_back({
        "2x4",
        {{{0,0}, {0,1}, {0,2}, {0,3}, {1,0}, {1,1}, {1,2}, {1,3}},
         {{0,0}, {0,1}, {1,0}, {1,1}, {2,0}, {2,1}, {3,0}, {3,1}},
         {{0,0}, {0,1}, {0,2}, {0,3}, {1,0}, {1,1}, {1,2}, {1,3}},
         {{0,0}, {0,1}, {1,0}, {1,1}, {2,0}, {2,1}, {3,0}, {3,1}}},
        3,
        colors[colorIndex++]
    });
    
    // 4. 2x3
    pieces.push_back({
        "2x3",
        {{{0,0}, {0,1}, {0,2}, {1,0}, {1,1}, {1,2}},
         {{0,0}, {1,0}, {2,0}, {0,1}, {1,1}, {2,1}},
         {{0,0}, {0,1}, {0,2}, {1,0}, {1,1}, {1,2}},
         {{0,0}, {1,0}, {2,0}, {0,1}, {1,1}, {2,1}}},
        4,
        colors[colorIndex++]
    });
    
    // 5. L-shape (第二列的格子下一行)
    // 基础形状（0度）：L形状
    vector<pair<int, int>> lShapeBase = {{0,0}, {1,0}, {2,0}, {2,1}};
    vector<pair<int, int>> lShape90 = rotateShape(lShapeBase);  // 90度
    vector<pair<int, int>> lShape180 = rotateShape(lShape90);   // 180度
    vector<pair<int, int>> lShape270 = rotateShape(lShape180);  // 270度
    pieces.push_back({
        "L-shape",
        {lShapeBase, lShape90, lShape180, lShape270},
        5,
        colors[colorIndex++]
    });
    
    // 6. L-mirror (L-shape的水平镜像)
    // L-shape基础：{{0,0}, {1,0}, {2,0}, {2,1}} - 垂直L，底部向右（L形状）
    // L-mirror基础：{{0,1}, {1,1}, {2,0}, {2,1}} - 垂直L，底部向左（┘形状）
    // 这是 L-shape 的水平镜像，不能通过旋转 L-shape 得到
    vector<pair<int, int>> lMirrorBase = {{0,1}, {1,1}, {2,0}, {2,1}};
    vector<pair<int, int>> lMirror90 = rotateShape(lMirrorBase);   // 90度
    vector<pair<int, int>> lMirror180 = rotateShape(lMirror90);    // 180度
    vector<pair<int, int>> lMirror270 = rotateShape(lMirror180);   // 270度
    pieces.push_back({
        "L-mirror",
        {lMirrorBase, lMirror90, lMirror180, lMirror270},
        6,
        colors[colorIndex++]
    });
    
    // 7. L3
    pieces.push_back({
        "L3",
        {{{0,1}, {1,0}, {1,1}},
         {{0,0}, {1,0}, {1,1}},
         {{0,0}, {0,1}, {1,0}},
         {{0,0}, {0,1}, {1,1}}},
        7,
        colors[colorIndex++]
    });
    
    // 8. Z-mirror
    pieces.push_back({
        "Z-mirror",
        {{{0,0}, {0,1}, {1,1}, {1,2}},
         {{0,1}, {1,0}, {1,1}, {2,0}},
         {{0,0}, {0,1}, {1,1}, {1,2}},
         {{0,1}, {1,0}, {1,1}, {2,0}}},
        8,
        colors[colorIndex++]
    });
    
    // 9. Z-shape (Z-mirror的镜像，水平翻转)
    pieces.push_back({
        "Z-shape",
        {{{0,1}, {0,2}, {1,0}, {1,1}},  
         {{0,0}, {1,0}, {1,1}, {2,1}},
         {{0,1}, {0,2}, {1,0}, {1,1}},
         {{0,0}, {1,0}, {1,1}, {2,1}}},
        9,
        colors[colorIndex++]
    });
    
    // 10. line4
    pieces.push_back({
        "line4",
        {{{0,0}, {0,1}, {0,2}, {0,3}},
         {{0,0}, {1,0}, {2,0}, {3,0}}},
        10,
        colors[colorIndex++]
    });
    
    // 11. cross
    pieces.push_back({
        "cross",
        {{{0,1}, {1,0}, {1,1}, {1,2}, {2,1}}},
        11,
        colors[colorIndex++]
    });
    
    // 12. T-shape
    pieces.push_back({
        "T-shape",
        {{{0,0}, {0,1}, {0,2}, {1,1}},
         {{0,2}, {1,1}, {1,2}, {2,2}},
         {{1,1}, {2,0}, {2,1}, {2,2}},
         {{0,0}, {1,0}, {1,1}, {2,0}}},
        12,
        colors[colorIndex++]
    });
    
    // 13. line3
    pieces.push_back({
        "line3",
        {{{0,0}, {0,1}, {0,2}},
         {{0,0}, {1,0}, {2,0}},
         {{0,0}, {0,1}, {0,2}},
         {{0,0}, {1,0}, {2,0}}},
        13,
        colors[colorIndex++]
    });
    
    // 14. 1x1-1
    pieces.push_back({
        "1x1-1",
        {{{0,0}}},
        14,
        colors[colorIndex++]
    });
    
    // 15. line2
    pieces.push_back({
        "line2",
        {{{0,0}, {0,1}},
         {{0,0}, {1,0}},
         {{0,0}, {0,1}},
         {{0,0}, {1,0}}},
        15,
        colors[colorIndex++]
    });
    
    piecePositions.resize(pieces.size());
    
    // 初始化图块数量（根据用户要求设置，保证有解的组合）
    pieceCounts.clear();
    // 为每个图块设置初始数量
    for (const auto& piece : pieces) {
        int count = 0;  // 默认0个
        if (piece.name == "3x3") count = 1;
        else if (piece.name == "3x3L") count = 1;
        else if (piece.name == "2x4") count = 1;
        else if (piece.name == "2x3") count = 1;
        else if (piece.name == "L-shape") count = 2;
        else if (piece.name == "L-mirror") count = 2;
        else if (piece.name == "Z-shape") count = 1;
        else if (piece.name == "line4") count = 1;
        else if (piece.name == "cross") count = 1;
        else if (piece.name == "line3") count = 1;
        else if (piece.name == "line2") count = 1;
        else if (piece.name == "1x1-1") count = 2;
        // 其他图块（如Z-mirror、T-shape、L3等）默认为0
        pieceCounts.push_back({piece.id, count, 0});
    }
}

// 预估求解时间（根据图块组合的复杂度）
float estimateSolveTime(const vector<PieceCount>& counts) {
    int totalPieces = 0;
    int totalCells = 0;
    int largePieceCount = 0;  // 大图块数量（>=5格）
    int smallPieceCount = 0;  // 小图块数量（1-2格）
    int crossCount = 0;  // cross图块数量（搜索空间大）
    
    for (const auto& pc : counts) {
        if (pc.count == 0) continue;
        
        // 找到对应的图块
        const Piece* piece = nullptr;
        for (const auto& p : pieces) {
            if (p.id == pc.pieceId) {
                piece = &p;
                break;
            }
        }
        
        if (!piece || piece->shapes.empty()) continue;
        
        int pieceSize = (int)piece->shapes[0].size();
        int count = pc.count;
        
        totalPieces += count;
        totalCells += pieceSize * count;
        
        if (piece->name == "cross") {
            crossCount += count;
            largePieceCount += count;
        } else if (pieceSize >= 5) {
            largePieceCount += count;
        } else if (pieceSize <= 2) {
            smallPieceCount += count;
        }
    }
    
    // 基础时间：根据总图块数量
    float baseTime = 5.0f + totalPieces * 0.5f;
    
    // 大图块增加时间（搜索空间大）
    baseTime += largePieceCount * 15.0f;
    
    // cross图块特别耗时（搜索空间很大）
    baseTime += crossCount * 20.0f;
    
    // 小图块（1x1）很多时，如果其他图块少，可能很快
    if (smallPieceCount > 30 && largePieceCount <= 4) {
        baseTime = max(baseTime, 30.0f);  // 至少30秒
    }
    
    // 确保最小时间
    baseTime = max(baseTime, 30.0f);
    
    // 最大时间限制（避免过长）
    baseTime = min(baseTime, 300.0f);  // 最多5分钟
    
    return baseTime;
}

// 设置测试用例：4个cross和44个1x1（总计：4×5 + 44×1 = 20 + 44 = 64格）
void setTestCase1() {
    pieceCounts.clear();
    for (const auto& piece : pieces) {
        int count = 0;
        if (piece.name == "cross") count = 4;
        else if (piece.name == "1x1-1") count = 44;
        pieceCounts.push_back({piece.id, count, 0});
    }
}

// 设置测试用例2：15个L-shape和4个1x1（总计：15×4 + 4×1 = 60 + 4 = 64格）
void setTestCase2() {
    pieceCounts.clear();
    for (const auto& piece : pieces) {
        int count = 0;
        if (piece.name == "L-shape") count = 15;
        else if (piece.name == "1x1-1") count = 4;
        pieceCounts.push_back({piece.id, count, 0});
    }
}

// 检查图块是否可以放置在指定位置
// 参数说明：
//   - shape: 图块形状，坐标是相对于基准点(row, col)的偏移量
//   - row, col: 基准点（reference point），不一定是图块占据的第一个单元格
//   - 注意：如果形状在(0,0)位置为空，基准点位置可以放置其他图块
//   - ignoreRow, ignoreCol: 忽略的位置（通常是正在拖拽的图块的原始位置）
bool canPlace(const vector<pair<int, int>>& shape, int row, int col, 
              int ignoreRow = -1, int ignoreCol = -1) {
    // 只检查形状中实际定义的单元格，不检查基准点本身
    // 如果形状在(0,0)位置为空，基准点位置不会被占用，可以放置其他图块
    for (const auto& cell : shape) {
        int newRow = row + cell.first;
        int newCol = col + cell.second;
        
        if (newRow < 0 || newRow >= BOARD_SIZE || 
            newCol < 0 || newCol >= BOARD_SIZE) {
            return false;
        }
        
        // 如果这个位置是被忽略的位置（通常是正在拖拽的图块的原始位置），允许放置
        if (newRow == ignoreRow && newCol == ignoreCol) {
            continue;
        }
        
        if (board[newRow][newCol] != 0) {
            return false;
        }
    }
    return true;
}

// 将图块放置到棋盘上
// 参数说明：
//   - shape: 图块形状，坐标是相对于基准点(row, col)的偏移量
//   - row, col: 基准点（reference point），不一定是图块占据的第一个单元格
//   - 注意：只放置形状中实际定义的单元格，如果形状在(0,0)位置为空，基准点位置不会被占用
void placePiece(const vector<pair<int, int>>& shape, int row, int col, int id) {
    // 只放置形状中实际定义的单元格，不占用基准点本身
    for (const auto& cell : shape) {
        board[row + cell.first][col + cell.second] = id;
    }
}

// 应用已知的解：4个cross和44个1x1的测试用例解

// 从棋盘上移除图块
// 参数说明：
//   - shape: 图块形状，坐标是相对于基准点(row, col)的偏移量
//   - row, col: 基准点（reference point），不一定是图块占据的第一个单元格
//   - 注意：只移除形状中实际定义的单元格，不处理基准点本身
void removePiece(const vector<pair<int, int>>& shape, int row, int col) {
    // 只移除形状中实际定义的单元格
    for (const auto& cell : shape) {
        board[row + cell.first][col + cell.second] = 0;
    }
}

// 获取图块在游戏板上的位置
pair<int, int> getPiecePosition(int pieceId) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (board[i][j] == pieceId) {
                return {i, j};
            }
        }
    }
    return {-1, -1};
}

// 移除游戏板上的指定图块
void removePieceFromBoard(int pieceId) {
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (board[i][j] == pieceId) {
                board[i][j] = 0;
            }
        }
    }
}

// 计算每个图块类型的已使用数量（每个图块实例只计数一次）
vector<int> calculateUsedPieceCounts() {
    vector<int> usedCounts(pieces.size(), 0);
    vector<vector<bool>> counted(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    
        for (int row = 0; row < BOARD_SIZE; row++) {
            for (int col = 0; col < BOARD_SIZE; col++) {
            if (board[row][col] != 0 && !counted[row][col]) {
                // 找到这个格子所属的图块ID
                int pieceId = board[row][col];
                
                // 找到对应的图块定义
                const Piece* piece = nullptr;
                size_t pieceIndex = 0;
                for (size_t i = 0; i < pieces.size(); i++) {
                    if (pieces[i].id == pieceId) {
                        piece = &pieces[i];
                        pieceIndex = i;
                        break;
                    }
                }
                
                if (piece) {
                    // 找到这个图块的所有格子（通过DFS找到所有相邻的同ID格子）
                    vector<pair<int, int>> pieceCells;
                    vector<vector<bool>> visited(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
                    
                    // 使用DFS找到所有相连的格子
                    function<void(int, int)> dfs = [&](int r, int c) {
                        if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return;
                        if (visited[r][c] || board[r][c] != pieceId) return;
                        
                        visited[r][c] = true;
                        pieceCells.push_back({r, c});
                        
                        // 检查四个方向的相邻格子
                        dfs(r - 1, c);
                        dfs(r + 1, c);
                        dfs(r, c - 1);
                        dfs(r, c + 1);
                    };
                    
                    dfs(row, col);
                    
                    // 标记所有属于这个图块的格子为已计数
                    for (const auto& cell : pieceCells) {
                        counted[cell.first][cell.second] = true;
                    }
                    
                    // 这个图块实例计数+1
                    usedCounts[pieceIndex]++;
                }
            }
        }
    }
    
    return usedCounts;
}

// 前向声明
void drawPieceTexture(RenderWindow& window, const Piece& piece, int shapeIndex,
                     int baseRow, int baseCol, int offsetX, int offsetY, int cellSize,
                     vector<vector<bool>>& drawn);

// 绘制单个图块形状（用于预览和编辑器，使用贴图）
void drawPieceShape(RenderWindow& window, const Piece& piece, int shapeIndex,
                    int offsetX, int offsetY, int cellSize, 
                    bool useTexture = true) {
    if (shapeIndex < 0 || shapeIndex >= (int)piece.shapes.size()) return;
    
    const auto& shape = piece.shapes[shapeIndex];
    
    // 尝试使用贴图
    if (useTexture) {
        Texture* tex = nullptr;
        for (auto& pt : pieceTextures) {
            if (pt.pieceId == piece.id && pt.loaded) {
                tex = &pt.texture;
                break;
            }
        }
        
        if (tex) {
            // 计算图块的边界
            int minRow = shape[0].first, maxRow = shape[0].first;
            int minCol = shape[0].second, maxCol = shape[0].second;
            for (const auto& cell : shape) {
                minRow = min(minRow, cell.first);
                maxRow = max(maxRow, cell.first);
                minCol = min(minCol, cell.second);
                maxCol = max(maxCol, cell.second);
            }
            
            int shapeWidth = (maxCol - minCol + 1) * cellSize;
            int shapeHeight = (maxRow - minRow + 1) * cellSize;
            
            // 使用RenderTexture绘制完整形状
            // 缓存键包含cellSize，以便不同尺寸可以正确缓存
            static map<tuple<int, int, int>, unique_ptr<RenderTexture>> previewCache;
            tuple<int, int, int> cacheKey = {piece.id, shapeIndex, cellSize};
            
            if (previewCache.find(cacheKey) == previewCache.end()) {
                auto renderTex = make_unique<RenderTexture>();
                renderTex->create(shapeWidth, shapeHeight);
                renderTex->clear(Color::Transparent);
                
                Sprite sprite(*tex);
                // 根据shapeIndex旋转贴图（假设shapes是按旋转顺序排列的）
                float rotation = shapeIndex * 90.0f;
                sprite.setRotation(rotation);
                
                // 计算旋转后的中心点
                float centerX = tex->getSize().x / 2.0f;
                float centerY = tex->getSize().y / 2.0f;
                sprite.setOrigin(centerX, centerY);
                
                // 计算缩放和位置，使贴图充满形状区域
                // 如果旋转了90度或270度，纹理的宽高在视觉上会交换
                float scaleX, scaleY;
                if ((int)rotation % 180 == 90) {
                    // 旋转90/270度：形状宽度对应纹理高度，形状高度对应纹理宽度
                    scaleX = (float)shapeWidth / tex->getSize().y;
                    scaleY = (float)shapeHeight / tex->getSize().x;
                    // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                    float uniformScale = max(scaleX, scaleY);
                    scaleX = uniformScale;
                    scaleY = uniformScale;
                } else {
                    // 0度或180度：正常对应
                    scaleX = (float)shapeWidth / tex->getSize().x;
                    scaleY = (float)shapeHeight / tex->getSize().y;
                    // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                    float uniformScale = max(scaleX, scaleY);
                    scaleX = uniformScale;
                    scaleY = uniformScale;
                }
                sprite.setScale(scaleX, scaleY);
                
                // 设置位置到形状中心
                sprite.setPosition(shapeWidth / 2.0f, shapeHeight / 2.0f);
                renderTex->draw(sprite);
                
                renderTex->display();
                previewCache[cacheKey] = move(renderTex);
            }
            
            // 绘制到窗口
            Sprite finalSprite(previewCache[cacheKey]->getTexture());
            finalSprite.setPosition(offsetX + minCol * cellSize,
                                  offsetY + minRow * cellSize);
            window.draw(finalSprite);
            return;
        }
    }
    
    // 回退到颜色填充
    for (const auto& cell : shape) {
        RectangleShape rect(Vector2f(cellSize - 2, cellSize - 2));
        rect.setPosition(offsetX + cell.second * cellSize + 1, 
                        offsetY + cell.first * cellSize + 1);
        rect.setFillColor(piece.color);
        rect.setOutlineThickness(1);
        rect.setOutlineColor(Color::Black);
        window.draw(rect);
    }
}

// 绘制图块编辑器
void drawPieceEditor(RenderWindow& window, Font& font) {
    if (!showEditor) return;
    
    int editorX = editorDrag.editorX;
    int editorY = editorDrag.editorY;
    int editorWidth = WINDOW_WIDTH - 100;
    int editorHeight = min(400, WINDOW_HEIGHT - editorY - 50);
    
    // 编辑器背景
    RectangleShape bg(Vector2f(editorWidth, editorHeight));
    bg.setPosition(editorX, editorY);
    bg.setFillColor(Color(250, 250, 250));
    bg.setOutlineThickness(2);
    bg.setOutlineColor(Color::Black);
    window.draw(bg);
    
    bool fontAvailable = font.getInfo().family != "";
    int currentY = editorY + 20;
    
    if (fontAvailable) {
        Text title("Piece Count Editor", font, 24);
        title.setPosition(editorX + 20, currentY);
        title.setFillColor(Color::Black);
        window.draw(title);
        currentY += 40;
    }
    
    // 图块列表（左侧）
    int itemsPerRow = 5;
    int itemWidth = 150;
    int itemHeight = 80;
    int startX = editorX + 20;
    int startY = currentY;
    
    // 计算右侧区域的位置（示意图、数量修改、确认按钮）
    int rightAreaX = startX + itemsPerRow * itemWidth + 40;  // 图块列表右侧
    int rightAreaY = currentY;
    
    for (size_t i = 0; i < pieces.size(); i++) {
        int col = i % itemsPerRow;
        int row = i / itemsPerRow;
        int x = startX + col * itemWidth;
        int y = startY + row * itemHeight;
        
        // 选中高亮
        if (selectedPieceType == (int)i) {
            RectangleShape highlight(Vector2f(itemWidth - 10, itemHeight - 10));
            highlight.setPosition(x - 5, y - 5);
            highlight.setFillColor(Color(200, 220, 255));
            highlight.setOutlineThickness(2);
            highlight.setOutlineColor(Color::Blue);
            window.draw(highlight);
        }
        
        // 绘制图块预览（放大四倍，使用贴图）
        int previewSize = min(160, itemHeight - 30);  // 从40放大到160（四倍）
        int previewX = x + (itemWidth - previewSize) / 2;
        int previewY = y;
        
        if (!pieces[i].shapes.empty()) {
            int maxDim = 0;
            for (const auto& cell : pieces[i].shapes[0]) {
                maxDim = max(maxDim, max(cell.first, cell.second));
            }
            int cellSize = maxDim > 0 ? previewSize / (maxDim + 1) : previewSize / 3;
            drawPieceShape(window, pieces[i], 0, previewX, previewY, cellSize, true);
        }
        
        // 图块名称和数量
        if (fontAvailable) {
            Text name(pieces[i].name, font, 18);
            name.setPosition(x, y + previewSize + 5);
            name.setFillColor(Color::Black);
            window.draw(name);
            
            // 数量显示
            int count = 0;
            for (const auto& pc : pieceCounts) {
                if (pc.pieceId == pieces[i].id) {
                    count = pc.count;
                    break;
                }
            }
            string countStr = "x" + to_string(count);
            Text countText(countStr, font, 18);
            countText.setPosition(x + itemWidth - 50, y + previewSize + 5);
            countText.setFillColor(Color::Black);
            window.draw(countText);
        }
    }
    
    // 右侧区域：示意图、数量修改、确认按钮
    int rightCurrentY = rightAreaY;
    
    // 图块示意图（公用区域，在右侧顶部）
    if (selectedPieceType >= 0 && selectedPieceType < (int)pieces.size() &&
        previewPieceType >= 0 && previewPieceType < (int)pieces.size()) {
        int previewAreaX = rightAreaX;
        int previewAreaY = rightCurrentY;
        int previewAreaSize = EDITOR_PREVIEW_SIZE;  // 固定大小为 200x200
        
        // 绘制固定大小的预览区域背景
        RectangleShape previewBg(Vector2f(previewAreaSize, previewAreaSize));
        previewBg.setPosition(previewAreaX, previewAreaY);
        previewBg.setFillColor(Color::White);
        previewBg.setOutlineThickness(2);
        previewBg.setOutlineColor(Color::Black);
        window.draw(previewBg);
        
        if (fontAvailable) {
            Text previewLabel("Piece Preview", font, 18);
            previewLabel.setPosition(previewAreaX, previewAreaY - 20);
            previewLabel.setFillColor(Color::Black);
            window.draw(previewLabel);
        }
        
        // 计算图块尺寸，并在预览区域内居中显示
        const auto& previewPiece = pieces[previewPieceType];
        if (!previewPiece.shapes.empty() && 
            pieceCounts[previewPieceType].currentShapeIndex < (int)previewPiece.shapes.size()) {
            const auto& shape = previewPiece.shapes[pieceCounts[previewPieceType].currentShapeIndex];
            
            // 计算图块的实际尺寸（宽度和高度）
            int minRow = shape[0].first, maxRow = shape[0].first;
            int minCol = shape[0].second, maxCol = shape[0].second;
            for (const auto& cell : shape) {
                minRow = min(minRow, cell.first);
                maxRow = max(maxRow, cell.first);
                minCol = min(minCol, cell.second);
                maxCol = max(maxCol, cell.second);
            }
            int shapeWidth = maxCol - minCol + 1;
            int shapeHeight = maxRow - minRow + 1;
            
            // 使用CELL_SIZE作为单元格大小（与游戏板一致）
            int previewCellSize = CELL_SIZE;
            int piecePixelWidth = shapeWidth * previewCellSize;
            int piecePixelHeight = shapeHeight * previewCellSize;
            
            // 计算居中位置（图块边界框的左上角）
            int centerX = previewAreaX + (previewAreaSize - piecePixelWidth) / 2;
            int centerY = previewAreaY + (previewAreaSize - piecePixelHeight) / 2;
            
            // drawPieceShape内部会将offsetX/offsetY加上minCol*cellSize和minRow*cellSize
            // 所以我们需要减去这些偏移，使得最终绘制位置是我们想要的居中位置
            int offsetX = centerX - minCol * previewCellSize;
            int offsetY = centerY - minRow * previewCellSize;
            
            // 绘制图块，在预览区域内居中显示
            drawPieceShape(window, previewPiece, pieceCounts[previewPieceType].currentShapeIndex,
                          offsetX, offsetY, previewCellSize, true);
        }
        
        rightCurrentY += previewAreaSize + 30;
        
        // 数量选择区域（在示意图下方）
        if (fontAvailable) {
            Text label("Count: ", font, 16);
            label.setPosition(previewAreaX, rightCurrentY);
            label.setFillColor(Color::Black);
            window.draw(label);
            
            // 减号按钮
            RectangleShape minusBtn(Vector2f(30, 30));
            minusBtn.setPosition(previewAreaX + 80, rightCurrentY);
            minusBtn.setFillColor(Color(200, 200, 200));
            minusBtn.setOutlineThickness(1);
            minusBtn.setOutlineColor(Color::Black);
            window.draw(minusBtn);
            
            Text minusText("-", font, 20);
            minusText.setPosition(previewAreaX + 90, rightCurrentY);
            minusText.setFillColor(Color::Black);
            window.draw(minusText);
            
            // 数量显示
            int count = 0;
            for (auto& pc : pieceCounts) {
                if (pc.pieceId == pieces[selectedPieceType].id) {
                    count = pc.count;
                    break;
                }
            }
            Text countDisplay(to_string(count), font, 16);
            countDisplay.setPosition(previewAreaX + 120, rightCurrentY + 5);
            countDisplay.setFillColor(Color::Black);
            window.draw(countDisplay);
            
            // 加号按钮
            RectangleShape plusBtn(Vector2f(30, 30));
            plusBtn.setPosition(previewAreaX + 160, rightCurrentY);
            plusBtn.setFillColor(Color(200, 200, 200));
            plusBtn.setOutlineThickness(1);
            plusBtn.setOutlineColor(Color::Black);
            window.draw(plusBtn);
            
            Text plusText("+", font, 20);
            plusText.setPosition(previewAreaX + 170, rightCurrentY);
            plusText.setFillColor(Color::Black);
            window.draw(plusText);
        }
        
        rightCurrentY += 50;
        
        // 确认修改按钮（放在数量修改下方）
        RectangleShape confirmBtn(Vector2f(120, 35));
        confirmBtn.setPosition(previewAreaX+35, rightCurrentY);
        confirmBtn.setFillColor(Color(100, 200, 100));
        confirmBtn.setOutlineThickness(2);
        confirmBtn.setOutlineColor(Color::Black);
        window.draw(confirmBtn);
        
        if (fontAvailable) {
            Text confirmText("Confirm Changes", font, 16);
            confirmText.setPosition(previewAreaX + 40, rightCurrentY + 5);
            confirmText.setFillColor(Color::White);
            window.draw(confirmText);
        }
    }
}

// 绘制图块预选区（放在游戏区下方）
void drawPiecePreviewArea(RenderWindow& window, Font& font) {
    int boardOffsetX = 50;
    int boardOffsetY = 50;
    int previewX = boardOffsetX;
    int previewY = boardOffsetY + BOARD_SIZE * CELL_SIZE + 20;
    int previewWidth = BOARD_SIZE * CELL_SIZE;
    int previewHeight = 200;
    
    // 预选区背景
    RectangleShape bg(Vector2f(previewWidth, previewHeight));
    bg.setPosition(previewX, previewY);
    bg.setFillColor(Color(245, 245, 245));
    bg.setOutlineThickness(2);
    bg.setOutlineColor(Color::Black);
    window.draw(bg);
    
    bool fontAvailable = font.getInfo().family != "";
    if (fontAvailable) {
        Text title("Piece Preview Area", font, 20);
        title.setPosition(previewX + 10, previewY + 10);
        title.setFillColor(Color::Black);
        window.draw(title);
    }
    
    // 显示所有可用的图块（未使用的）
    int currentY = previewY + 40;
    int itemsPerRow = 8;  // 增加每行数量以适应游戏区宽度
    int itemSize = (previewWidth - 40) / itemsPerRow - 10;
    int spacing = 10;
    
    // 计算每个图块类型的已使用数量（每个图块实例只计数一次）
    vector<int> usedCounts = calculateUsedPieceCounts();
    
    // 显示所有可用的图块（根据pieceCounts和已使用数量）
    int displayIndex = 0;
    for (size_t i = 0; i < pieces.size(); i++) {
        // 获取该图块类型的总数量
        int totalCount = 0;
        for (const auto& pc : pieceCounts) {
            if (pc.pieceId == pieces[i].id) {
                totalCount = pc.count;
                break;
            }
        }
        
        // 计算可用数量（总数量 - 已使用数量）
        int availableCount = totalCount - usedCounts[i];
        
        // 显示所有可用的图块实例
        for (int instance = 0; instance < availableCount; instance++) {
        
            int col = (displayIndex % itemsPerRow);
            int row = (displayIndex / itemsPerRow);
            int x = previewX + 20 + col * (itemSize + spacing);
            int y = currentY + row * (itemSize + spacing);
            
            // 绘制图块（使用贴图）
            if (!pieces[i].shapes.empty()) {
                int maxDim = 0;
                for (const auto& cell : pieces[i].shapes[0]) {
                    maxDim = max(maxDim, max(cell.first, cell.second));
                }
                int cellSize = maxDim > 0 ? itemSize / (maxDim + 1) : itemSize / 3;
                drawPieceShape(window, pieces[i], 0, x, y, cellSize, true);
            }
            
            displayIndex++;
        }
    }
}

// 计算指定pieceId的已放置实例数（使用DFS精确计算）
int countPlacedInstances(int pieceId) {
    int instanceCount = 0;
    vector<vector<bool>> counted(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    
    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            if (board[row][col] == pieceId && !counted[row][col]) {
                // 使用DFS找到所有相连的格子（属于同一个图块实例）
                vector<vector<bool>> visited(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
                
                function<void(int, int)> dfs = [&](int r, int c) {
                    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return;
                    if (visited[r][c] || board[r][c] != pieceId) return;
                    
                    visited[r][c] = true;
                    counted[r][c] = true;
                    
                    // 检查四个方向的相邻格子
                    dfs(r - 1, c);
                    dfs(r + 1, c);
                    dfs(r, c - 1);
                    dfs(r, c + 1);
                };
                
                dfs(row, col);
                instanceCount++;
            }
        }
    }
    
    return instanceCount;
}

// 获取指定pieceId的所有已放置实例的位置和形状信息
struct PieceInstance {
    int baseRow;
    int baseCol;
    int shapeIndex;
    set<pair<int, int>> cells;
};

vector<PieceInstance> getPlacedInstances(int pieceId) {
    vector<PieceInstance> instances;
    vector<vector<bool>> counted(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    
    // 找到对应的图块定义
    const Piece* piece = nullptr;
    for (const auto& p : pieces) {
        if (p.id == pieceId) {
            piece = &p;
            break;
        }
    }
    
    if (!piece) return instances;
    
    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            if (board[row][col] == pieceId && !counted[row][col]) {
                // 使用DFS找到所有相连的格子（属于同一个图块实例）
                vector<pair<int, int>> instanceCells;
                vector<vector<bool>> visited(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
                
                function<void(int, int)> dfs = [&](int r, int c) {
                    if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return;
                    if (visited[r][c] || board[r][c] != pieceId) return;
                    
                    visited[r][c] = true;
                    counted[r][c] = true;
                    instanceCells.push_back({r, c});
                    
                    // 检查四个方向的相邻格子
                    dfs(r - 1, c);
                    dfs(r + 1, c);
                    dfs(r, c - 1);
                    dfs(r, c + 1);
                };
                
                dfs(row, col);
                
                // 尝试匹配这个实例到某个形状
                set<pair<int, int>> cellSet(instanceCells.begin(), instanceCells.end());
                int matchedShapeIndex = -1;
                int baseRow = -1, baseCol = -1;
                
                for (size_t s = 0; s < piece->shapes.size(); s++) {
                    const auto& shape = piece->shapes[s];
                    if ((int)shape.size() != (int)instanceCells.size()) continue;
                    
                    // 尝试匹配
                    for (const auto& startCell : instanceCells) {
                        for (const auto& cellPos : shape) {
                            int startRow = startCell.first - cellPos.first;
                            int startCol = startCell.second - cellPos.second;
                            
                            if (startRow < 0 || startCol < 0) continue;
                            
                            set<pair<int, int>> shapeCells;
                            bool matches = true;
                            
                            for (const auto& pos : shape) {
                                int checkRow = startRow + pos.first;
                                int checkCol = startCol + pos.second;
                                if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                    checkCol < 0 || checkCol >= BOARD_SIZE) {
                                    matches = false;
                                    break;
                                }
                                if (board[checkRow][checkCol] != pieceId) {
                                    matches = false;
                                    break;
                                }
                                shapeCells.insert({checkRow, checkCol});
                            }
                            
                            if (matches && shapeCells.size() == cellSet.size()) {
                                bool allMatch = true;
                                for (const auto& cell : cellSet) {
                                    if (shapeCells.find(cell) == shapeCells.end()) {
                                        allMatch = false;
                                        break;
                                    }
                                }
                                if (allMatch) {
                                    matchedShapeIndex = s;
                                    baseRow = startRow;
                                    baseCol = startCol;
                                    break;
                                }
                            }
                        }
                        if (matchedShapeIndex >= 0) break;
                    }
                    if (matchedShapeIndex >= 0) break;
                }
                
                if (matchedShapeIndex >= 0) {
                    instances.push_back({baseRow, baseCol, matchedShapeIndex, cellSet});
                }
            }
        }
    }
    
    return instances;
}

// 移除指定pieceId的超出数量的实例（从棋盘上移除多余的实例）
void removeExcessInstances(int pieceId, int maxCount) {
    vector<PieceInstance> instances = getPlacedInstances(pieceId);
    
    if ((int)instances.size() <= maxCount) return;
    
    // 按位置排序，优先移除位置靠后的实例
    sort(instances.begin(), instances.end(),
        [](const PieceInstance& a, const PieceInstance& b) {
            if (a.baseRow != b.baseRow) {
                return a.baseRow > b.baseRow;  // 行号大的先移除
            }
            return a.baseCol > b.baseCol;  // 列号大的先移除
        });
    
    // 移除多余的实例
    int toRemove = (int)instances.size() - maxCount;
    for (int i = 0; i < toRemove; i++) {
        const auto& instance = instances[i];
        // 移除这个实例的所有单元格
        for (const auto& cell : instance.cells) {
            board[cell.first][cell.second] = 0;
        }
    }
}

// 计算孤立区域数量（用于启发式搜索，检查是否有小于5格的孤立空区域）
int countSmallIsolatedRegions() {
    vector<vector<bool>> visited(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    int smallRegionCount = 0;
    
    function<int(int, int)> dfs = [&](int r, int c) -> int {
        if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return 0;
        if (visited[r][c] || board[r][c] != 0) return 0;
        
        visited[r][c] = true;
        int size = 1;
        size += dfs(r - 1, c);
        size += dfs(r + 1, c);
        size += dfs(r, c - 1);
        size += dfs(r, c + 1);
        return size;
    };
    
    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int col = 0; col < BOARD_SIZE; col++) {
            if (!visited[row][col] && board[row][col] == 0) {
                int size = dfs(row, col);
                if (size > 0 && size < 5) {  // 小于5格的孤立区域（无法放置cross）
                    smallRegionCount++;
                }
            }
        }
    }
    
    return smallRegionCount;
}

bool solve(int pieceIndex, const vector<PieceCount>& counts) {
    // 检查超时（每200次递归调用检查一次，更频繁的检查）
    solveCheckCount++;
    if (solveCheckCount % 200 == 0) {
        if (solveTimer.getElapsedTime().asSeconds() > estimatedSolveTime) {
            solveTimeout = true;
            return false;
        }
    }
    
    if (solveTimeout) {
        return false;
    }
    
    // 快速计算已填充的单元格数（避免重复遍历）
    int filledCells = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (board[i][j] != 0) filledCells++;
        }
    }
    
    // 预先计算所有图块的已放置实例数（避免重复计算）
    map<int, int> placedCounts;
    for (const auto& pc : counts) {
        if (pc.count > 0) {
            placedCounts[pc.pieceId] = countPlacedInstances(pc.pieceId);
        }
    }
    
    // 验证每种图块类型的使用数量是否正好等于用户指定的数量（不能多也不能少）
    bool allPiecesUsedCorrectly = true;
    for (const auto& pc : counts) {
        int placedCount = 0;
        if (placedCounts.find(pc.pieceId) != placedCounts.end()) {
            placedCount = placedCounts[pc.pieceId];
        }
        
        // 使用数量必须正好等于用户指定的数量
        if (placedCount != pc.count) {
            allPiecesUsedCorrectly = false;
            break;
        }
    }
    
    // 只有当游戏板填满且所有图块类型都使用了正确的数量时，才认为求解成功
    if (filledCells == BOARD_SIZE * BOARD_SIZE && allPiecesUsedCorrectly) {
        return true;
    }
    
    // 剪枝优化：如果剩余空间不足以放置剩余图块，提前返回false
    int emptyCells = BOARD_SIZE * BOARD_SIZE - filledCells;
    int requiredCells = 0;
    for (const auto& pc : counts) {
        if (pc.count > 0) {
            int placedCount = placedCounts[pc.pieceId];
            int remaining = pc.count - placedCount;
            if (remaining > 0) {
                const Piece* piece = nullptr;
                for (const auto& p : pieces) {
                    if (p.id == pc.pieceId) {
                        piece = &p;
                        break;
                    }
                }
                if (piece && !piece->shapes.empty()) {
                    int cellCount = (int)piece->shapes[0].size();
                    requiredCells += remaining * cellCount;
                }
            }
        }
    }
    if (requiredCells > emptyCells) {
        return false;  // 剩余空间不足，剪枝
    }
    
    // 创建一个图块列表，按大小和剩余数量排序（大的先放，剩余数量少的优先）
    vector<tuple<int, int, int>> pieceList; // {pieceId, size, remaining}
    for (const auto& pc : counts) {
        // 如果数量为0，跳过
        if (pc.count == 0) continue;
        
        // 使用预先计算的结果
        int placedCount = placedCounts[pc.pieceId];
        
        // 如果已经放置了足够的数量，跳过
        if (placedCount >= pc.count) continue;
        
        // 找到对应的图块并计算大小
        const Piece* piece = nullptr;
        for (const auto& p : pieces) {
            if (p.id == pc.pieceId) {
                piece = &p;
                break;
            }
        }
        
        if (!piece || piece->shapes.empty()) continue;
        
        // 计算图块大小（使用第一个形状的单元格数）
        int pieceSize = (int)piece->shapes[0].size();
        int remaining = pc.count - placedCount;
        pieceList.push_back({pc.pieceId, pieceSize, remaining});
    }
    
    // 如果没有任何可放置的图块，返回false
    if (pieceList.empty()) {
        return false;
    }
    
    // 按大小降序排序，然后按剩余数量升序排序（大的先放，剩余少的优先）
    sort(pieceList.begin(), pieceList.end(), 
         [](const tuple<int, int, int>& a, const tuple<int, int, int>& b) {
             if (get<1>(a) != get<1>(b)) {
                 return get<1>(a) > get<1>(b);  // 大小降序
             }
             return get<2>(a) < get<2>(b);  // 剩余数量升序
         });
    
    // 尝试放置每种类型的图块（按排序后的顺序）
    for (const auto& item : pieceList) {
        int pcPieceId = get<0>(item);
        
        // 找到对应的PieceCount
        const PieceCount* pc = nullptr;
        for (const auto& count : counts) {
            if (count.pieceId == pcPieceId) {
                pc = &count;
                break;
            }
        }
        if (!pc) continue;
        
        // 找到对应的图块
        const Piece* piece = nullptr;
        for (const auto& p : pieces) {
            if (p.id == pcPieceId) {
                piece = &p;
                break;
            }
        }
        
        if (!piece) continue;
        
        // 去重形状：只尝试唯一的形状（避免重复尝试相同的形状）
        vector<vector<pair<int, int>>> uniqueShapes;
        for (const auto& shape : piece->shapes) {
            bool isDuplicate = false;
            for (const auto& existingShape : uniqueShapes) {
                if (shape.size() != existingShape.size()) continue;
                bool matches = true;
                for (size_t i = 0; i < shape.size(); i++) {
                    if (shape[i] != existingShape[i]) {
                        matches = false;
                        break;
                    }
                }
                if (matches) {
                    isDuplicate = true;
                    break;
                }
            }
            if (!isDuplicate) {
                uniqueShapes.push_back(shape);
            }
        }
        
        // 获取图块大小（用于优化搜索范围）
        int pieceSize = (int)piece->shapes[0].size();
        
        // 尝试所有唯一形状和位置
        for (const auto& shape : uniqueShapes) {
            if (solveTimeout) return false;
            
            // 优化：对于大图块（如cross），限制搜索范围以提高效率
            int maxRow = BOARD_SIZE;
            int maxCol = BOARD_SIZE;
            if (pieceSize > 1) {
                // 计算形状的边界
                int shapeMaxRow = 0, shapeMaxCol = 0;
                for (const auto& cell : shape) {
                    shapeMaxRow = max(shapeMaxRow, cell.first);
                    shapeMaxCol = max(shapeMaxCol, cell.second);
                }
                maxRow = BOARD_SIZE - shapeMaxRow;
                maxCol = BOARD_SIZE - shapeMaxCol;
            }
            
            // 优化：对于1x1图块，使用更高效的填充策略（按顺序填充空位）
            if (pieceSize == 1) {
                // 1x1图块：直接按顺序填充空位，避免重复尝试
                int remaining1x1 = pc->count - placedCounts[pcPieceId];
                
                // 收集所有空位
                vector<pair<int, int>> emptyCells;
        for (int row = 0; row < BOARD_SIZE; row++) {
            for (int col = 0; col < BOARD_SIZE; col++) {
                        if (board[row][col] == 0) {
                            emptyCells.push_back({row, col});
                        }
                    }
                }
                
                // 如果剩余空位少于剩余1x1数量，不可能成功（剪枝）
                if ((int)emptyCells.size() < remaining1x1) {
                    return false;
                }
                
                // 如果空位数量正好等于剩余1x1数量，直接全部填充（这是最优情况）
                if ((int)emptyCells.size() == remaining1x1) {
                    for (const auto& pos : emptyCells) {
                        placePiece(shape, pos.first, pos.second, piece->id);
                    }
                    if (solve(pieceIndex + 1, counts)) {
                        return true;
                    }
                    // 恢复
                    for (const auto& pos : emptyCells) {
                        removePiece(shape, pos.first, pos.second);
                    }
                } else {
                    // 按顺序尝试填充（对于大量1x1，这样可以减少递归深度）
                    for (const auto& pos : emptyCells) {
                        if (solveTimeout) return false;
                        placePiece(shape, pos.first, pos.second, piece->id);
                        
                        if (solve(pieceIndex + 1, counts)) {
                            return true;
                        }
                        
                        removePiece(shape, pos.first, pos.second);
                    }
                }
            } else {
                // 大图块：从左上角开始，按行优先顺序尝试
                // 优化：对于cross（5格），使用更智能的搜索策略
                if (pieceSize == 5 && piece->name == "cross") {
                    // Cross特殊优化：优先尝试不会产生孤立小区域的位置
                    vector<tuple<int, int, int>> positions; // {score, row, col}
                    for (int row = 0; row < maxRow; row++) {
                        for (int col = 0; col < maxCol; col++) {
                if (canPlace(shape, row, col)) {
                                // 计算启发式分数
                                int centerRow = BOARD_SIZE / 2;
                                int centerCol = BOARD_SIZE / 2;
                                int distFromCenter = abs(row - centerRow) + abs(col - centerCol);
                                
                                // 检查放置后是否会产生过小的孤立区域
                                placePiece(shape, row, col, piece->id);
                                int smallRegions = countSmallIsolatedRegions();
                                removePiece(shape, row, col);
                                
                                // 分数：距离中心越近越好，孤立区域越少越好
                                // 如果产生孤立小区域，大幅增加分数（降低优先级）
                                int score = distFromCenter * 10 + smallRegions * 1000;
                                positions.push_back({score, row, col});
                            }
                        }
                    }
                    // 按分数排序（分数小的优先）
                    sort(positions.begin(), positions.end());
                    for (const auto& pos : positions) {
                        if (solveTimeout) return false;
                        int row = get<1>(pos);
                        int col = get<2>(pos);
                        placePiece(shape, row, col, piece->id);
                        
                        if (solve(pieceIndex + 1, counts)) {
                        return true;
                    }
                    
                    removePiece(shape, row, col);
                }
                } else {
                    // 其他大图块：正常搜索
                    for (int row = 0; row < maxRow; row++) {
                        if (solveTimeout) return false;
                        for (int col = 0; col < maxCol; col++) {
                            if (solveTimeout) return false;
                            
                            if (canPlace(shape, row, col)) {
                                placePiece(shape, row, col, piece->id);
                                
                                if (solve(pieceIndex + 1, counts)) {
                                    return true;
                                }
                                
                                removePiece(shape, row, col);
                            }
                        }
                    }
                }
            }
        }
        // 继续尝试其他图块类型
    }
    
    // 如果所有图块类型都无法放置，返回false
    return false;
}

void drawBoard(RenderWindow& window, Font& font) {
    int offsetX = 50;
    int offsetY = 50;
    
    // 绘制网格
    for (int i = 0; i <= BOARD_SIZE; i++) {
        // 垂直线
        RectangleShape line(Vector2f(2, BOARD_SIZE * CELL_SIZE));
        line.setPosition(offsetX + i * CELL_SIZE, offsetY);
        line.setFillColor(Color::Black);
        window.draw(line);
        
        // 水平线
        RectangleShape line2(Vector2f(BOARD_SIZE * CELL_SIZE, 2));
        line2.setPosition(offsetX, offsetY + i * CELL_SIZE);
        line2.setFillColor(Color::Black);
        window.draw(line2);
    }
    
    // 绘制图块（使用纹理，按完整形状）
    lock_guard<mutex> lock(boardMutex);
    vector<vector<bool>> drawn(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    
    // 先收集所有图块的位置和形状
    struct PiecePlacement {
        const Piece* piece;
        int shapeIndex;
        int baseRow;
        int baseCol;
    };
    vector<PiecePlacement> placements;
    
    // 问题修复：三个以上相邻图块消失和移位的问题
    // 使用临时标记数组来跟踪在当前绘制周期内已匹配的单元格
    // 这样可以避免多个图块相邻时重复匹配或错误匹配
    vector<vector<bool>> matchedInThisFrame(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            int cellValue = showSolution ? solutionBoard[i][j] : board[i][j];
            if (cellValue != 0 && !drawn[i][j] && !matchedInThisFrame[i][j]) {
                const Piece* piece = nullptr;
                int shapeIndex = -1;
                int baseRow = -1, baseCol = -1;
                
                // 找到对应的图块类型
                for (const auto& p : pieces) {
                    if (p.id == cellValue) {
                        piece = &p;
                        break;
                    }
                }
                
                if (!piece) continue;
                
                // 收集所有可能的匹配候选
                struct MatchCandidate {
                    int shapeIndex;
                    int baseRow;
                    int baseCol;
                    int matchedCells;
                    set<pair<int, int>> shapeCells;  // 存储形状的所有单元格
                };
                vector<MatchCandidate> candidates;
                
                // 从当前单元格(i, j)开始，尝试匹配所有可能的形状
                for (size_t s = 0; s < piece->shapes.size(); s++) {
                    const auto& shape = piece->shapes[s];
                    
                    // 尝试将 (i, j) 作为形状中的每个单元格
                    for (const auto& cellPos : shape) {
                        // 假设 (i, j) 对应形状中的 cellPos 位置
                        // 那么形状的基准点是 (i - cellPos.first, j - cellPos.second)
                        int startRow = i - cellPos.first;
                        int startCol = j - cellPos.second;
                        
                        // 检查这个起始位置是否有效
                        if (startRow < 0 || startCol < 0) continue;
                        
                        // 检查整个形状是否匹配，且所有单元格都未被其他形状匹配
                        bool matches = true;
                        int matchedCells = 0;
                        set<pair<int, int>> shapeCells;
                        
                        for (const auto& pos : shape) {
                            int checkRow = startRow + pos.first;
                            int checkCol = startCol + pos.second;
                            if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                checkCol < 0 || checkCol >= BOARD_SIZE) {
                                matches = false;
                                break;
                            }
                            int val = showSolution ? solutionBoard[checkRow][checkCol] : board[checkRow][checkCol];
                            if (val != cellValue) {
                                matches = false;
                                break;
                            }
                            // 检查这个单元格是否已经被绘制或在本帧中已匹配
                            if (drawn[checkRow][checkCol] || matchedInThisFrame[checkRow][checkCol]) {
                                matches = false;
                                break;
                            }
                            shapeCells.insert({checkRow, checkCol});
                            matchedCells++;
                        }
                        
                        // 确保匹配的单元格数量等于形状大小
                        if (matches && matchedCells == (int)shape.size()) {
                            candidates.push_back({(int)s, startRow, startCol, matchedCells, shapeCells});
                        }
                    }
                }
                
                // 选择最合适的匹配：确保选择正确的形状，避免因相邻图块而改变旋转角度
                if (!candidates.empty()) {
                    // 改进的排序策略：
                    // 1. 优先选择形状索引最小的（通常是0度旋转，最稳定）
                    // 2. 如果形状索引相同，按baseRow和baseCol排序（选择位置更靠上的）
                    // 这样可以确保当多个候选都匹配时，选择最稳定的形状
                    sort(candidates.begin(), candidates.end(), 
                        [](const MatchCandidate& a, const MatchCandidate& b) {
                            // 首先按形状索引排序（优先选择索引小的，即0度旋转）
                            if (a.shapeIndex != b.shapeIndex) {
                                return a.shapeIndex < b.shapeIndex;
                            }
                            // 如果形状索引相同，按位置排序
                            if (a.baseRow != b.baseRow) {
                                return a.baseRow < b.baseRow;
                            }
                            return a.baseCol < b.baseCol;
                        });
                    
                    // 选择第一个候选（形状索引最小的）
                    // 但需要确保这个候选的单元格集合是唯一的，不会与其他候选重叠
                    int selectedIndex = 0;
                    for (size_t idx = 0; idx < candidates.size(); idx++) {
                        bool hasOverlap = false;
                        for (size_t otherIdx = 0; otherIdx < candidates.size(); otherIdx++) {
                            if (idx == otherIdx) continue;
                            // 检查是否有重叠的单元格
                            for (const auto& cell : candidates[idx].shapeCells) {
                                if (candidates[otherIdx].shapeCells.find(cell) != candidates[otherIdx].shapeCells.end()) {
                                    hasOverlap = true;
                                    break;
                                }
                            }
                            if (hasOverlap) break;
                        }
                        // 如果没有重叠，选择这个候选
                        if (!hasOverlap) {
                            selectedIndex = idx;
                            break;
                        }
                    }
                    
                    shapeIndex = candidates[selectedIndex].shapeIndex;
                    baseRow = candidates[selectedIndex].baseRow;
                    baseCol = candidates[selectedIndex].baseCol;
                    
                    // 立即标记这个形状的所有单元格为已匹配（在本帧中）
                    // 这样可以避免在同一个绘制周期内重复匹配
                    for (const auto& cell : candidates[selectedIndex].shapeCells) {
                        matchedInThisFrame[cell.first][cell.second] = true;
                    }
                }
                
                // 如果这个图块实例正在被拖拽，跳过绘制（避免重影）
                if (draggedPiece.isDragging && draggedPiece.pieceId == cellValue &&
                    baseRow >= 0 && baseCol >= 0 &&
                    baseRow == draggedPiece.originalRow && baseCol == draggedPiece.originalCol) {
                    continue;
                }
                
                if (piece && shapeIndex >= 0 && shapeIndex < (int)piece->shapes.size() && baseRow >= 0) {
                    placements.push_back({piece, shapeIndex, baseRow, baseCol});
                }
            }
        }
    }
    
    // 绘制所有图块
    for (const auto& placement : placements) {
        drawPieceTexture(window, *placement.piece, placement.shapeIndex,
                        placement.baseRow, placement.baseCol,
                        offsetX, offsetY, CELL_SIZE, drawn);
    }
    
    // 绘制自动求解按钮（取消图块列表，按钮直接放在右侧）
    int buttonX = offsetX + BOARD_SIZE * CELL_SIZE + 30;
    int buttonY = offsetY;  // 直接放在顶部
    bool fontAvailable = font.getInfo().family != "";
    int buttonWidth = 150;
    int buttonHeight = 40;
    
    RectangleShape solveButton(Vector2f(buttonWidth, buttonHeight));
    solveButton.setPosition(buttonX, buttonY);
    if (solving) {
        solveButton.setFillColor(Color(150, 150, 150));  // 灰色表示正在求解
    } else {
        solveButton.setFillColor(Color(100, 200, 100));  // 绿色表示可以点击
    }
    solveButton.setOutlineThickness(2);
    solveButton.setOutlineColor(Color::Black);
    window.draw(solveButton);
    
    if (fontAvailable) {
        Text buttonText;
        buttonText.setFont(font);
        if (solving) {
            buttonText.setString("Solving...");
        } else if (solutionFound) {
            buttonText.setString("Show Solution");
        } else {
            buttonText.setString("Auto Solve");
        }
        buttonText.setCharacterSize(16);
        buttonText.setFillColor(Color::Black);
        FloatRect textBounds = buttonText.getLocalBounds();
        buttonText.setPosition(buttonX + (buttonWidth - textBounds.width) / 2,
                              buttonY + (buttonHeight - textBounds.height) / 2);
        window.draw(buttonText);
    }
    
    // 绘制求解结果和时间信息
    if (fontAvailable) {
        string resultText;
        if (solving) {
            // 实时显示精确时间（保留1位小数）
            float currentTime = solveTimer.getElapsedTime().asSeconds();
            ostringstream oss;
            oss.precision(1);
            oss << fixed << currentTime;
            resultText = "Solving... Time: " + oss.str() + "s";
        } else if (solutionFound && solveTime > 0.0f) {
            ostringstream oss;
            oss.precision(1);
            oss << fixed << solveTime;
            resultText = "Solution Found! Time: " + oss.str() + "s";
        } else if (!solving && solveTime > 0.0f && !solutionFound) {
            if (solveTimeout) {
                ostringstream timeoutOss;
                timeoutOss.precision(1);
                timeoutOss << fixed << estimatedSolveTime;
                resultText = "Timeout! No solution found in " + timeoutOss.str() + "s";
            } else {
                ostringstream oss;
                oss.precision(1);
                oss << fixed << solveTime;
                resultText = "No solution found. Time: " + oss.str() + "s";
            }
        }
        
        if (!resultText.empty()) {
            Text result;
            result.setFont(font);
            result.setString(resultText);
            result.setCharacterSize(14);
            result.setPosition(buttonX, buttonY + buttonHeight + 5);
            result.setFillColor(solutionFound ? Color(0, 150, 0) : (solveTimeout ? Color(200, 0, 0) : Color::Black));
            window.draw(result);
        }
        
        // 绘制测试用例1按钮（在Auto Solve按钮右边）
        int testButton1X = buttonX + buttonWidth + 10;
        RectangleShape testButton1(Vector2f(buttonWidth, buttonHeight));
        testButton1.setPosition(testButton1X, buttonY);
        testButton1.setFillColor(Color(100, 150, 200));  // 蓝色表示测试按钮
        testButton1.setOutlineThickness(2);
        testButton1.setOutlineColor(Color::Black);
        window.draw(testButton1);
        
        if (fontAvailable) {
            Text testButton1Text;
            testButton1Text.setFont(font);
            testButton1Text.setString("Test Case 1");
            testButton1Text.setCharacterSize(14);
            testButton1Text.setFillColor(Color::White);
            FloatRect test1TextBounds = testButton1Text.getLocalBounds();
            testButton1Text.setPosition(testButton1X + (buttonWidth - test1TextBounds.width) / 2,
                                     buttonY + (buttonHeight - test1TextBounds.height) / 2);
            window.draw(testButton1Text);
            
            // 在测试用例1按钮下方显示预估求解时间
            float testCase1EstimatedTime = estimateSolveTime(pieceCounts);
            // 临时设置测试用例1的pieceCounts来计算预估时间
            vector<PieceCount> tempCounts1;
            for (const auto& piece : pieces) {
                int count = 0;
                if (piece.name == "cross") count = 4;
                else if (piece.name == "1x1-1") count = 44;
                tempCounts1.push_back({piece.id, count, 0});
            }
            testCase1EstimatedTime = estimateSolveTime(tempCounts1);
            ostringstream est1Oss;
            est1Oss.precision(1);
            est1Oss << fixed << testCase1EstimatedTime;
            Text estimatedTime1Text("Est. Time: " + est1Oss.str() + "s", font, 12);
            estimatedTime1Text.setPosition(testButton1X, buttonY + buttonHeight + 5);
            estimatedTime1Text.setFillColor(Color(100, 100, 100));  // 灰色
            window.draw(estimatedTime1Text);
        }
        
        // 绘制测试用例2按钮（在测试用例1按钮右边）
        int testButton2X = testButton1X + buttonWidth + 10;
        RectangleShape testButton2(Vector2f(buttonWidth, buttonHeight));
        testButton2.setPosition(testButton2X, buttonY);
        testButton2.setFillColor(Color(150, 100, 200));  // 紫色表示测试用例2按钮
        testButton2.setOutlineThickness(2);
        testButton2.setOutlineColor(Color::Black);
        window.draw(testButton2);
        
        if (fontAvailable) {
            Text testButton2Text;
            testButton2Text.setFont(font);
            testButton2Text.setString("Test Case 2");
            testButton2Text.setCharacterSize(14);
            testButton2Text.setFillColor(Color::White);
            FloatRect test2TextBounds = testButton2Text.getLocalBounds();
            testButton2Text.setPosition(testButton2X + (buttonWidth - test2TextBounds.width) / 2,
                                     buttonY + (buttonHeight - test2TextBounds.height) / 2);
            window.draw(testButton2Text);
            
            // 在测试用例2按钮下方显示预估求解时间
            vector<PieceCount> tempCounts2;
            for (const auto& piece : pieces) {
                int count = 0;
                if (piece.name == "L-shape") count = 15;
                else if (piece.name == "1x1-1") count = 4;
                tempCounts2.push_back({piece.id, count, 0});
            }
            float testCase2EstimatedTime = estimateSolveTime(tempCounts2);
            ostringstream est2Oss;
            est2Oss.precision(1);
            est2Oss << fixed << testCase2EstimatedTime;
            Text estimatedTime2Text("Est. Time: " + est2Oss.str() + "s", font, 12);
            estimatedTime2Text.setPosition(testButton2X, buttonY + buttonHeight + 5);
            estimatedTime2Text.setFillColor(Color(100, 100, 100));  // 灰色
            window.draw(estimatedTime2Text);
        }
        
        // 按键说明（移除space键介绍）
        // 往下移，避免遮盖求解时间显示（求解时间在buttonY + buttonHeight + 5，按键说明从buttonY + buttonHeight + 30开始）
        int controlsY = buttonY + buttonHeight + 30;
        Text controlsTitle("Controls:", font, 16);
        controlsTitle.setPosition(buttonX, controlsY);
        controlsTitle.setFillColor(Color::Black);
        controlsTitle.setStyle(Text::Bold);
        window.draw(controlsTitle);
        controlsY += 25;
        
        vector<string> controlTexts = {
            "E - Open/Close Editor",
            "Left Click - Drag Piece",
            "Right Click - Rotate/Remove Piece",
            "Mouse - Drag Editor Window"
        };
        
        for (const auto& text : controlTexts) {
            Text control(text, font, 12);
            control.setPosition(buttonX, controlsY);
            control.setFillColor(Color::Black);
            window.draw(control);
            controlsY += 20;
        }
    }
    
    // 绘制拖拽预览
    if (draggedPiece.isDragging && draggedPiece.pieceId >= 0) {
        Vector2i mousePos = Mouse::getPosition(window);
        int offsetX = 50;
        int offsetY = 50;
        
        // 检查鼠标是否在预览区
        int previewX = 50;
        int previewY = 50 + BOARD_SIZE * CELL_SIZE + 20;
        int previewWidth = BOARD_SIZE * CELL_SIZE;
        int previewHeight = 200;
        bool inPreviewArea = (mousePos.x >= previewX && mousePos.x < previewX + previewWidth &&
                             mousePos.y >= previewY && mousePos.y < previewY + previewHeight);
        
        // 如果鼠标在预览区，将预览显示在预览区上方
        int previewDisplayX, previewDisplayY;
        if (inPreviewArea) {
            // 计算图块的边界
            const Piece* piece = nullptr;
            for (const auto& p : pieces) {
                if (p.id == draggedPiece.pieceId) {
                    piece = &p;
                    break;
                }
            }
            if (piece && draggedPiece.shapeIndex < (int)piece->shapes.size()) {
                const auto& shape = piece->shapes[draggedPiece.shapeIndex];
                int minRow = shape[0].first, maxRow = shape[0].first;
                int minCol = shape[0].second, maxCol = shape[0].second;
                for (const auto& cell : shape) {
                    minRow = min(minRow, cell.first);
                    maxRow = max(maxRow, cell.first);
                    minCol = min(minCol, cell.second);
                    maxCol = max(maxCol, cell.second);
                }
                int shapeWidth = (maxCol - minCol + 1) * CELL_SIZE;
                int shapeHeight = (maxRow - minRow + 1) * CELL_SIZE;
                previewDisplayX = previewX + (previewWidth - shapeWidth) / 2;
                previewDisplayY = previewY - shapeHeight - 10;  // 预览区上方10像素
    } else {
                previewDisplayX = mousePos.x;
                previewDisplayY = mousePos.y;
            }
        } else {
            // 计算鼠标在游戏板上的位置
            int boardX = mousePos.x - offsetX;
            int boardY = mousePos.y - offsetY;
            previewDisplayX = offsetX;
            previewDisplayY = offsetY;
        }
        
        // 计算鼠标在游戏板上的位置（用于边框显示）
        int boardX = mousePos.x - offsetX;
        int boardY = mousePos.y - offsetY;
        int col = boardX / CELL_SIZE;
        int row = boardY / CELL_SIZE;
        
        // 找到对应的图块
        const Piece* piece = nullptr;
        for (const auto& p : pieces) {
            if (p.id == draggedPiece.pieceId) {
                piece = &p;
                break;
            }
        }
        
        if (piece && draggedPiece.shapeIndex < (int)piece->shapes.size()) {
            const auto& shape = piece->shapes[draggedPiece.shapeIndex];
            
            // 使用贴图绘制预览（半透明）
            Texture* tex = nullptr;
            for (auto& pt : pieceTextures) {
                if (pt.pieceId == piece->id && pt.loaded) {
                    tex = &pt.texture;
                    break;
                }
            }
            
            if (tex) {
                // 计算图块的边界
                int minRow = shape[0].first, maxRow = shape[0].first;
                int minCol = shape[0].second, maxCol = shape[0].second;
                for (const auto& cell : shape) {
                    minRow = min(minRow, cell.first);
                    maxRow = max(maxRow, cell.first);
                    minCol = min(minCol, cell.second);
                    maxCol = max(maxCol, cell.second);
                }
                
                int shapeWidth = (maxCol - minCol + 1) * CELL_SIZE;
                int shapeHeight = (maxRow - minRow + 1) * CELL_SIZE;
                
                // 使用RenderTexture绘制完整形状（半透明）
                static map<pair<int, int>, unique_ptr<RenderTexture>> dragPreviewCache;
                pair<int, int> cacheKey = {piece->id, draggedPiece.shapeIndex};
                
                if (dragPreviewCache.find(cacheKey) == dragPreviewCache.end()) {
                    auto renderTex = make_unique<RenderTexture>();
                    renderTex->create(shapeWidth, shapeHeight);
                    renderTex->clear(Color::Transparent);
                    
                    Sprite sprite(*tex);
                    // 根据shapeIndex旋转贴图（假设shapes是按旋转顺序排列的）
                    float rotation = draggedPiece.shapeIndex * 90.0f;
                    sprite.setRotation(rotation);
                    
                    // 计算旋转后的中心点
                    float centerX = tex->getSize().x / 2.0f;
                    float centerY = tex->getSize().y / 2.0f;
                    sprite.setOrigin(centerX, centerY);
                    
                    // 计算缩放和位置，使贴图充满形状区域
                    // 如果旋转了90度或270度，纹理的宽高在视觉上会交换
                    float scaleX, scaleY;
                    if ((int)rotation % 180 == 90) {
                        // 旋转90/270度：形状宽度对应纹理高度，形状高度对应纹理宽度
                        scaleX = (float)shapeWidth / tex->getSize().y;
                        scaleY = (float)shapeHeight / tex->getSize().x;
                        // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                        float uniformScale = max(scaleX, scaleY);
                        scaleX = uniformScale;
                        scaleY = uniformScale;
                    } else {
                        // 0度或180度：正常对应
                        scaleX = (float)shapeWidth / tex->getSize().x;
                        scaleY = (float)shapeHeight / tex->getSize().y;
                        // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                        float uniformScale = max(scaleX, scaleY);
                        scaleX = uniformScale;
                        scaleY = uniformScale;
                    }
                    sprite.setScale(scaleX, scaleY);
                    
                    // 设置位置到形状中心
                    sprite.setPosition(shapeWidth / 2.0f, shapeHeight / 2.0f);
                    renderTex->draw(sprite);
                    
                    renderTex->display();
                    dragPreviewCache[cacheKey] = move(renderTex);
                }
                
                // 绘制到窗口（半透明）
                Sprite finalSprite(dragPreviewCache[cacheKey]->getTexture());
                if (inPreviewArea) {
                    finalSprite.setPosition(previewDisplayX + minCol * CELL_SIZE,
                                          previewDisplayY + minRow * CELL_SIZE);
                } else {
                    // 计算预览位置：鼠标位置减去拖拽偏移量，再减去形状的最小偏移
                    // 这样鼠标在图块上的相对位置保持不变
                    int previewX = mousePos.x - draggedPiece.dragOffset.x - minCol * CELL_SIZE;
                    int previewY = mousePos.y - draggedPiece.dragOffset.y - minRow * CELL_SIZE;
                    finalSprite.setPosition(previewX, previewY);
                }
                finalSprite.setColor(Color(255, 255, 255, 150));  // 半透明
                window.draw(finalSprite);
                
                // 绘制黄色边框
                if (inPreviewArea) {
                    for (const auto& cell : shape) {
                        int previewRow = row + cell.first;
                        int previewCol = col + cell.second;
                        
                        if (previewRow >= 0 && previewRow < BOARD_SIZE &&
                            previewCol >= 0 && previewCol < BOARD_SIZE) {
                            RectangleShape outline(Vector2f(CELL_SIZE - 2, CELL_SIZE - 2));
                            outline.setPosition(previewDisplayX + previewCol * CELL_SIZE + 1,
                                              previewDisplayY + previewRow * CELL_SIZE + 1);
                            outline.setFillColor(Color::Transparent);
                            outline.setOutlineThickness(2);
                            outline.setOutlineColor(Color::Yellow);
                            window.draw(outline);
                        }
                    }
                } else {
                    // 在游戏板上：使用与预览图块相同的位置计算方式
                    // minCol 和 minRow 已经在上面计算过了（在tex块中）
                    for (const auto& cell : shape) {
                        int cellX = mousePos.x - draggedPiece.dragOffset.x + (cell.second - minCol) * CELL_SIZE;
                        int cellY = mousePos.y - draggedPiece.dragOffset.y + (cell.first - minRow) * CELL_SIZE;
                        RectangleShape outline(Vector2f(CELL_SIZE - 2, CELL_SIZE - 2));
                        outline.setPosition(cellX + 1, cellY + 1);
                        outline.setFillColor(Color::Transparent);
                        outline.setOutlineThickness(2);
                        outline.setOutlineColor(Color::Yellow);
                        window.draw(outline);
                    }
                }
    } else {
                // 回退到颜色填充
                // 计算图块的边界（与上面的tex块中相同）
                int minRow = shape[0].first, maxRow = shape[0].first;
                int minCol = shape[0].second, maxCol = shape[0].second;
                for (const auto& cell : shape) {
                    minRow = min(minRow, cell.first);
                    maxRow = max(maxRow, cell.first);
                    minCol = min(minCol, cell.second);
                    maxCol = max(maxCol, cell.second);
                }
                
                if (inPreviewArea) {
                    for (const auto& cell : shape) {
                        int previewRow = row + cell.first;
                        int previewCol = col + cell.second;
                        
                        if (previewRow >= 0 && previewRow < BOARD_SIZE &&
                            previewCol >= 0 && previewCol < BOARD_SIZE) {
                            RectangleShape previewCell(Vector2f(CELL_SIZE - 2, CELL_SIZE - 2));
                            previewCell.setPosition(previewDisplayX + previewCol * CELL_SIZE + 1,
                                                  previewDisplayY + previewRow * CELL_SIZE + 1);
                            Color previewColor = piece->color;
                            previewColor.a = 150;  // 半透明
                            previewCell.setFillColor(previewColor);
                            previewCell.setOutlineThickness(2);
                            previewCell.setOutlineColor(Color::Yellow);
                            window.draw(previewCell);
                        }
                    }
                } else {
                    // 在游戏板上：使用与预览图块相同的位置计算方式
                    for (const auto& cell : shape) {
                        int cellX = mousePos.x - draggedPiece.dragOffset.x + (cell.second - minCol) * CELL_SIZE;
                        int cellY = mousePos.y - draggedPiece.dragOffset.y + (cell.first - minRow) * CELL_SIZE;
                        RectangleShape previewCell(Vector2f(CELL_SIZE - 2, CELL_SIZE - 2));
                        previewCell.setPosition(cellX + 1, cellY + 1);
                        Color previewColor = piece->color;
                        previewColor.a = 150;  // 半透明
                        previewCell.setFillColor(previewColor);
                        previewCell.setOutlineThickness(2);
                        previewCell.setOutlineColor(Color::Yellow);
                        window.draw(previewCell);
                    }
                }
            }
        }
    }
    
    // 绘制预选区和编辑器
    drawPiecePreviewArea(window, font);
    drawPieceEditor(window, font);
}

// 读取配置文件，获取图块贴图路径映射
map<string, string> loadTextureConfig() {
    map<string, string> textureMap;
    ifstream configFile("texture_config.txt");
    
    if (configFile.is_open()) {
        string line;
        while (getline(configFile, line)) {
            // 跳过空行和注释行（以#开头）
            if (line.empty() || line[0] == '#') continue;
            
            // 解析格式：图块名称=贴图路径
            size_t pos = line.find('=');
            if (pos != string::npos) {
                string pieceName = line.substr(0, pos);
                string texturePath = line.substr(pos + 1);
                
                // 去除前后空格
                pieceName.erase(0, pieceName.find_first_not_of(" \t"));
                pieceName.erase(pieceName.find_last_not_of(" \t") + 1);
                texturePath.erase(0, texturePath.find_first_not_of(" \t"));
                texturePath.erase(texturePath.find_last_not_of(" \t") + 1);
                
                if (!pieceName.empty() && !texturePath.empty()) {
                    textureMap[pieceName] = texturePath;
                }
            }
        }
        configFile.close();
    }
    
    return textureMap;
}

// 加载图块纹理资源（按图块名称）
void loadPieceTextures() {
    pieceTextures.clear();
    
    // 确保资源文件夹存在
    #ifdef _WIN32
    system("if not exist resources mkdir resources >nul 2>&1");
    #else
    system("mkdir -p resources 2>/dev/null");
    #endif
    
    // 读取配置文件
    map<string, string> textureConfig = loadTextureConfig();
    
    for (const auto& piece : pieces) {
        PieceTexture pt;
        pt.pieceId = piece.id;
        pt.loaded = false;
        
        // 首先检查配置文件中是否指定了贴图路径
        string texturePath;
        bool useConfigPath = false;
        if (textureConfig.find(piece.name) != textureConfig.end()) {
            // 使用配置文件中指定的路径
            texturePath = textureConfig[piece.name];
            useConfigPath = true;
        } else {
            // 如果没有指定，使用默认路径（资源文件夹下的图块名称.png）
            texturePath = "resources/" + piece.name + ".png";
        }
        
        // 尝试加载贴图
        if (pt.texture.loadFromFile(texturePath)) {
            pt.loaded = true;
        } else {
            // 如果配置文件中指定的路径加载失败，且不是默认路径，尝试使用默认路径
            if (useConfigPath) {
                string defaultPath = "resources/" + piece.name + ".png";
                if (pt.texture.loadFromFile(defaultPath)) {
                    pt.loaded = true;
                    texturePath = defaultPath;  // 更新为默认路径，用于后续保存
                }
            }
            
            // 如果仍然加载失败，创建默认贴图
            if (!pt.loaded) {
                // 如果文件不存在，创建默认贴图（显示图块形状）
                if (!piece.shapes.empty()) {
                // 使用所有形状的最大边界来确保长宽比正确
                int minRow = 1000, maxRow = -1000;
                int minCol = 1000, maxCol = -1000;
                
                for (const auto& shape : piece.shapes) {
                    for (const auto& cell : shape) {
                        minRow = min(minRow, cell.first);
                        maxRow = max(maxRow, cell.first);
                        minCol = min(minCol, cell.second);
                        maxCol = max(maxCol, cell.second);
                    }
                }
                
                const auto& shape = piece.shapes[0];
                int shapeWidth = (maxCol - minCol + 1) * 20;
                int shapeHeight = (maxRow - minRow + 1) * 20;
                int padding = 4;
                int imgWidth = shapeWidth + padding * 2;
                int imgHeight = shapeHeight + padding * 2;
                
                Image img;
                img.create(imgWidth, imgHeight, Color::Transparent);
                
                // 绘制图块形状
                for (const auto& cell : shape) {
                    int x = (cell.second - minCol) * 20 + padding;
                    int y = (cell.first - minRow) * 20 + padding;
                    
                    // 绘制单元格
                    for (int py = 0; py < 20; py++) {
                        for (int px = 0; px < 20; px++) {
                            if (x + px < imgWidth && y + py < imgHeight) {
                                img.setPixel(x + px, y + py, piece.color);
                            }
                        }
                    }
                    
                    // 绘制边框
                    for (int px = 0; px < 20; px++) {
                        if (x + px < imgWidth) {
                            if (y > 0) img.setPixel(x + px, y - 1, Color::Black);
                            if (y + 20 < imgHeight) img.setPixel(x + px, y + 20, Color::Black);
                        }
                    }
                    for (int py = 0; py < 20; py++) {
                        if (y + py < imgHeight) {
                            if (x > 0) img.setPixel(x - 1, y + py, Color::Black);
                            if (x + 20 < imgWidth) img.setPixel(x + 20, y + py, Color::Black);
                        }
                    }
                }
                
                pt.texture.loadFromImage(img);
                pt.loaded = true;
                
                // 始终保存默认贴图到资源文件夹
                try {
                    img.saveToFile(texturePath);
                } catch (...) {
                    // 如果保存失败，尝试创建文件夹后重试
                    #ifdef _WIN32
                    system("if not exist resources mkdir resources >nul 2>&1");
                    #else
                    system("mkdir -p resources 2>/dev/null");
                    #endif
                    img.saveToFile(texturePath);
                }
                } else {
                    // 如果没有形状，创建简单的单色纹理
                    Image img;
                    img.create(64, 64, piece.color);
                    pt.texture.loadFromImage(img);
                    pt.loaded = true;
                    // 始终保存默认贴图到资源文件夹
                    try {
                        img.saveToFile(texturePath);
                    } catch (...) {
                        // 如果保存失败，尝试创建文件夹后重试
                        #ifdef _WIN32
                        system("if not exist resources mkdir resources >nul 2>&1");
                        #else
                        system("mkdir -p resources 2>/dev/null");
                        #endif
                        img.saveToFile(texturePath);
                    }
                }
            }
        }
        
        pieceTextures.push_back(pt);
    }
}

// 绘制图块纹理（按完整形状，只绘制一次）
// 参数说明：
//   - baseRow, baseCol: 基准点（reference point），不一定是图块占据的第一个单元格
//   - 注意：只绘制形状中实际定义的单元格，如果形状在(0,0)位置为空，基准点位置不会被绘制
void drawPieceTexture(RenderWindow& window, const Piece& piece, int shapeIndex,
                     int baseRow, int baseCol, int offsetX, int offsetY, int cellSize,
                     vector<vector<bool>>& drawn) {
    if (shapeIndex < 0 || shapeIndex >= (int)piece.shapes.size()) return;
    
    const auto& shape = piece.shapes[shapeIndex];
    
    // 检查是否已经绘制过这个图块（检查所有单元格，而不是只检查基准点）
    // 只检查形状中实际定义的单元格，不检查基准点本身
    bool alreadyDrawn = false;
    for (const auto& cell : shape) {
        int row = baseRow + cell.first;
        int col = baseCol + cell.second;
        if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
            if (drawn[row][col]) {
                alreadyDrawn = true;
                break;
            }
        }
    }
    if (alreadyDrawn) return;
    
    // 查找对应的纹理
    Texture* tex = nullptr;
    for (auto& pt : pieceTextures) {
        if (pt.pieceId == piece.id && pt.loaded) {
            tex = &pt.texture;
            break;
        }
    }
    
    if (tex) {
        // 计算图块的边界
        int minRow = shape[0].first, maxRow = shape[0].first;
        int minCol = shape[0].second, maxCol = shape[0].second;
        for (const auto& cell : shape) {
            minRow = min(minRow, cell.first);
            maxRow = max(maxRow, cell.first);
            minCol = min(minCol, cell.second);
            maxCol = max(maxCol, cell.second);
        }
        
        int shapeWidth = (maxCol - minCol + 1) * cellSize;
        int shapeHeight = (maxRow - minRow + 1) * cellSize;
        
        // 使用RenderTexture绘制完整形状（使用unique_ptr避免复制问题）
        static map<pair<int, int>, unique_ptr<RenderTexture>> renderCache;
        pair<int, int> cacheKey = {piece.id, shapeIndex};
        
        if (renderCache.find(cacheKey) == renderCache.end()) {
            auto renderTex = make_unique<RenderTexture>();
            renderTex->create(shapeWidth, shapeHeight);
            renderTex->clear(Color::Transparent);
            
            // 整体绘制图块（不使用小格组合）
            Sprite sprite(*tex);
            // 根据shapeIndex旋转贴图（假设shapes是按旋转顺序排列的）
            float rotation = shapeIndex * 90.0f;
            sprite.setRotation(rotation);
            
            // 计算旋转后的中心点
            float centerX = tex->getSize().x / 2.0f;
            float centerY = tex->getSize().y / 2.0f;
            sprite.setOrigin(centerX, centerY);
            
            // 计算缩放和位置，使贴图充满形状区域
            // 如果旋转了90度或270度，纹理的宽高在视觉上会交换
            float scaleX, scaleY;
            if ((int)rotation % 180 == 90) {
                // 旋转90/270度：形状宽度对应纹理高度，形状高度对应纹理宽度
                scaleX = (float)shapeWidth / tex->getSize().y;
                scaleY = (float)shapeHeight / tex->getSize().x;
                // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                float uniformScale = max(scaleX, scaleY);
                scaleX = uniformScale;
                scaleY = uniformScale;
            } else {
                // 0度或180度：正常对应
                scaleX = (float)shapeWidth / tex->getSize().x;
                scaleY = (float)shapeHeight / tex->getSize().y;
                // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                float uniformScale = max(scaleX, scaleY);
                scaleX = uniformScale;
                scaleY = uniformScale;
            }
            sprite.setScale(scaleX, scaleY);
            
            // 设置位置到形状中心
            sprite.setPosition(shapeWidth / 2.0f, shapeHeight / 2.0f);
            renderTex->draw(sprite);
            
            renderTex->display();
            renderCache[cacheKey] = move(renderTex);
        }
        
        // 绘制到窗口
        Sprite finalSprite(renderCache[cacheKey]->getTexture());
        finalSprite.setPosition(offsetX + (baseCol + minCol) * cellSize,
                              offsetY + (baseRow + minRow) * cellSize);
        window.draw(finalSprite);
        
        // 标记已绘制（使用实际的棋盘坐标）
        // 问题4修复：在绘制时标记drawn数组，确保图块显示且避免重复匹配
        for (const auto& cell : shape) {
            int row = baseRow + cell.first;
            int col = baseCol + cell.second;
            if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
                drawn[row][col] = true;
            }
        }
    } else {
        // 回退到颜色填充
        for (const auto& cell : shape) {
            RectangleShape rect(Vector2f(cellSize - 2, cellSize - 2));
            rect.setPosition(offsetX + (baseCol + cell.second) * cellSize + 1, 
                            offsetY + (baseRow + cell.first) * cellSize + 1);
            rect.setFillColor(piece.color);
            rect.setOutlineThickness(1);
            rect.setOutlineColor(Color::Black);
            window.draw(rect);
        }
        // 标记已绘制（使用实际的棋盘坐标）
        // 问题4修复：在绘制时标记drawn数组，确保图块显示且避免重复匹配
        if (baseRow >= 0 && baseRow < BOARD_SIZE && baseCol >= 0 && baseCol < BOARD_SIZE) {
            for (const auto& cell : shape) {
                int row = baseRow + cell.first;
                int col = baseCol + cell.second;
                if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
                    drawn[row][col] = true;
                }
            }
        }
    }
}

int main() {
    // 设置控制台代码页为UTF-8（Windows）
    #ifdef _WIN32
    system("chcp 65001 >nul");
    #endif
    
    RenderWindow window(VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), 
                       "8x8 Puzzle Game", Style::Close);
    window.setFramerateLimit(60);
    
    Font font;
    bool fontLoaded = false;
    
    // 首先尝试从嵌入的资源加载字体
    #ifdef _WIN32
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(101), RT_RCDATA);
    if (hRes) {
        HGLOBAL hMem = LoadResource(NULL, hRes);
        if (hMem) {
            void* pFontData = LockResource(hMem);
            DWORD size = SizeofResource(NULL, hRes);
            if (pFontData && size > 0) {
                // 从内存加载字体
                if (font.loadFromMemory(pFontData, (size_t)size)) {
                    fontLoaded = true;
                }
            }
        }
    }
    #endif
    
    // 如果嵌入资源加载失败，尝试从文件加载
    if (!fontLoaded) {
    if (font.loadFromFile("arial.ttf")) {
        fontLoaded = true;
    } else if (font.loadFromFile("C:/Windows/Fonts/arial.ttf")) {
        fontLoaded = true;
    } else if (font.loadFromFile("C:/Windows/Fonts/msyh.ttc")) {
        fontLoaded = true;
        }
    }
    
    initializePieces();
    loadPieceTextures();
    
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            board[i][j] = 0;
            solutionBoard[i][j] = 0;
        }
    }
    
    // 求解计时器相关变量已在全局作用域定义
    
    Vector2i mousePos;
    bool mouseLeftPressed = false;
    bool mouseRightPressed = false;
    
    while (window.isOpen()) {
        Event event;
        while (window.pollEvent(event)) {
            if (event.type == Event::Closed) {
                window.close();
            }
            
            if (event.type == Event::KeyPressed) {
                // 空格键功能已改为按钮，这里只保留E键打开编辑器
                if (event.key.code == Keyboard::E) {
                    showEditor = !showEditor;
                }
            }
            
            // 处理自动求解按钮点击
            if (event.type == Event::MouseButtonPressed && 
                event.mouseButton.button == Mouse::Left) {
                mousePos = Mouse::getPosition(window);  // 更新鼠标位置
                int offsetX = 50;
                int offsetY = 50;
                // 按钮位置与drawBoard中一致
                int buttonX = offsetX + BOARD_SIZE * CELL_SIZE + 30;
                int buttonY = offsetY;  // 直接放在顶部
                int buttonWidth = 150;
                int buttonHeight = 40;
                
                if (mousePos.x >= buttonX && mousePos.x < buttonX + buttonWidth &&
                    mousePos.y >= buttonY && mousePos.y < buttonY + buttonHeight) {
                    if (solutionFound && !solving) {
                        // 如果已找到解且不在求解中，切换显示
                        showSolution = !showSolution;
                    } else if (!solving) {
                        // 开始自动求解（允许多次求解）
                        solving = true;
                        solutionFound = false;
                        solved = false;
                        solveTime = 0.0f;
                        solveTimeout = false;  // 重置超时标志
                        solveCheckCount = 0;  // 重置求解调用计数器
                        estimatedSolveTime = estimateSolveTime(pieceCounts);  // 预估求解时间
                        solveTimer.restart();  // 开始计时
                        // 清空游戏板
                        for (int i = 0; i < BOARD_SIZE; i++) {
                            for (int j = 0; j < BOARD_SIZE; j++) {
                                board[i][j] = 0;
                            }
                        }
                        // 等待之前的线程结束（如果存在）
                        if (solveThread && solveThread->joinable()) {
                            solveThread->join();
                            delete solveThread;
                            solveThread = nullptr;
                        }
                        solveThread = new thread([&]() {
            lock_guard<mutex> lock(boardMutex);
                            // 创建pieceCounts的副本，确保求解使用的图块数量与求解开始时一致
                            vector<PieceCount> countsCopy = pieceCounts;
                            // 在120秒内求解
                            if (solve(0, countsCopy)) {
            solutionBoard = board;
            solutionFound = true;
            solved = true;
        } else {
                                solved = false;
                                if (solveTimeout) {
                                    // 超时了
            solved = false;
                                }
        }
        solving = false;
                            solveTime = solveTimer.getElapsedTime().asSeconds();  // 记录求解时间
                        });
                        solveThread->detach();
                    }
                }
                
                // 处理测试用例1按钮点击
                int testButton1X = buttonX + buttonWidth + 10;
                if (mousePos.x >= testButton1X && mousePos.x < testButton1X + buttonWidth &&
                    mousePos.y >= buttonY && mousePos.y < buttonY + buttonHeight) {
                    if (!solving) {
                        // 设置测试用例1：4个cross和44个1x1
                        setTestCase1();
                        // 清空游戏板
                        for (int i = 0; i < BOARD_SIZE; i++) {
                            for (int j = 0; j < BOARD_SIZE; j++) {
                                board[i][j] = 0;
                            }
                        }
                        // 清除之前的求解结果
                        solutionFound = false;
                        solved = false;
                        showSolution = false;
                        
                        // 开始自动求解（使用优化后的算法）
                        solving = true;
                        solveTime = 0.0f;
                        solveTimeout = false;
                        solveCheckCount = 0;
                        estimatedSolveTime = estimateSolveTime(pieceCounts);  // 预估求解时间
                        solveTimer.restart();
                        // 等待之前的线程结束（如果存在）
                        if (solveThread && solveThread->joinable()) {
                            solveThread->join();
                            delete solveThread;
                            solveThread = nullptr;
                        }
                        solveThread = new thread([&]() {
                            lock_guard<mutex> lock(boardMutex);
                            // 创建pieceCounts的副本，确保求解使用的图块数量与求解开始时一致
                            vector<PieceCount> countsCopy = pieceCounts;
                            // 在120秒内求解（使用优化后的算法）
                            if (solve(0, countsCopy)) {
                                solutionBoard = board;
                                solutionFound = true;
                                solved = true;
                            } else {
                                solved = false;
                                if (solveTimeout) {
                                    // 超时了
                                    solved = false;
                                }
                            }
                            solving = false;
                            solveTime = solveTimer.getElapsedTime().asSeconds();
                        });
                        solveThread->detach();
                    }
                }
                
                // 处理测试用例2按钮点击
                int testButton2X = testButton1X + buttonWidth + 10;
                if (mousePos.x >= testButton2X && mousePos.x < testButton2X + buttonWidth &&
                    mousePos.y >= buttonY && mousePos.y < buttonY + buttonHeight) {
                    if (!solving) {
                        // 设置测试用例2：15个L-shape和4个1x1
                        setTestCase2();
                        // 清空游戏板
                        for (int i = 0; i < BOARD_SIZE; i++) {
                            for (int j = 0; j < BOARD_SIZE; j++) {
                                board[i][j] = 0;
                            }
                        }
                        // 清除之前的求解结果
                        solutionFound = false;
                        solved = false;
                        showSolution = false;
                        
                        // 开始自动求解（使用优化后的算法）
                        solving = true;
                        solveTime = 0.0f;
                        solveTimeout = false;
                        solveCheckCount = 0;
                        estimatedSolveTime = estimateSolveTime(pieceCounts);  // 预估求解时间
                        solveTimer.restart();
                        // 等待之前的线程结束（如果存在）
                        if (solveThread && solveThread->joinable()) {
                            solveThread->join();
                            delete solveThread;
                            solveThread = nullptr;
                        }
                        solveThread = new thread([&]() {
                            lock_guard<mutex> lock(boardMutex);
                            // 创建pieceCounts的副本，确保求解使用的图块数量与求解开始时一致
                            vector<PieceCount> countsCopy = pieceCounts;
                            // 在预估时间内求解（使用优化后的算法）
                            if (solve(0, countsCopy)) {
                                solutionBoard = board;
                                solutionFound = true;
                                solved = true;
                            } else {
                                solved = false;
                                if (solveTimeout) {
                                    // 超时了
                                    solved = false;
                                }
                            }
                            solving = false;
                            solveTime = solveTimer.getElapsedTime().asSeconds();
                        });
                        solveThread->detach();
                    }
                }
            }
            
            // 鼠标事件处理
            if (event.type == Event::MouseButtonPressed) {
                mousePos = Mouse::getPosition(window);
                int offsetX = 50;
                int offsetY = 50;
                
                if (event.mouseButton.button == Mouse::Left) {
                    mouseLeftPressed = true;
                    
                    mousePos = Mouse::getPosition(window);  // 更新鼠标位置
                    int offsetX = 50;
                    int offsetY = 50;
                    
                    // 先检查是否点击在编辑器区域（如果编辑器打开，优先处理编辑器事件）
                    bool clickedInEditor = false;
                    if (showEditor) {
                        int editorX = editorDrag.editorX;
                        int editorY = editorDrag.editorY;
                        int editorWidth = WINDOW_WIDTH - 100;
                        int editorHeight = min(400, WINDOW_HEIGHT - editorY - 50);
                        
                        clickedInEditor = (mousePos.x >= editorX && mousePos.x < editorX + editorWidth &&
                                          mousePos.y >= editorY && mousePos.y < editorY + editorHeight);
                    }
                    
                    // 如果点击在编辑器内，不检查预览区（编辑器处理逻辑在后面）
                    // 先检查是否点击在预览区（可以从预览区拖动图块）
                    int previewX = 50;
                    int previewY = 50 + BOARD_SIZE * CELL_SIZE + 20;
                    int previewWidth = BOARD_SIZE * CELL_SIZE;
                    int previewHeight = 200;
                    
                    // 只有当编辑器未打开，或者点击不在编辑器内时，才检查预览区
                    if (!clickedInEditor && 
                        mousePos.x >= previewX && mousePos.x < previewX + previewWidth &&
                        mousePos.y >= previewY + 40 && mousePos.y < previewY + previewHeight) {
                        // 计算点击的图块索引（需要与绘制逻辑完全一致）
                        int currentY = previewY + 40;  // 与绘制时使用相同的起始Y坐标
                        int itemsPerRow = 8;
                        int itemSize = (previewWidth - 40) / itemsPerRow - 10;
                        int spacing = 10;
                        
                        // 计算每个图块类型的已使用数量（与绘制时完全相同的逻辑）
                        vector<int> usedCounts = calculateUsedPieceCounts();
                        
                        // 遍历所有图块，精确检测鼠标点击在哪个图块上
                        int displayIndex = 0;
                        int foundPieceIndex = -1;
                        int foundInstanceIndex = -1;
                        for (size_t i = 0; i < pieces.size(); i++) {
                            // 获取该图块类型的总数量
                            int totalCount = 0;
                            for (const auto& pc : pieceCounts) {
                                if (pc.pieceId == pieces[i].id) {
                                    totalCount = pc.count;
                                    break;
                                }
                            }
                            
                            // 计算可用数量（总数量 - 已使用数量）
                            int availableCount = totalCount - usedCounts[i];
                            
                            // 检查该图块类型的每个实例
                            for (int instance = 0; instance < availableCount; instance++) {
                                int col = (displayIndex % itemsPerRow);
                                int row = (displayIndex / itemsPerRow);
                                int x = previewX + 20 + col * (itemSize + spacing);
                                int y = currentY + row * (itemSize + spacing);
                                
                                // 检查鼠标是否点击在这个图块的位置范围内
                                // 使用与绘制时完全相同的逻辑来计算图块的实际绘制区域
                                if (!pieces[i].shapes.empty()) {
                                    const auto& shape = pieces[i].shapes[0];
                                    
                                    // 计算cellSize（与绘制时相同）
                                    int maxDim = 0;
                                    for (const auto& cell : shape) {
                                        maxDim = max(maxDim, max(cell.first, cell.second));
                                    }
                                    int cellSize = maxDim > 0 ? itemSize / (maxDim + 1) : itemSize / 3;
                                    
                                    // 计算图块的边界（与drawPieceShape中的逻辑相同）
                                    int minRow = shape[0].first, maxRow = shape[0].first;
                                    int minCol = shape[0].second, maxCol = shape[0].second;
                                    for (const auto& cell : shape) {
                                        minRow = min(minRow, cell.first);
                                        maxRow = max(maxRow, cell.first);
                                        minCol = min(minCol, cell.second);
                                        maxCol = max(maxCol, cell.second);
                                    }
                                    
                                    // 计算图块实际绘制的区域（与drawPieceShape中的逻辑相同）
                                    int shapeWidth = (maxCol - minCol + 1) * cellSize;
                                    int shapeHeight = (maxRow - minRow + 1) * cellSize;
                                    int actualX = x + minCol * cellSize;
                                    int actualY = y + minRow * cellSize;
                                    
                                    // 检查鼠标是否在这个图块的实际绘制区域内
                                    if (mousePos.x >= actualX && mousePos.x < actualX + shapeWidth &&
                                        mousePos.y >= actualY && mousePos.y < actualY + shapeHeight) {
                                        foundPieceIndex = i;
                                        foundInstanceIndex = instance;
                                        break;
                                    }
                                } else {
                                    // 如果没有形状，使用itemSize作为点击区域
                                    if (mousePos.x >= x && mousePos.x < x + itemSize &&
                                        mousePos.y >= y && mousePos.y < y + itemSize) {
                                        foundPieceIndex = i;
                                        foundInstanceIndex = instance;
                                        break;
                                    }
                                }
                                
                                displayIndex++;
                            }
                            
                            if (foundPieceIndex >= 0) break;
                        }
                        
                        if (foundPieceIndex >= 0 && foundPieceIndex < (int)pieces.size()) {
                            // 从预览区开始拖拽
                            draggedPiece.pieceId = pieces[foundPieceIndex].id;
                            draggedPiece.originalRow = -1;  // 从预览区拖出，没有原位置
                            draggedPiece.originalCol = -1;
                            draggedPiece.shapeIndex = 0;
                            draggedPiece.isDragging = true;
                        }
                    }
                    
                    // 检查是否点击在游戏板上（如果点击在编辑器内，跳过游戏板检测）
                    if (!clickedInEditor) {
                    int boardX = mousePos.x - offsetX;
                    int boardY = mousePos.y - offsetY;
                    int col = boardX / CELL_SIZE;
                    int row = boardY / CELL_SIZE;
                    
                    if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
                        int cellValue = board[row][col];
                        if (cellValue != 0) {
                            // 找到图块和形状（保持当前角度，不重置）
                            const Piece* clickedPiece = nullptr;
                            int clickedShapeIndex = 0;
                            int baseRow = -1, baseCol = -1;
                            int clickedCellInShapeRow = -1, clickedCellInShapeCol = -1;  // 点击位置在形状中的相对坐标
                            
                            for (size_t i = 0; i < pieces.size(); i++) {
                                if (pieces[i].id == cellValue) {
                                    clickedPiece = &pieces[i];
                                    // 找到当前使用的形状，通过尝试不同的基础位置来匹配
                                    // 对于L-shape和L-mirror等有多个旋转角度的图块，必须精确匹配正确的角度
                                    for (size_t s = 0; s < pieces[i].shapes.size(); s++) {
                                        const auto& shape = pieces[i].shapes[s];
                                        
                                        // 找到形状中哪个cell对应点击位置
                                        for (const auto& cell : shape) {
                                            // 尝试这个cell作为点击位置
                                            int tryBaseRow = row - cell.first;
                                            int tryBaseCol = col - cell.second;
                                            
                                            // 检查这个基础位置是否匹配整个形状
                                            // 必须所有单元格都匹配，且数量完全一致
                                            bool matches = true;
                                            int matchedCount = 0;
                                            for (const auto& checkCell : shape) {
                                                int checkRow = tryBaseRow + checkCell.first;
                                                int checkCol = tryBaseCol + checkCell.second;
                                                if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                                    checkCol < 0 || checkCol >= BOARD_SIZE) {
                                                    matches = false;
                                                    break;
                                                }
                                                if (board[checkRow][checkCol] == cellValue) {
                                                    matchedCount++;
                                                } else {
                                                    matches = false;
                                                    break;
                                                }
                                            }
                                            
                                            // 必须完全匹配：所有单元格都匹配，且数量等于形状大小
                                            if (matches && matchedCount == (int)shape.size()) {
                                                draggedPiece.shapeIndex = s;  // 保持当前角度
                                                clickedShapeIndex = s;
                                                baseRow = tryBaseRow;
                                                baseCol = tryBaseCol;
                                                clickedCellInShapeRow = cell.first;  // 记录点击位置在形状中的相对坐标
                                                clickedCellInShapeCol = cell.second;
                                                break;
                                            }
                                        }
                                        
                                        if (baseRow >= 0) break;  // 找到匹配后立即退出
                                    }
                                    break;
                                }
                            }
                            
                            // 如果找到了图块的基础位置，开始拖拽
                            if (baseRow >= 0 && baseCol >= 0) {
                                // 开始拖拽游戏板上的图块
                                draggedPiece.pieceId = cellValue;
                                draggedPiece.originalRow = baseRow;  // 使用基础位置，而不是点击位置
                                draggedPiece.originalCol = baseCol;
                                draggedPiece.isDragging = true;
                                
                                // 计算拖拽偏移量：鼠标相对于点击的单元格的位置
                                // 这样在拖拽时，鼠标相对于图块的位置保持不变
                                int clickedCellX = offsetX + col * CELL_SIZE;
                                int clickedCellY = offsetY + row * CELL_SIZE;
                                draggedPiece.dragOffset.x = mousePos.x - clickedCellX;
                                draggedPiece.dragOffset.y = mousePos.y - clickedCellY;
                                
                                // 移除原位置的图块（只移除被选中的那个图块实例）
                                if (clickedPiece && clickedShapeIndex < (int)clickedPiece->shapes.size()) {
                                    const auto& shape = clickedPiece->shapes[clickedShapeIndex];
                                    removePiece(shape, baseRow, baseCol);
                                }
                            }
                        }
                    }
                    }  // 关闭 !clickedInEditor 的if块
                    
                    // 检查是否点击在编辑器区域（处理编辑器内的交互）
                    if (showEditor && clickedInEditor) {
                        int editorX = editorDrag.editorX;
                        int editorY = editorDrag.editorY;
                        int editorWidth = WINDOW_WIDTH - 100;
                        int editorHeight = min(400, WINDOW_HEIGHT - editorY - 50);
                            // 检查是否点击在编辑器标题栏（可拖拽区域）
                            if (mousePos.x >= editorX && mousePos.x < editorX + editorWidth &&
                                mousePos.y >= editorY && mousePos.y < editorY + 50) {
                                editorDrag.isDragging = true;
                                editorDrag.dragOffset.x = mousePos.x - editorX;
                                editorDrag.dragOffset.y = mousePos.y - editorY;
                            }
                            
                            int itemsPerRow = 5;
                            int itemWidth = 150;
                            int itemHeight = 80;
                            int startX = editorX + 20;
                            int startY = editorY + 60;
                            
                            // 计算右侧区域位置
                            int rightAreaX = startX + itemsPerRow * itemWidth + 40;
                            
                            // 检查是否点击在图块列表区域
                            bool clickedInPieceList = false;
                            for (size_t i = 0; i < pieces.size(); i++) {
                                int col = i % itemsPerRow;
                                int row = i / itemsPerRow;
                                int x = startX + col * itemWidth;
                                int y = startY + row * itemHeight;
                                
                                if (mousePos.x >= x && mousePos.x < x + itemWidth &&
                                    mousePos.y >= y && mousePos.y < y + itemHeight) {
                                    selectedPieceType = i;
                                    previewPieceType = i;
                                    clickedInPieceList = true;
                                    break;
                                }
                            }
                            
                            // 检查数量按钮（在右侧区域，不能穿透）
                            if (selectedPieceType >= 0 && !clickedInPieceList) {
                                int btnX = rightAreaX;
                                // 预览区域高度固定为 EDITOR_PREVIEW_SIZE
                                int btnY = startY + EDITOR_PREVIEW_SIZE + 30;
                                
                                // 减号按钮：减少数量，并移除棋盘上多余的实例
                                if (mousePos.x >= btnX + 80 && mousePos.x < btnX + 110 &&
                                    mousePos.y >= btnY && mousePos.y < btnY + 30) {
                                    for (auto& pc : pieceCounts) {
                                        if (pc.pieceId == pieces[selectedPieceType].id && pc.count > 0) {
                                            int oldCount = pc.count;
                                            pc.count--;
                                            
                                            // 如果数量减少，移除棋盘上多余的实例
                                            if (oldCount > pc.count) {
                                                removeExcessInstances(pc.pieceId, pc.count);
                                            }
                                            break;
                                        }
                                    }
                                }
                                
                                // 加号按钮：增加数量（不需要从棋盘添加，因为用户可以手动放置）
                                if (mousePos.x >= btnX + 160 && mousePos.x < btnX + 190 &&
                                    mousePos.y >= btnY && mousePos.y < btnY + 30) {
                                    for (auto& pc : pieceCounts) {
                                        if (pc.pieceId == pieces[selectedPieceType].id) {
                                            pc.count++;
                                            break;
                                        }
                                    }
                                }
                                
                                // 确认按钮
                                if (mousePos.x >= btnX && mousePos.x < btnX + 120 &&
                                    mousePos.y >= btnY + 50 && mousePos.y < btnY + 85) {
                                    // 确认修改，关闭编辑器并清空游戏板（不自动求解）
                                    showEditor = false;
                                    // 清空游戏板
                                    for (int i = 0; i < BOARD_SIZE; i++) {
                                        for (int j = 0; j < BOARD_SIZE; j++) {
                                            board[i][j] = 0;
                                        }
                                    }
                                    // 清除之前的求解结果
                                    solutionFound = false;
                                    solved = false;
                                    showSolution = false;
                                }
                            }
                        }  // 关闭 showEditor && clickedInEditor 的if块
                    } else if (event.mouseButton.button == Mouse::Right) {
                    mouseRightPressed = true;
                    
                    mousePos = Mouse::getPosition(window);  // 更新鼠标位置
                    int offsetX = 50;
                    int offsetY = 50;
                    
                    // 问题1和2修复：确保右键行为正确
                    // 如果正在拖拽，旋转图块（只在拖拽状态下按右键才旋转）
                    if (draggedPiece.isDragging && draggedPiece.pieceId >= 0) {
                        isRotating = true;
                        for (size_t i = 0; i < pieces.size(); i++) {
                            if (pieces[i].id == draggedPiece.pieceId) {
                                if (draggedPiece.shapeIndex < (int)pieces[i].shapes.size()) {
                                    // 旋转到下一个形状
                                    draggedPiece.shapeIndex = 
                                        (draggedPiece.shapeIndex + 1) % pieces[i].shapes.size();
                                }
                                break;
                            }
                        }
                    } else {
                        // 如果不在拖拽状态，右键只取下图块，不旋转
                        // 检查是否点击在游戏板上，如果是则取下图块
                        int boardX = mousePos.x - offsetX;
                        int boardY = mousePos.y - offsetY;
                        int col = boardX / CELL_SIZE;
                        int row = boardY / CELL_SIZE;
                        
                        if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
                            int cellValue = board[row][col];
                            if (cellValue != 0) {
                                // 右键取下图块（只移除被点击的那个图块实例）
                                // 找到对应的图块和形状
                                const Piece* clickedPiece = nullptr;
                                int clickedShapeIndex = 0;
                                int baseRow = -1, baseCol = -1;
                                
                                for (size_t i = 0; i < pieces.size(); i++) {
                                    if (pieces[i].id == cellValue) {
                                        clickedPiece = &pieces[i];
                                        // 找到当前使用的形状，通过尝试不同的基础位置来匹配
                                        for (size_t s = 0; s < pieces[i].shapes.size(); s++) {
                                            const auto& shape = pieces[i].shapes[s];
                                            
                                            // 找到形状中哪个cell对应点击位置
                                            for (const auto& cell : shape) {
                                                // 尝试这个cell作为点击位置
                                                int tryBaseRow = row - cell.first;
                                                int tryBaseCol = col - cell.second;
                                                
                                                // 检查这个基础位置是否匹配整个形状
                                                bool matches = true;
                                                for (const auto& checkCell : shape) {
                                                    int checkRow = tryBaseRow + checkCell.first;
                                                    int checkCol = tryBaseCol + checkCell.second;
                                                    if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                                        checkCol < 0 || checkCol >= BOARD_SIZE ||
                                                        board[checkRow][checkCol] != cellValue) {
                                                        matches = false;
                                                        break;
                                                    }
                                                }
                                                
                                                if (matches) {
                                                    clickedShapeIndex = s;
                                                    baseRow = tryBaseRow;
                                                    baseCol = tryBaseCol;
                                                    break;
                                                }
                                            }
                                            
                                            if (baseRow >= 0) break;
                                        }
                                        break;
                                    }
                                }
                                
                                // 移除被点击的图块实例
                                if (clickedPiece && clickedShapeIndex < (int)clickedPiece->shapes.size() && baseRow >= 0) {
                                    const auto& shape = clickedPiece->shapes[clickedShapeIndex];
                                    removePiece(shape, baseRow, baseCol);
                                }
                            }
                        }
                    }
                }
            }
            
            if (event.type == Event::MouseButtonReleased) {
                mousePos = Mouse::getPosition(window);
                int offsetX = 50;
                int offsetY = 50;
                
                if (event.mouseButton.button == Mouse::Left) {
                    mouseLeftPressed = false;
                    
                    // 停止编辑器拖拽
                    if (editorDrag.isDragging) {
                        editorDrag.isDragging = false;
                    }
                    
                    if (draggedPiece.isDragging) {
                        // 计算放置位置
                        int boardX = mousePos.x - offsetX;
                        int boardY = mousePos.y - offsetY;
                        int col = boardX / CELL_SIZE;
                        int row = boardY / CELL_SIZE;
                        
                        // 找到对应的图块
                        const Piece* piece = nullptr;
                        for (const auto& p : pieces) {
                            if (p.id == draggedPiece.pieceId) {
                                piece = &p;
                                break;
                            }
                        }
                        
                        if (piece && draggedPiece.shapeIndex < (int)piece->shapes.size()) {
                            const auto& shape = piece->shapes[draggedPiece.shapeIndex];
                            
                            // 检查是否可以放置
                            bool canPlaceHere = false;
                            if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) {
                                if (draggedPiece.originalRow >= 0 && draggedPiece.originalCol >= 0) {
                                    // 从游戏板上拖拽：需要检查目标位置，但忽略原始位置
                                    // 重要：只检查形状中实际定义的单元格，不检查边界框内的"透明"区域
                                    canPlaceHere = true;
                                    for (const auto& cell : shape) {
                                        int newRow = row + cell.first;
                                        int newCol = col + cell.second;
                                        
                                        if (newRow < 0 || newRow >= BOARD_SIZE || 
                                            newCol < 0 || newCol >= BOARD_SIZE) {
                                            canPlaceHere = false;
                                            break;
                                        }
                                        
                                        // 检查这个位置是否在原始图块的范围内（允许放回原位置）
                                        // 只检查原始图块实际占据的单元格，不检查边界框
                                        bool isInOriginal = false;
                                        for (const auto& origCell : shape) {
                                            int origRow = draggedPiece.originalRow + origCell.first;
                                            int origCol = draggedPiece.originalCol + origCell.second;
                                            if (newRow == origRow && newCol == origCol) {
                                                isInOriginal = true;
                                                break;
                                            }
                                        }
                                        
                                        // 如果不在原始位置范围内，且被其他图块占据，则不能放置
                                        // 注意：只检查形状中定义的单元格，边界框内的"透明"区域应该被视为空白
                                        if (!isInOriginal && board[newRow][newCol] != 0) {
                                            canPlaceHere = false;
                                            break;
                                        }
                                    }
                                } else {
                                    // 从预览区拖出：正常检查（只检查形状中定义的单元格）
                                    // 问题3修复：确保从预览区拖出时能正确放置
                                    canPlaceHere = canPlace(shape, row, col);
                                }
                            }
                            
                            if (canPlaceHere) {
                                // 放置图块
                                placePiece(shape, row, col, draggedPiece.pieceId);
                            } else {
                                // 无法放置，恢复到原位置（如果有）
                                if (draggedPiece.originalRow >= 0 && draggedPiece.originalCol >= 0) {
                                    placePiece(shape, draggedPiece.originalRow, 
                                             draggedPiece.originalCol, draggedPiece.pieceId);
                                }
                                // 如果从预览区拖出，无法放置时不恢复（图块回到预览区）
                            }
                        }
                        
                        // 重置拖拽状态
                        draggedPiece.isDragging = false;
                        draggedPiece.pieceId = -1;
                    }
                } else if (event.mouseButton.button == Mouse::Right) {
                    mouseRightPressed = false;
                    isRotating = false;
                }
            }
            
            if (event.type == Event::MouseMoved) {
                mousePos = Mouse::getPosition(window);
                
                // 处理编辑器拖拽
                if (editorDrag.isDragging) {
                    editorDrag.editorX = mousePos.x - editorDrag.dragOffset.x;
                    editorDrag.editorY = mousePos.y - editorDrag.dragOffset.y;
                    // 限制在窗口内
                    editorDrag.editorX = max(0, min(editorDrag.editorX, WINDOW_WIDTH - 400));
                    editorDrag.editorY = max(0, min(editorDrag.editorY, WINDOW_HEIGHT - 400));
                }
            }
        }
        
        window.clear(Color(240, 240, 240));
        drawBoard(window, font);
        window.display();
    }
    
    return 0;
}

