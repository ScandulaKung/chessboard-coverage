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

// 6x6游戏板
const int BOARD_SIZE = 6;
const int CELL_SIZE = 60;
const int WINDOW_WIDTH = 1800;
const int WINDOW_HEIGHT = 1000;
const int PREVIEW_CELL_SIZE = 30;
const int EDITOR_PREVIEW_SIZE = 200;

vector<vector<int>> board(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
vector<vector<int>> solutionBoard(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
vector<vector<int>> bestBoard(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));  // 保存最好的成果
int bestFilledCells = 0;  // 最好的填充单元格数
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
    int tempId;  // 临时唯一ID，用于区分同形状同角度的不同图块实例
};

vector<PieceCount> pieceCounts;  // 每种图块的数量
int nextTempId = 1000;  // 下一个可用的临时ID（从1000开始，避免与pieceId冲突）
bool showEditor = false;  // 是否显示编辑器
int selectedPieceType = -1;  // 选中的图块类型（用于编辑器）
int previewPieceType = -1;  // 预览的图块类型（用于编辑器示意图）

// 键盘操作相关（未开启编辑器时）
int previewSelectedIndex = -1;  // 预览区选中的图块索引（当编辑器未开启时）
int keyboardPlaceRow = -1;  // 键盘操作时，准备放置图块的行位置
int keyboardPlaceCol = -1;  // 键盘操作时，准备放置图块的列位置
int keyboardPlacePieceId = -1;  // 键盘操作时，准备放置的图块ID
int keyboardPlaceShapeIndex = -1;  // 键盘操作时，准备放置的图块形状索引

// 拖拽相关
struct DraggedPiece {
    int pieceId;
    int tempId;  // 临时唯一ID，用于区分同形状同角度的不同图块实例
    int shapeIndex;
    int originalRow;
    int originalCol;
    bool isDragging;
    Vector2i dragOffset;
};

DraggedPiece draggedPiece = {-1, -1, 0, -1, -1, false, {0, 0}};
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

// 前向声明
void assignTempIds();
void clearOldTempIds();

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
    
    // 16. AAA/A A形状（3个单元格，L形但中间有空隙）
    // 形状：第一行三个，第三行第一个（中间行空）
    // 例如：AAA
    //       A A
    vector<pair<int, int>> Big_cBase = {{0,0}, {0,1}, {0,2}, {1,0},{1,2}};
    vector<pair<int, int>> Big_c90 = rotateShape(Big_cBase);   // 90度
    vector<pair<int, int>> Big_c180 = rotateShape(Big_c90);     // 180度
    vector<pair<int, int>> Big_c270 = rotateShape(Big_c180);   // 270度
    pieces.push_back({
        "Big_c",
        {Big_cBase, Big_c90, Big_c180, Big_c270},
        16,
        colors[colorIndex++]
    });
    
    // 17. BBBBB形状（5个单元格的直线）
    pieces.push_back({
        "line5",
        {{{0,0}, {0,1}, {0,2}, {0,3}, {0,4}},
         {{0,0}, {1,0}, {2,0}, {3,0}, {4,0}}},
        17,
        colors[colorIndex++]
    });
    
    piecePositions.resize(pieces.size());
    
    // 初始化图块数量（根据用户要求设置，保证有解的组合）
    pieceCounts.clear();
    // 为每个图块设置初始数量
    for (const auto& piece : pieces) {
        int count = 0;  // 默认0个
        int shapeIndex = 0;  // 默认0度
        
        // 用户配置：一个small-L 90度，一个big-c，一个line5 90度，一个cross，二个line3 90度，一个line4，一个L-mirror，一个L-shape 90度
        if (piece.name == "L3") {  // small-L
            count = 1;
            shapeIndex = 2;  // 180度
        } else if (piece.name == "Big_c") {  // big-c
            count = 1;
            shapeIndex = 0;  // 0度
        } else if (piece.name == "line5") {
            count = 1;
            shapeIndex = 1;  // 90度（垂直方向）
        } else if (piece.name == "cross") {
            count = 1;
            shapeIndex = 0;  // 0度
        } else if (piece.name == "line3") {
            count = 2;
            shapeIndex = 1;  // 90度（垂直方向）
        } else if (piece.name == "line4") {
            count = 1;
            shapeIndex = 0;  // 0度
        } else if (piece.name == "L-mirror") {
            count = 1;
            shapeIndex = 0;  // 0度
        } else if (piece.name == "L-shape") {
            count = 1;
            shapeIndex = 1;  // 90度
        }
        
        pieceCounts.push_back({piece.id, count, shapeIndex, 0});  // tempId将在初始化时分配
    }
    
    // 为所有pieceCounts分配临时唯一ID
    assignTempIds();
}

// 为pieceCounts分配临时唯一ID
void assignTempIds() {
    for (auto& pc : pieceCounts) {
        if (pc.count > 0) {
            // 每个pieceCounts条目都分配一个唯一的tempId
            // 即使同形状同角度，也分配不同的tempId
            pc.tempId = nextTempId++;
        } else {
            pc.tempId = 0;  // count为0时，tempId为0
        }
    }
}

// 清理board上的旧临时ID（在编辑器确认更改时调用）
void clearOldTempIds() {
    // 收集所有旧的tempId
    set<int> oldTempIds;
    for (const auto& pc : pieceCounts) {
        if (pc.tempId > 0) {
            oldTempIds.insert(pc.tempId);
        }
    }
    
    // 清理board和solutionBoard上的旧tempId
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (oldTempIds.find(board[i][j]) != oldTempIds.end()) {
                board[i][j] = 0;
            }
            if (oldTempIds.find(solutionBoard[i][j]) != oldTempIds.end()) {
                solutionBoard[i][j] = 0;
            }
        }
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
    baseTime = max(baseTime, 60.0f);
    
    // 最大时间限制（避免过长）
    baseTime = min(baseTime, 300.0f);  // 最多5分钟
    
    return baseTime;
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

// 应用已知的解（如果存在）

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
// 如果showSolution为true，使用solutionBoard；否则使用board
// 计算指定棋盘上每个pieceCounts条目的已使用数量（统一的计数逻辑）
// 这个函数被solve()和calculateUsedPieceCounts()共同使用，确保计数一致
vector<int> countPiecesOnBoard(const vector<vector<int>>& targetBoard, const vector<PieceCount>& targetCounts) {
    // 返回按targetCounts索引的已使用数量数组，每个pieceCounts条目对应一个计数
    vector<int> usedCounts(targetCounts.size(), 0);
    vector<vector<bool>> counted(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    
        for (int row = 0; row < BOARD_SIZE; row++) {
            for (int col = 0; col < BOARD_SIZE; col++) {
            if (targetBoard[row][col] != 0 && !counted[row][col]) {
                // 找到这个格子所属的临时ID
                int tempId = targetBoard[row][col];
                
                // 先通过DFS找到这个图块实例的所有单元格
                    vector<vector<bool>> visited(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
                set<pair<int, int>> instanceCells;
                    
                    function<void(int, int)> dfs = [&](int r, int c) {
                        if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return;
                    if (visited[r][c] || targetBoard[r][c] != tempId) return;
                        
                        visited[r][c] = true;
                    instanceCells.insert({r, c});
                        
                        // 检查四个方向的相邻格子
                        dfs(r - 1, c);
                        dfs(r + 1, c);
                        dfs(r, c - 1);
                        dfs(r, c + 1);
                    };
                    
                    dfs(row, col);
                    
                // 尝试匹配这个实例到某个pieceCounts条目
                // 通过匹配shape来确定pieceId和currentShapeIndex
                size_t matchedPcIndex = SIZE_MAX;
                const Piece* matchedPiece = nullptr;
                int matchedShapeIndex = -1;
                
                // 遍历所有pieceCounts条目，尝试匹配
                for (size_t pcIndex = 0; pcIndex < targetCounts.size(); pcIndex++) {
                    const auto& pc = targetCounts[pcIndex];
                    if (pc.count == 0) continue;
                    
                    // 找到对应的图块定义
                    const Piece* piece = nullptr;
                    for (size_t i = 0; i < pieces.size(); i++) {
                        if (pieces[i].id == pc.pieceId) {
                            piece = &pieces[i];
                            break;
                        }
                    }
                    
                    if (!piece || piece->shapes.empty()) continue;
                    
                    // 尝试匹配到指定的shapeIndex
                    int shapeIndex = pc.currentShapeIndex;
                    if (shapeIndex < 0 || shapeIndex >= (int)piece->shapes.size()) {
                        shapeIndex = 0;
                    }
                    
                    const auto& shape = piece->shapes[shapeIndex];
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
                                if (targetBoard[checkRow][checkCol] != tempId) {
                                    matches = false;
                                    break;
                                }
                                shapeCells.insert({checkRow, checkCol});
                            }
                            
                            if (matches && shapeCells.size() == instanceCells.size()) {
                                bool allMatch = true;
                                for (const auto& cell : instanceCells) {
                                    if (shapeCells.find(cell) == shapeCells.end()) {
                                        allMatch = false;
                                        break;
                                    }
                                }
                                if (allMatch) {
                                    matchedPcIndex = pcIndex;
                                    matchedPiece = piece;
                                    matchedShapeIndex = shapeIndex;
                                    break;
                                }
                            }
                        }
                        if (matchedPcIndex != SIZE_MAX) break;
                    }
                    if (matchedPcIndex != SIZE_MAX) break;
                    }
                    
                // 如果找到了匹配的pieceCounts条目，计数并标记单元格
                if (matchedPcIndex != SIZE_MAX && matchedPiece != nullptr && matchedShapeIndex >= 0) {
                    const auto& pc = targetCounts[matchedPcIndex];
                    // 检查匹配的shapeIndex是否与pieceCounts中指定的currentShapeIndex一致
                    int expectedShapeIndex = pc.currentShapeIndex;
                    if (expectedShapeIndex < 0 || expectedShapeIndex >= (int)matchedPiece->shapes.size()) {
                        expectedShapeIndex = matchedShapeIndex;
                    }
                    if (matchedShapeIndex == expectedShapeIndex) {
                        usedCounts[matchedPcIndex]++;
                        // 只有在匹配成功后才标记所有单元格为已计数
                        for (const auto& cell : instanceCells) {
                            counted[cell.first][cell.second] = true;
                        }
                    }
                }
            }
        }
    }
    
    return usedCounts;
}

vector<int> calculateUsedPieceCounts() {
    // 选择使用哪个棋盘：如果显示解，使用solutionBoard；否则使用board
    const vector<vector<int>>* currentBoard = showSolution ? &solutionBoard : &board;
    
    // 使用统一的计数函数
    return countPiecesOnBoard(*currentBoard, pieceCounts);
}

// 前向声明
void drawPieceTexture(RenderWindow& window, const Piece& piece, int shapeIndex,
                     int baseRow, int baseCol, int offsetX, int offsetY, int cellSize,
                     vector<vector<bool>>& drawn);

