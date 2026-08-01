#ifndef HANOI_GAME_H
#define HANOI_GAME_H

#include <vector>
#include <utility>

// 汉诺塔游戏类
class HanoiGame 
{
public:
    int numDisks;  // 圆盘数量
    std::vector<std::vector<int> > rods;  // 三个柱子，每个柱子存储圆盘（从下到上，数字表示圆盘大小）
    std::vector<std::pair<int, int> > solutionMoves;  // 自动求解的移动步骤
    int currentMoveIndex;  // 当前动画步骤索引
    bool isAnimating;  // 是否正在播放动画
    int selectedRod;  // 选中的柱子（-1表示未选中）
    
    HanoiGame(int disks = 3);
    
    // 重置游戏
    void reset(int disks);
    
    // 检查是否可以移动（从from柱移动到to柱）
    bool canMove(int from, int to) const;
    
    // 移动圆盘
    bool move(int from, int to);
    
    // 检查是否完成（所有圆盘都在第三个柱子上）
    bool isComplete() const;
    
    // 生成自动求解步骤（递归算法）
    void generateSolution();
    
    // 获取下一步移动
    std::pair<int, int> getNextMove();
    
    // 执行下一步移动
    bool executeNextMove();
    
private:
    // 递归求解算法
    void solveRecursive(int n, int from, int to, int aux);
};

#endif // HANOI_GAME_H
