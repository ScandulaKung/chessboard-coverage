#ifndef COLOR_BOTTLE_GAME_H
#define COLOR_BOTTLE_GAME_H

#include <QtWidgets/QWidget>
#include <QtGui/QPainter>
#include <QtGui/QPainterPath>
#include <QtGui/QPixmap>
#include <QtGui/QMouseEvent>
#include <QtGui/QKeyEvent>
#include <QtGui/QResizeEvent>
#include <QtGui/QTransform>
#include <QtSvg/QSvgRenderer>
#include <QtCore/QTimer>
#include <QtCore/QHash>
#include <QtCore/QByteArray>
#include <QtCore/QString>
#ifdef QT_MULTIMEDIA_AVAILABLE
#include <QtMultimedia/QSoundEffect>
#endif
#include <vector>
#include <random>
#include <algorithm>
#include <cmath>
#include <string>
#include <sstream>
#include <functional>
#include "common.h"
#include "nonogram_game.h"
#include "hanoi_game.h"
#include "snake_game.h"
#include "picnic_game.h"
#include <array>
#include <map>
#include <utility>
#include <tuple>
#include <QtCore/QDateTime>

class ColorBottleGame : public QWidget 
{
    Q_OBJECT

private:
    // 游戏模式
    GameMode currentMode;
    
    // 游戏状态（瓶子游戏 / 猜瓶子）
    int currentLevel;       // 1–15，通关第 15 关后再继续回到 1
    int maxLevels;          // 固定 15
    int bottleCount;        // 本关隐藏瓶数量
    int maxAttempts;        // 本关允许尝试次数
    int remainingAttempts;
    bool gameWon;
    bool gameOver;
    bool showingResult;     // 本轮比对结束，等待点击继续
    
    // 颜色定义
    std::vector<QColor> allColors;          // 全局色库
    std::vector<QColor> availableColors;     // 当前 Color Palette 刷子颜色（顺序即绘制顺序）
    std::vector<QColor> hiddenBottles;      // 本关答案（与瓶位一一对应）
    std::vector<QColor> userBottles;        // 当前尝试行用户所选色
    std::vector<QColor> usedColorsInAttempt;
    std::vector<std::vector<QColor>> attemptHistory;      // 历史尝试占位（透明=该位曾尝试）
    std::vector<std::vector<QColor>> attemptWrongColors;  // 历史错误飞溅色
    std::vector<QColor> wrongColors;        // 当前行错误飞溅
    std::vector<bool> matchedBottles;       // 哪些瓶位已永久匹配
    std::vector<int> matchedIndices;
    int currentAttemptRow;    
    // UI元素位置
    float bottleSize;
    float bottleSpacing;
    float startX;
    float startY;
    float colorPaletteX;  // 颜色面板X位置（右侧）
    float colorPaletteY;  // 颜色面板Y位置
    int colorPaletteColumns;  // 本关锁定的列数（开局若为两列则颜色减少后仍保持两列）
    int colorPaletteFirstColRows;  // 两列时锁定的第一列行数（减色先从第二列开始）
    int colorPaletteStartCount;    // 本关开局色板颜色数
    
    // 随机数生成器
    std::mt19937 rng;
    
    // SVG渲染器（油漆刷）
    QSvgRenderer* brushRenderer;
    
    // 刷子 / 瓶子贴图
    QPixmap brushPixmap;
    QPixmap bottlePixmap;
    
    // 棋盘填色游戏
    NonogramGame nonogramGame;
    bool nonogramEditorMode;      // 编辑器模式
    bool nonogramShowSolution;    // 显示求解结果
    
    // 汉诺塔游戏
    HanoiGame hanoiGame;
    QTimer* hanoiAnimationTimer;  // 动画计时器
    int hanoiDiskCount;           // 圆盘数量（可调整）
    
    // 贪吃蛇游戏
    SnakeGame snakeGame;
    QTimer* snakeGameTimer;       // 贪吃蛇游戏计时器
#ifdef QT_MULTIMEDIA_AVAILABLE
    QSoundEffect* foodSoundEffect;    // 吃到食物的音效
    QSoundEffect* gameOverSoundEffect; // 游戏结束的音效
#endif