// 清除预览区缓存的函数（在图块取下或放置后调用，强制重新渲染）
void clearPreviewCache() {
    // 注意：previewCache是drawPieceShape中的静态变量，无法直接清除
    // 但可以通过重新计算来强制刷新预览区显示
    // 实际上，由于calculateUsedPieceCounts()会在每帧重新计算，预览区应该会自动更新
    // 这里添加一个标记，确保预览区会重新计算
}

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
            // 注意：这个缓存是静态的，但在图块数量变化时，预览区会通过calculateUsedPieceCounts()重新计算可用数量
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
    // 只要选中了图块就显示，不要求previewPieceType也设置
    if (selectedPieceType >= 0 && selectedPieceType < (int)pieces.size()) {
        // 确保previewPieceType与selectedPieceType同步
        previewPieceType = selectedPieceType;
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
        // 找到对应的pieceCounts条目
        int currentShapeIndex = 0;
        for (const auto& pc : pieceCounts) {
            if (pc.pieceId == previewPiece.id) {
                currentShapeIndex = pc.currentShapeIndex;
                break;
            }
        }
        if (!previewPiece.shapes.empty() && 
            currentShapeIndex >= 0 && currentShapeIndex < (int)previewPiece.shapes.size()) {
            const auto& shape = previewPiece.shapes[currentShapeIndex];
            
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
            drawPieceShape(window, previewPiece, currentShapeIndex,
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
        
        // 角度选择区域（在数量修改下方）
        if (fontAvailable && !previewPiece.shapes.empty()) {
            Text angleLabel("Angle: ", font, 16);
            angleLabel.setPosition(previewAreaX, rightCurrentY);
            angleLabel.setFillColor(Color::Black);
            window.draw(angleLabel);
            
            // 角度选择按钮（左箭头）
            RectangleShape leftAngleBtn(Vector2f(30, 30));
            leftAngleBtn.setPosition(previewAreaX + 80, rightCurrentY);
            leftAngleBtn.setFillColor(Color(200, 200, 200));
            leftAngleBtn.setOutlineThickness(1);
            leftAngleBtn.setOutlineColor(Color::Black);
            window.draw(leftAngleBtn);
            
            Text leftAngleText("<", font, 20);
            leftAngleText.setPosition(previewAreaX + 90, rightCurrentY);
            leftAngleText.setFillColor(Color::Black);
            window.draw(leftAngleText);
            
            // 当前角度显示
            int currentAngle = 0;
            for (const auto& pc : pieceCounts) {
                if (pc.pieceId == previewPiece.id) {
                    currentAngle = pc.currentShapeIndex;
                    break;
                }
            }
            int maxAngle = (int)previewPiece.shapes.size() - 1;
            string angleStr = to_string(currentAngle) + "/" + to_string(maxAngle);
            Text angleDisplay(angleStr, font, 16);
            angleDisplay.setPosition(previewAreaX + 120, rightCurrentY + 5);
            angleDisplay.setFillColor(Color::Black);
            window.draw(angleDisplay);
            
            // 角度选择按钮（右箭头）
            RectangleShape rightAngleBtn(Vector2f(30, 30));
            rightAngleBtn.setPosition(previewAreaX + 180, rightCurrentY);
            rightAngleBtn.setFillColor(Color(200, 200, 200));
            rightAngleBtn.setOutlineThickness(1);
            rightAngleBtn.setOutlineColor(Color::Black);
            window.draw(rightAngleBtn);
            
            Text rightAngleText(">", font, 20);
            rightAngleText.setPosition(previewAreaX + 190, rightCurrentY);
            rightAngleText.setFillColor(Color::Black);
            window.draw(rightAngleText);
        }
        
        rightCurrentY += 50;
        
        // 确认修改按钮（放在角度选择下方）
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
    // 改进：遍历pieceCounts而不是pieces，确保每个pieceCounts条目都被正确显示
    int displayIndex = 0;
    for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) {
        const auto& pc = pieceCounts[pcIndex];
        
        // 找到对应的图块定义
        const Piece* piece = nullptr;
    for (size_t i = 0; i < pieces.size(); i++) {
            if (pieces[i].id == pc.pieceId) {
                piece = &pieces[i];
                break;
            }
        }
        
        if (!piece || pc.count == 0) continue;
        
        // 计算可用数量（总数量 - 已使用数量）
        // usedCounts现在按pieceCounts索引，所以直接使用pcIndex
        int availableCount = pc.count - usedCounts[pcIndex];
        
        // 获取该图块的角度设置
        int shapeIndex = pc.currentShapeIndex;
        if (shapeIndex < 0 || shapeIndex >= (int)piece->shapes.size()) {
            shapeIndex = 0;  // 如果索引无效，使用0度
        }
        
        // 显示所有可用的图块实例
        for (int instance = 0; instance < availableCount; instance++) {
            int col = (displayIndex % itemsPerRow);
            int row = (displayIndex / itemsPerRow);
            int x = previewX + 20 + col * (itemSize + spacing);
            int y = currentY + row * (itemSize + spacing);
            
            // 绘制图块（使用贴图，使用pieceCounts指定的角度）
            if (!piece->shapes.empty()) {
                int maxDim = 0;
                for (const auto& cell : piece->shapes[shapeIndex]) {
                    maxDim = max(maxDim, max(cell.first, cell.second));
                }
                int cellSize = maxDim > 0 ? itemSize / (maxDim + 1) : itemSize / 3;
                drawPieceShape(window, *piece, shapeIndex, x, y, cellSize, true);
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
void removeExcessInstances(int pieceId, int maxCount) 
{
    vector<PieceInstance> instances = getPlacedInstances(pieceId);
    
    if ((int)instances.size() <= maxCount) return;
    
    // 按位置排序，优先移除位置靠后的实例
    sort(instances.begin(), instances.end(),
        [](const PieceInstance& a, const PieceInstance& b) 
        {
            if (a.baseRow != b.baseRow) 
            {
                return a.baseRow > b.baseRow;  // 行号大的先移除
            }
            return a.baseCol > b.baseCol;  // 列号大的先移除
        });
    
    // 移除多余的实例
    int toRemove = (int)instances.size() - maxCount;
    for (int i = 0; i < toRemove; i++) 
    {
        const auto& instance = instances[i];
        // 移除这个实例的所有单元格
        for (const auto& cell : instance.cells) 
        {
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

// 更新solutionBoard以实时显示求解过程（使用try_lock避免阻塞）
void updateSolutionBoardDisplay() {
    if (boardMutex.try_lock()) {
        solutionBoard = board;
        showSolution = true;
        boardMutex.unlock();
        }
    }
    
bool solve(int pieceIndex, const vector<PieceCount>& counts, int& nextTempIdForSolve) {
    // 限时检查已移除，允许无限时间求解
    // nextTempIdForSolve: 用于为每个图块实例分配唯一的tempId
    
    // 快速计算已填充的单元格数（避免重复遍历）
    int filledCells = 0;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (board[i][j] != 0) filledCells++;
        }
    }
    
    // 跟踪最好的成果：如果当前状态比之前最好的状态更好，保存它
    if (filledCells > bestFilledCells) {
        bestFilledCells = filledCells;
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                bestBoard[i][j] = board[i][j];
            }
        }
        
        // 实时更新solutionBoard，让主线程能够显示求解过程
        // 为了避免频繁加锁影响性能，每10次更新才真正更新一次（或者每次更新都更新，因为bestBoard更新频率不高）
        // 使用try_lock避免阻塞，如果无法获取锁就跳过这次更新
        if (boardMutex.try_lock()) {
            solutionBoard = bestBoard;
            showSolution = true;  // 自动显示求解过程
            boardMutex.unlock();
        }
    }
    
    // 预先计算每个pieceCounts条目的已放置实例数（按pieceId+shapeIndex组合）
    // 使用统一的计数函数，确保与calculateUsedPieceCounts一致
    vector<int> placedCountsByIndex = countPiecesOnBoard(board, counts);
    
    // 验证每个pieceCounts条目的使用数量是否正好等于用户指定的数量
    bool allPiecesUsedCorrectly = true;
    for (size_t pcIndex = 0; pcIndex < counts.size(); pcIndex++) {
        const auto& pc = counts[pcIndex];
        int placedCount = placedCountsByIndex[pcIndex];
        
        // 使用数量必须正好等于用户指定的数量
        if (placedCount != pc.count) {
            allPiecesUsedCorrectly = false;
            break;
        }
    }
    
    // 只有当游戏板填满且所有图块类型都使用了正确的数量时，才认为求解成功
    if (filledCells == BOARD_SIZE * BOARD_SIZE && allPiecesUsedCorrectly) {
        // 确保bestBoard是最新的完整解（虽然上面已经更新过，但为了保险再更新一次）
        if (filledCells > bestFilledCells) {
            bestFilledCells = filledCells;
            for (int i = 0; i < BOARD_SIZE; i++) {
                for (int j = 0; j < BOARD_SIZE; j++) {
                    bestBoard[i][j] = board[i][j];
                }
            }
            
            // 更新solutionBoard，显示完整解
            if (boardMutex.try_lock()) {
                solutionBoard = bestBoard;
                showSolution = true;
                boardMutex.unlock();
            }
        }
        return true;
    }
    
    // 剪枝优化：如果剩余空间不足以放置剩余图块，提前返回false
    // 注意：这里使用filledCells（实际填充的单元格数）而不是placedCountsByIndex（识别的实例数）
    // 因为countPiecesOnBoard可能无法正确识别所有实例（特别是当多个实例相邻时）
    // 所以，我们使用更保守的剪枝策略：只有当剩余空间明显不足时才剪枝
    int emptyCells = BOARD_SIZE * BOARD_SIZE - filledCells;
    int requiredCells = 0;
    for (size_t pcIndex = 0; pcIndex < counts.size(); pcIndex++) {
        const auto& pc = counts[pcIndex];
        if (pc.count > 0) {
            int placedCount = placedCountsByIndex[pcIndex];
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
                    // 使用指定的shapeIndex对应的形状大小
                    int shapeIndex = pc.currentShapeIndex;
                    if (shapeIndex < 0 || shapeIndex >= (int)piece->shapes.size()) {
                        shapeIndex = 0;
                    }
                    int cellCount = (int)piece->shapes[shapeIndex].size();
                    requiredCells += remaining * cellCount;
                }
            }
        }
    }
    // 修改剪枝逻辑：只有当剩余空间明显不足时才剪枝（留一些余量，避免因计数错误而误剪）
    // 如果requiredCells > emptyCells + 1，说明即使考虑计数误差，也不可能成功
    if (requiredCells > emptyCells + 1) {
        return false;  // 剩余空间明显不足，剪枝
    }
    
    // 创建一个图块列表，按大小和剩余数量排序（大的先放，剩余数量少的优先）
    // 每个pieceCounts条目都单独处理
    vector<tuple<size_t, int, int>> pieceList; // {pcIndex, size, remaining}
    for (size_t pcIndex = 0; pcIndex < counts.size(); pcIndex++) {
        const auto& pc = counts[pcIndex];
        // 如果数量为0，跳过
        if (pc.count == 0) continue;
        
        // 使用预先计算的结果
        int placedCount = placedCountsByIndex[pcIndex];
        
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
        
        // 使用指定的shapeIndex对应的形状大小
        int shapeIndex = pc.currentShapeIndex;
        if (shapeIndex < 0 || shapeIndex >= (int)piece->shapes.size()) {
            shapeIndex = 0;
        }
        int pieceSize = (int)piece->shapes[shapeIndex].size();
        int remaining = pc.count - placedCount;
        pieceList.push_back({pcIndex, pieceSize, remaining});
    }
    
    // 如果没有任何可放置的图块，返回false
    if (pieceList.empty()) {
        return false;
    }
    
    // 按大小降序排序，然后按剩余数量升序排序（大的先放，剩余少的优先）
    sort(pieceList.begin(), pieceList.end(), 
         [](const tuple<size_t, int, int>& a, const tuple<size_t, int, int>& b) {
             if (get<1>(a) != get<1>(b)) {
                 return get<1>(a) > get<1>(b);  // 大小降序
             }
             return get<2>(a) < get<2>(b);  // 剩余数量升序
         });
    
    // 尝试放置每种类型的图块（按排序后的顺序）
    for (const auto& item : pieceList) {
        size_t pcIndex = get<0>(item);
        
        // 直接使用pcIndex获取对应的PieceCount
        const PieceCount* pc = &counts[pcIndex];
        if (!pc) continue;
        
        // 找到对应的图块
        const Piece* piece = nullptr;
        for (const auto& p : pieces) {
            if (p.id == pc->pieceId) {
                piece = &p;
                break;
            }
        }
        
        if (!piece) continue;
        
        // 只使用指定的角度（currentShapeIndex），不再允许旋转
        int shapeIndex = pc->currentShapeIndex;
        if (shapeIndex < 0 || shapeIndex >= (int)piece->shapes.size()) {
            shapeIndex = 0;  // 如果索引无效，使用第一个形状
        }
        const auto& shape = piece->shapes[shapeIndex];
        
        // 获取图块大小（用于优化搜索范围）
        int pieceSize = (int)shape.size();
        
        // 只尝试指定的形状和位置（不再遍历所有角度）
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
            
            // 优化：对于1x1图块，简化搜索逻辑（只要有空格就能放置）
            if (pieceSize == 1) {
                // 1x1图块：收集所有空位，按顺序尝试
                int remaining1x1 = pc->count - placedCountsByIndex[pcIndex];
                
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
                
                // 按顺序尝试每个空位，每次只放置一个1x1，然后递归
                // 这样可以避免组合爆炸，让递归自然地处理剩余图块
                    for (const auto& pos : emptyCells) {
                    // 为每个实例分配唯一的tempId
                    int instanceTempId = nextTempIdForSolve++;
                    placePiece(shape, pos.first, pos.second, instanceTempId);
                    updateSolutionBoardDisplay();  // 更新显示
                    
                    if (solve(pieceIndex + 1, counts, nextTempIdForSolve)) {
                        return true;
                    }
                    
                        removePiece(shape, pos.first, pos.second);
                    updateSolutionBoardDisplay();  // 更新显示（移除后）
                    }
                
                return false;  // 所有位置都尝试过了，没有找到解
                } else {
                // 大图块：从左上角开始，按行优先顺序尝试
                // 优化：对于cross（5格），使用更智能的搜索策略
                if (pieceSize == 5 && piece->name == "cross") {
                    // Cross特殊优化：快速收集所有可放置位置，然后按优先级尝试
                    // 避免在收集位置时进行耗时的孤立区域计算
                    vector<pair<int, int>> positions; // 所有可放置位置
                    vector<pair<int, int>> priorityPositions; // 优先位置（特定模式）
                    
                    // 快速收集所有可放置位置（不进行耗时计算）
                    for (int row = 0; row < maxRow; row++) {
                        for (int col = 0; col < maxCol; col++) {
                            if (canPlace(shape, row, col)) {
                                // 检查是否是优先位置（特定模式）
                                bool isPriority = false;
                                // 如果放在第1行（row=1），优先尝试
                                if (row == 1) {
                                    isPriority = true;
                                }
                                // 如果放在第4行（row=4），优先尝试
                                else if (row == 4) {
                                    isPriority = true;
                                }
                                // 如果放在第0行且col=1或4，优先尝试
                                else if (row == 0 && (col == 1 || col == 4)) {
                                    isPriority = true;
                                }
                                // 如果放在第2行且col=1或4，优先尝试
                                else if (row == 2 && (col == 1 || col == 4)) {
                                    isPriority = true;
                                }
                                // 如果放在第3行且col=1或4，优先尝试
                                else if (row == 3 && (col == 1 || col == 4)) {
                                    isPriority = true;
                                }
                                // 如果放在第5行且col=1或4，优先尝试
                                else if (row == 5 && (col == 1 || col == 4)) {
                                    isPriority = true;
                                }
                                
                                if (isPriority) {
                                    priorityPositions.push_back({row, col});
                                } else {
                                    positions.push_back({row, col});
                                }
                            }
                        }
                    }
                    
                    // 先尝试优先位置
                    for (const auto& pos : priorityPositions) {
                        // 为每个实例分配唯一的tempId
                        int instanceTempId = nextTempIdForSolve++;
                        placePiece(shape, pos.first, pos.second, instanceTempId);
                        updateSolutionBoardDisplay();  // 更新显示
                        
                        if (solve(pieceIndex + 1, counts, nextTempIdForSolve)) {
                            return true;
                        }
                        
                        removePiece(shape, pos.first, pos.second);
                        updateSolutionBoardDisplay();  // 更新显示（移除后）
                    }
                    
                    // 然后尝试其他位置
                    for (const auto& pos : positions) {
                        // 为每个实例分配唯一的tempId
                        int instanceTempId = nextTempIdForSolve++;
                        placePiece(shape, pos.first, pos.second, instanceTempId);
                        updateSolutionBoardDisplay();  // 更新显示
                        
                        if (solve(pieceIndex + 1, counts, nextTempIdForSolve)) {
                            return true;
                        }
                        
                        removePiece(shape, pos.first, pos.second);
                        updateSolutionBoardDisplay();  // 更新显示（移除后）
                }
            } else {
                    // 其他大图块：正常搜索
                    // 优化：对于3x3这样的正方形图块，使用更智能的搜索策略
                    if (pieceSize == 9 && piece->name == "3x3") {
                        // 3x3图块：优先尝试角落位置，然后尝试边缘位置，最后尝试中心位置
                        vector<pair<int, int>> cornerPositions;  // 角落位置
                        vector<pair<int, int>> edgePositions;   // 边缘位置
                        vector<pair<int, int>> centerPositions; // 中心位置
                        
                    for (int row = 0; row < maxRow; row++) {
                        for (int col = 0; col < maxCol; col++) {
                if (canPlace(shape, row, col)) {
                                    // 判断位置类型
                                    bool isCorner = (row == 0 || row == maxRow - 1) && (col == 0 || col == maxCol - 1);
                                    bool isEdge = (row == 0 || row == maxRow - 1) || (col == 0 || col == maxCol - 1);
                                    
                                    if (isCorner) {
                                        cornerPositions.push_back({row, col});
                                    } else if (isEdge) {
                                        edgePositions.push_back({row, col});
                                    } else {
                                        centerPositions.push_back({row, col});
                            }
                        }
                    }
                        }
                        
                        // 先尝试角落位置
                        for (const auto& pos : cornerPositions) {
                            // 为每个实例分配唯一的tempId
                            int instanceTempId = nextTempIdForSolve++;
                            placePiece(shape, pos.first, pos.second, instanceTempId);
                            updateSolutionBoardDisplay();  // 更新显示
                            if (solve(pieceIndex + 1, counts, nextTempIdForSolve)) {
                        return true;
                    }
                            removePiece(shape, pos.first, pos.second);
                            updateSolutionBoardDisplay();  // 更新显示（移除后）
                        }
                        
                        // 然后尝试边缘位置
                        for (const auto& pos : edgePositions) {
                            // 为每个实例分配唯一的tempId
                            int instanceTempId = nextTempIdForSolve++;
                            placePiece(shape, pos.first, pos.second, instanceTempId);
                            updateSolutionBoardDisplay();  // 更新显示
                            if (solve(pieceIndex + 1, counts, nextTempIdForSolve)) {
                                return true;
                            }
                            removePiece(shape, pos.first, pos.second);
                            updateSolutionBoardDisplay();  // 更新显示（移除后）
                        }
                        
                        // 最后尝试中心位置
                        for (const auto& pos : centerPositions) {
                            // 为每个实例分配唯一的tempId
                            int instanceTempId = nextTempIdForSolve++;
                            placePiece(shape, pos.first, pos.second, instanceTempId);
                            updateSolutionBoardDisplay();  // 更新显示
                            if (solve(pieceIndex + 1, counts, nextTempIdForSolve)) {
                                return true;
                            }
                            removePiece(shape, pos.first, pos.second);
                            updateSolutionBoardDisplay();  // 更新显示（移除后）
                }
                } else {
                    // 其他大图块：正常搜索
                    for (int row = 0; row < maxRow; row++) {
                        for (int col = 0; col < maxCol; col++) {
                            if (canPlace(shape, row, col)) {
                                // 为每个实例分配唯一的tempId
                                int instanceTempId = nextTempIdForSolve++;
                                placePiece(shape, row, col, instanceTempId);
                                updateSolutionBoardDisplay();  // 更新显示
                                
                                if (solve(pieceIndex + 1, counts, nextTempIdForSolve)) {
                                    return true;
                                }
                                
                                removePiece(shape, row, col);
                                updateSolutionBoardDisplay();  // 更新显示（移除后）
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
    
    // 使用与calculateUsedPieceCounts相同的计数逻辑，确保只绘制符合pieceCounts限制的图块
    // 选择使用哪个棋盘：如果显示解，使用solutionBoard；否则使用board
    const vector<vector<int>>* currentBoard = showSolution ? &solutionBoard : &board;
    
    // 先计算应该绘制的图块数量（使用与calculateUsedPieceCounts相同的逻辑）
    // 注意：当showSolution=false时（用户手动编辑），需要正确计数已放置的图块
    vector<int> shouldDrawCounts;
    if (showSolution) {
        // 自动求解结果：只绘制符合pieceCounts限制的图块
        shouldDrawCounts = countPiecesOnBoard(*currentBoard, pieceCounts);
    } else {
        // 用户手动编辑：需要正确计数已放置的图块，然后显示所有已放置的图块
        // 使用countPiecesOnBoard来正确计数，确保计数逻辑与自动求解一致
        shouldDrawCounts = countPiecesOnBoard(*currentBoard, pieceCounts);
        // 但是，为了确保所有已放置的图块都能被绘制，我们需要设置一个足够大的值
        // 或者直接使用计数结果，因为countPiecesOnBoard会返回实际已放置的数量
        // 这里我们使用计数结果，但确保至少能绘制所有已放置的图块
        for (size_t i = 0; i < shouldDrawCounts.size(); i++) {
            // 确保至少能绘制已放置的图块数量
            if (shouldDrawCounts[i] < pieceCounts[i].count) {
                // 如果已放置的数量小于配置的数量，使用配置的数量（允许继续放置）
                shouldDrawCounts[i] = pieceCounts[i].count;
            }
        }
    }
    
    // 计算已绘制的图块数量（按pieceCounts索引）
    vector<int> drawnCounts(pieceCounts.size(), 0);
    
    // 问题修复：三个以上相邻图块消失和移位的问题
    // 使用临时标记数组来跟踪在当前绘制周期内已匹配的单元格
    // 这样可以避免多个图块相邻时重复匹配或错误匹配
    vector<vector<bool>> matchedInThisFrame(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
    
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            int cellValue = (*currentBoard)[i][j];
            if (cellValue != 0 && !drawn[i][j] && !matchedInThisFrame[i][j]) 
            {
                const Piece* piece = nullptr;
                int shapeIndex = -1;
                int baseRow = -1, baseCol = -1;
                
                // 当showSolution为true时，solutionBoard中的tempId是从10000开始的，无法匹配pieceCounts中的tempId
                // 所以需要通过匹配shape来识别图块，而不是通过tempId
                int pieceId = -1;
                if (showSolution) {
                    // 对于求解结果，通过匹配shape来识别图块（与countPiecesOnBoard相同的逻辑）
                    // 先通过DFS找到这个图块实例的所有单元格
                    int tempId = cellValue;
                    vector<vector<bool>> visited(BOARD_SIZE, vector<bool>(BOARD_SIZE, false));
                    set<pair<int, int>> instanceCells;
                    
                    function<void(int, int)> dfs = [&](int r, int c) {
                        if (r < 0 || r >= BOARD_SIZE || c < 0 || c >= BOARD_SIZE) return;
                        if (visited[r][c] || (*currentBoard)[r][c] != tempId) return;
                        
                        visited[r][c] = true;
                        instanceCells.insert({r, c});
                        
                        dfs(r - 1, c);
                        dfs(r + 1, c);
                        dfs(r, c - 1);
                        dfs(r, c + 1);
                    };
                    
                    dfs(i, j);
                    
                    // 尝试匹配这个实例到某个pieceCounts条目
                    for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) {
                        const auto& pc = pieceCounts[pcIndex];
                        if (pc.count == 0) continue;
                        
                        const Piece* candidatePiece = nullptr;
                        for (size_t idx = 0; idx < pieces.size(); idx++) {
                            if (pieces[idx].id == pc.pieceId) {
                                candidatePiece = &pieces[idx];
                                break;
                            }
                        }
                        
                        if (!candidatePiece || candidatePiece->shapes.empty()) continue;
                        
                        // 尝试匹配到指定的shapeIndex
                        int testShapeIndex = pc.currentShapeIndex;
                        if (testShapeIndex < 0 || testShapeIndex >= (int)candidatePiece->shapes.size()) {
                            testShapeIndex = 0;
                        }
                        
                        const auto& shape = candidatePiece->shapes[testShapeIndex];
                        if ((int)shape.size() != (int)instanceCells.size()) continue;
                        
                        // 尝试匹配
                        bool matched = false;
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
                                    if ((*currentBoard)[checkRow][checkCol] != tempId) {
                                        matches = false;
                                        break;
                                    }
                                    shapeCells.insert({checkRow, checkCol});
                                }
                                
                                if (matches && shapeCells.size() == instanceCells.size()) {
                                    bool allMatch = true;
                                    for (const auto& cell : instanceCells) {
                                        if (shapeCells.find(cell) == shapeCells.end()) {
                                            allMatch = false;
                                            break;
                                        }
                                    }
                                    if (allMatch) {
                                        pieceId = pc.pieceId;
                                        piece = candidatePiece;
                                        shapeIndex = testShapeIndex;
                                        baseRow = startRow;
                                        baseCol = startCol;
                                        matched = true;
                                        break;
                                    }
                                }
                            }
                            if (matched) break;
                        }
                        if (matched) break;
                    }
                } else {
                    // 对于用户手动编辑，通过tempId查找
                    // 首先尝试通过pieceCounts中的tempId查找
                    for (const auto& pc : pieceCounts) 
                    {
                        if (pc.tempId == cellValue) 
                        {
                            pieceId = pc.pieceId;
                            break;
                        }
                    }
                    
                    // 如果通过pieceCounts找不到，尝试通过board上的值直接识别
                    // 这可以处理从预览区拖出的图块（它们的tempId可能不在pieceCounts中）
                    // 但是，需要确保不会匹配到错误的图块类型（特别是当多个图块相邻时）
                    // 重要：优先匹配较大的图块，避免将大图块的一部分误识别为小图块
                    if (pieceId < 0) 
                    {
                        // 按图块大小排序，优先匹配较大的图块
                        vector<pair<const Piece*, size_t>> piecesWithSizes;
                        for (const auto& p : pieces) 
                        {
                            if (!p.shapes.empty()) 
                            {
                                // 使用第一个形状的大小作为图块大小
                                size_t pieceSize = p.shapes[0].size();
                                piecesWithSizes.push_back({&p, pieceSize});
                            }
                        }
                        // 按大小降序排序
                        sort(piecesWithSizes.begin(), piecesWithSizes.end(),
                            [](const pair<const Piece*, size_t>& a, const pair<const Piece*, size_t>& b) {
                                return a.second > b.second;
                            });
                        
                        // 遍历所有图块类型（按大小排序），尝试匹配形状
                        // 注意：需要确保匹配的单元格都未被其他图块匹配，且值都相同
                        for (const auto& pieceSizePair : piecesWithSizes) 
                        {
                            const Piece* p = pieceSizePair.first;
                            // 检查这个图块类型的所有形状，看是否能匹配board上的值
                            for (size_t s = 0; s < p->shapes.size(); s++) 
                            {
                                const auto& testShape = p->shapes[s];
                                // 尝试将当前单元格作为形状的一部分
                                for (const auto& cellPos : testShape) 
                                {
                                    int testBaseRow = i - cellPos.first;
                                    int testBaseCol = j - cellPos.second;
                                    if (testBaseRow < 0 || testBaseCol < 0) continue;
                                    
                                    // 检查整个形状是否匹配，且所有单元格都未被其他形状匹配
                                    bool matches = true;
                                    int matchedCells = 0;
                                    for (const auto& pos : testShape) 
                                    {
                                        int checkRow = testBaseRow + pos.first;
                                        int checkCol = testBaseCol + pos.second;
                                        if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                            checkCol < 0 || checkCol >= BOARD_SIZE) 
                                        {
                                            matches = false;
                                            break;
                                        }
                                        
                                        // 检查值是否匹配
                                        int val = (*currentBoard)[checkRow][checkCol];
                                        if (val != cellValue) 
                                        {
                                            matches = false;
                                            break;
                                        }
                                        
                                        // 检查是否已经被其他图块匹配（避免重复匹配）
                                        if (matchedInThisFrame[checkRow][checkCol]) 
                                        {
                                            matches = false;
                                            break;
                                        }
                                        
                                        matchedCells++;
                                    }
                                    
                                    // 确保匹配的单元格数量等于形状大小
                                    if (matches && matchedCells == (int)testShape.size()) 
                                    {
                                        pieceId = p->id;
                                        piece = p;
                                        // 保存匹配的形状索引和基准点，供后续使用
                                        shapeIndex = (int)s;
                                        baseRow = testBaseRow;
                                        baseCol = testBaseCol;
                                        
                                        // 立即标记这些单元格为已匹配（在本帧中）
                                        // 这样可以避免在同一个绘制周期内重复匹配
                                        for (const auto& pos : testShape) 
                                        {
                                            int checkRow = testBaseRow + pos.first;
                                            int checkCol = testBaseCol + pos.second;
                                            if (checkRow >= 0 && checkRow < BOARD_SIZE &&
                                                checkCol >= 0 && checkCol < BOARD_SIZE) 
                                            {
                                                matchedInThisFrame[checkRow][checkCol] = true;
                                            }
                                        }
                                        
                                        break;
                                    }
                                }
                                if (pieceId >= 0) break;
                            }
                            if (pieceId >= 0) break;
                        }
                    } else 
                    {
                        // 如果通过pieceCounts找到了pieceId，找到对应的图块类型
                        for (const auto& p : pieces) 
                        {
                            if (p.id == pieceId) 
                            {
                        piece = &p;
                        break;
                            }
                        }
                    }
                }
                
                if (!piece) continue;
                
                // 如果已经通过shape匹配找到了piece和shapeIndex（showSolution=true时），直接使用
                // 否则，收集所有可能的匹配候选
                struct MatchCandidate {
                    int shapeIndex;
                    int baseRow;
                    int baseCol;
                    int matchedCells;
                    set<pair<int, int>> shapeCells;  // 存储形状的所有单元格
                };
                vector<MatchCandidate> candidates;
                
                // 保存选中的形状单元格（用于后续标记drawn）
                set<pair<int, int>> selectedShapeCells;
                
                // 如果已经通过shape匹配找到了shapeIndex和baseRow/baseCol（showSolution=true时），跳过形状匹配
                if (showSolution && shapeIndex >= 0 && baseRow >= 0 && baseCol >= 0) {
                    // 直接使用已匹配的结果
                    selectedShapeCells.clear();
                    const auto& shape = piece->shapes[shapeIndex];
                    for (const auto& pos : shape) {
                        selectedShapeCells.insert({baseRow + pos.first, baseCol + pos.second});
                    }
                    // 标记这些单元格为已匹配（在本帧中）
                    for (const auto& cell : selectedShapeCells) {
                        matchedInThisFrame[cell.first][cell.second] = true;
                    }
                } else {
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
                            int val = (*currentBoard)[checkRow][checkCol];
                            if (val != cellValue) {
                                matches = false;
                                break;
                            }
                            // 检查这个单元格是否已经被绘制或在本帧中已匹配
                            // 注意：当showSolution=false时（用户手动编辑），不检查drawn，因为图块可能刚被放置
                            if (showSolution && (drawn[checkRow][checkCol] || matchedInThisFrame[checkRow][checkCol])) {
                                matches = false;
                                break;
                            }
                            // 当showSolution=false时，只检查matchedInThisFrame，避免重复匹配
                            if (!showSolution && matchedInThisFrame[checkRow][checkCol]) {
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
                    selectedShapeCells = candidates[selectedIndex].shapeCells;  // 保存选中的单元格
                    
                    // 立即标记这个形状的所有单元格为已匹配（在本帧中）
                    // 这样可以避免在同一个绘制周期内重复匹配
                    for (const auto& cell : selectedShapeCells) {
                        matchedInThisFrame[cell.first][cell.second] = true;
                    }
                }
                
                // 如果这个图块实例正在被拖拽，跳过绘制（避免重影）
                if (draggedPiece.isDragging && draggedPiece.tempId == cellValue &&
                    baseRow >= 0 && baseCol >= 0 &&
                    baseRow == draggedPiece.originalRow && baseCol == draggedPiece.originalCol) {
                    continue;
                }
                
                // 检查这个图块实例是否应该被绘制
                bool shouldDraw = false;
                
                // 如果找到了匹配的形状（shapeIndex >= 0 且 baseRow >= 0），才考虑绘制
                if (piece && shapeIndex >= 0 && shapeIndex < (int)piece->shapes.size() && baseRow >= 0) {
                    if (showSolution) {
                        // 自动求解结果：只绘制符合pieceCounts限制的图块
                        // 找到对应的pieceCounts条目
                        bool foundMatch = false;
                        for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) {
                            const auto& pc = pieceCounts[pcIndex];
                            if (pc.pieceId == piece->id && pc.currentShapeIndex == shapeIndex) {
                                // 检查是否已经绘制了足够的数量
                                // 使用shouldDrawCounts来确定应该绘制的数量（与calculateUsedPieceCounts一致）
                                if (drawnCounts[pcIndex] < shouldDrawCounts[pcIndex]) {
                                    shouldDraw = true;
                                    drawnCounts[pcIndex]++;  // 增加已绘制计数
                                    foundMatch = true;
                                    break;
                                }
                            }
                        }
                        // 如果没有找到精确匹配（可能因为currentShapeIndex不匹配），
                        // 检查是否有相同形状的其他角度（如1x1、cross等）
                        if (!foundMatch) {
                            // 如果图块只有一个形状（如1x1、cross、3x3），所有角度的形状都相同
                            if (piece->shapes.size() == 1) {
                                for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) {
                                    const auto& pc = pieceCounts[pcIndex];
                                    if (pc.pieceId == piece->id) {
                                        if (drawnCounts[pcIndex] < shouldDrawCounts[pcIndex]) {
                                            shouldDraw = true;
                                            drawnCounts[pcIndex]++;
                                            foundMatch = true;
                                            break;
                                        }
                                    }
                                }
                            } else {
                                // 检查是否有相同形状的其他角度
                                const auto& matchedShape = piece->shapes[shapeIndex];
                                for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) {
                                    const auto& pc = pieceCounts[pcIndex];
                                    if (pc.pieceId == piece->id) {
                                        if (pc.currentShapeIndex >= 0 && pc.currentShapeIndex < (int)piece->shapes.size()) {
                                            const auto& pcShape = piece->shapes[pc.currentShapeIndex];
                                            if (matchedShape.size() == pcShape.size()) {
                                                set<pair<int, int>> matchedShapeSet(matchedShape.begin(), matchedShape.end());
                                                set<pair<int, int>> pcShapeSet(pcShape.begin(), pcShape.end());
                                                if (matchedShapeSet == pcShapeSet) {
                                                    if (drawnCounts[pcIndex] < shouldDrawCounts[pcIndex]) {
                                                        shouldDraw = true;
                                                        drawnCounts[pcIndex]++;
                                                        foundMatch = true;
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        // 用户手动编辑：需要正确计数已放置的图块
                        // 找到对应的pieceCounts条目，检查是否已经绘制了足够的数量
                        bool foundMatch = false;
                        for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) {
                            const auto& pc = pieceCounts[pcIndex];
                            if (pc.pieceId == piece->id) {
                                // 检查形状是否匹配（考虑旋转）
                                bool shapeMatches = false;
                                if (pc.currentShapeIndex >= 0 && pc.currentShapeIndex < (int)piece->shapes.size()) {
                                    const auto& pcShape = piece->shapes[pc.currentShapeIndex];
                                    const auto& matchedShape = piece->shapes[shapeIndex];
                                    if (pcShape.size() == matchedShape.size()) {
                                        set<pair<int, int>> pcShapeSet(pcShape.begin(), pcShape.end());
                                        set<pair<int, int>> matchedShapeSet(matchedShape.begin(), matchedShape.end());
                                        if (pcShapeSet == matchedShapeSet) {
                                            shapeMatches = true;
                                        }
                                    }
                                }
                                // 如果形状匹配，或者图块只有一个形状（如1x1、cross、3x3），允许绘制
                                if (shapeMatches || piece->shapes.size() == 1) {
                                    // 检查是否已经绘制了足够的数量
                                    if (drawnCounts[pcIndex] < shouldDrawCounts[pcIndex]) {
                                        shouldDraw = true;
                                        drawnCounts[pcIndex]++;  // 增加已绘制计数
                                        foundMatch = true;
                                        break;
                                    }
                                }
                            }
                        }
                        // 如果没有找到匹配，仍然尝试绘制（可能是新放置的图块）
                        if (!foundMatch) {
                            shouldDraw = true;
                        }
                    }
                }
                
                // 用户手动编辑：如果匹配失败，尝试回退方案
                if (!shouldDraw && !showSolution && piece) {
                    // 用户手动编辑：即使没有找到精确匹配的形状，也尝试绘制
                    // 如果candidates不为空，使用第一个候选
                    if (!candidates.empty()) {
                        shapeIndex = candidates[0].shapeIndex;
                        baseRow = candidates[0].baseRow;
                        baseCol = candidates[0].baseCol;
                        selectedShapeCells = candidates[0].shapeCells;
                        shouldDraw = true;
                    } else {
                        // 如果匹配完全失败，尝试使用第一个形状和找到的最小边界
                        // 找到这个图块实例的所有单元格
                        int minRow = BOARD_SIZE, minCol = BOARD_SIZE;
                        set<pair<int, int>> instanceCells;
                        for (int r = 0; r < BOARD_SIZE; r++) {
                            for (int c = 0; c < BOARD_SIZE; c++) {
                                if ((*currentBoard)[r][c] == cellValue && !drawn[r][c] && !matchedInThisFrame[r][c]) {
                                    minRow = min(minRow, r);
                                    minCol = min(minCol, c);
                                    instanceCells.insert({r, c});
                                }
                            }
                        }
                        if (minRow < BOARD_SIZE && !piece->shapes.empty()) {
                            // 尝试匹配第一个形状
                            shapeIndex = 0;
                            baseRow = minRow;
                            baseCol = minCol;
                            selectedShapeCells = instanceCells;
                            shouldDraw = true;
                        }
                    }
                }
                
                if (shouldDraw) {
                    placements.push_back({piece, shapeIndex, baseRow, baseCol});
                    // 注意：不要在这里标记drawn，让drawPieceTexture自己标记
                    // 这样可以避免drawPieceTexture检查时认为已经绘制过而跳过
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
    if (fontAvailable) 
    {
        string resultText;
        
        // 计算手动求解进度（当不在自动求解时显示）
        if (!solving && !showSolution) 
        {
            int filledCells = 0;
            for (int i = 0; i < BOARD_SIZE; i++) 
            {
                for (int j = 0; j < BOARD_SIZE; j++) 
                {
                    if (board[i][j] != 0) filledCells++;
                }
            }
            int totalCells = BOARD_SIZE * BOARD_SIZE;
            resultText = "Manual Progress: " + to_string(filledCells) + "/" + to_string(totalCells) + " cells";
            
            // 如果完成，显示成功消息
            if (filledCells == totalCells) 
            {
                // 检查是否所有图块都已正确放置
                vector<int> usedCounts = calculateUsedPieceCounts();
                bool allPiecesUsed = true;
                for (size_t i = 0; i < pieceCounts.size(); i++) 
                {
                    if (usedCounts[i] != pieceCounts[i].count) 
                    {
                        allPiecesUsed = false;
                        break;
                    }
                }
                if (allPiecesUsed) 
                {
                    resultText = "Manual Solution Complete!";
                }
            }
        } else if (solving) 
        {
            // 实时显示精确时间（保留1位小数）
            float currentTime = solveTimer.getElapsedTime().asSeconds();
            ostringstream oss;
            oss.precision(1);
            oss << fixed << currentTime;
            resultText = "Solving... Time: " + oss.str() + "s";
        } else if (solutionFound && solveTime > 0.0f) 
        {
            ostringstream oss;
            oss.precision(1);
            oss << fixed << solveTime;
            // 检查是否是完整解
            int filledCells = 0;
            for (int i = 0; i < BOARD_SIZE; i++)
             {
                for (int j = 0; j < BOARD_SIZE; j++) 
                {
                    if (solutionBoard[i][j] != 0) filledCells++;
                }
            }
            if (filledCells == BOARD_SIZE * BOARD_SIZE && solved) 
            {
            resultText = "Solution Found! Time: " + oss.str() + "s";
            } else
             {
                // 部分解：显示最好的成果
                resultText = "Best Progress: " + to_string(bestFilledCells) + "/" + to_string(BOARD_SIZE * BOARD_SIZE) + " cells. Time: " + oss.str() + "s";
            }
        } else if (!solving && solveTime > 0.0f && !solutionFound) 
        {
            if (solveTimeout) 
            {
                ostringstream timeoutOss;
                timeoutOss.precision(1);
                timeoutOss << fixed << estimatedSolveTime;
                resultText = "Timeout! No solution found in " + timeoutOss.str() + "s";
            } else 
            {
                ostringstream oss;
                oss.precision(1);
                oss << fixed << solveTime;
                resultText = "No solution found. Time: " + oss.str() + "s";
            }
        }
        
        if (!resultText.empty()) 
        {
            Text result;
            result.setFont(font);
            result.setString(resultText);
            result.setCharacterSize(14);
            result.setPosition(buttonX, buttonY + buttonHeight + 5);
            
            // 设置颜色：手动求解完成显示绿色，自动求解完成显示绿色，超时显示红色，其他显示黑色
            Color textColor = Color::Black;
            if (resultText.find("Manual Solution Complete") != string::npos) 
            {
                textColor = Color(0, 150, 0);  // 绿色
            } else if (solutionFound) 
            {
                textColor = Color(0, 150, 0);  // 绿色
            } else if (solveTimeout) 
            {
                textColor = Color(200, 0, 0);  // 红色
            } else if (resultText.find("Manual Progress") != string::npos) 
            {
                // 手动求解进度：根据完成度显示不同颜色
                int filledCells = 0;
                for (int i = 0; i < BOARD_SIZE; i++) 
                {
                    for (int j = 0; j < BOARD_SIZE; j++) 
                    {
                        if (board[i][j] != 0) filledCells++;
                    }
                }
                int totalCells = BOARD_SIZE * BOARD_SIZE;
                if (filledCells == totalCells) 
                {
                    textColor = Color(0, 150, 0);  // 完成：绿色
                } else if (filledCells >= totalCells * 0.8) 
                {
                    textColor = Color(200, 150, 0);  // 接近完成：橙色
                } else 
                {
                    textColor = Color::Black;  // 进行中：黑色
                }
            }
            
            result.setFillColor(textColor);
            window.draw(result);
        }
        
        
        // 按键说明（移除space键介绍）
        // 往下移，避免遮盖求解时间显示（求解时间在buttonY + buttonHeight + 5，按键说明从buttonY + buttonHeight + 30开始）
        int controlsY = buttonY + buttonHeight + 30;
        Text controlsTitle("Controls:", font, 20);
        controlsTitle.setPosition(buttonX, controlsY);
        controlsTitle.setFillColor(Color::Black);
        controlsTitle.setStyle(Text::Bold);
        window.draw(controlsTitle);
        controlsY += 25;
        
        vector<string> controlTexts = 
        {
            "Mouse Controls:",
            "  Left Click - Drag Piece",
            "  Right Click - Remove Piece",
            "  Mouse - Drag Editor Window",
            "",
            "Keyboard Controls (General):",
            "  E - Open/Close Editor",
            "  Tab - Select Piece in Preview",
            "  Shift+Tab - Previous Piece",
            "  W/A/S/D - Move Selected Piece",
            "  Space - Place Selected Piece",
            "  Delete - Clear Board",
            "  Backspace - Remove Last Piece",
            "",
            "Keyboard Controls (Editor):",
            "  Tab/Shift+Tab - Switch Piece",
            "  +/- - Change Count",
            "  Left/Right - Change Angle",
            "  Enter - Confirm Changes"
        };
        
        for (const auto& text : controlTexts)
         {
            if (text.empty()) 
            {
                // 空行，增加间距
                controlsY += 10;
                continue;
            }
            
            Text control(text, font, 16);
            control.setPosition(buttonX, controlsY);
            
            // 如果是标题行（以冒号结尾），使用粗体
            if (text.back() == ':') 
            {
                control.setFillColor(Color::Black);
                control.setStyle(Text::Bold);
            } else 
            {
                control.setFillColor(Color(60, 60, 60));  // 深灰色
                control.setStyle(Text::Regular);
            }
            
            window.draw(control);
            controlsY += 22;
        }
        
        // 显示初始图块列表
        controlsY += 10;  // 增加间距
        Text initialTitle("Initial Pieces:", font, 20);
        initialTitle.setPosition(buttonX, controlsY);
        initialTitle.setFillColor(Color::Black);
        initialTitle.setStyle(Text::Bold);
        window.draw(initialTitle);
        controlsY += 22;
        
        // 显示每个初始图块的种类、数量和角度
        for (const auto& pc : pieceCounts) 
        {
            if (pc.count == 0) continue;
            
            // 找到对应的图块定义
            const Piece* piece = nullptr;
            for (const auto& p : pieces) 
            {
                if (p.id == pc.pieceId) 
                {
                    piece = &p;
                    break;
                }
            }
            
            if (!piece) continue;
            
            // 格式化角度显示
            int angle = pc.currentShapeIndex * 90;
            string angleStr = to_string(angle) + "deg";
            
            // 构建显示文本：图块名称、数量、角度
            ostringstream oss;
            oss << "  " << piece->name << " x" << pc.count << " (" << angleStr << ")";
            
            Text pieceInfo(oss.str(), font, 16);
            pieceInfo.setPosition(buttonX, controlsY);
            pieceInfo.setFillColor(Color(60, 60, 60));  // 深灰色
            window.draw(pieceInfo);
            controlsY += 20;
        }
        
        // 显示已使用图块列表（仅在求解结束后显示）
        if (solutionFound || solved)
         {
            controlsY += 10;  // 增加间距
            Text usedTitle("Used Pieces:", font, 20);
            usedTitle.setPosition(buttonX, controlsY);
            usedTitle.setFillColor(Color::Black);
            usedTitle.setStyle(Text::Bold);
            window.draw(usedTitle);
            controlsY += 22;
            
            // 计算已使用的图块
            vector<int> usedCounts = calculateUsedPieceCounts();
            
            // 显示每个已使用的图块
            for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++)
             {
                const auto& pc = pieceCounts[pcIndex];
                int usedCount = usedCounts[pcIndex];
                
                if (usedCount == 0) continue;
                
                // 找到对应的图块定义
                const Piece* piece = nullptr;
                for (const auto& p : pieces) 
                {
                    if (p.id == pc.pieceId) 
                    {
                        piece = &p;
                        break;
                    }
                }
                
                if (!piece) continue;
                
                // 格式化角度显示
                int angle = pc.currentShapeIndex * 90;
                string angleStr = to_string(angle) + "deg";
                
                // 构建显示文本：图块名称、已使用数量/总数量、角度
                ostringstream oss;
                oss << "  " << piece->name << " " << usedCount << "/" << pc.count << " (" << angleStr << ")";
                
                Text pieceInfo(oss.str(), font, 16);
                pieceInfo.setPosition(buttonX, controlsY);
                // 如果全部使用，用绿色；否则用橙色
                if (usedCount == pc.count) 
                {
                    pieceInfo.setFillColor(Color(0, 150, 0));  // 绿色
                } else 
                {
                    pieceInfo.setFillColor(Color(200, 100, 0));  // 橙色
                }
                window.draw(pieceInfo);
                controlsY += 20;
            }
        }
    }
    
    // 绘制拖拽预览
    if (draggedPiece.isDragging && draggedPiece.pieceId >= 0) 
    {
        Vector2i mousePos = Mouse::getPosition(window);
        int offsetX = 50;
        int offsetY = 50;
        
        // 检查鼠标是否在预览区（用于边框显示，但不影响图块跟随鼠标）
        int previewX = 50;
        int previewY = 50 + BOARD_SIZE * CELL_SIZE + 20;
        int previewWidth = BOARD_SIZE * CELL_SIZE;
        int previewHeight = 200;
        bool inPreviewArea = (mousePos.x >= previewX && mousePos.x < previewX + previewWidth &&
                             mousePos.y >= previewY && mousePos.y < previewY + previewHeight);
        
        // 计算鼠标在游戏板上的位置（用于边框显示）
        int boardX = mousePos.x - offsetX;
        int boardY = mousePos.y - offsetY;
        int col = boardX / CELL_SIZE;
        int row = boardY / CELL_SIZE;
        
        // 找到对应的图块
        const Piece* piece = nullptr;
        for (const auto& p : pieces)
         {
            if (p.id == draggedPiece.pieceId) 
            {
                piece = &p;
                break;
            }
        }
        
        if (piece && draggedPiece.shapeIndex < (int)piece->shapes.size()) 
        {
            const auto& shape = piece->shapes[draggedPiece.shapeIndex];
            
            // 使用贴图绘制预览（半透明）
            Texture* tex = nullptr;
            for (auto& pt : pieceTextures) 
            {
                if (pt.pieceId == piece->id && pt.loaded) 
                {
                    tex = &pt.texture;
                    break;
                }
            }
            
            if (tex) 
            {
                // 计算图块的边界
                int minRow = shape[0].first, maxRow = shape[0].first;
                int minCol = shape[0].second, maxCol = shape[0].second;
                for (const auto& cell : shape) 
                {
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
                
                if (dragPreviewCache.find(cacheKey) == dragPreviewCache.end()) 
                {
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
                    if ((int)rotation % 180 == 90)
                     {
                        // 旋转90/270度：形状宽度对应纹理高度，形状高度对应纹理宽度
                        scaleX = (float)shapeWidth / tex->getSize().y;
                        scaleY = (float)shapeHeight / tex->getSize().x;
                        // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                        float uniformScale = max(scaleX, scaleY);
                        scaleX = uniformScale;
                        scaleY = uniformScale;
                    } else
                     {
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
                {
                    // 计算预览位置：鼠标位置减去拖拽偏移量，再减去形状的最小偏移
                    // 这样鼠标在图块上的相对位置保持不变
                    // 无论鼠标在哪里，图块都应该跟随鼠标
                    int spriteX = mousePos.x - draggedPiece.dragOffset.x - minCol * CELL_SIZE;
                    int spriteY = mousePos.y - draggedPiece.dragOffset.y - minRow * CELL_SIZE;
                    finalSprite.setPosition(spriteX, spriteY);
                }
                finalSprite.setColor(Color(255, 255, 255, 150));  // 半透明
                window.draw(finalSprite);
                
                // 绘制黄色边框：无论鼠标在哪里，都使用相同的位置计算方式
                    // minCol 和 minRow 已经在上面计算过了（在tex块中）
                for (const auto& cell : shape) 
                {
                        int cellX = mousePos.x - draggedPiece.dragOffset.x + (cell.second - minCol) * CELL_SIZE;
                        int cellY = mousePos.y - draggedPiece.dragOffset.y + (cell.first - minRow) * CELL_SIZE;
                        RectangleShape outline(Vector2f(CELL_SIZE - 2, CELL_SIZE - 2));
                        outline.setPosition(cellX + 1, cellY + 1);
                        outline.setFillColor(Color::Transparent);
                        outline.setOutlineThickness(2);
                        outline.setOutlineColor(Color::Yellow);
                        window.draw(outline);
                    }
            } else 
            {
                // 回退到颜色填充
                // 计算图块的边界（与上面的tex块中相同）
                int minRow = shape[0].first, maxRow = shape[0].first;
                int minCol = shape[0].second, maxCol = shape[0].second;
                for (const auto& cell : shape) 
                {
                    minRow = min(minRow, cell.first);
                    maxRow = max(maxRow, cell.first);
                    minCol = min(minCol, cell.second);
                    maxCol = max(maxCol, cell.second);
                }
                
                if (inPreviewArea) 
                {
                    for (const auto& cell : shape) 
                    {
                        int previewRow = row + cell.first;
                        int previewCol = col + cell.second;
                        
                        if (previewRow >= 0 && previewRow < BOARD_SIZE &&
                            previewCol >= 0 && previewCol < BOARD_SIZE)
                             {
                            RectangleShape previewCell(Vector2f(CELL_SIZE - 2, CELL_SIZE - 2));
                            int cellX = mousePos.x - draggedPiece.dragOffset.x + (previewCol - minCol) * CELL_SIZE;
                            int cellY = mousePos.y - draggedPiece.dragOffset.y + (previewRow - minRow) * CELL_SIZE;
                            previewCell.setPosition(cellX + 1, cellY + 1);
                            Color previewColor = piece->color;
                            previewColor.a = 150;  // 半透明
                            previewCell.setFillColor(previewColor);
                            previewCell.setOutlineThickness(2);
                            previewCell.setOutlineColor(Color::Yellow);
                            window.draw(previewCell);
                        }
                    }
                } else 
                {
                    // 在游戏板上：使用与预览图块相同的位置计算方式
                    for (const auto& cell : shape) 
                    {
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
    
    // 绘制键盘选中的图块预览（在游戏板上）
    if (!showEditor && previewSelectedIndex >= 0 && keyboardPlacePieceId >= 0 && 
        keyboardPlaceRow >= 0 && keyboardPlaceCol >= 0 && 
        keyboardPlaceShapeIndex >= 0) 
    {
        // 找到对应的图块
        const Piece* piece = nullptr;
        for (const auto& p : pieces) 
        {
            if (p.id == keyboardPlacePieceId) 
            {
                piece = &p;
                break;
            }
        }
        
        if (piece && keyboardPlaceShapeIndex < (int)piece->shapes.size()) 
        {
            const auto& shape = piece->shapes[keyboardPlaceShapeIndex];
            
            // 计算图块的边界（用于计算基准点）
            int minRow = shape[0].first, maxRow = shape[0].first;
            int minCol = shape[0].second, maxCol = shape[0].second;
            for (const auto& cell : shape) 
            {
                minRow = min(minRow, cell.first);
                maxRow = max(maxRow, cell.first);
                minCol = min(minCol, cell.second);
                maxCol = max(maxCol, cell.second);
            }
            
            // 计算基准点（确保图块在游戏板内）
            int baseRow = keyboardPlaceRow - minRow;
            int baseCol = keyboardPlaceCol - minCol;
            
            // 检查是否可以放置（用于显示不同颜色）
            bool canPlaceHere = false;
            if (baseRow >= 0 && baseRow < BOARD_SIZE && 
                baseCol >= 0 && baseCol < BOARD_SIZE) 
            {
                canPlaceHere = canPlace(shape, baseRow, baseCol);
            }
            
            // 使用贴图绘制预览（半透明）
            Texture* tex = nullptr;
            for (auto& pt : pieceTextures) 
            {
                if (pt.pieceId == piece->id && pt.loaded) 
                {
                    tex = &pt.texture;
                    break;
                }
            }
            
            if (tex) 
            {
                int shapeWidth = (maxCol - minCol + 1) * CELL_SIZE;
                int shapeHeight = (maxRow - minRow + 1) * CELL_SIZE;
                
                // 使用RenderTexture绘制完整形状（半透明）
                static map<pair<int, int>, unique_ptr<RenderTexture>> keyboardPreviewCache;
                pair<int, int> cacheKey = {piece->id, keyboardPlaceShapeIndex};
                
                if (keyboardPreviewCache.find(cacheKey) == keyboardPreviewCache.end()) 
                {
                    auto renderTex = make_unique<RenderTexture>();
                    renderTex->create(shapeWidth, shapeHeight);
                    renderTex->clear(Color::Transparent);
                    
                    Sprite sprite(*tex);
                    // 根据shapeIndex旋转贴图
                    float rotation = keyboardPlaceShapeIndex * 90.0f;
                    sprite.setRotation(rotation);
                    
                    // 计算旋转后的中心点
                    float centerX = tex->getSize().x / 2.0f;
                    float centerY = tex->getSize().y / 2.0f;
                    sprite.setOrigin(centerX, centerY);
                    
                    // 计算缩放和位置
                    float scaleX, scaleY;
                    if ((int)rotation % 180 == 90) 
                    {
                        scaleX = (float)shapeWidth / tex->getSize().y;
                        scaleY = (float)shapeHeight / tex->getSize().x;
                        float uniformScale = max(scaleX, scaleY);
                        scaleX = uniformScale;
                        scaleY = uniformScale;
                    } else 
                    {
                        scaleX = (float)shapeWidth / tex->getSize().x;
                        scaleY = (float)shapeHeight / tex->getSize().y;
                        float uniformScale = max(scaleX, scaleY);
                        scaleX = uniformScale;
                        scaleY = uniformScale;
                    }
                    sprite.setScale(scaleX, scaleY);
                    sprite.setPosition(shapeWidth / 2.0f, shapeHeight / 2.0f);
                    renderTex->draw(sprite);
                    renderTex->display();
                    keyboardPreviewCache[cacheKey] = move(renderTex);
                }
                
                // 绘制到窗口（半透明）
                Sprite finalSprite(keyboardPreviewCache[cacheKey]->getTexture());
                int spriteX = offsetX + baseCol * CELL_SIZE + minCol * CELL_SIZE;
                int spriteY = offsetY + baseRow * CELL_SIZE + minRow * CELL_SIZE;
                finalSprite.setPosition(spriteX, spriteY);
                
                // 根据是否可以放置显示不同颜色
                if (canPlaceHere) 
                {
                    finalSprite.setColor(Color(255, 255, 255, 150));  // 半透明白色（可以放置）
                } else 
                {
                    finalSprite.setColor(Color(255, 100, 100, 150));  // 半透明红色（不能放置）
                }
                window.draw(finalSprite);
                
                // 绘制边框
                Color outlineColor = canPlaceHere ? Color::Yellow : Color::Red;
                for (const auto& cell : shape) 
                {
                    int cellRow = baseRow + cell.first;
                    int cellCol = baseCol + cell.second;
                    if (cellRow >= 0 && cellRow < BOARD_SIZE && 
                        cellCol >= 0 && cellCol < BOARD_SIZE) 
                    {
                        RectangleShape outline(Vector2f(CELL_SIZE - 2, CELL_SIZE - 2));
                        outline.setPosition(offsetX + cellCol * CELL_SIZE + 1, 
                                          offsetY + cellRow * CELL_SIZE + 1);
                        outline.setFillColor(Color::Transparent);
                        outline.setOutlineThickness(2);
                        outline.setOutlineColor(outlineColor);
                        window.draw(outline);
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
        bool textureLoaded = false;
        if (pt.texture.loadFromFile(texturePath)) {
            textureLoaded = true;
            // 对于line5，检查贴图是否完全透明
            if (piece.name == "line5") {
                // 检查贴图是否有效（至少有一些不透明的像素）
                Image checkImg = pt.texture.copyToImage();
                bool hasOpaquePixel = false;
                for (unsigned int y = 0; y < checkImg.getSize().y && !hasOpaquePixel; y++) {
                    for (unsigned int x = 0; x < checkImg.getSize().x; x++) {
                        Color pixel = checkImg.getPixel(x, y);
                        if (pixel.a > 0) {  // 有不透明像素
                            hasOpaquePixel = true;
                            break;
                        }
                    }
                }
                // 如果贴图完全透明，标记为未加载，让程序创建默认贴图
                if (!hasOpaquePixel) {
                    textureLoaded = false;
                }
            }
        }
        
            // 如果配置文件中指定的路径加载失败，且不是默认路径，尝试使用默认路径
        if (!textureLoaded && useConfigPath) {
                string defaultPath = "resources/" + piece.name + ".png";
                if (pt.texture.loadFromFile(defaultPath)) {
                textureLoaded = true;
                    texturePath = defaultPath;  // 更新为默认路径，用于后续保存
                // 对于line5，再次检查透明度
                if (piece.name == "line5") {
                    Image checkImg = pt.texture.copyToImage();
                    bool hasOpaquePixel = false;
                    for (unsigned int y = 0; y < checkImg.getSize().y && !hasOpaquePixel; y++) {
                        for (unsigned int x = 0; x < checkImg.getSize().x; x++) {
                            Color pixel = checkImg.getPixel(x, y);
                            if (pixel.a > 0) {
                                hasOpaquePixel = true;
                                break;
                            }
                        }
                    }
                    if (!hasOpaquePixel) {
                        textureLoaded = false;
                    }
                }
                }
            }
            
        pt.loaded = textureLoaded;
        
        if (!textureLoaded) {
            // 如果仍然加载失败，创建默认贴图
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
            
            // 计算缩放和位置，使贴图充满形状区域
            // 注意：贴图创建时有padding（4像素），使用IntRect裁剪出内容区域
            int padding = 4;  // 与创建贴图时的padding一致
            
            // 使用IntRect裁剪出内容区域（去除padding）
            IntRect textureRect(padding, padding, 
                               tex->getSize().x - 2 * padding, 
                               tex->getSize().y - 2 * padding);
            sprite.setTextureRect(textureRect);
            
            // 根据shapeIndex旋转贴图（假设shapes是按旋转顺序排列的）
            float rotation = shapeIndex * 90.0f;
            
            // 计算缩放，使裁剪后的内容区域充满形状区域
            // 如果旋转了90度或270度，纹理的宽高在视觉上会交换
            float scaleX, scaleY;
            if ((int)rotation % 180 == 90) {
                // 旋转90/270度：形状宽度对应纹理高度，形状高度对应纹理宽度
                scaleX = (float)shapeWidth / textureRect.height;
                scaleY = (float)shapeHeight / textureRect.width;
            } else {
                // 0度或180度：正常对应
                scaleX = (float)shapeWidth / textureRect.width;
                scaleY = (float)shapeHeight / textureRect.height;
            }
                // 使用统一的缩放比例确保完全填充（取较大的缩放值）
                float uniformScale = max(scaleX, scaleY);
            sprite.setScale(uniformScale, uniformScale);
            
            // 设置origin到裁剪区域的中心（在旋转之前设置）
            // origin是相对于裁剪后的纹理区域的，所以直接使用裁剪区域的中心
            sprite.setOrigin(textureRect.width / 2.0f, textureRect.height / 2.0f);
            
            // 然后旋转（旋转是围绕origin进行的）
            sprite.setRotation(rotation);
            
            // 设置位置到形状中心
            // 由于origin已经设置为裁剪区域的中心，直接设置到形状中心即可
            sprite.setPosition(shapeWidth / 2.0f, shapeHeight / 2.0f);
            renderTex->draw(sprite);
            
            renderTex->display();
            renderCache[cacheKey] = move(renderTex);
        }
        
        // 绘制到窗口
        // 修复对齐问题：贴图创建时有padding，但RenderTexture已经去除了padding
        // RenderTexture的尺寸是shapeWidth x shapeHeight，不包含padding
        // 所以直接使用baseCol和minCol计算位置即可
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

int main() 
{
    // 设置控制台代码页为UTF-8（Windows）
    #ifdef _WIN32
    system("chcp 65001 >nul");
    #endif
    
    RenderWindow window(VideoMode(WINDOW_WIDTH, WINDOW_HEIGHT), 
                       "6x6 Chessboard-coverage", Style::Close);
    window.setFramerateLimit(60);
    
    Font font;
    bool fontLoaded = false;
    
    // 首先尝试从嵌入的资源加载字体
    #ifdef _WIN32
    HRSRC hRes = FindResource(NULL, MAKEINTRESOURCE(101), RT_RCDATA);
    if (hRes)
     {
        HGLOBAL hMem = LoadResource(NULL, hRes);
        if (hMem) 
        {
            void* pFontData = LockResource(hMem);
            DWORD size = SizeofResource(NULL, hRes);
            if (pFontData && size > 0) 
            {
                // 从内存加载字体
                if (font.loadFromMemory(pFontData, (size_t)size)) 
                {
                    fontLoaded = true;
                }
            }
        }
    }
    #endif
    
    // 如果嵌入资源加载失败，尝试从文件加载
    if (!fontLoaded) 
    {
    if (font.loadFromFile("arial.ttf")) 
        {
        fontLoaded = true;
        } else if (font.loadFromFile("C:/Windows/Fonts/arial.ttf")) 
        {
        fontLoaded = true;
        } else if (font.loadFromFile("C:/Windows/Fonts/msyh.ttc")) 
        {
        fontLoaded = true;
        }
    }
    
    initializePieces();
    loadPieceTextures();
    
    for (int i = 0; i < BOARD_SIZE; i++) 
    {
        for (int j = 0; j < BOARD_SIZE; j++)
         {
            board[i][j] = 0;
            solutionBoard[i][j] = 0;
        }
    }
    
    // 求解计时器相关变量已在全局作用域定义
    
    Vector2i mousePos;
    bool mouseLeftPressed = false;
    bool mouseRightPressed = false;
    
    while (window.isOpen()) 
    {
        Event event;
        while (window.pollEvent(event)) 
        {
            if (event.type == Event::Closed) 
            {
                window.close();
            }
            
            if (event.type == Event::KeyPressed) 
            {
                // E键打开/关闭编辑器
                if (event.key.code == Keyboard::E) 
                {
                    showEditor = !showEditor;
                    // 如果打开编辑器且没有选中图块，自动选中第一个图块
                    if (showEditor && (selectedPieceType < 0 || selectedPieceType >= (int)pieces.size())) {
                        if (!pieces.empty()) {
                            selectedPieceType = 0;
                            previewPieceType = 0;
                        }
                }
            }
            
                // Tab键：在未开启编辑器时，在预览区选中图块
                if (!showEditor && event.key.code == Keyboard::Tab) 
                {
                    // 计算预览区可用的图块列表
                    vector<int> availablePieceIndices;
                    vector<int> usedCounts = calculateUsedPieceCounts();
                    
                    for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) 
                    {
                        const auto& pc = pieceCounts[pcIndex];
                        if (pc.count > 0) 
                        {
                            int availableCount = pc.count - usedCounts[pcIndex];
                            if (availableCount > 0) 
                            {
                                // 找到对应的图块索引
                                for (size_t i = 0; i < pieces.size(); i++) 
                                {
                                    if (pieces[i].id == pc.pieceId) 
                                    {
                                        // 添加该图块的每个可用实例
                                        for (int instance = 0; instance < availableCount; instance++) 
                                        {
                                            availablePieceIndices.push_back((int)i);
                                        }
                                        break;
                                    }
                                }
                            }
                        }
                    }
                    
                    if (!availablePieceIndices.empty()) 
                    {
                        bool shiftPressed = Keyboard::isKeyPressed(Keyboard::LShift) || 
                                           Keyboard::isKeyPressed(Keyboard::RShift);
                        
                        // 如果还没有选中（第一次按Tab键），默认选中第一个
                        if (previewSelectedIndex < 0) 
                        {
                            previewSelectedIndex = 0;
                        } else if (shiftPressed) 
                        {
                            // Shift+Tab：反向切换（向前）
                            if (previewSelectedIndex <= 0) 
                            {
                                previewSelectedIndex = (int)availablePieceIndices.size() - 1;
                            } else 
                            {
                                previewSelectedIndex--;
                            }
                        } else 
                        {
                            // Tab：正向切换（向后）
                            if (previewSelectedIndex >= (int)availablePieceIndices.size() - 1) 
                            {
                                previewSelectedIndex = 0;
                            } else 
                            {
                                previewSelectedIndex++;
                            }
                        }
                        
                        // 设置选中的图块信息
                        int selectedPieceIdx = availablePieceIndices[previewSelectedIndex];
                        keyboardPlacePieceId = pieces[selectedPieceIdx].id;
                        
                        // 找到对应的pieceCounts条目，获取角度设置
                        for (const auto& pc : pieceCounts) 
                        {
                            if (pc.pieceId == keyboardPlacePieceId) 
                            {
                                keyboardPlaceShapeIndex = pc.currentShapeIndex;
                                if (keyboardPlaceShapeIndex < 0 || keyboardPlaceShapeIndex >= (int)pieces[selectedPieceIdx].shapes.size()) 
                                {
                                    keyboardPlaceShapeIndex = 0;
                                }
                                break;
                            }
                        }
                        
                        // 初始化放置位置（游戏板中心）
                        keyboardPlaceRow = BOARD_SIZE / 2;
                        keyboardPlaceCol = BOARD_SIZE / 2;
                    }
                }
                
                // WASD键：在游戏板上移动准备放置的图块（只在未开启编辑器且有选中图块时有效）
                if (!showEditor && previewSelectedIndex >= 0 && keyboardPlacePieceId >= 0) 
                {
                    if (event.key.code == Keyboard::W) 
                    {
                        // 上移
                        if (keyboardPlaceRow > 0) 
                        {
                            keyboardPlaceRow--;
                        }
                    } else if (event.key.code == Keyboard::S) 
                    {
                        // 下移
                        if (keyboardPlaceRow < BOARD_SIZE - 1) 
                        {
                            keyboardPlaceRow++;
                        }
                    } else if (event.key.code == Keyboard::A) 
                    {
                        // 左移
                        if (keyboardPlaceCol > 0) 
                        {
                            keyboardPlaceCol--;
                        }
                    } else if (event.key.code == Keyboard::D) 
                    {
                        // 右移
                        if (keyboardPlaceCol < BOARD_SIZE - 1) 
                        {
                            keyboardPlaceCol++;
                        }
                    } else if (event.key.code == Keyboard::Space) 
                    {
                        // 放下图块
                        // 找到对应的图块定义
                        const Piece* piece = nullptr;
                        for (const auto& p : pieces) 
                        {
                            if (p.id == keyboardPlacePieceId) 
                            {
                                piece = &p;
                                break;
                            }
                        }
                        
                        if (piece && keyboardPlaceShapeIndex >= 0 && 
                            keyboardPlaceShapeIndex < (int)piece->shapes.size()) 
                        {
                            const auto& shape = piece->shapes[keyboardPlaceShapeIndex];
                            
                            // 计算图块的边界（用于计算基准点）
                            int minRow = shape[0].first, maxRow = shape[0].first;
                            int minCol = shape[0].second, maxCol = shape[0].second;
                            for (const auto& cell : shape) 
                            {
                                minRow = min(minRow, cell.first);
                                maxRow = max(maxRow, cell.first);
                                minCol = min(minCol, cell.second);
                                maxCol = max(maxCol, cell.second);
                            }
                            
                            // 计算基准点（确保图块在游戏板内）
                            int baseRow = keyboardPlaceRow - minRow;
                            int baseCol = keyboardPlaceCol - minCol;
                            
                            // 检查是否可以放置
                            if (baseRow >= 0 && baseRow < BOARD_SIZE && 
                                baseCol >= 0 && baseCol < BOARD_SIZE && 
                                canPlace(shape, baseRow, baseCol)) 
                            {
                                // 找到对应的pieceCounts条目，分配tempId
                                int tempId = -1;
                                for (auto& pc : pieceCounts) 
                                {
                                    if (pc.pieceId == keyboardPlacePieceId) 
                                    {
                                        tempId = nextTempId++;
                                        break;
                                    }
                                }
                                
                                if (tempId >= 0) 
                                {
                                    // 放置图块
                                    placePiece(shape, baseRow, baseCol, tempId);
                                    
                                    // 重置选中状态
                                    previewSelectedIndex = -1;
                                    keyboardPlaceRow = -1;
                                    keyboardPlaceCol = -1;
                                    keyboardPlacePieceId = -1;
                                    keyboardPlaceShapeIndex = -1;
                                }
                            }
                        }
                    }
                }
                
                // Delete键：清空游戏板
                if (event.key.code == Keyboard::Delete) 
                {
                    // 如果正在显示解，先切换到编辑模式
                    if (showSolution) 
                    {
                        lock_guard<mutex> lock(boardMutex);
                        for (int i = 0; i < BOARD_SIZE; i++) 
                        {
                            for (int j = 0; j < BOARD_SIZE; j++) 
                            {
                                board[i][j] = solutionBoard[i][j];
                            }
                        }
                        showSolution = false;
                    }
                    
                    // 清空游戏板
                    for (int i = 0; i < BOARD_SIZE; i++) 
                    {
                        for (int j = 0; j < BOARD_SIZE; j++) 
                        {
                            board[i][j] = 0;
                        }
                    }
                }
                
                // Backspace键：取下最晚放置在游戏板上的图块
                if (event.key.code == Keyboard::Backspace) 
                {
                    // 如果正在显示解，先切换到编辑模式
                    if (showSolution) 
                    {
                        lock_guard<mutex> lock(boardMutex);
                        for (int i = 0; i < BOARD_SIZE; i++) 
                        {
                            for (int j = 0; j < BOARD_SIZE; j++) 
                            {
                                board[i][j] = solutionBoard[i][j];
                            }
                        }
                        showSolution = false;
                    }
                    
                    // 找到所有图块的tempId，找到最大的（最晚放置的）
                    int maxTempId = -1;
                    for (int i = 0; i < BOARD_SIZE; i++) 
                    {
                        for (int j = 0; j < BOARD_SIZE; j++) 
                        {
                            if (board[i][j] != 0 && board[i][j] > maxTempId) 
                            {
                                maxTempId = board[i][j];
                            }
                        }
                    }
                    
                    // 如果找到了图块，移除它
                    if (maxTempId > 0) 
                    {
                        // 找到这个图块的形状和位置
                        const Piece* foundPiece = nullptr;
                        int foundShapeIndex = -1;
                        int baseRow = -1, baseCol = -1;
                        
                        // 首先尝试通过pieceCounts查找
                        int pieceId = -1;
                        for (const auto& pc : pieceCounts) 
                        {
                            if (pc.tempId == maxTempId) 
                            {
                                pieceId = pc.pieceId;
                                break;
                            }
                        }
                        
                        // 如果通过pieceCounts找不到，尝试通过形状匹配
                        if (pieceId >= 0) 
                        {
                            for (const auto& p : pieces) 
                            {
                                if (p.id == pieceId) 
                                {
                                    foundPiece = &p;
                                    // 找到当前使用的形状
                                    for (const auto& pc : pieceCounts) 
                                    {
                                        if (pc.pieceId == pieceId && pc.tempId == maxTempId) 
                                        {
                                            foundShapeIndex = pc.currentShapeIndex;
                                            if (foundShapeIndex < 0 || foundShapeIndex >= (int)p.shapes.size()) 
                                            {
                                                foundShapeIndex = 0;
                                            }
                                            break;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                        
                        // 如果还没找到，尝试通过形状匹配
                        if (!foundPiece) 
                        {
                            // 找到包含maxTempId的单元格
                            for (int i = 0; i < BOARD_SIZE; i++) 
                            {
                                for (int j = 0; j < BOARD_SIZE; j++) 
                                {
                                    if (board[i][j] == maxTempId) 
                                    {
                                        // 尝试匹配所有图块类型
                                        for (const auto& p : pieces) 
                                        {
                                            for (size_t s = 0; s < p.shapes.size(); s++) 
                                            {
                                                const auto& testShape = p.shapes[s];
                                                // 尝试将当前单元格作为形状的一部分
                                                for (const auto& cellPos : testShape) 
                                                {
                                                    int tryBaseRow = i - cellPos.first;
                                                    int tryBaseCol = j - cellPos.second;
                                                    if (tryBaseRow < 0 || tryBaseCol < 0) continue;
                                                    
                                                    // 检查这个基础位置是否匹配整个形状
                                                    bool matches = true;
                                                    int matchedCells = 0;
                                                    for (const auto& checkCell : testShape) 
                                                    {
                                                        int checkRow = tryBaseRow + checkCell.first;
                                                        int checkCol = tryBaseCol + checkCell.second;
                                                        if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                                            checkCol < 0 || checkCol >= BOARD_SIZE ||
                                                            board[checkRow][checkCol] != maxTempId) 
                                                        {
                                                            matches = false;
                                                            break;
                                                        }
                                                        matchedCells++;
                                                    }
                                                    
                                                    // 确保匹配的单元格数量等于形状大小
                                                    if (matches && matchedCells == (int)testShape.size()) 
                                                    {
                                                        foundPiece = &p;
                                                        foundShapeIndex = (int)s;
                                                        baseRow = tryBaseRow;
                                                        baseCol = tryBaseCol;
                                                        break;
                                                    }
                                                }
                                                if (foundPiece) break;
                                            }
                                            if (foundPiece) break;
                                        }
                                        if (foundPiece) break;
                                    }
                                }
                                if (foundPiece) break;
                            }
                        } else 
                        {
                            // 如果通过pieceCounts找到了，需要找到图块的位置
                            for (int i = 0; i < BOARD_SIZE; i++) 
                            {
                                for (int j = 0; j < BOARD_SIZE; j++) 
                                {
                                    if (board[i][j] == maxTempId) 
                                    {
                                        const auto& shape = foundPiece->shapes[foundShapeIndex];
                                        // 尝试将当前单元格作为形状的一部分
                                        for (const auto& cellPos : shape) 
                                        {
                                            int tryBaseRow = i - cellPos.first;
                                            int tryBaseCol = j - cellPos.second;
                                            if (tryBaseRow < 0 || tryBaseCol < 0) continue;
                                            
                                            // 检查这个基础位置是否匹配整个形状
                                            bool matches = true;
                                            int matchedCells = 0;
                                            for (const auto& checkCell : shape) 
                                            {
                                                int checkRow = tryBaseRow + checkCell.first;
                                                int checkCol = tryBaseCol + checkCell.second;
                                                if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                                    checkCol < 0 || checkCol >= BOARD_SIZE ||
                                                    board[checkRow][checkCol] != maxTempId) 
                                                {
                                                    matches = false;
                                                    break;
                                                }
                                                matchedCells++;
                                            }
                                            
                                            // 确保匹配的单元格数量等于形状大小
                                            if (matches && matchedCells == (int)shape.size()) 
                                            {
                                                baseRow = tryBaseRow;
                                                baseCol = tryBaseCol;
                                                break;
                                            }
                                        }
                                        if (baseRow >= 0) break;
                                    }
                                }
                                if (baseRow >= 0) break;
                            }
                        }
                        
                        // 如果找到了图块，移除它
                        if (foundPiece && foundShapeIndex >= 0 && 
                            foundShapeIndex < (int)foundPiece->shapes.size() && 
                            baseRow >= 0 && baseCol >= 0) 
                        {
                            const auto& shape = foundPiece->shapes[foundShapeIndex];
                            removePiece(shape, baseRow, baseCol);
                        }
                    }
                }
                
                // 编辑器快捷键（只在编辑器打开时有效）
                if (showEditor) 
                {
                    // Tab键：在图块列表中循环切换选中图块
                    // Shift+Tab：反向切换
                    if (event.key.code == Keyboard::Tab) 
                    {
                        if (!pieces.empty()) {
                            bool shiftPressed = Keyboard::isKeyPressed(Keyboard::LShift) || 
                                               Keyboard::isKeyPressed(Keyboard::RShift);
                            
                            if (shiftPressed) {
                                // Shift+Tab：反向切换（向前）
                                if (selectedPieceType <= 0) {
                                    // 如果当前没有选中或选中第一个，切换到最后一个
                                    selectedPieceType = (int)pieces.size() - 1;
                                    previewPieceType = selectedPieceType;
                                } else {
                                    // 切换到上一个图块
                                    selectedPieceType--;
                                    previewPieceType = selectedPieceType;
                                }
                            } else {
                                // Tab：正向切换（向后）
                                if (selectedPieceType < 0 || selectedPieceType >= (int)pieces.size() - 1) {
                                    // 如果当前没有选中或选中最后一个，切换到第一个
                                    selectedPieceType = 0;
                                    previewPieceType = 0;
                                } else {
                                    // 切换到下一个图块
                                    selectedPieceType++;
                                    previewPieceType = selectedPieceType;
                                }
                            }
                        }
                    }
                    
                    // 减号键：减少选中图块的数量
                    if (event.key.code == Keyboard::Subtract || event.key.code == Keyboard::Dash) 
                    {
                        if (selectedPieceType >= 0 && selectedPieceType < (int)pieces.size()) {
                            for (auto& pc : pieceCounts) 
                            {
                                if (pc.pieceId == pieces[selectedPieceType].id && pc.count > 0) 
                                {
                                    int oldCount = pc.count;
                                    pc.count--;
                                    
                                    // 如果数量减少，移除棋盘上多余的实例
                                    if (oldCount > pc.count) 
                                    {
                                        removeExcessInstances(pc.pieceId, pc.count);
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    
                    // 加号键：增加选中图块的数量
                    if (event.key.code == Keyboard::Add || event.key.code == Keyboard::Equal) 
                    {
                        if (selectedPieceType >= 0 && selectedPieceType < (int)pieces.size()) {
                            for (auto& pc : pieceCounts) 
                            {
                                if (pc.pieceId == pieces[selectedPieceType].id) 
                                {
                                    pc.count++;
                                    break;
                                }
                            }
                        }
                    }
                    
                    // 方向键左：减少角度（等同鼠标按"<"）
                    if (event.key.code == Keyboard::Left) 
                    {
                        if (selectedPieceType >= 0 && selectedPieceType < (int)pieces.size()) {
                            for (auto& pc : pieceCounts) 
                            {
                                if (pc.pieceId == pieces[selectedPieceType].id) 
                                {
                                    if (pc.currentShapeIndex > 0) 
                                    {
                                        pc.currentShapeIndex--;
                                    } else 
                                    {
                                        // 循环到最后一个角度
                                        if (!pieces[selectedPieceType].shapes.empty()) 
                                        {
                                            pc.currentShapeIndex = (int)pieces[selectedPieceType].shapes.size() - 1;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    
                    // 方向键右：增加角度（等同鼠标按">"）
                    if (event.key.code == Keyboard::Right) 
                    {
                        if (selectedPieceType >= 0 && selectedPieceType < (int)pieces.size()) {
                            for (auto& pc : pieceCounts) 
                            {
                                if (pc.pieceId == pieces[selectedPieceType].id) 
                                {
                                    if (!pieces[selectedPieceType].shapes.empty()) 
                                    {
                                        if (pc.currentShapeIndex < (int)pieces[selectedPieceType].shapes.size() - 1) 
                                        {
                                            pc.currentShapeIndex++;
                                        } else 
                                        {
                                            // 循环到第一个角度
                                            pc.currentShapeIndex = 0;
                                        }
                                    }
                                    break;
                                }
                            }
                        }
                    }
                    
                    // Enter键：确认编辑器修改（等同鼠标点击确认按钮）
                    if (event.key.code == Keyboard::Return || event.key.code == Keyboard::Enter) 
                    {
                        // 确认修改，关闭编辑器并清空游戏板
                        showEditor = false;
                        clearOldTempIds();
                        assignTempIds();
                        for (int i = 0; i < BOARD_SIZE; i++) 
                        {
                            for (int j = 0; j < BOARD_SIZE; j++) 
                            {
                                board[i][j] = 0;
                                solutionBoard[i][j] = 0;
                            }
                        }
                        solutionFound = false;
                        solved = false;
                        showSolution = false;
                        solving = false;
                        solveTime = 0.0f;
                        solveTimeout = false;
                        // 注意：不在这里关闭求解线程，让用户手动触发求解
                        // 如果求解线程正在运行，用户可以通过点击求解按钮来重新开始
                    }
                }
            }
            
            // 鼠标事件处理
            if (event.type == Event::MouseButtonPressed)
        {
                mousePos = Mouse::getPosition(window);
                int offsetX = 50;
                int offsetY = 50;
                
                if (event.mouseButton.button == Mouse::Left) 
                {
                    mouseLeftPressed = true;
                    
                mousePos = Mouse::getPosition(window);  // 更新鼠标位置
                int offsetX = 50;
                int offsetY = 50;
                    
                    // 先检查是否点击在自动求解按钮上（优先级最高）
                int buttonX = offsetX + BOARD_SIZE * CELL_SIZE + 30;
                int buttonY = offsetY;  // 直接放在顶部
                int buttonWidth = 150;
                int buttonHeight = 40;
                
                    bool clickedOnSolveButton = (mousePos.x >= buttonX && mousePos.x < buttonX + buttonWidth &&
                                                 mousePos.y >= buttonY && mousePos.y < buttonY + buttonHeight);
                    
                    if (clickedOnSolveButton) 
                    {
                        if (solutionFound && !solving)
                        {
                        // 如果已找到解且不在求解中，切换显示
                        showSolution = !showSolution;
                        } else if (!solving)
                        {
                        // 开始自动求解（允许多次求解）
                            // 确保使用最新的配置：如果编辑器打开，先确保配置已更新
                            // 注意：用户在编辑器中修改配置时，pieceCounts已经被直接更新了
                            // 但为了确保一致性，我们在求解前确保tempId已正确分配
                            if (showEditor) {
                                // 如果编辑器打开，先确保tempId已正确分配
                                clearOldTempIds();
                                assignTempIds();
                            }
                            
                        solving = true;
                        solutionFound = false;
                        solved = false;
                        solveTime = 0.0f;
                        solveTimeout = false;  // 重置超时标志
                        solveCheckCount = 0;  // 重置求解调用计数器
                        estimatedSolveTime = estimateSolveTime(pieceCounts);  // 预估求解时间
                        solveTimer.restart();  // 开始计时
                            
                            // 如果之前处于显示解模式，先切换到编辑模式，允许用户操作
                            // 这样用户在求解开始前可以继续操作游戏板
                            if (showSolution) 
                            {
                                // 将solutionBoard复制到board，允许用户编辑
                                lock_guard<mutex> lock(boardMutex);
                                for (int i = 0; i < BOARD_SIZE; i++) 
                                {
                                    for (int j = 0; j < BOARD_SIZE; j++) 
                                    {
                                        board[i][j] = solutionBoard[i][j];
                            }
                        }
                                showSolution = false;  // 切换到编辑模式
                            }
                            
                            showSolution = true;  // 开始显示求解过程
                        // 等待之前的线程结束（如果存在）
                            if (solveThread && solveThread->joinable()) 
                            {
                            solveThread->join();
                            delete solveThread;
                            solveThread = nullptr;
                        }
                            // 清空游戏板（在等待线程结束后，不需要锁，因为不会有其他线程访问）
                            for (int i = 0; i < BOARD_SIZE; i++) 
                            {
                                for (int j = 0; j < BOARD_SIZE; j++) 
                                {
                                    board[i][j] = 0;
                                }
        }
                            solveThread = new thread([&]() 
                            {
                                // 初始化最好的成果（不需要锁，因为bestBoard是求解线程专用的）
                                bestFilledCells = 0;
                                for (int i = 0; i < BOARD_SIZE; i++) 
                                {
                                    for (int j = 0; j < BOARD_SIZE; j++) 
                                    {
                                        bestBoard[i][j] = 0;
                            }
                        }
                                
                                // 创建pieceCounts的深拷贝，确保求解使用的图块数量与求解开始时一致
                                // 注意：这里必须捕获当前的pieceCounts，包括用户修改后的配置
                                vector<PieceCount> countsCopy;
                                countsCopy.reserve(pieceCounts.size());
                                for (const auto& pc : pieceCounts) {
                                    countsCopy.push_back(pc);  // 深拷贝，包括count和currentShapeIndex
                                }
                                
                                // 在求解过程中，只在需要访问board时获取锁
                                // solve函数内部会直接访问board，所以需要在solve调用前后使用锁
                                // 但是，solve函数运行时间很长，如果一直持有锁会导致UI卡死
                                // 解决方案：solve函数内部在访问board时获取锁，而不是在整个求解过程中持有锁
                                // 但是，solve函数是递归的，频繁获取/释放锁会影响性能
                                // 更好的方案：使用局部board副本进行求解，求解完成后再复制回board
                                
                                // 创建board的副本用于求解
                                vector<vector<int>> solveBoard(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
                                
                                // 在estimatedSolveTime秒内求解（使用局部board副本）
                                // 注意：solve函数访问的是全局board，需要修改为使用局部副本
                                // 暂时先使用全局board，但只在求解完成时获取锁来更新solutionBoard
                                bool result = false;
                                {
                                    // 只在初始化时获取锁，清空board
                            lock_guard<mutex> lock(boardMutex);
                                    for (int i = 0; i < BOARD_SIZE; i++) 
                                    {
                                        for (int j = 0; j < BOARD_SIZE; j++) 
                                        {
                                board[i][j] = 0;
                            }
                        }
                                }
                                
                                // 求解（不使用锁，直接访问board）
                                // 为求解过程分配独立的tempId空间，从10000开始，避免与编辑器中的tempId冲突
                                int nextTempIdForSolve = 10000;
                                result = solve(0, countsCopy, nextTempIdForSolve);
                                
                                // 求解完成，更新solutionBoard（需要锁）
                                {
                            lock_guard<mutex> lock(boardMutex);
                                    if (result)
                                    {
                                        // 找到完整解：使用bestBoard而不是board，确保显示的是最优解
                                        // 注意：bestBoard中的tempId是从10000开始的，但绘制时需要能够识别
                                        // countPiecesOnBoard函数现在通过pieceId和shapeIndex匹配，不依赖tempId
                                        solutionBoard = bestBoard;
                                        // 同时更新board，确保显示正确
                                        board = bestBoard;
                                solutionFound = true;
                                solved = true;
                                        showSolution = true;  // 自动显示解
                                    } else
                                    {
                                solved = false;
                                        // 如果没有找到完整解，显示最好的成果
                                        if (bestFilledCells > 0) 
                                        {
                                            solutionBoard = bestBoard;
                                            // 同时更新board，确保显示正确
                                            board = bestBoard;
                                            solutionFound = true;  // 标记为找到部分解，可以显示
                                            showSolution = true;  // 自动显示部分解
                                        } else
                                        {
                                            solutionFound = false;
                                            showSolution = false;
                                        }
                                        if (solveTimeout)
                                        {
                                    // 超时了
                                    solved = false;
                                }
                            }
                                }
                                
                                // 求解完成，重置求解状态（不需要锁）
                            solving = false;
                                solveTime = solveTimer.getElapsedTime().asSeconds();  // 记录求解时间
                        });
                        solveThread->detach();
                    }
                        // 按钮点击后，不再处理其他鼠标事件，直接跳过后续处理
                        continue;  // 跳过后续处理，继续处理下一个事件
                    }
                    
                    // 没有点击按钮，继续处理其他鼠标事件
                    // 先检查是否点击在编辑器区域（如果编辑器打开，优先处理编辑器事件）
                    bool clickedInEditor = false;
                    if (showEditor) 
                    {
                        int editorX = editorDrag.editorX;
                        int editorY = editorDrag.editorY;
                        int editorWidth = WINDOW_WIDTH - 100;
                        // 编辑器高度应该足够大，以包含所有内容
                        // 图块列表、示意图、按钮等，至少需要600像素
                        // 使用min确保不超过窗口高度，但至少600像素
                        int editorHeight = min(600, WINDOW_HEIGHT - editorY - 50);
                        if (editorHeight < 600) {
                            editorHeight = 600;  // 确保至少600像素
                        }
                        
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
                        mousePos.y >= previewY + 40 && mousePos.y < previewY + previewHeight) 
                    {
                        // 计算点击的图块索引（需要与绘制逻辑完全一致）
                        int currentY = previewY + 40;  // 与绘制时使用相同的起始Y坐标
                        int itemsPerRow = 8;
                        int itemSize = (previewWidth - 40) / itemsPerRow - 10;
                        int spacing = 10;
                        
                        // 计算每个图块类型的已使用数量（与绘制时完全相同的逻辑）
                        vector<int> usedCounts = calculateUsedPieceCounts();
                        
                        // 遍历所有图块，精确检测鼠标点击在哪个图块上
                        // 改进：遍历pieceCounts而不是pieces，确保与绘制逻辑一致
                        int displayIndex = 0;
                        int foundPieceIndex = -1;
                        int foundPcIndex = -1;  // 保存找到的pieceCounts索引
                        int foundInstanceIndex = -1;
                        int foundPieceActualX = -1, foundPieceActualY = -1;  // 保存点击的图块实际绘制位置
                        int foundPieceX = -1, foundPieceY = -1;  // 保存点击的图块位置（兼容旧代码）
                        int foundPieceCellSize = -1;  // 保存图块的cellSize
                        int foundPieceMinRow = -1, foundPieceMinCol = -1;  // 保存图块的边界
                        for (size_t pcIndex = 0; pcIndex < pieceCounts.size(); pcIndex++) 
                        {
                            const auto& pc = pieceCounts[pcIndex];
                            
                            // 找到对应的图块定义
                            const Piece* piece = nullptr;
                            for (size_t i = 0; i < pieces.size(); i++) 
                            {
                                if (pieces[i].id == pc.pieceId) 
                                {
                                    piece = &pieces[i];
                                    foundPieceIndex = i;
                                    break;
                                }
                            }
                            
                            if (!piece || pc.count == 0) continue;
                            
                            // 计算可用数量（总数量 - 已使用数量）
                            // usedCounts现在按pieceCounts索引，所以直接使用pcIndex
                            int availableCount = pc.count - usedCounts[pcIndex];
                            
                            // 获取该图块的角度设置（与绘制时相同）
                            int shapeIndex = pc.currentShapeIndex;
                            if (shapeIndex < 0 || shapeIndex >= (int)piece->shapes.size()) 
                            {
                                shapeIndex = 0;  // 如果索引无效，使用0度
                            }
                            
                            // 检查该图块类型的每个实例
                            for (int instance = 0; instance < availableCount; instance++) 
                            {
                                int col = (displayIndex % itemsPerRow);
                                int row = (displayIndex / itemsPerRow);
                                int x = previewX + 20 + col * (itemSize + spacing);
                                int y = currentY + row * (itemSize + spacing);
                                
                                // 检查鼠标是否点击在这个图块的位置范围内
                                // 使用与绘制时完全相同的逻辑来计算图块的实际绘制区域
                                if (!piece->shapes.empty()) 
                                {
                                    const auto& shape = piece->shapes[shapeIndex];  // 使用正确的shapeIndex
                                    
                                    // 计算cellSize（与绘制时相同）
                                    int maxDim = 0;
                                    for (const auto& cell : shape) 
                                    {
                                        maxDim = max(maxDim, max(cell.first, cell.second));
                                    }
                                    int cellSize = maxDim > 0 ? itemSize / (maxDim + 1) : itemSize / 3;
                                    
                                    // 计算图块的边界（与drawPieceShape中的逻辑相同）
                                    int minRow = shape[0].first, maxRow = shape[0].first;
                                    int minCol = shape[0].second, maxCol = shape[0].second;
                                    for (const auto& cell : shape) 
                                    {
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
                                        mousePos.y >= actualY && mousePos.y < actualY + shapeHeight)
                                         {
                                        foundPcIndex = pcIndex;
                                        foundInstanceIndex = instance;
                                        foundPieceX = actualX;  // 保存图块实际绘制区域的左上角
                                        foundPieceY = actualY;
                                        foundPieceActualX = actualX;  // 保存图块实际绘制区域的左上角（用于计算dragOffset）
                                        foundPieceActualY = actualY;
                                        foundPieceCellSize = cellSize;
                                        foundPieceMinRow = minRow;
                                        foundPieceMinCol = minCol;
                                        break;
                                    }
                                } else 
                                {
                                    // 如果没有形状，使用itemSize作为点击区域
                                    if (mousePos.x >= x && mousePos.x < x + itemSize &&
                                        mousePos.y >= y && mousePos.y < y + itemSize)
                                         {
                                        foundPcIndex = pcIndex;
                                        foundInstanceIndex = instance;
                                        break;
                                    }
                                }
                                
                                displayIndex++;
                            }
                            
                            if (foundPcIndex >= 0) break;
                        }
                        
                        if (foundPcIndex >= 0 && foundPieceIndex >= 0 && foundPieceIndex < (int)pieces.size()) 
                        {
                            // 从预览区开始拖拽
                            const auto& pc = pieceCounts[foundPcIndex];
                            draggedPiece.pieceId = pc.pieceId;
                            draggedPiece.originalRow = -1;  // 从预览区拖出，没有原位置
                            draggedPiece.originalCol = -1;
                            // 使用编辑器指定的角度（currentShapeIndex）
                            draggedPiece.shapeIndex = pc.currentShapeIndex;
                            if (draggedPiece.shapeIndex < 0 || 
                                draggedPiece.shapeIndex >= (int)pieces[foundPieceIndex].shapes.size())
                            {
                                draggedPiece.shapeIndex = 0;  // 如果索引无效，使用0度
                            }
                            // 为这个新实例分配一个唯一的tempId
                            draggedPiece.tempId = nextTempId++;
                            draggedPiece.isDragging = true;
                            
                            // 计算拖拽偏移量：从预览区拖拽时，使用保存的图块实际绘制位置
                            // 这样可以避免在图块空白区域点击时计算错误
                            if (foundPieceActualX >= 0 && foundPieceActualY >= 0) 
                            {
                                // 使用保存的图块实际绘制区域的左上角作为参考点
                                // 鼠标相对于图块实际绘制区域的位置
                                draggedPiece.dragOffset.x = mousePos.x - foundPieceActualX;
                                draggedPiece.dragOffset.y = mousePos.y - foundPieceActualY;
                            } else 
                            {
                                // 回退：如果找不到位置信息，使用0
                                draggedPiece.dragOffset.x = 0;
                                draggedPiece.dragOffset.y = 0;
                            }
                        }
                    }
                    
                    // 检查是否点击在游戏板上（如果点击在编辑器内，跳过游戏板检测）
                    if (!clickedInEditor) 
                    {
                    int boardX = mousePos.x - offsetX;
                    int boardY = mousePos.y - offsetY;
                    int col = boardX / CELL_SIZE;
                    int row = boardY / CELL_SIZE;
                    
                        if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) 
                        {
                            // 如果正在显示解，且用户点击了图块，切换到编辑模式
                            if (showSolution) 
                        {
                            // 将solutionBoard复制到board，允许用户编辑
                            lock_guard<mutex> lock(boardMutex);
                            for (int i = 0; i < BOARD_SIZE; i++) 
                            {
                                for (int j = 0; j < BOARD_SIZE; j++) 
                                {
                                    board[i][j] = solutionBoard[i][j];
                                }
                            }
                            showSolution = false;  // 切换到编辑模式
                        }
                        
                        int cellValue = board[row][col];
                        if (cellValue != 0) 
                        {
                            // 找到图块和形状（保持当前角度，不重置）
                            // 通过tempId找到对应的pieceCounts条目，然后找到对应的piece
                            const Piece* clickedPiece = nullptr;
                            int clickedPieceId = -1;
                            for (const auto& pc : pieceCounts) 
                            {
                                if (pc.tempId == cellValue) 
                                {
                                    clickedPieceId = pc.pieceId;
                                    break;
                                }
                            }
                            
                            int clickedShapeIndex = 0;
                            int baseRow = -1, baseCol = -1;
                            int clickedCellInShapeRow = -1, clickedCellInShapeCol = -1;  // 点击位置在形状中的相对坐标
                            
                            if (clickedPieceId >= 0) 
                            {
                                for (size_t i = 0; i < pieces.size(); i++) 
                                {
                                    if (pieces[i].id == clickedPieceId) 
                                    {
                                    clickedPiece = &pieces[i];
                                    // 找到当前使用的形状，通过尝试不同的基础位置来匹配
                                    // 对于L-shape和L-mirror等有多个旋转角度的图块，必须精确匹配正确的角度
                                    for (size_t s = 0; s < pieces[i].shapes.size(); s++) 
                                    {
                                        const auto& shape = pieces[i].shapes[s];
                                        
                                        // 找到形状中哪个cell对应点击位置
                                        for (const auto& cell : shape) 
                                        {
                                            // 尝试这个cell作为点击位置
                                            int tryBaseRow = row - cell.first;
                                            int tryBaseCol = col - cell.second;
                                            
                                            // 检查这个基础位置是否匹配整个形状
                                            // 必须所有单元格都匹配，且数量完全一致
                                            bool matches = true;
                                            int matchedCount = 0;
                                            for (const auto& checkCell : shape) 
                                            {
                                                int checkRow = tryBaseRow + checkCell.first;
                                                int checkCol = tryBaseCol + checkCell.second;
                                                if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                                    checkCol < 0 || checkCol >= BOARD_SIZE) 
                                                {
                                                    matches = false;
                                                    break;
                                                }
                                                if (board[checkRow][checkCol] == cellValue) 
                                                {
                                                    matchedCount++;
                                                } else 
                                                {
                                                    matches = false;
                                                    break;
                                                }
                                            }
                                            
                                            // 必须完全匹配：所有单元格都匹配，且数量等于形状大小
                                            if (matches && matchedCount == (int)shape.size()) 
                                            {
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
                            if (clickedPiece && baseRow >= 0 && baseCol >= 0) 
                            {
                                // 开始拖拽游戏板上的图块
                                draggedPiece.pieceId = clickedPieceId;
                                draggedPiece.tempId = cellValue;  // 从游戏板拖拽时，cellValue就是tempId
                                draggedPiece.originalRow = baseRow;  // 使用基础位置，而不是点击位置
                                draggedPiece.originalCol = baseCol;
                                draggedPiece.isDragging = true;
                                
                                // 计算拖拽偏移量：鼠标相对于点击的单元格的位置
                                // 使用实际点击的单元格位置（clickedCellInShapeRow, clickedCellInShapeCol）
                                // 而不是游戏板上的col和row，避免在图块空白区域点击时计算错误
                                if (clickedCellInShapeRow >= 0 && clickedCellInShapeCol >= 0) 
                                {
                                    // 计算点击的单元格在游戏板上的实际位置
                                    int clickedCellX = offsetX + (baseCol + clickedCellInShapeCol) * CELL_SIZE;
                                    int clickedCellY = offsetY + (baseRow + clickedCellInShapeRow) * CELL_SIZE;
                                    draggedPiece.dragOffset.x = mousePos.x - clickedCellX;
                                    draggedPiece.dragOffset.y = mousePos.y - clickedCellY;
                                } else 
                                {
                                    // 回退：如果找不到点击的单元格，使用游戏板上的col和row
                                int clickedCellX = offsetX + col * CELL_SIZE;
                                int clickedCellY = offsetY + row * CELL_SIZE;
                                draggedPiece.dragOffset.x = mousePos.x - clickedCellX;
                                draggedPiece.dragOffset.y = mousePos.y - clickedCellY;
                                }
                                
                                // 移除原位置的图块（只移除被选中的那个图块实例）
                                if (clickedPiece && clickedShapeIndex < (int)clickedPiece->shapes.size()) 
                                {
                                    const auto& shape = clickedPiece->shapes[clickedShapeIndex];
                                    removePiece(shape, baseRow, baseCol);
                                }
                            }
                        }
                    }
                    }  // 关闭 !clickedInEditor 的if块
                    
                    // 检查是否点击在编辑器区域（处理编辑器内的交互）
                    if (showEditor && clickedInEditor) 
                    {
                        int editorX = editorDrag.editorX;
                        int editorY = editorDrag.editorY;
                        int editorWidth = WINDOW_WIDTH - 100;
                        // 编辑器高度应该足够大，以包含所有内容（与clickedInEditor计算保持一致）
                        // 图块列表、示意图、按钮等，至少需要600像素
                        // 使用min确保不超过窗口高度，但至少600像素
                        int editorHeight = min(600, WINDOW_HEIGHT - editorY - 50);
                        if (editorHeight < 600) {
                            editorHeight = 600;  // 确保至少600像素
                        }
                        
                            // 检查是否点击在编辑器标题栏（可拖拽区域）
                        bool clickedInTitleBar = (mousePos.x >= editorX && mousePos.x < editorX + editorWidth &&
                                                  mousePos.y >= editorY && mousePos.y < editorY + 50);
                        if (clickedInTitleBar) 
                        {
                                editorDrag.isDragging = true;
                                editorDrag.dragOffset.x = mousePos.x - editorX;
                                editorDrag.dragOffset.y = mousePos.y - editorY;
                            // 点击标题栏时，不处理其他点击，直接返回
                        } else {
                            // 不在标题栏，处理编辑器内的其他点击
                            // 使用与绘制时完全一致的坐标计算
                            int itemsPerRow = 5;
                            int itemWidth = 150;
                            int itemHeight = 80;
                            int startX = editorX + 20;
                            // 计算startY，与绘制时保持一致
                            // 绘制时：currentY = editorY + 20，如果有字体则 currentY += 40
                            // 所以 startY = editorY + 20（如果有字体则 + 40）
                            int startY = editorY + 20;  // currentY的初始值
                            bool fontAvailable = font.getInfo().family != "";
                            if (fontAvailable) {
                                startY += 40;  // 如果有字体，加上标题高度
                            }
                            
                            // 计算右侧区域位置
                            int rightAreaX = startX + itemsPerRow * itemWidth + 40;
                            
                            // 先检查是否点击在图块列表区域（优先级最高，确保图块选择能正常工作）
                            bool clickedInPieceList = false;
                            for (size_t i = 0; i < pieces.size(); i++) 
                            {
                                int col = i % itemsPerRow;
                                int row = i / itemsPerRow;
                                int x = startX + col * itemWidth;
                                int y = startY + row * itemHeight;
                                
                                if (mousePos.x >= x && mousePos.x < x + itemWidth &&
                                    mousePos.y >= y && mousePos.y < y + itemHeight) 
                                {
                                    selectedPieceType = (int)i;
                                    previewPieceType = (int)i;
                                    clickedInPieceList = true;
                                    // 强制更新预览显示
                                    break;
                                }
                            }
                            
                            // 如果没点击在图块列表，检查按钮点击
                            if (!clickedInPieceList)
                            {
                                bool buttonClicked = false;
                                // 使用与绘制时完全一致的坐标计算
                                // 绘制时：rightAreaY = currentY = editorY + 20（如果有字体则 + 40）
                                // 绘制时：rightCurrentY = rightAreaY，然后绘制预览区域后 rightCurrentY += previewAreaSize + 30
                                // 所以数量按钮的Y坐标是 rightAreaY + previewAreaSize + 30
                                int rightAreaY = startY;  // 与绘制时的rightAreaY对齐（startY已经包含了editorY和字体偏移）
                                int previewAreaX = rightAreaX;  // 与绘制时的previewAreaX对齐
                                int btnY = rightAreaY + EDITOR_PREVIEW_SIZE + 30;  // 数量按钮的Y坐标（与绘制时的rightCurrentY一致）
                                
                                // 确认按钮（使用与绘制时一致的坐标）- 不需要选中图块就可以点击
                                int confirmBtnX = previewAreaX + 35;
                                int confirmBtnY = btnY + 100;  // btnY + 50 (数量区域高度) + 50 (角度区域高度) = btnY + 100
                                if (mousePos.x >= confirmBtnX && mousePos.x < confirmBtnX + 120 &&
                                    mousePos.y >= confirmBtnY && mousePos.y < confirmBtnY + 35) 
                                {
                                    // 确认修改，关闭编辑器并清空游戏板
                                    showEditor = false;
                                    clearOldTempIds();
                                    assignTempIds();
                                    for (int i = 0; i < BOARD_SIZE; i++) 
                                    {
                                        for (int j = 0; j < BOARD_SIZE; j++) 
                                        {
                                            board[i][j] = 0;
                                            solutionBoard[i][j] = 0;
                                        }
                                    }
                                    solutionFound = false;
                                    solved = false;
                                    showSolution = false;
                                    solving = false;
                                    solveTime = 0.0f;
                                    solveTimeout = false;
                                    // 注意：不在这里关闭求解线程，让用户手动触发求解
                                    // 如果求解线程正在运行，用户可以通过点击求解按钮来重新开始
                                    buttonClicked = true;
                                }
                                
                                // 其他按钮需要选中图块才能操作
                                if (!buttonClicked && selectedPieceType >= 0 && selectedPieceType < (int)pieces.size())
                                {
                                    // 减号按钮：减少数量（使用与绘制时一致的坐标）
                                    // 注意：按钮高度是30，但绘制时使用的是30x30的按钮
                                    if (mousePos.x >= previewAreaX + 80 && mousePos.x < previewAreaX + 110 &&
                                        mousePos.y >= btnY && mousePos.y < btnY + 30) 
                                    {
                                        for (auto& pc : pieceCounts) 
                                        {
                                            if (pc.pieceId == pieces[selectedPieceType].id && pc.count > 0) 
                                            {
                                            int oldCount = pc.count;
                                            pc.count--;
                                            
                                            // 如果数量减少，移除棋盘上多余的实例
                                                if (oldCount > pc.count) 
                                                {
                                                removeExcessInstances(pc.pieceId, pc.count);
                                            }
                                                buttonClicked = true;
                                            break;
                                        }
                                    }
                                }
                                
                                    // 加号按钮：增加数量（使用与绘制时一致的坐标）
                                    if (!buttonClicked && mousePos.x >= previewAreaX + 160 && mousePos.x < previewAreaX + 190 &&
                                        mousePos.y >= btnY && mousePos.y < btnY + 30) 
                                    {
                                        for (auto& pc : pieceCounts) 
                                        {
                                            if (pc.pieceId == pieces[selectedPieceType].id) 
                                            {
                                            pc.count++;
                                                buttonClicked = true;
                                            break;
                                        }
                                    }
                                }
                                
                                    // 角度选择按钮（左箭头）（使用与绘制时一致的坐标）
                                    int angleBtnY = btnY + 50;  // 角度按钮的Y坐标（数量区域高度50）
                                    if (!buttonClicked && mousePos.x >= previewAreaX + 80 && mousePos.x < previewAreaX + 110 &&
                                        mousePos.y >= angleBtnY && mousePos.y < angleBtnY + 30) 
                                    {
                                        for (auto& pc : pieceCounts) 
                                        {
                                            if (pc.pieceId == pieces[selectedPieceType].id) 
                                            {
                                                if (pc.currentShapeIndex > 0) 
                                                {
                                                    pc.currentShapeIndex--;
                                                } else 
                                                {
                                                    // 循环到最后一个角度
                                                    if (!pieces[selectedPieceType].shapes.empty()) 
                                                    {
                                                        pc.currentShapeIndex = (int)pieces[selectedPieceType].shapes.size() - 1;
                                                    }
                                                }
                                                buttonClicked = true;
                                                break;
                                            }
                                        }
                                    }
                                    
                                    // 角度选择按钮（右箭头）（使用与绘制时一致的坐标）
                                    if (!buttonClicked && mousePos.x >= previewAreaX + 180 && mousePos.x < previewAreaX + 210 &&
                                        mousePos.y >= angleBtnY && mousePos.y < angleBtnY + 30) 
                                    {
                                        for (auto& pc : pieceCounts) 
                                        {
                                            if (pc.pieceId == pieces[selectedPieceType].id) 
                                            {
                                                if (!pieces[selectedPieceType].shapes.empty()) 
                                                {
                                                    if (pc.currentShapeIndex < (int)pieces[selectedPieceType].shapes.size() - 1) 
                                                    {
                                                        pc.currentShapeIndex++;
                                                    } else 
                                                    {
                                                        // 循环到第一个角度
                                                        pc.currentShapeIndex = 0;
                                                    }
                                                }
                                                buttonClicked = true;
                                                break;
                                            }
                                        }
                                    }
                                }
                                }
                            }
                        }  // 关闭 showEditor && clickedInEditor 的if块
                } else if (event.mouseButton.button == Mouse::Right) 
            {
                    mouseRightPressed = true;
                    
                    mousePos = Mouse::getPosition(window);  // 更新鼠标位置
                    int offsetX = 50;
                    int offsetY = 50;
                    
                    // 确保右键行为正确：只有在鼠标选中并持续按住左键的时候，才能旋转
                    // 检查条件：1. 正在拖拽 2. 左键仍然被按住 3. 在游戏板区域内
                    bool isInBoardArea = false;
                        int boardX = mousePos.x - offsetX;
                        int boardY = mousePos.y - offsetY;
                        int col = boardX / CELL_SIZE;
                        int row = boardY / CELL_SIZE;
                    if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) 
                    {
                        isInBoardArea = true;
                    }
                    
                    // 禁用右键旋转功能：除编辑器指定角度外，图块不能旋转
                    // 不再允许在拖拽状态下通过右键旋转图块
                    // 右键只取下图块，不旋转
                    // 检查是否点击在游戏板上，如果是则取下图块
                    // 注意：boardX, boardY, col, row 已经在上面定义过了
                    if (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE) 
                    {
                        // 如果正在显示解，且用户右键点击了图块，切换到编辑模式
                        if (showSolution) 
                        {
                            // 将solutionBoard复制到board，允许用户编辑
                            lock_guard<mutex> lock(boardMutex);
                            for (int i = 0; i < BOARD_SIZE; i++) 
                            {
                                for (int j = 0; j < BOARD_SIZE; j++) 
                                {
                                    board[i][j] = solutionBoard[i][j];
                                }
                            }
                            showSolution = false;
                        }
                        
                            int cellValue = board[row][col];
                        if (cellValue != 0) 
                        {
                                // 右键取下图块（只移除被点击的那个图块实例）
                                // 找到对应的图块和形状
                                // 首先尝试通过tempId找到对应的pieceCounts条目
                                const Piece* clickedPiece = nullptr;
                                int clickedPieceId = -1;
                                int clickedShapeIndex = 0;
                                int baseRow = -1, baseCol = -1;
                                
                                // 首先尝试通过pieceCounts查找
                                for (const auto& pc : pieceCounts) 
                                {
                                    if (pc.tempId == cellValue) 
                                    {
                                        clickedPieceId = pc.pieceId;
                                        break;
                                    }
                                }
                                
                                // 如果通过pieceCounts找不到，尝试通过形状匹配（处理从预览区拖出的图块）
                                if (clickedPieceId < 0) 
                                {
                                    // 遍历所有图块类型，尝试匹配形状
                                    for (const auto& p : pieces) 
                                    {
                                        for (size_t s = 0; s < p.shapes.size(); s++) 
                                        {
                                            const auto& testShape = p.shapes[s];
                                            // 尝试将当前单元格作为形状的一部分
                                            for (const auto& cellPos : testShape) 
                                            {
                                                int tryBaseRow = row - cellPos.first;
                                                int tryBaseCol = col - cellPos.second;
                                                if (tryBaseRow < 0 || tryBaseCol < 0) continue;
                                                
                                                // 检查这个基础位置是否匹配整个形状
                                                bool matches = true;
                                                int matchedCells = 0;
                                                for (const auto& checkCell : testShape) 
                                                {
                                                    int checkRow = tryBaseRow + checkCell.first;
                                                    int checkCol = tryBaseCol + checkCell.second;
                                                    if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                                        checkCol < 0 || checkCol >= BOARD_SIZE ||
                                                        board[checkRow][checkCol] != cellValue) 
                                                    {
                                                        matches = false;
                                                        break;
                                                    }
                                                    matchedCells++;
                                                }
                                                
                                                // 确保匹配的单元格数量等于形状大小
                                                if (matches && matchedCells == (int)testShape.size()) 
                                                {
                                                    clickedPieceId = p.id;
                                                    clickedPiece = &p;
                                                    clickedShapeIndex = (int)s;
                                                    baseRow = tryBaseRow;
                                                    baseCol = tryBaseCol;
                                                    break;
                                                }
                                            }
                                            if (clickedPieceId >= 0) break;
                                        }
                                        if (clickedPieceId >= 0) break;
                                    }
                                } else 
                                {
                                    // 如果通过pieceCounts找到了pieceId，找到对应的图块类型
                                    for (size_t i = 0; i < pieces.size(); i++) 
                                    {
                                        if (pieces[i].id == clickedPieceId) 
                                        {
                                        clickedPiece = &pieces[i];
                                        // 找到当前使用的形状，通过尝试不同的基础位置来匹配
                                            for (size_t s = 0; s < pieces[i].shapes.size(); s++) 
                                            {
                                            const auto& shape = pieces[i].shapes[s];
                                            
                                            // 找到形状中哪个cell对应点击位置
                                                for (const auto& cell : shape) 
                                                {
                                                // 尝试这个cell作为点击位置
                                                int tryBaseRow = row - cell.first;
                                                int tryBaseCol = col - cell.second;
                                                
                                                // 检查这个基础位置是否匹配整个形状
                                                bool matches = true;
                                                    int matchedCells = 0;
                                                    for (const auto& checkCell : shape) 
                                                    {
                                                    int checkRow = tryBaseRow + checkCell.first;
                                                    int checkCol = tryBaseCol + checkCell.second;
                                                    if (checkRow < 0 || checkRow >= BOARD_SIZE ||
                                                        checkCol < 0 || checkCol >= BOARD_SIZE ||
                                                            board[checkRow][checkCol] != cellValue) 
                                                        {
                                                        matches = false;
                                                        break;
                                                    }
                                                        matchedCells++;
                                                }
                                                
                                                    // 确保匹配的单元格数量等于形状大小
                                                    if (matches && matchedCells == (int)shape.size()) 
                                                    {
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
                                }
                                
                                // 移除被点击的图块实例
                                if (clickedPiece && baseRow >= 0 && baseCol >= 0 &&
                                    clickedShapeIndex >= 0 && clickedShapeIndex < (int)clickedPiece->shapes.size()) 
                                {
                                    const auto& shape = clickedPiece->shapes[clickedShapeIndex];
                                    removePiece(shape, baseRow, baseCol);
                            }
                        }
                    }
                }
            }
            
            if (event.type == Event::MouseButtonReleased) 
            {
                mousePos = Mouse::getPosition(window);
                int offsetX = 50;
                int offsetY = 50;
                
                if (event.mouseButton.button == Mouse::Left) 
                {
                    mouseLeftPressed = false;
                    
                    // 停止编辑器拖拽
                    if (editorDrag.isDragging) 
                    {
                        editorDrag.isDragging = false;
                    }
                    
                    if (draggedPiece.isDragging) 
                    {
                        // 如果正在显示解，先切换到编辑模式，允许用户放置图块
                        // 这样用户在求解开始前或求解过程中可以继续操作游戏板
                        if (showSolution) 
                        {
                            // 将solutionBoard复制到board，允许用户编辑
                            lock_guard<mutex> lock(boardMutex);
                            for (int i = 0; i < BOARD_SIZE; i++) 
                            {
                                for (int j = 0; j < BOARD_SIZE; j++) 
                                {
                                    board[i][j] = solutionBoard[i][j];
                                }
                            }
                            showSolution = false;  // 切换到编辑模式
                        }
                        
                        // 计算放置位置
                        int boardX = mousePos.x - offsetX;
                        int boardY = mousePos.y - offsetY;
                        int col = boardX / CELL_SIZE;
                        int row = boardY / CELL_SIZE;
                        
                        // 找到对应的图块
                        const Piece* piece = nullptr;
                        for (const auto& p : pieces) 
                        {
                            if (p.id == draggedPiece.pieceId) 
                            {
                                piece = &p;
                                break;
                            }
                        }
                        
                        if (piece && draggedPiece.shapeIndex < (int)piece->shapes.size()) 
                        {
                            const auto& shape = piece->shapes[draggedPiece.shapeIndex];
                            
                            // 计算图块的边界（用于计算基准点）
                            int minRow = shape[0].first, maxRow = shape[0].first;
                            int minCol = shape[0].second, maxCol = shape[0].second;
                            for (const auto& cell : shape) 
                            {
                                minRow = min(minRow, cell.first);
                                maxRow = max(maxRow, cell.first);
                                minCol = min(minCol, cell.second);
                                maxCol = max(maxCol, cell.second);
                            }
                            
                            // 从预览区拖出时，需要根据dragOffset计算正确的基准点位置
                            // dragOffset是鼠标相对于图块实际绘制区域左上角的位置（像素）
                            // 图块实际绘制区域的左上角在游戏板上的位置 = 鼠标位置 - offset - dragOffset
                            // 基准点位置 = (图块实际绘制区域左上角位置) / CELL_SIZE - (minCol, minRow)
                            int baseRow = row, baseCol = col;
                            if (draggedPiece.originalRow < 0 && draggedPiece.originalCol < 0) 
                            {
                                // 从预览区拖出：根据dragOffset计算基准点
                                // dragOffset是鼠标相对于图块实际绘制区域左上角的位置（像素）
                                // 在预览区中，图块使用动态cellSize，在游戏板中使用固定CELL_SIZE
                                // 需要将dragOffset从预览区坐标系转换为游戏板坐标系
                                
                                // 重新计算预览区的cellSize（与点击时相同）
                                int maxDim = 0;
                                for (const auto& cell : shape) 
                                {
                                    maxDim = max(maxDim, max(cell.first, cell.second));
                                }
                                // 预览区的itemSize（与点击时相同）
                                int previewWidth = BOARD_SIZE * CELL_SIZE;
                                int itemsPerRow = 8;
                                int itemSize = (previewWidth - 40) / itemsPerRow - 10;
                                int previewCellSize = maxDim > 0 ? itemSize / (maxDim + 1) : itemSize / 3;
                                
                                // 将dragOffset从预览区坐标系转换为游戏板坐标系
                                // dragOffset是像素偏移，需要按比例缩放
                                if (previewCellSize > 0) 
                                {
                                    float scale = (float)CELL_SIZE / (float)previewCellSize;
                                    int scaledDragOffsetX = (int)(draggedPiece.dragOffset.x * scale);
                                    int scaledDragOffsetY = (int)(draggedPiece.dragOffset.y * scale);
                                    
                                    // 计算图块实际绘制区域的左上角在游戏板上的位置（像素）
                                    int shapeTopLeftX = boardX - scaledDragOffsetX;
                                    int shapeTopLeftY = boardY - scaledDragOffsetY;
                                    // 转换为单元格坐标
                                    int shapeTopLeftCol = shapeTopLeftX / CELL_SIZE;
                                    int shapeTopLeftRow = shapeTopLeftY / CELL_SIZE;
                                    // 基准点 = 图块实际绘制区域左上角 - (minCol, minRow)
                                    baseCol = shapeTopLeftCol - minCol;
                                    baseRow = shapeTopLeftRow - minRow;
                                } else 
                                {
                                    // 如果无法计算cellSize，使用简单的方法：直接使用鼠标位置
                                    baseCol = col - minCol;
                                    baseRow = row - minRow;
                                }
                            }
                            
                            // 检查是否可以放置
                            bool canPlaceHere = false;
                            // 首先检查鼠标是否在游戏板区域内
                            bool mouseInBoard = (row >= 0 && row < BOARD_SIZE && col >= 0 && col < BOARD_SIZE);
                            
                            if (mouseInBoard && baseRow >= 0 && baseRow < BOARD_SIZE && baseCol >= 0 && baseCol < BOARD_SIZE) 
                            {
                                if (draggedPiece.originalRow >= 0 && draggedPiece.originalCol >= 0) 
                                {
                                    // 从游戏板上拖拽：需要检查目标位置，但忽略原始位置
                                    // 重要：只检查形状中实际定义的单元格，不检查边界框内的"透明"区域
                                    canPlaceHere = true;
                                    for (const auto& cell : shape) 
                                    {
                                        int newRow = baseRow + cell.first;
                                        int newCol = baseCol + cell.second;
                                        
                                        if (newRow < 0 || newRow >= BOARD_SIZE || 
                                            newCol < 0 || newCol >= BOARD_SIZE) 
                                        {
                                            canPlaceHere = false;
                                            break;
                                        }
                                        
                                        // 检查这个位置是否在原始图块的范围内（允许放回原位置）
                                        // 只检查原始图块实际占据的单元格，不检查边界框
                                        bool isInOriginal = false;
                                        for (const auto& origCell : shape) 
                                        {
                                            int origRow = draggedPiece.originalRow + origCell.first;
                                            int origCol = draggedPiece.originalCol + origCell.second;
                                            if (newRow == origRow && newCol == origCol) 
                                            {
                                                isInOriginal = true;
                                                break;
                                            }
                                        }
                                        
                                        // 如果不在原始位置范围内，且被其他图块占据，则不能放置
                                        // 注意：只检查形状中定义的单元格，边界框内的"透明"区域应该被视为空白
                                        if (!isInOriginal && board[newRow][newCol] != 0) 
                                        {
                                            canPlaceHere = false;
                                            break;
                                        }
                                    }
                                } else 
                                {
                                    // 从预览区拖出：正常检查（只检查形状中定义的单元格）
                                    canPlaceHere = canPlace(shape, baseRow, baseCol);
                                }
                            }
                            
                            if (canPlaceHere) 
                            {
                                // 放置图块（使用tempId和正确的基准点位置）
                                placePiece(shape, baseRow, baseCol, draggedPiece.tempId);
                                
                                // 重要：从预览区拖出的图块，tempId是新分配的，需要更新到pieceCounts中
                                // 这样绘制时才能正确识别图块
                                // 找到对应的pieceCounts条目，更新tempId（如果还没有设置）
                                if (draggedPiece.originalRow < 0 && draggedPiece.originalCol < 0) 
                                {
                                    // 从预览区拖出：需要更新pieceCounts中的tempId
                                    for (auto& pc : pieceCounts) 
                                    {
                                        if (pc.pieceId == draggedPiece.pieceId) 
                                        {
                                            // 如果pieceCounts中的tempId还没有被使用（board上没有这个tempId），
                                            // 或者这个tempId就是当前放置的tempId，则更新
                                            bool tempIdInUse = false;
                                            for (int i = 0; i < BOARD_SIZE; i++) 
                                            {
                                                for (int j = 0; j < BOARD_SIZE; j++) 
                                                {
                                                    if (board[i][j] == pc.tempId && pc.tempId != draggedPiece.tempId) 
                                                    {
                                                        tempIdInUse = true;
                                                        break;
                                                    }
                                                }
                                                if (tempIdInUse) break;
                                            }
                                            
                                            // 如果tempId没有被使用，或者就是当前放置的tempId，则更新
                                            if (!tempIdInUse || pc.tempId == draggedPiece.tempId) 
                                            {
                                                pc.tempId = draggedPiece.tempId;
                                                break;
                                            }
                                        }
                                    }
                                }
                                
                                // 图块放置后，强制重新渲染预览区（通过重新计算已使用数量）
                                // calculateUsedPieceCounts()会在下一帧自动重新计算，预览区会更新
                            } else 
                            {
                                // 无法放置，恢复到原位置（如果有）
                                if (draggedPiece.originalRow >= 0 && draggedPiece.originalCol >= 0) 
                                {
                                    placePiece(shape, draggedPiece.originalRow, 
                                             draggedPiece.originalCol, draggedPiece.tempId);
                                    // 恢复到原位置后，预览区不需要更新（数量没变）
                                }
                                // 如果从预览区拖出，无法放置时不恢复（图块回到预览区）
                                // 此时预览区会通过calculateUsedPieceCounts()自动更新
                            }
                        }
                        
                        // 重置拖拽状态
                        draggedPiece.isDragging = false;
                        draggedPiece.pieceId = -1;
                        draggedPiece.tempId = -1;
                    }
                    }
                } else if (event.mouseButton.button == Mouse::Right) 
                {
                    mouseRightPressed = false;
                    isRotating = false;
                    
                    // 如果正在拖拽图块，右键取消拖拽，让图块回到预览区或原位置
                    if (draggedPiece.isDragging) 
                    {
                        // 找到对应的图块
                        const Piece* piece = nullptr;
                        for (const auto& p : pieces) 
                        {
                            if (p.id == draggedPiece.pieceId) 
                            {
                                piece = &p;
                                break;
                            }
                        }
                        
                        if (piece && draggedPiece.shapeIndex < (int)piece->shapes.size()) 
                        {
                            const auto& shape = piece->shapes[draggedPiece.shapeIndex];
                            
                            // 如果从游戏板上拖拽，恢复到原位置
                            if (draggedPiece.originalRow >= 0 && draggedPiece.originalCol >= 0) 
                            {
                                placePiece(shape, draggedPiece.originalRow, 
                                         draggedPiece.originalCol, draggedPiece.tempId);
                            }
                            // 如果从预览区拖出，不恢复（图块回到预览区）
                            // 此时预览区会通过calculateUsedPieceCounts()自动更新
                        }
                        
                        // 重置拖拽状态
                        draggedPiece.isDragging = false;
                        draggedPiece.pieceId = -1;
                        draggedPiece.tempId = -1;
                    }
                }
            }
            
            if (event.type == Event::MouseMoved) 
            {
                mousePos = Mouse::getPosition(window);
                
                // 处理编辑器拖拽
                if (editorDrag.isDragging) 
                {
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

