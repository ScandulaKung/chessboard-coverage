#include "hanoi_game.h"

HanoiGame::HanoiGame(int disks) : numDisks(disks), currentMoveIndex(0), isAnimating(false), selectedRod(-1)
{
    rods.resize(3);
    // 初始化：所有圆盘都在第一个柱子上
    for (int i = numDisks; i >= 1; i--) 
    {
        rods[0].push_back(i);
    }
}

void HanoiGame::reset(int disks)
{
    numDisks = disks;
    rods.clear();
    rods.resize(3);
    solutionMoves.clear();
    currentMoveIndex = 0;
    isAnimating = false;
    selectedRod = -1;
    // 初始化：所有圆盘都在第一个柱子上
    for (int i = numDisks; i >= 1; i--) 
    {
        rods[0].push_back(i);
    }
}

bool HanoiGame::canMove(int from, int to) const
{
    if (from < 0 || from >= 3 || to < 0 || to >= 3 || from == to) 
    {
        return false;
    }
    if (rods[from].empty()) 
    {
        return false;
    }
    if (rods[to].empty()) 
    {
        return true;
    }
    return rods[from].back() < rods[to].back();
}

bool HanoiGame::move(int from, int to)
{
    if (!canMove(from, to)) 
    {
        return false;
    }
    rods[to].push_back(rods[from].back());
    rods[from].pop_back();
    return true;
}

bool HanoiGame::isComplete() const
{
    return rods[0].empty() && rods[1].empty() && rods[2].size() == numDisks;
}

void HanoiGame::generateSolution()
{
    solutionMoves.clear();
    currentMoveIndex = 0;
    solveRecursive(numDisks, 0, 2, 1);
}

std::pair<int, int> HanoiGame::getNextMove()
{
    if (currentMoveIndex < static_cast<int>(solutionMoves.size())) 
    {
        return solutionMoves[currentMoveIndex];
    }
    return std::make_pair(-1, -1);
}

bool HanoiGame::executeNextMove()
{
    if (currentMoveIndex < static_cast<int>(solutionMoves.size())) 
    {
        auto move = solutionMoves[currentMoveIndex];
        bool success = this->move(move.first, move.second);
        currentMoveIndex++;
        return success;
    }
    return false;
}

void HanoiGame::solveRecursive(int n, int from, int to, int aux)
{
    if (n == 1) 
    {
        solutionMoves.push_back(std::make_pair(from, to));
        return;
    }
    solveRecursive(n - 1, from, aux, to);
    solutionMoves.push_back(std::make_pair(from, to));
    solveRecursive(n - 1, aux, to, from);
}
