#include "nonogram_game.h"
#include <sstream>
#include <functional>

NonogramGame::NonogramGame(int r, int c) : rows(r), cols(c) 
{
    rowClues.resize(rows);
    colClues.resize(cols);
    grid.resize(rows);
    solution.resize(rows);
    for (int i = 0; i < rows; i++) 
    {
        grid[i].resize(cols, 0);
        solution[i].resize(cols, 0);
    }
}

void NonogramGame::setSize(int r, int c)
{
    rows = r;
    cols = c;
    rowClues.resize(rows);
    colClues.resize(cols);
    grid.resize(rows);
    solution.resize(rows);
    for (int i = 0; i < rows; i++) 
    {
        grid[i].resize(cols, 0);
        solution[i].resize(cols, 0);
    }
}

std::vector<int> NonogramGame::parseClue(const std::string& clueStr) 
{
    std::vector<int> clue;
    std::stringstream ss(clueStr);
    std::string item;
    while (std::getline(ss, item, ',')) 
    {
        if (!item.empty()) 
        {
            clue.push_back(std::stoi(item));
        }
    }
    return clue;
}

void NonogramGame::generateClues() 
{
    // 生成行线索
    for (int r = 0; r < rows; r++)
     {
        rowClues[r].clear();
        int count = 0;
        for (int c = 0; c < cols; c++) 
        {
            if (grid[r][c] == 1) 
            {
                count++;
            } else 
            {
                if (count > 0) 
                {
                    rowClues[r].push_back(count);
                    count = 0;
                }
            }
        }
        if (count > 0) 
        {
            rowClues[r].push_back(count);
        }
    }
    
    // 生成列线索
    for (int c = 0; c < cols; c++) 
    {
        colClues[c].clear();
        int count = 0;
        for (int r = 0; r < rows; r++) 
        {
            if (grid[r][c] == 1) 
            {
                count++;
            } else 
            {
                if (count > 0) 
                {
                    colClues[c].push_back(count);
                    count = 0;
                }
            }
        }
        if (count > 0) 
        {
            colClues[c].push_back(count);
        }
    }
}

bool NonogramGame::checkRowClue(int row) const
{
    std::vector<int> clue;
    int count = 0;
    for (int c = 0; c < cols; c++) 
    {
        if (grid[row][c] == 1) 
        {
            count++;
        } else 
        {
            if (count > 0) 
            {
                clue.push_back(count);
                count = 0;
            }
        }
    }
    if (count > 0) 
    {
        clue.push_back(count);
    }
    return clue == rowClues[row];
}

bool NonogramGame::checkColClue(int col) const
{
    std::vector<int> clue;
    int count = 0;
    for (int r = 0; r < rows; r++) 
    {
        if (grid[r][col] == 1) 
        {
            count++;
        } else 
        {
            if (count > 0) 
            {
                clue.push_back(count);
                count = 0;
            }
        }
    }
    if (count > 0) 
    {
        clue.push_back(count);
    }
    return clue == colClues[col];
}

std::vector<std::vector<int> > NonogramGame::generateRowSolutions(const std::vector<int>& clue, int rowLength) const
{
    std::vector<std::vector<int> > solutions;
    
    if (clue.empty()) 
    {
        // 如果没有约束，只有全空一种方案
        solutions.push_back(std::vector<int>(rowLength, 0));
        return solutions;
    }
    
    // 计算需要的最小空间
    int minSpace = 0;
    for (int c : clue) 
    {
        minSpace += c;
    }
    minSpace += static_cast<int>(clue.size()) - 1;  // 段之间的间隔
    
    if (minSpace > rowLength) 
    {
        return solutions;  // 无解
    }
    
    // 递归生成所有可能的方案
    std::function<void(int, int, std::vector<int>&)> generate;
    generate = [&](int clueIndex, int startPos, std::vector<int>& current) 
    {
        if (clueIndex >= static_cast<int>(clue.size())) 
        {
            // 所有约束都已放置，剩余位置填0（未涂色）
            while (static_cast<int>(current.size()) < rowLength) 
            {
                current.push_back(0);
            }
            solutions.push_back(current);
            return;
        }
        
        int blockSize = clue[clueIndex];
        int remainingClues = static_cast<int>(clue.size()) - clueIndex;
        int remainingSpace = rowLength - startPos;
        int remainingMinSpace = 0;
        for (int i = clueIndex + 1; i < static_cast<int>(clue.size()); i++) 
        {
            remainingMinSpace += clue[i] + 1;
        }
        
        // 尝试在当前约束的所有可能位置放置
        int maxStart = rowLength - remainingMinSpace - blockSize;
        for (int pos = startPos; pos <= maxStart; pos++) 
        {
            std::vector<int> newCurrent = current;
            // 填充间隔（未涂色）
            while (static_cast<int>(newCurrent.size()) < pos) 
            {
                newCurrent.push_back(0);
            }
            // 填充当前块（涂色）
            for (int i = 0; i < blockSize; i++) 
            {
                newCurrent.push_back(1);
            }
            // 如果不是最后一个块，需要至少一个间隔
            if (clueIndex < static_cast<int>(clue.size()) - 1) 
            {
                newCurrent.push_back(0);
            }
            
            generate(clueIndex + 1, pos + blockSize + 1, newCurrent);
        }
    };
    
    std::vector<int> current;
    generate(0, 0, current);
    return solutions;
}

