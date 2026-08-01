#include "snake_game.h"
#include <algorithm>

SnakeGame::SnakeGame(int width, int height) 
    : gridWidth(width), gridHeight(height), direction(RIGHT), nextDirection(RIGHT),
      score(0), gameOver(false), paused(false), rng(std::random_device()()),
      onFoodEaten(nullptr), onGameOver(nullptr)
{
    reset();
}

void SnakeGame::reset()
{
    snake.clear();
    // 初始化蛇在中间位置，长度为3
    int startX = gridWidth / 2;
    int startY = gridHeight / 2;
    snake.push_back(Point(startX, startY));
    snake.push_back(Point(startX - 1, startY));
    snake.push_back(Point(startX - 2, startY));
    
    direction = RIGHT;
    nextDirection = RIGHT;
    score = 0;
    gameOver = false;
    paused = false;
    
    generateFood();
}

void SnakeGame::setDirection(Direction dir)
{
    // 防止180度转向
    if ((direction == UP && dir == DOWN) ||
        (direction == DOWN && dir == UP) ||
        (direction == LEFT && dir == RIGHT) ||
        (direction == RIGHT && dir == LEFT))
    {
        return;
    }
    nextDirection = dir;
}

void SnakeGame::update()
{
    if (gameOver || paused)
    {
        return;
    }
    
    // 更新方向
    direction = nextDirection;
    
    // 计算新头部位置
    Point newHead = snake[0];
    switch (direction)
    {
        case UP:
            newHead.y--;
            break;
        case DOWN:
            newHead.y++;
            break;
        case LEFT:
            newHead.x--;
            break;
        case RIGHT:
            newHead.x++;
            break;
    }
    
    // 检查是否吃到食物
    bool ateFood = (newHead.x == food.x && newHead.y == food.y);
    
    // 移动蛇
    snake.insert(snake.begin(), newHead);
    
    if (!ateFood)
    {
        // 没吃到食物，移除尾部
        snake.pop_back();
    }
    else
    {
        // 吃到食物，增加分数，生成新食物
        score++;
        generateFood();
        // 触发吃到食物的回调
        if (onFoodEaten)
        {
            onFoodEaten();
        }
    }
    
    // 检查碰撞
    bool wasGameOver = gameOver;
    if (checkCollision())
    {
        gameOver = true;
        // 如果游戏刚刚结束，触发游戏结束回调
        if (!wasGameOver && onGameOver)
        {
            onGameOver();
        }
    }
}

void SnakeGame::generateFood()
{
    // 生成不在蛇身上的随机位置
    int attempts = 0;
    do
    {
        food.x = static_cast<int>(rng() % gridWidth);
        food.y = static_cast<int>(rng() % gridHeight);
        attempts++;
    } while (isPointOnSnake(food) && attempts < 1000);
}

bool SnakeGame::checkCollision() const
{
    if (snake.empty())
    {
        return true;
    }
    
    Point head = snake[0];
    
    // 检查是否撞墙
    if (head.x < 0 || head.x >= gridWidth || head.y < 0 || head.y >= gridHeight)
    {
        return true;
    }
    
    // 检查是否撞到自己（从第二个节点开始检查，因为第一个是头部）
    for (size_t i = 1; i < snake.size(); i++)
    {
        if (snake[i] == head)
        {
            return true;
        }
    }
    
    return false;
}

bool SnakeGame::isPointOnSnake(const Point& p) const
{
    for (const auto& segment : snake)
    {
        if (segment == p)
        {
            return true;
        }
    }
    return false;
}