    // 野餐日拼图
    PicnicGame picnicGame;
    PicnicDifficulty picnicDifficulty;
    int picnicCellSize;
    int picnicBoardX;
    int picnicBoardY;
    int picnicMouseX;
    int picnicMouseY;
    bool picnicDragging;
    QString picnicFeedback;
    qint64 picnicFeedbackUntilMs;
    bool picnicShowShapeEditor;
    bool picnicStoneEditMode;  // 摆石头模式：左键在棋盘上切换石头
    bool picnicEnumerateCancel;
    bool picnicEnumerateRunning;
    bool picnicEditorDragging;
    int picnicEditorX;
    int picnicEditorY;
    int picnicEditorWidth;
    int picnicEditorHeight;
    int picnicEditorDragOffsetX;
    int picnicEditorDragOffsetY;
    int picnicEditorSelectedIndex;  // 编辑器当前选中的图块行，-1 表示未选中
    // name, counts[0/90/180/270], color
    std::vector<std::tuple<std::string, std::array<int, 4>, Color>> picnicShapeInfo;
    QString picnicResourcesPath;
    bool picnicUseTextures;
    QHash<QString, QPixmap> picnicBaseTextures;      // 图块名 -> 基础贴图
    QHash<QString, QPixmap> picnicRotatedTextures;   // name_rotN -> 旋转后贴图
    int picnicTexturesLoaded;
    // 自动求解成功记录（外部文件）
    struct PicnicSolveRecord {
        PicnicDifficulty difficulty = PicnicDifficulty::Hard;
        std::vector<std::pair<int, int>> stones;  // (x, y)
        std::map<std::string, int> shapeRotations;
    };
    std::vector<PicnicSolveRecord> picnicSolveRecords;
    QHash<QByteArray, int> picnicSolveRecordIndexByFp;  // 指纹 -> 索引，O(1) 去重
    bool picnicSolveRecordsLoaded;  // 延迟加载，避免启动卡顿
    int picnicSolveRecordIndex;  // -1 表示未从记录加载
    QString picnicSolveRecordsPath;

    int menuBarHeight;            // 菜单栏高度
    
    void initializeLevel();
    void checkMatch();
    bool isAllBottlesColored();
    void refreshAvailableColors();
    void drawBottle(QPainter& painter, float x, float y, QColor color, bool isMatched, bool isHidden, bool showRealColor = false);
    void drawWrongColorPuddle(QPainter& painter, float x, float y, QColor color);
    void drawColorPalette(QPainter& painter);
    int colorPaletteColumnCount() const;
    QRectF colorPaletteCellRect(int index) const;
    int getColorIndexAt(float x, float y);
    int getBottleIndexAt(float x, float y);
    void drawMenuBar(QPainter& painter);
    void drawHanoiGame(QPainter& painter);
    void drawSnakeGame(QPainter& painter);
    void drawNonogramGame(QPainter& painter);
    void drawNonogramEditor(QPainter& painter);
    void drawPicnicGame(QPainter& painter);
    void drawPicnicShapeEditor(QPainter& painter);
    int getNonogramCellAt(float x, float y);
    // 侧栏 X：固定按 6 列棋盘宽度对齐（与难度无关）
    int picnicSidebarX() const;
    bool isPicnicBoardPoint(int x, int y) const;
    bool isPicnicPanelPoint(int x, int y) const;
    bool handlePicnicSidebarClick(int x, int y);
    std::shared_ptr<Shape> getPicnicShapeFromPanel(int x, int y);
    void solvePicnicPuzzle();
    void enumeratePicnicFillCombinations();
    void setPicnicFeedback(const QString& message, int durationMs = 3000);
    void initializePicnicShapeEditor();
    void handlePicnicShapeEditorClick(int x, int y);
    void applyPicnicShapeEditorChanges();
    bool getPicnicSnapPlacement(int& outX, int& outY, bool& canPlace) const;
    void initializePicnicTextures();
    QString findPicnicResourcesPath() const;
    QPixmap loadPicnicBaseTexture(const std::string& shapeName);
    QPixmap getPicnicShapeTexture(const std::string& shapeName, int rotation);
    void drawPicnicShapePixmap(QPainter& painter, int x, int y, const std::string& shapeName,
                               int rotation, int cellSize, qreal opacity = 1.0);
    void setPicnicDifficulty(PicnicDifficulty difficulty);
    QString picnicSolveRecordsFilePath() const;
    void ensurePicnicSolveRecordsLoaded();
    void loadPicnicSolveRecords();
    QByteArray picnicSolveRecordFingerprint(const PicnicSolveRecord& record) const;
    PicnicSolveRecord capturePicnicSolveRecord() const;
    bool picnicSolveRecordsEqual(const PicnicSolveRecord& a, const PicnicSolveRecord& b) const;
    void appendPicnicSolveRecord(const PicnicSolveRecord& record);
    void applyPicnicSolveRecord(int index);
    void navigatePicnicSolveRecord(int delta);
    bool tryApplyRandomPicnicSolveRecord(PicnicDifficulty difficulty);
    std::vector<int> picnicSolveRecordIndicesFor(PicnicDifficulty difficulty) const;
    bool picnicSolveRecordMeetsLimits(const PicnicSolveRecord& record) const;

    // 设计分辨率缩放（窗口可缩放，内容按 1400×900 等比适配并居中）
    // 绘制走设计坐标；鼠标事件需先 mapToDesign 再参与命中测试
    static constexpr int kDesignWidth = 1400;
    static constexpr int kDesignHeight = 900;
    qreal uiScale() const;
    QPointF uiOrigin() const;
    QPointF mapToDesign(const QPointF& widgetPos) const;
    void applyUiTransform(QPainter& painter) const;

public:
    ColorBottleGame(QWidget* parent = nullptr);
    ~ColorBottleGame();
    
protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
};

#endif // COLOR_BOTTLE_GAME_H