bool NonogramGame::solve() 
{
    // 使用递归生成所有可能的行组合，然后用列约束验证
    solution = grid;  // 初始化为当前状态
    
    // 为每一行生成所有可能的涂色方案
    std::vector<std::vector<std::vector<int> > > rowSolutions(rows);
    for (int r = 0; r < rows; r++) 
    {
        rowSolutions[r] = generateRowSolutions(rowClues[r], cols);
        if (rowSolutions[r].empty()) 
        {
            return false;  // 某行无解
        }
    }
    
    // 递归尝试所有行的组合
    std::vector<int> rowIndices(rows, 0);
    return solveRecursiveWithRows(rowSolutions, rowIndices, 0);
}

bool NonogramGame::solveRecursiveWithRows(const std::vector<std::vector<std::vector<int> > >& rowSolutions,
                            std::vector<int>& rowIndices, int currentRow)
{
    if (currentRow >= rows) 
    {
        // 所有行都已选择，检查列约束
        return isValidSolution();
    }
    
    // 尝试当前行的所有可能方案
    for (size_t i = 0; i < rowSolutions[currentRow].size(); i++) 
    {
        rowIndices[currentRow] = static_cast<int>(i);
        // 设置当前行的解
        solution[currentRow] = rowSolutions[currentRow][i];
        
        // 早期剪枝：检查已设置的行是否满足部分列约束
        if (!checkPartialColumns(currentRow + 1)) 
        {
            continue;  // 不满足部分列约束，跳过
        }
        
        if (solveRecursiveWithRows(rowSolutions, rowIndices, currentRow + 1)) 
        {
            return true;
        }
    }
    
    return false;
}

bool NonogramGame::checkPartialColumns(int rowsSet) 
{
    // 检查已设置的前rowsSet行是否满足列约束的前部分
    for (int c = 0; c < cols; c++) 
    {
        // 计算当前列在前rowsSet行中的连续块
        std::vector<int> partialClue;
        int count = 0;
        bool inBlock = false;
        
        for (int r = 0; r < rowsSet; r++) 
        {
            if (solution[r][c] == 1) 
            {
                count++;
                inBlock = true;
            } else 
            {
                if (inBlock) 
                {
                    partialClue.push_back(count);
                    count = 0;
                    inBlock = false;
                }
            }
        }
        
        // 检查已完成的块数不能超过要求的块数
        if (partialClue.size() > colClues[c].size()) 
        {
            return false;  // 块数已经超过要求
        }
        
        // 检查已完成的块的大小必须完全匹配
        for (size_t i = 0; i < partialClue.size(); i++) 
        {
            if (partialClue[i] != colClues[c][i]) 
            {
                return false;  // 已完成的块必须完全匹配
            }
        }
        
        // 如果当前列还在一个块中（最后一行是1），检查这个进行中的块是否超过要求
        if (inBlock && !partialClue.empty()) 
        {
            // 当前块应该对应最后一个已完成的块之后的下一个块
            size_t currentBlockIndex = partialClue.size();
            if (currentBlockIndex >= colClues[c].size()) 
            {
                return false;  // 没有更多块了，但还在块中
            }
            if (count > colClues[c][currentBlockIndex]) 
            {
                return false;  // 进行中的块已经超过要求
            }
        } else if (inBlock && partialClue.empty()) 
        {
            // 第一个块正在进行中
            if (colClues[c].empty() || count > colClues[c][0]) 
            {
                return false;
            }
        }
    }
    
    return true;
}

bool NonogramGame::isValidSolution() 
{
    // 检查列线索
    for (int c = 0; c < cols; c++) 
    {
        std::vector<int> clue;
        int count = 0;
        for (int r = 0; r < rows; r++) 
        {
            if (solution[r][c] == 1) 
            {
                count++;
            } else 
            {
                if (count > 0) 
                {
                    clue.push_back(count);
                    count = 0;
                }
            }
        }
        if (count > 0) 
        {
            clue.push_back(count);
        }
        if (clue != colClues[c]) 
        {
            return false;
        }
    }
    
    return true;
}
