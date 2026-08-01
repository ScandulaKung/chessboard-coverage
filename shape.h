#ifndef SHAPE_H
#define SHAPE_H

#include <vector>
#include <string>

// 简单的颜色结构
struct Color {
    int r, g, b;
    Color(int r = 0, int g = 0, int b = 0) : r(r), g(g), b(b) {}
};

// 图块形状类
class Shape {
private:
    std::vector<std::vector<bool>> pattern;
    std::string name;
    char symbol;
    Color color;
    int width;
    int height;
    int id; // 唯一标识符

public:
    Shape(const std::string& name, const std::vector<std::vector<bool>>& pattern, 
          char symbol, const Color& color, int id);
    
    // 顺时针旋转90度
    void rotate();
    
    // 获取旋转后的pattern（不修改原pattern）
    std::vector<std::vector<bool>> getRotatedPattern(int rotations) const;
    
    // Getters
    const std::vector<std::vector<bool>>& getPattern() const { return pattern; }
    const std::string& getName() const { return name; }
    char getSymbol() const { return symbol; }
    const Color& getColor() const { return color; }
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getId() const { return id; }
    
    // 计算实际格子数量（面积）
    int getArea() const {
        int count = 0;
        for (const auto& row : pattern) {
            for (bool cell : row) {
                if (cell) count++;
            }
        }
        return count;
    }
    
    // 设置pattern（用于旋转）
    void setPattern(const std::vector<std::vector<bool>>& newPattern);
};

#endif