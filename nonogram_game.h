#ifndef NONOGRAM_GAME_H
#define NONOGRAM_GAME_H

#include <vector>
#include <string>
#include <functional>

// 棋盘填色游戏类（Nonogram/Picross）
class NonogramGame 
{
public:
    int rows;
    int cols;
    std::vector<std::vector<int> > rowClues;  // 行线索，每行是一个数字列表
    std::vector<std::vector<int> > colClues;  // 列线索，每列是一个数字列表
    std::vector<std::vector<int> > grid;      // 棋盘状态，0=未涂色，1=涂色，-1=X（不涂色）
    std::vector<std::vector<int> > solution;  // 求解结果
    
    NonogramGame(int r = 11, int c = 8);
    
    // 设置棋盘大小
    void setSize(int r, int c);
    
    // 从字符串解析线索（如 "3,4,2"）
    static std::vector<int> parseClue(const std::string& clueStr);
    
    // 生成线索（从当前grid生成）
    void generateClues();
    
    // 检查一行是否满足约束条件
    bool checkRowClue(int row) const;
    
    // 检查一列是否满足约束条件
    bool checkColClue(int col) const;
    
    // 根据行约束条件生成所有可能的涂色方案
    std::vector<std::vector<int> > generateRowSolutions(const std::vector<int>& clue, int rowLength) const;
    
    // 自动求解（改进算法：使用行约束生成所有可能的涂色方案，然后用列约束验证）
    bool solve();
    
private:
    bool solveRecursiveWithRows(const std::vector<std::vector<std::vector<int> > >& rowSolutions,
                                std::vector<int>& rowIndices, int currentRow);
    
    bool checkPartialColumns(int rowsSet);
    
    bool isValidSolution();
};

#endif // NONOGRAM_GAME_H
