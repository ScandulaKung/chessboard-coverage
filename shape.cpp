#include "shape.h"

Shape::Shape(const std::string& name, const std::vector<std::vector<bool>>& pattern, 
             char symbol, const Color& color, int id)
    : name(name), pattern(pattern), symbol(symbol), color(color), id(id) {
    width = pattern.empty() ? 0 : pattern[0].size();
    height = pattern.size();
}

void Shape::rotate() {
    if (pattern.empty()) return;
    
    int rows = pattern.size();
    int cols = pattern[0].size();
    std::vector<std::vector<bool>> rotated(cols, std::vector<bool>(rows, false));
    
    for (int i = 0; i < rows; i++) {
        for (int j = 0; j < cols; j++) {
            rotated[j][rows - 1 - i] = pattern[i][j];
        }
    }
    
    pattern = rotated;
    width = pattern[0].size();
    height = pattern.size();
}

std::vector<std::vector<bool>> Shape::getRotatedPattern(int rotations) const {
    std::vector<std::vector<bool>> result = pattern;
    
    for (int r = 0; r < rotations % 4; r++) {
        if (result.empty()) break;
        
        int rows = result.size();
        int cols = result[0].size();
        std::vector<std::vector<bool>> temp(cols, std::vector<bool>(rows, false));
        
        for (int i = 0; i < rows; i++) {
            for (int j = 0; j < cols; j++) {
                temp[j][rows - 1 - i] = result[i][j];
            }
        }
        
        result = temp;
    }
    
    return result;
}

void Shape::setPattern(const std::vector<std::vector<bool>>& newPattern) {
    pattern = newPattern;
    width = pattern.empty() ? 0 : pattern[0].size();
    height = pattern.size();
}