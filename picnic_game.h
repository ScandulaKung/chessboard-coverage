#ifndef PICNIC_GAME_H
#define PICNIC_GAME_H

#include "shape.h"
#include <vector>
#include <memory>
#include <map>
#include <functional>
#include <cmath>
#include <ctime>

// 已放置图块信息结构
struct PlacedShape {
    std::shared_ptr<Shape> shape;
    int x;
    int y;
    int rotation;
    int shapeId; // 用于精确匹配
    
    PlacedShape(std::shared_ptr<Shape> s, int x, int y, int rot)
        : shape(s), x(x), y(y), rotation(rot), shapeId(s->getId()) {}
};

// 野餐日难度：初级 4×4、中级 4×6、高级 6×6
enum class PicnicDifficulty {
    Easy = 0,     // 初级 4行×4列
    Medium = 1,   // 中级 4行×6列
    Hard = 2      // 高级 6行×6列
};

// 野餐日拼图填色游戏核心逻辑
class PicnicGame {
private:
    int boardCols;
    int boardRows;
    int cellPixelSize;
    PicnicDifficulty difficulty;
    std::vector<std::vector<std::shared_ptr<Shape>>> board;
    std::vector<std::vector<bool>> stones;  // true = 石头占格，不可放图块
    std::vector<std::shared_ptr<Shape>> availableShapes;
    std::vector<PlacedShape> placedShapes;
    
    // 全局唯一ID计数器
    static int globalShapeIdCounter;
    
    // 拖拽状态
    std::shared_ptr<Shape> selectedShape;
    bool isDragging;
    bool isDraggedOut; // 是否拖出游戏板
    int dragOffsetX, dragOffsetY;
    int draggedOutRotation; // 拖出状态下的旋转角度
    bool draggingFromPanel; // 是否从面板拖拽
    
    // 原始位置信息（用于恢复）
    int originalX, originalY;
    int originalRotation;
    bool hasOriginalPosition;
    
    // 面板旋转状态
    std::map<int, int> panelRotations; // shapeId -> rotation
    
    // 求解过程刷新回调函数
    std::function<void()> solveUpdateCallback;
    
    void initBoardStorage();
    void applyDefaultShapesForDifficulty();
    
public:
    // 图块数量配置（旧格式，兼容性）
    std::map<std::string, int> shapeCounts;
    
    // 图块角度配置（新格式，形状名_角度 -> 数量）
    std::map<std::string, int> shapeRotations;
    PicnicGame();
    PicnicGame(const std::string& configFile);
    
    // 配置文件加载
    void loadConfigFromFile(const std::string& filename);
    
    // 初始化游戏
    void initializeShapes();
    
    // 难度 / 棋盘尺寸
    PicnicDifficulty getDifficulty() const { return difficulty; }
    void setDifficulty(PicnicDifficulty d);
    int getBoardCols() const { return boardCols; }
    int getBoardRows() const { return boardRows; }
    int getBoardArea() const { return boardCols * boardRows; }
    int getPlayableArea() const { return getBoardArea() - getStoneCount(); }
    int getCellPixelSize() const { return cellPixelSize; }
    void setCellPixelSize(int size) { cellPixelSize = size > 0 ? size : 60; }

    // 石头：占格且不可放入图块；重置棋盘不清除石头
    bool isStone(int x, int y) const;
    bool isCellFree(int x, int y) const;  // 无图块且无石头
    bool setStone(int x, int y, bool on);
    bool toggleStone(int x, int y);       // 有图块时失败；返回是否变为石头
    void clearStones();
    int getStoneCount() const;
    
    // 图块操作
    bool canPlaceShape(std::shared_ptr<Shape> shape, int x, int y, int rotation = 0);
    bool canPlaceShapeConst(std::shared_ptr<Shape> shape, int x, int y, int rotation = 0) const;
    bool placeShape(std::shared_ptr<Shape> shape, int x, int y, int rotation = 0);
    void removeShapeAt(int x, int y);
    void removeShapeById(int shapeId);
    bool rotateSelectedShape();
    
    // 拖拽操作
    void startDrag(std::shared_ptr<Shape> shape, int mouseX, int mouseY, bool fromPanel = false);
    void updateDrag(int mouseX, int mouseY);
    bool endDrag(int mouseX, int mouseY);
    void cancelDrag();
    
    // 游戏状态
    bool isGameWon() const;
    void resetGame();
    
    // Getters
    std::shared_ptr<Shape> getSelectedShape() const { return selectedShape; }
    bool getIsDragging() const { return isDragging; }
    bool getIsDraggedOut() const { return isDraggedOut; }
    int getDraggedOutRotation() const { return draggedOutRotation; }
    bool getDraggingFromPanel() const { return draggingFromPanel; }
    int getDragOffsetX() const { return dragOffsetX; }
    int getDragOffsetY() const { return dragOffsetY; }
    
    const std::vector<std::shared_ptr<Shape>>& getAvailableShapes() const { return availableShapes; }
    std::vector<std::shared_ptr<Shape>> getAvailableShapesForPanel() const;
    const std::vector<PlacedShape>& getPlacedShapes() const { return placedShapes; }
    std::shared_ptr<Shape> getBoardCell(int x, int y) const;
    
    // 面板旋转管理
    int getPanelRotation(int shapeId) const;
    void setPanelRotation(int shapeId, int rotation);
    int incrementPanelRotation(int shapeId);
    
    // 图块数量编辑
    void updateShapeCount(const std::string& shapeName, int count);
    int getShapeCount(const std::string& shapeName) const;
    
    // 图块角度配置编辑
    void updateShapeRotation(const std::string& shapeName, int angle, int count);
    int getShapeRotationCount(const std::string& shapeName, int angle) const;
    std::vector<std::string> getAllShapeNames() const;
    void rebuildShapes();
    
    // ID管理
    static void resetGlobalShapeIdCounter();
    static int getNextShapeId();
    
    bool isShapeUsed(std::shared_ptr<Shape> shape) const;
    std::shared_ptr<Shape> getShapeByName(const std::string& name) const;
    
    bool solveFillEntireBoard();
    bool isBoardCompletelyFilled() const;

    // 穷举所有可填满当前棋盘（含石头）的图块组合（含角度）。
    // 限制：1x1≤2，Line2 各角度合计≤2。onCombination 每发现一个唯一组合调用一次。
    // shouldStop 返回 true 时中止。返回发现的唯一组合数。
    int enumerateFillCombinations(
        const std::function<void(const std::map<std::string, int>&)>& onCombination,
        const std::function<bool()>& shouldStop = nullptr,
        const std::function<void(int attempts, int found)>& onProgress = nullptr);

    void setSolveUpdateCallback(std::function<void()> callback) { solveUpdateCallback = callback; }
    
private:
    std::vector<std::vector<bool>> getRotatedPattern(std::shared_ptr<Shape> shape, int rotation) const;
    PlacedShape* findPlacedShape(int shapeId);
    void loadConfigFromJson(std::ifstream& file);
    
    void findConnectedRegions(std::vector<std::vector<std::pair<int, int>>>& regions) const;
    bool canFillAreaWithShapes(int area, const std::vector<int>& shapeAreas) const;
    void syncBoardWithPlacedShapes();
};

#endif
