#ifndef SNAKE_GAME_H
#define SNAKE_GAME_H

#include <vector>
#include <utility>
#include <random>
#include <functional>

// 贪吃蛇游戏类
class SnakeGame 
{
public:
    enum Direction {
        UP,
        DOWN,
        LEFT,
        RIGHT
    };
    
    struct Point {
        int x;
        int y;
        Point(int x = 0, int y = 0) : x(x), y(y) {}
        bool operator==(const Point& other) const {
            return x == other.x && y == other.y;
        }
    };
    
    int gridWidth;      // 网格宽度（格子数）
    int gridHeight;     // 网格高度（格子数）
    std::vector<Point> snake;  // 蛇的身体（第一个是头部）
    Direction direction;       // 当前方向
    Direction nextDirection;  // 下一个方向（用于防止180度转向）
    Point food;         // 食物位置
    int score;          // 分数
    bool gameOver;      // 游戏是否结束
    bool paused;        // 是否暂停
    std::mt19937 rng;   // 随机数生成器
    
    // 回调函数
    std::function<void()> onFoodEaten;      // 吃到食物时的回调
    std::function<void()> onGameOver;       // 游戏结束时的回调
    
    SnakeGame(int width = 20, int height = 20);
    
    // 重置游戏
    void reset();
    
    // 设置方向
    void setDirection(Direction dir);
    
    // 更新游戏状态（移动蛇）
    void update();
    
    // 生成新食物
    void generateFood();
    
    // 检查碰撞
    bool checkCollision() const;
    
    // 检查点是否在蛇身上
    bool isPointOnSnake(const Point& p) const;
};

#endif // SNAKE_GAME_H
