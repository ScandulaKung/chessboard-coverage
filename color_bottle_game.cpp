#include "color_bottle_game.h"
#include <memory>
#include <cmath>
#include <map>
#include <QApplication>
#include <QtGui/QPaintEvent>
#include <QtGui/QKeyEvent>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QRegularExpression>
#include <QtCore/QEventLoop>
#include <QtWidgets/QApplication>

// =============================================================================
// ColorBottleGame 实现
// -----------------------------------------------------------------------------
// 本文件包含多游戏合集主窗口逻辑，当前模式由 currentMode 切换：
//   MODE_BOTTLE_COLOR  猜瓶子（本文件前半大量逻辑）
//   MODE_NONOGRAM      棋盘填色
//   MODE_HANOI         汉诺塔
//   MODE_SNAKE         贪吃蛇
//   MODE_PICNIC        野餐日拼图
//
// 猜瓶子要点：
//   - 共 15 关，三段难度（见 initializeLevel）
//   - 设计分辨率 1400×900，窗口可缩放（uiScale / mapToDesign）
//   - 色板两列时锁定列数与第一列行数，匹配后优先缩短第二列
//   - 尝试历史行间距收紧，超出高度后向右分列绘制
// =============================================================================

// ---------- 猜瓶子：关卡初始化 ----------
void ColorBottleGame::initializeLevel() 
{
    // 15 关三段难度：瓶数按段内 2→6；尝试/色数按段规则
    // 1–5：瓶=等级+1，尝试=瓶+1，色=瓶
    // 6–10：瓶=等级-4，尝试=瓶，色=瓶
    // 11–15：瓶=等级-9，尝试=瓶，色=瓶+1
    int paletteColorCount = 0;
    if (currentLevel <= 5)
    {
        bottleCount = currentLevel + 1;
        maxAttempts = bottleCount + 1;
        paletteColorCount = bottleCount;
    }
    else if (currentLevel <= 10)
    {
        bottleCount = currentLevel - 4;
        maxAttempts = bottleCount;
        paletteColorCount = bottleCount;
    }
    else
    {
        bottleCount = currentLevel - 9;
        maxAttempts = bottleCount;
        paletteColorCount = bottleCount + 1;
    }
    // 边界保护：瓶数至少 2；色板数不少于瓶数
    if (bottleCount < 2) bottleCount = 2;
    if (paletteColorCount < bottleCount) paletteColorCount = bottleCount;
    
    remainingAttempts = maxAttempts;
    gameWon = false;
    gameOver = false;
    showingResult = false;
    
    // 清空本关运行时状态
    hiddenBottles.clear();
    userBottles.clear();
    matchedBottles.clear();
    wrongColors.clear();
    matchedIndices.clear();
    attemptHistory.clear();       // 历史尝试行（用于回顾未匹配位置）
    attemptWrongColors.clear();   // 与历史行对应的错误涂色（飞溅）
    usedColorsInAttempt.clear();
    currentAttemptRow = 0;
    
    // 每个瓶子位一份错误涂色缓存（透明 = 无飞溅）
    wrongColors.resize(bottleCount, QColor(Qt::transparent));
    
    // 从调色板随机取色生成隐藏答案；不足时用两色混合作渐变色，且两两 RGB 差 ≥ 30
    hiddenBottles.clear();
    std::vector<int> colorIndices;
    for (size_t i = 0; i < allColors.size(); i++) 
    {
        colorIndices.push_back(static_cast<int>(i));
    }
    std::shuffle(colorIndices.begin(), colorIndices.end(), rng);
    
    // 生成隐藏瓶子颜色（确保两两不相同）
    std::vector<QColor> tempHiddenColors;
    for (int i = 0; i < bottleCount; i++) 
    {
        QColor newColor;
        bool colorValid = false;
        int attempts = 0;
        
        while (!colorValid && attempts < 100) 
        {
            if (i < static_cast<int>(colorIndices.size())) 
            {
                // 使用基础颜色
                newColor = allColors[colorIndices[i]];
            } else 
            {
                // 颜色不够，使用渐变色（混合两种颜色）
                int baseIdx1 = colorIndices[(i * 2) % colorIndices.size()];
                int baseIdx2 = colorIndices[(i * 2 + 1) % colorIndices.size()];
                QColor c1 = allColors[baseIdx1];
                QColor c2 = allColors[baseIdx2];
                // 创建渐变色（混合两种颜色）
                newColor = QColor(
                    (c1.red() + c2.red()) / 2,
                    (c1.green() + c2.green()) / 2,
                    (c1.blue() + c2.blue()) / 2
                );
            }
            
            // 检查是否与已有颜色重复（RGB差值大于30）
            colorValid = true;
            for (const auto& existing : tempHiddenColors) 
            {
                int diff = abs(newColor.red() - existing.red()) + 
                           abs(newColor.green() - existing.green()) + 
                           abs(newColor.blue() - existing.blue());
                if (diff < 30) 
                {
                    colorValid = false;
                    break;
                }
            }
            
            if (!colorValid && i < static_cast<int>(colorIndices.size())) 
            {
                // 如果基础颜色重复，尝试下一个
                colorIndices[i] = (colorIndices[i] + 1) % static_cast<int>(allColors.size());
            }
            attempts++;
        }
        
        tempHiddenColors.push_back(newColor);
        hiddenBottles.push_back(newColor);
        userBottles.push_back(QColor(Qt::transparent));
        matchedBottles.push_back(false);
    }
    
    // 生成颜色选择面板（含干扰色时数量可为瓶数+1）
    availableColors = hiddenBottles;
    
    // 移除重复颜色（如果有）
    std::vector<QColor> uniqueColors;
    for (const auto& color : availableColors) 
    {
        bool isDuplicate = false;
        for (const auto& uniqueColor : uniqueColors) 
        {
            // 检查颜色是否相似（RGB差值小于阈值）
            int diff = abs(color.red() - uniqueColor.red()) + 
                       abs(color.green() - uniqueColor.green()) + 
                       abs(color.blue() - uniqueColor.blue());
            if (diff < 30) 
            {  // 颜色差别阈值
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) 
        {
            uniqueColors.push_back(color);
        }
    }
    
    // 补足到本关色板目标数量
    while (static_cast<int>(uniqueColors.size()) < paletteColorCount) 
    {
        int idx1 = rng() % static_cast<int>(allColors.size());
        int idx2 = rng() % static_cast<int>(allColors.size());
        while (idx2 == idx1) idx2 = rng() % static_cast<int>(allColors.size());
        
        QColor c1 = allColors[idx1];
        QColor c2 = allColors[idx2];
        QColor gradient(
            (c1.red() + c2.red()) / 2,
            (c1.green() + c2.green()) / 2,
            (c1.blue() + c2.blue()) / 2
        );
        
        // 检查新颜色是否与已有颜色差别明显
        bool isDistinct = true;
        for (const auto& existing : uniqueColors) 
        {
            int diff = abs(gradient.red() - existing.red()) + 
                       abs(gradient.green() - existing.green()) + 
                       abs(gradient.blue() - existing.blue());
            if (diff < 30) 
            {
                isDistinct = false;
                break;
            }
        }
        if (isDistinct) 
        {
            uniqueColors.push_back(gradient);
        }
    }
    
    availableColors = uniqueColors;
    if (static_cast<int>(availableColors.size()) > paletteColorCount)
        availableColors.resize(static_cast<size_t>(paletteColorCount));
    // 打乱色板顺序，避免与隐藏瓶答案顺序完全一致（否则无挑战）
    do {
        std::shuffle(availableColors.begin(), availableColors.end(), rng);
    } while (availableColors == hiddenBottles && bottleCount > 1);

    // 记录开局色板规模，供 refreshAvailableColors 计算「应剩几支刷子」
    colorPaletteStartCount = static_cast<int>(availableColors.size());

    // 按开局颜色数量决定是否两列，并锁定：
    //   colorPaletteColumns     —— 本关列数（减色后不改回一列）
    //   colorPaletteFirstColRows —— 第一列行数（减色优先缩短第二列）
    {
        const float rollerSize = 100.0f;
        const float spacing = 15.0f;
        const float pitch = rollerSize + spacing;
        const int n = colorPaletteStartCount;
        const float availableH = static_cast<float>(kDesignHeight) - colorPaletteY - 50.0f;
        const float needH = static_cast<float>(n) * pitch - spacing;
        colorPaletteColumns = (n > 1 && needH > availableH) ? 2 : 1;
        colorPaletteFirstColRows = (colorPaletteColumns >= 2)
            ? (n + 1) / 2
            : n;
    }
    
    usedColorsInAttempt.clear();
}

// ---------- 猜瓶子：比对本轮填色 ----------
void ColorBottleGame::checkMatch() 
{
    showingResult = true;  // 显示「匹配/未匹配」提示，等待点击继续
    bool allMatched = true;
    
    for (int i = 0; i < bottleCount; i++) 
    {
        if (userBottles[i] == hiddenBottles[i]) 
        {
            // 位置与颜色都正确：锁定该瓶，后续尝试不再重涂
            matchedBottles[i] = true;
            matchedIndices.push_back(i);
            wrongColors[i] = QColor(Qt::transparent);
        } else 
        {
            matchedBottles[i] = false;
            // 错误色保留为飞溅，并清空该位以便下一轮重选
            if (userBottles[i] != QColor(Qt::transparent)) 
            {
                wrongColors[i] = userBottles[i];
            }
            userBottles[i] = QColor(Qt::transparent);
            allMatched = false;
        }
    }
    
    if (allMatched) 
    {
        gameWon = true;  // 本关通关，点击后进入下一关（15 后回第 1 关）
    } else 
    {
        remainingAttempts--;
        if (remainingAttempts <= 0) 
        {
            gameOver = true;
        }
    }
}

bool ColorBottleGame::isAllBottlesColored() 
{
    // 所有「尚未匹配」的瓶子都已选色 → 可提交本轮比对
    for (int i = 0; i < bottleCount; i++) 
    {
        if (!matchedBottles[i] && userBottles[i] == QColor(Qt::transparent)) 
        {
            return false;
        }
    }
    return true;
}

// ---------- 猜瓶子：匹配成功后刷新色板（不重排剩余刷子顺序） ----------
void ColorBottleGame::refreshAvailableColors() 
{
    // 目标刷子数 = 开局色板数 - 已匹配瓶数（第 11–15 关含干扰色时同样适用）
    int matchedCount = 0;
    for (bool matched : matchedBottles)
    {
        if (matched) matchedCount++;
    }
    const int neededColorCount = colorPaletteStartCount - matchedCount;
    if (neededColorCount <= 0)
    {
        availableColors.clear();
        usedColorsInAttempt.clear();
        return;
    }

    // RGB 曼哈顿距离 < 30 视为同一色（与生成逻辑一致）
    auto similar = [](const QColor& a, const QColor& b) {
        return abs(a.red() - b.red()) + abs(a.green() - b.green()) + abs(a.blue() - b.blue()) < 30;
    };

    // 每个已匹配答案色从色板中移除一次（避免重复色误删多次）
    std::vector<QColor> matchedTargets;
    for (int i = 0; i < bottleCount; ++i)
    {
        if (matchedBottles[i])
            matchedTargets.push_back(hiddenBottles[i]);
    }

    std::vector<QColor> next;
    next.reserve(availableColors.size());
    for (const QColor& c : availableColors)
    {
        bool remove = false;
        for (size_t t = 0; t < matchedTargets.size(); ++t)
        {
            if (similar(c, matchedTargets[t]))
            {
                matchedTargets.erase(matchedTargets.begin() + static_cast<std::ptrdiff_t>(t));
                remove = true;
                break;
            }
        }
        if (!remove)
            next.push_back(c);  // 保留相对顺序 → 两列布局下视觉上先缩短第二列
    }

    // 数量不足时再补色（追加到末尾，不打乱已有顺序）
    if (static_cast<int>(next.size()) < neededColorCount)
    {
        std::vector<QColor> unmatchedHidden;
        for (int i = 0; i < bottleCount; ++i)
        {
            if (matchedBottles[i])
                continue;
            bool already = false;
            for (const auto& existing : next)
            {
                if (similar(existing, hiddenBottles[i]))
                {
                    already = true;
                    break;
                }
            }
            if (!already)
                unmatchedHidden.push_back(hiddenBottles[i]);
        }
        for (const QColor& c : unmatchedHidden)
        {
            if (static_cast<int>(next.size()) >= neededColorCount)
                break;
            next.push_back(c);
        }
        for (const QColor& c : allColors)
        {
            if (static_cast<int>(next.size()) >= neededColorCount)
                break;
            bool already = false;
            for (const auto& existing : next)
            {
                if (similar(existing, c))
                {
                    already = true;
                    break;
                }
            }
            if (!already)
                next.push_back(c);
        }
    }

    if (static_cast<int>(next.size()) > neededColorCount)
        next.resize(static_cast<size_t>(neededColorCount));

    availableColors = std::move(next);
    usedColorsInAttempt.clear();
}

// ---------- 猜瓶子：绘制单个瓶子（优先 bottle.png，失败则矢量备用） ----------
void ColorBottleGame::drawBottle(QPainter& painter, float x, float y, QColor color, bool isMatched, bool isHidden, bool showRealColor) 
{
    // 布局槽：宽 bottleSize，高 1.5 倍（贴图按 KeepAspectRatio 居中放入）
    float width = bottleSize;
    float height = bottleSize * 1.5f;
    float centerX = x + width * 0.5f;

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    if (!bottlePixmap.isNull())
    {
        QPixmap scaled = bottlePixmap.scaled(
            static_cast<int>(width),
            static_cast<int>(height),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation);
        const int ox = static_cast<int>(x + (width - scaled.width()) * 0.5f);
        const int oy = static_cast<int>(y + (height - scaled.height()) * 0.5f);
        painter.drawPixmap(ox, oy, scaled);

        // 瓶内叠色区：胶囊/键槽路径，参数相对贴图比例调校；垂直渐变 alpha 50→250
        const bool showColor = (!isHidden || showRealColor)
            && color != QColor(Qt::transparent)
            && color.alpha() > 0;
        if (showColor)
        {
            const qreal slotW = scaled.width() * 0.675;
            const qreal slotH = scaled.height() * 0.75;
            const qreal sx = ox + scaled.width() * 0.1625;
            const qreal sy = oy + scaled.height() * 0.36;
            const qreal r = slotW * 0.5;
            const QPointF pivot(sx + slotW * 0.5, sy + slotH * 0.5);

            QPainterPath keySlot;
            keySlot.moveTo(sx, sy + r);
            keySlot.lineTo(sx, sy + slotH - r);
            keySlot.arcTo(QRectF(sx, sy + slotH - 2 * r, slotW, 1.125 * r), 180, 180);
            keySlot.lineTo(sx + slotW, sy + r);
            keySlot.arcTo(QRectF(sx, sy, slotW, 1.125* r), 0, 180);
            keySlot.closeSubpath();

            QTransform rot;
            rot.translate(pivot.x(), pivot.y());
            rot.rotate(-0);
            rot.translate(-pivot.x(), -pivot.y());
            keySlot = rot.map(keySlot);

            // 覆盖素材中的深色占位：自上而下透明度 50 → 250
            const QRectF slotBounds = keySlot.boundingRect();
            QLinearGradient grad(slotBounds.topLeft(), slotBounds.bottomLeft());
            QColor topC = color;
            topC.setAlpha(50);
            QColor botC = color;
            botC.setAlpha(250);
            grad.setColorAt(0.0, topC);
            grad.setColorAt(1.0, botC);

            painter.setPen(Qt::NoPen);
            painter.setBrush(grad);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
            painter.drawPath(keySlot);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }

        if (isHidden && !showRealColor)
        {
            painter.setPen(QPen(QColor(60, 60, 60), 2));
            painter.setFont(QFont("Arial", qMax(18, static_cast<int>(scaled.height() * 0.28)), QFont::Bold));
            painter.drawText(QRectF(ox, oy + scaled.height() * 0.35,
                                    scaled.width(), scaled.height() * 0.35),
                             Qt::AlignCenter, "?");
        }

        if (isMatched)
        {
            QRectF glow(ox - 3, oy - 3, scaled.width() + 6, scaled.height() + 6);
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(0, 255, 0, 200), 3));
            painter.drawRoundedRect(glow, 8, 8);
            painter.setPen(QPen(QColor(0, 255, 0, 80), 2));
            painter.drawRoundedRect(glow.adjusted(-2, -2, 2, 2), 10, 10);
        }

        painter.restore();
        return;
    }

    // 贴图失败时的备用矢量绘制
    float jarRadius = width * 0.4f;
    float jarHeight = height * 0.7f;
    float lidHeight = height * 0.15f;
    float neckY = y + lidHeight;

    QRectF shadowEllipse(centerX - jarRadius * 0.8f, y + height - 5,
                        jarRadius * 1.6f, jarRadius * 0.3f);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 40));
    painter.drawEllipse(shadowEllipse);

    float lidY = y;
    QRectF lidRect(centerX - jarRadius * 0.9f, lidY, jarRadius * 1.8f, lidHeight);
    QLinearGradient lidGradient(lidRect.topLeft(), lidRect.bottomLeft());
    lidGradient.setColorAt(0, QColor(255, 215, 0));
    lidGradient.setColorAt(0.3, QColor(255, 200, 0));
    lidGradient.setColorAt(0.7, QColor(218, 165, 32));
    lidGradient.setColorAt(1, QColor(184, 134, 11));
    painter.setBrush(lidGradient);
    painter.setPen(QPen(QColor(184, 134, 11), 1.5));
    painter.drawRoundedRect(lidRect, 3, 3);

    float twineY = neckY;
    float twineWidth = jarRadius * 1.6f;
    float twineHeight = 4.0f;
    int stripeCount = 8;
    float stripeWidth = twineWidth / stripeCount;
    for (int i = 0; i < stripeCount; i++)
    {
        QRectF stripeRect(centerX - twineWidth * 0.5f + i * stripeWidth, twineY, stripeWidth, twineHeight);
        painter.setBrush((i % 2 == 0) ? QColor(220, 20, 60) : QColor(Qt::white));
        painter.setPen(Qt::NoPen);
        painter.drawRect(stripeRect);
    }

    float jarTopY = neckY + twineHeight + 2;
    float jarBottomY = jarTopY + jarHeight;
    QRectF jarBodyRect(centerX - jarRadius, jarTopY, jarRadius * 2, jarHeight);

    if (isHidden && !showRealColor)
    {
        float checkSize = 8.0f;
        painter.setPen(Qt::NoPen);
        for (float checkY = jarTopY; checkY < jarBottomY; checkY += checkSize)
        {
            for (float checkX = centerX - jarRadius; checkX < centerX + jarRadius; checkX += checkSize)
            {
                int xIndex = static_cast<int>((checkX - (centerX - jarRadius)) / checkSize);
                int yIndex = static_cast<int>((checkY - jarTopY) / checkSize);
                painter.setBrush(((xIndex + yIndex) % 2 == 0)
                                    ? QColor(240, 240, 240, 100)
                                    : QColor(220, 220, 220, 100));
                float distFromCenter = abs(checkX - centerX);
                float ellipseRadius = jarRadius * sqrt(1.0f - pow((checkY - jarTopY - jarHeight * 0.5f) / (jarHeight * 0.5f), 2));
                if (distFromCenter < ellipseRadius)
                    painter.drawRect(QRectF(checkX, checkY, checkSize, checkSize));
            }
        }
    }

    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(150, 150, 150, 180), 2));
    painter.drawEllipse(jarBodyRect);

    if ((!isHidden || showRealColor) && color != QColor(Qt::transparent))
    {
        float jamMargin = 3.0f;
        QRectF jamRect(centerX - jarRadius + jamMargin, jarTopY + jamMargin,
                      jarRadius * 2 - jamMargin * 2, jarHeight - jamMargin * 2);
        QLinearGradient jamGradient(jamRect.topLeft(), jamRect.bottomLeft());
        jamGradient.setColorAt(0, color.lighter(110));
        jamGradient.setColorAt(0.5, color);
        jamGradient.setColorAt(1, color.darker(130));
        painter.setPen(Qt::NoPen);
        painter.setBrush(jamGradient);
        painter.drawEllipse(jamRect);
    }

    if (isHidden && !showRealColor)
    {
        painter.setPen(QPen(QColor(100, 100, 100), 2));
        painter.setFont(QFont("Arial", 35, QFont::Bold));
        painter.drawText(QRectF(centerX - jarRadius, jarTopY + jarHeight * 0.3f,
                               jarRadius * 2, jarHeight * 0.4f),
                       Qt::AlignCenter, "?");
    }

    if (isMatched)
    {
        QRectF borderEllipse(centerX - jarRadius - 3, y - 3,
                           jarRadius * 2 + 6, height + 6);
        painter.setPen(QPen(QColor(0, 255, 0, 200), 4));
        painter.setBrush(Qt::NoBrush);
        painter.drawEllipse(borderEllipse);
    }

    painter.restore();
}

void ColorBottleGame::drawWrongColorPuddle(QPainter& painter, float x, float y, QColor color) 
{
    // 绘制错误颜色的有机飞溅效果（在瓶子位置）
    float centerX = x + bottleSize / 2.0f;
    float centerY = y + bottleSize * 0.75f;  // 稍微下移，使飞溅居中
    float baseRadius = bottleSize * 0.8f;  // 增大到接近瓶子尺寸（瓶子高度是1.5倍，所以0.8足够覆盖）
    
    painter.save();
    
    // 使用基于颜色的种子生成随机但一致的形状
    int seed = color.red() + color.green() * 256 + color.blue() * 65536;
    std::mt19937 localRng(seed);
    std::uniform_real_distribution<float> sizeDist(0.15f, 0.4f);
    std::uniform_real_distribution<float> angleDist(0.0f, 6.28318f);
    std::uniform_real_distribution<float> offsetDist(-0.3f, 0.3f);
    
    // 生成多个飞溅点（主飞溅 + 小飞溅）
    int numSplashes = 3 + (seed % 4); // 3-6个飞溅点
    
    for (int s = 0; s < numSplashes; s++) 
    {
        float splashSize = sizeDist(localRng) * baseRadius * 1.2f;  // 增大飞溅尺寸
        float angle = angleDist(localRng);
        float distance = baseRadius * (0.3f + offsetDist(localRng));
        float splashX = centerX + distance * cos(angle);
        float splashY = centerY + distance * sin(angle);
        
        // 创建有机形状的飞溅路径
        QPainterPath splashPath;
        
        // 使用多个控制点创建不规则形状
        int numPoints = 12 + (seed % 8); // 12-19个点
        QPointF firstPoint;
        bool first = true;
        
        for (int i = 0; i < numPoints; i++) 
        {
            float pointAngle = (i * 6.28318f) / numPoints;
            float radiusVariation = 0.7f + (localRng() % 60) / 100.0f; // 0.7-1.3
            float pointX = splashX + splashSize * radiusVariation * cos(pointAngle + offsetDist(localRng));
            float pointY = splashY + splashSize * radiusVariation * sin(pointAngle + offsetDist(localRng));
            
            QPointF point(pointX, pointY);
            
            if (first) 
            {
                splashPath.moveTo(point);
                firstPoint = point;
                first = false;
            } else 
            {
                // 使用二次贝塞尔曲线创建平滑的有机边缘
                float midX = (splashPath.currentPosition().x() + point.x()) / 2.0f;
                float midY = (splashPath.currentPosition().y() + point.y()) / 2.0f;
                float controlOffset = splashSize * 0.2f * (localRng() % 100) / 100.0f;
                float controlAngle = atan2(point.y() - splashPath.currentPosition().y(), 
                                         point.x() - splashPath.currentPosition().x()) + 1.5708f;
                QPointF controlPoint(midX + controlOffset * cos(controlAngle), 
                                   midY + controlOffset * sin(controlAngle));
                splashPath.quadTo(controlPoint, point);
            }
        }
        splashPath.closeSubpath();
        
        // 绘制飞溅（带渐变和透明度）
        QColor splashColor = color;
        if (s > 0) 
        {
            // 小飞溅使用更浅的颜色和更高的透明度
            splashColor = color.lighter(120);
            splashColor.setAlpha(180);
        } else
         {
            // 主飞溅
            splashColor.setAlpha(220);
        }
        
        // 渐变填充（从中心到边缘）
        QRadialGradient gradient(QPointF(splashX, splashY), splashSize * 1.5f);
        QColor centerColor = splashColor;
        centerColor.setAlpha(splashColor.alpha());
        QColor edgeColor = splashColor.darker(110);
        edgeColor.setAlpha(splashColor.alpha() * 0.7f);
        gradient.setColorAt(0, centerColor);
        gradient.setColorAt(0.6, splashColor);
        gradient.setColorAt(1, edgeColor);
        
        painter.setPen(QPen(QColor(0, 0, 0, 80), 1.0f));
        painter.setBrush(gradient);
        painter.drawPath(splashPath);
        
        // 添加小滴落效果（从主飞溅延伸）
        if (s == 0 && numSplashes > 1) 
        {
            for (int d = 0; d < 2; d++) 
            {
                float dropAngle = angleDist(localRng);
                float dropLength = splashSize * (0.8f + offsetDist(localRng));
                float dropX = splashX + dropLength * 0.3f * cos(dropAngle);
                float dropY = splashY + dropLength * 0.5f * sin(dropAngle);
                float dropSize = splashSize * 0.3f;
                
                QPainterPath dropPath;
                dropPath.addEllipse(QRectF(dropX - dropSize * 0.5f, dropY - dropSize * 0.5f, 
                                         dropSize, dropSize));
                
                QColor dropColor = splashColor;
                dropColor.setAlpha(150);
                painter.setPen(QPen(QColor(0, 0, 0, 60), 0.8f));
                painter.setBrush(dropColor);
                painter.drawPath(dropPath);
            }
        }
    }
    
    painter.restore();
}

// ---------- 猜瓶子：右侧 Color Palette（刷子贴图 + 刷毛叠色） ----------
void ColorBottleGame::drawColorPalette(QPainter& painter) 
{
    const float rollerSize = 100.0f;  // brush.png 底图放大 2 倍（原 50）
    const float spacing = 15.0f;
    const int cols = colorPaletteColumnCount();
    const QRectF firstCell = availableColors.empty()
        ? QRectF(colorPaletteX, colorPaletteY, rollerSize, rollerSize)
        : colorPaletteCellRect(0);
    const float paletteX = static_cast<float>(firstCell.x());
    const float paletteY = colorPaletteY;

    // 绘制标题（两列时加宽）
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 18));
    const float titleW = cols >= 2 ? (rollerSize * 2 + spacing + 20) : 200.0f;
    painter.drawText(QRectF(paletteX, paletteY - 30, titleW, 30), "Color Palette:");
    
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    
    // 每个可用颜色绘制一把刷子（过多超出窗口高度时自动两列）
    for (size_t i = 0; i < availableColors.size(); i++) 
    {
        const QRectF brushRect = colorPaletteCellRect(static_cast<int>(i));
        const float y = static_cast<float>(brushRect.y());
        const QColor paintColor = availableColors[i];
        
        if (!brushPixmap.isNull()) 
        {
            // 底图 brush.png：每个颜色槽绘制一把刷子
            QPixmap scaled = brushPixmap.scaled(
                static_cast<int>(brushRect.width()),
                static_cast<int>(brushRect.height()),
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
            const int ox = static_cast<int>(brushRect.x() + (brushRect.width() - scaled.width()) / 2);
            const int oy = static_cast<int>(brushRect.y() + (brushRect.height() - scaled.height()) / 2);
            painter.drawPixmap(ox, oy, scaled);

            // 刷毛叠色区域：键槽形（上下两段圆弧 + 左右两段直线）
            painter.setCompositionMode(QPainter::CompositionMode_Multiply);
            QColor stain = paintColor;
            stain.setAlpha(250);
            {
                const qreal slotW = scaled.width() * 0.2125;
                const qreal slotH = scaled.height() * 1.1;
                const qreal sx = ox+scaled.width()*0.21;
                const qreal sy = oy + scaled.height()  * 0.15;
                const qreal r = slotW * 0.5;  // 两端半径 = 半宽
                const QPointF pivot(sx + slotW * 0.5, sy + slotH * 0.5);

                QPainterPath keySlot;
                // 左直线（自上圆弧底到下圆弧顶）
                keySlot.moveTo(sx, sy + r);
                keySlot.lineTo(sx, sy + slotH - r);
                // 下圆弧（左→右）
                keySlot.arcTo(QRectF(sx, sy + slotH - 2 * r, slotW, 2 * r), 180, -180);
                // 右直线（下→上）
                keySlot.lineTo(sx + slotW, sy + r);
                // 上圆弧（右→左）
                keySlot.arcTo(QRectF(sx, sy, slotW, 2 * r), 0, 180);
                keySlot.closeSubpath();

                // 绕键槽中心逆时针旋转 58°（Qt 屏幕坐标 y 向下，负角为逆时针）
                QTransform rot;
                rot.translate(pivot.x(), pivot.y());
                rot.rotate(-59);
                rot.translate(-pivot.x(), -pivot.y());
                keySlot = rot.map(keySlot);

                painter.setPen(Qt::NoPen);
                painter.setBrush(stain);
                painter.drawPath(keySlot);
            }
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        }
        else 
        {
            // 贴图加载失败时的备用绘制
            float centerX = static_cast<float>(brushRect.x()) + rollerSize * 0.5f;
            float centerY = y + rollerSize * 0.5f;
            
            float rollerRadius = rollerSize * 0.18f;
            float rollerX = centerX - rollerSize * 0.25f;
            float rollerY = centerY;
            
            QRectF rollerEllipse(rollerX - rollerRadius, rollerY - rollerRadius * 0.5f, 
                                rollerRadius * 2, rollerRadius);
            QColor rollerColor(80, 80, 80);
            
            QLinearGradient rollerGradient(rollerEllipse.topLeft(), rollerEllipse.bottomLeft());
            rollerGradient.setColorAt(0, rollerColor.lighter(120));
            rollerGradient.setColorAt(0.5, rollerColor);
            rollerGradient.setColorAt(1, rollerColor.darker(120));
            painter.setBrush(rollerGradient);
            painter.setPen(QPen(rollerColor.darker(150), 1));
            painter.drawEllipse(rollerEllipse);
            
            QColor frameColor(180, 180, 180);
            painter.setPen(QPen(frameColor, 3));
            painter.setBrush(Qt::NoBrush);
            
            float frameStartX = rollerX + rollerRadius * 0.8f;
            float frameStartY = rollerY;
            float frameMidX = centerX;
            float frameMidY = centerY + rollerSize * 0.15f;
            float frameEndX = centerX + rollerSize * 0.2f;
            float frameEndY = centerY - rollerSize * 0.1f;
            
            QPainterPath framePath;
            framePath.moveTo(frameStartX, frameStartY);
            framePath.lineTo(frameMidX, frameMidY);
            framePath.lineTo(frameEndX, frameEndY);
            painter.drawPath(framePath);
            
            float handleX = centerX + rollerSize * 0.25f;
            float handleY = centerY - rollerSize * 0.08f;
            float handleRadius = rollerSize * 0.06f;
            float handleLength = rollerSize * 0.3f;
            
            QRectF handleEllipse(handleX, handleY - handleRadius * 0.5f,
                                handleLength, handleRadius);
            QLinearGradient handleGradient(handleEllipse.topLeft(), handleEllipse.bottomLeft());
            handleGradient.setColorAt(0, rollerColor.lighter(110));
            handleGradient.setColorAt(0.5, rollerColor);
            handleGradient.setColorAt(1, rollerColor.darker(110));
            painter.setBrush(handleGradient);
            painter.setPen(QPen(rollerColor.darker(150), 1));
            painter.drawEllipse(handleEllipse);
            
            QRectF paintEllipse(rollerX - rollerRadius * 0.9f, rollerY - rollerRadius * 0.45f,
                               rollerRadius * 1.8f, rollerRadius * 0.9f);
            
            QLinearGradient paintGradient(paintEllipse.topLeft(), paintEllipse.bottomLeft());
            QColor lightPaint = paintColor.lighter(115);
            QColor darkPaint = paintColor.darker(115);
            paintGradient.setColorAt(0, lightPaint);
            paintGradient.setColorAt(0.5, paintColor);
            paintGradient.setColorAt(1, darkPaint);
            
            painter.setBrush(paintGradient);
            painter.setPen(QPen(paintColor.darker(140), 1.5));
            painter.drawEllipse(paintEllipse);
        }
    }
    
    painter.restore();
}

// ---------- 猜瓶子：Color Palette 列数 / 单元格 / 点击命中 ----------
int ColorBottleGame::colorPaletteColumnCount() const
{
    // 返回本关开局锁定的列数（1 或 2），减色过程中不变
    return colorPaletteColumns < 1 ? 1 : colorPaletteColumns;
}

QRectF ColorBottleGame::colorPaletteCellRect(int index) const
{
    const float rollerSize = 100.0f;  // 与 drawColorPalette 中刷子尺寸一致
    const float spacing = 15.0f;
    const int n = static_cast<int>(availableColors.size());
    const int cols = colorPaletteColumnCount();

    // 两列时整体左移一格，避免右列画出窗口
    float baseX = colorPaletteX;
    if (cols >= 2)
        baseX = colorPaletteX - (rollerSize + spacing);

    int col = 0;
    int row = index;
    if (cols >= 2 && n > 0)
    {
        // 向量前 colorPaletteFirstColRows 个画在第一列，其余在第二列；
        // n 变小时 leftCount=min(n,锁定行数) → 先变短的是第二列
        const int leftCount = qMin(n, qMax(1, colorPaletteFirstColRows));
        if (index < leftCount)
        {
            col = 0;
            row = index;
        }
        else
        {
            col = 1;
            row = index - leftCount;
        }
    }

    return QRectF(baseX + col * (rollerSize + spacing),
                  colorPaletteY + row * (rollerSize + spacing),
                  rollerSize,
                  rollerSize);
}

int ColorBottleGame::getColorIndexAt(float x, float y) 
{
    // x/y 应为设计坐标（调用前已 mapToDesign）
    for (size_t i = 0; i < availableColors.size(); i++) 
    {
        if (colorPaletteCellRect(static_cast<int>(i)).contains(QPointF(x, y)))
            return static_cast<int>(i);
    }
    return -1;
}

int ColorBottleGame::getBottleIndexAt(float x, float y) 
{
    for (int i = 0; i < bottleCount; i++) 
    {
        float bottleX = startX + i * (bottleSize + bottleSpacing);
        float bottleY = startY;
        
        if (x >= bottleX && x <= bottleX + bottleSize &&
            y >= bottleY && y <= bottleY + bottleSize * 1.5f) 
            {
            return i;
        }
    }
    return -1;
}


void ColorBottleGame::drawMenuBar(QPainter& painter) 
{
    float menuY = 0;
    float menuHeight = static_cast<float>(menuBarHeight);
    
    // 绘制菜单栏背景
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(220, 220, 220));
    painter.drawRect(QRectF(0, menuY, kDesignWidth, menuHeight));
    
    // 绘制菜单项
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 14));
    
    float menu1X = 20;
    float menu2X = 200;
    float menuWidth = 150;
    
    // 菜单项1：6×6填图
    QRectF menu1Rect(menu1X, menuY + 5, menuWidth, menuHeight - 10);
    QColor menu1Color = (currentMode == MODE_BOTTLE_COLOR) ? QColor(200, 230, 255) : QColor(240, 240, 240);
    painter.setBrush(menu1Color);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(menu1Rect, 3, 3);
    painter.drawText(menu1Rect, Qt::AlignCenter, "1、猜瓶子");
    
    // 菜单项2：棋盘填色
    QRectF menu2Rect(menu2X, menuY + 5, menuWidth, menuHeight - 10);
    QColor menu2Color = (currentMode == MODE_NONOGRAM) ? QColor(200, 230, 255) : QColor(240, 240, 240);
    painter.setBrush(menu2Color);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(menu2Rect, 3, 3);
    painter.drawText(menu2Rect, Qt::AlignCenter, "2.棋盘填色");
    
    // 菜单项3：汉诺塔
    float menu3X = 380;
    QRectF menu3Rect(menu3X, menuY + 5, menuWidth, menuHeight - 10);
    QColor menu3Color = (currentMode == MODE_HANOI) ? QColor(200, 230, 255) : QColor(240, 240, 240);
    painter.setBrush(menu3Color);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(menu3Rect, 3, 3);
    painter.drawText(menu3Rect, Qt::AlignCenter, "3.汉诺塔");
    
    // 菜单项4：贪吃蛇
    float menu4X = 560;
    QRectF menu4Rect(menu4X, menuY + 5, menuWidth, menuHeight - 10);
    QColor menu4Color = (currentMode == MODE_SNAKE) ? QColor(200, 230, 255) : QColor(240, 240, 240);
    painter.setBrush(menu4Color);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(menu4Rect, 3, 3);
    painter.drawText(menu4Rect, Qt::AlignCenter, "4.贪吃蛇");

    // 菜单项5：野餐日
    float menu5X = 740;
    QRectF menu5Rect(menu5X, menuY + 5, menuWidth, menuHeight - 10);
    QColor menu5Color = (currentMode == MODE_PICNIC) ? QColor(200, 230, 255) : QColor(240, 240, 240);
    painter.setBrush(menu5Color);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(menu5Rect, 3, 3);
    painter.drawText(menu5Rect, Qt::AlignCenter, "5.野餐日");
}

void ColorBottleGame::drawHanoiGame(QPainter& painter) 
{
    float startX = 100;
    float startY = menuBarHeight + 80;
    float rodWidth = 15.0f;
    float rodHeight = 400.0f;
    float baseHeight = 30.0f;
    float baseWidth = 900.0f;
    float rodSpacing = 300.0f;
    float diskHeight = 30.0f;
    float maxDiskWidth = 200.0f;
    float minDiskWidth = 60.0f;
    
    // 绘制标题
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 24));
    painter.drawText(QRectF(startX, startY - 60, 400, 40), "汉诺塔游戏");
    
    // 绘制底座
    float baseY = startY + rodHeight;
    painter.setBrush(QColor(139, 69, 19));  // 棕色
    painter.setPen(QPen(Qt::black, 2));
    QRectF baseRect(startX, baseY, baseWidth, baseHeight);
    painter.drawRect(baseRect);
    
    // 绘制三个柱子
    float rod1X = startX + 150;
    float rod2X = rod1X + rodSpacing;
    float rod3X = rod2X + rodSpacing;
    float rodY = startY;
    
    for (int i = 0; i < 3; i++) 
    {
        float rodX = rod1X + i * rodSpacing;
        painter.setBrush(QColor(160, 82, 45));  // 深棕色
        painter.setPen(QPen(Qt::black, 2));
        QRectF rodRect(rodX - rodWidth / 2, rodY, rodWidth, rodHeight);
        painter.drawRect(rodRect);
        
        // 柱子标签
        painter.setFont(QFont("Arial", 16));
        painter.setPen(Qt::black);
        painter.drawText(QRectF(rodX - 30, baseY + baseHeight + 5, 60, 25), 
                       Qt::AlignCenter, "柱子" + QString::number(i + 1));
    }
    
    // 绘制圆盘
    for (int rodIndex = 0; rodIndex < 3; rodIndex++) 
    {
        float rodX = rod1X + rodIndex * rodSpacing;
        const auto& disks = hanoiGame.rods[rodIndex];
        
        for (size_t diskIndex = 0; diskIndex < disks.size(); diskIndex++) 
        {
            int diskSize = disks[diskIndex];
            float diskWidth = minDiskWidth + (maxDiskWidth - minDiskWidth) * 
                             (static_cast<float>(diskSize - 1) / (hanoiGame.numDisks - 1));
            float diskX = rodX - diskWidth / 2;
            float diskY = baseY - (diskIndex + 1) * diskHeight;
            
            // 圆盘颜色渐变（根据大小）
            QColor diskColor;
            float ratio = static_cast<float>(diskSize - 1) / (hanoiGame.numDisks - 1);
            if (ratio < 0.33f) 
            {
                diskColor = QColor(255, static_cast<int>(100 + ratio * 155 / 0.33f), 0);
            } else if (ratio < 0.66f) 
            {
                float localRatio = (ratio - 0.33f) / 0.33f;
                diskColor = QColor(static_cast<int>(255 - localRatio * 255), 255, 0);
            } else 
            {
                float localRatio = (ratio - 0.66f) / 0.34f;
                diskColor = QColor(0, static_cast<int>(255 - localRatio * 255), static_cast<int>(localRatio * 255));
            }
            
            // 绘制圆盘
            painter.setBrush(diskColor);
            painter.setPen(QPen(Qt::black, 2));
            QRectF diskRect(diskX, diskY, diskWidth, diskHeight);
            painter.drawRoundedRect(diskRect, 5, 5);
            
            // 圆盘高光
            QLinearGradient gradient(diskRect.topLeft(), diskRect.bottomLeft());
            gradient.setColorAt(0, QColor(255, 255, 255, 100));
            gradient.setColorAt(1, QColor(0, 0, 0, 50));
            painter.setBrush(gradient);
            painter.setPen(Qt::NoPen);
            painter.drawRoundedRect(diskRect, 5, 5);
        }
        
        // 高亮选中的柱子
        if (hanoiGame.selectedRod == rodIndex) 
        {
            painter.setBrush(QColor(255, 255, 0, 100));
            painter.setPen(QPen(QColor(255, 200, 0), 3));
            QRectF highlightRect(rodX - rodWidth / 2 - 5, rodY - 5, rodWidth + 10, rodHeight + 10);
            painter.drawRoundedRect(highlightRect, 5, 5);
        }
    }
    
    // 绘制控制按钮
    float buttonY = baseY + baseHeight + 60;
    float buttonWidth = 120;
    float buttonHeight = 35;
    float buttonSpacing = 20;
    
    // 重置按钮
    float resetButtonX = startX;
    QRectF resetRect(resetButtonX, buttonY, buttonWidth, buttonHeight);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(resetRect, 5, 5);
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 14));
    painter.drawText(resetRect, Qt::AlignCenter, "重置");
    
    // 自动求解按钮
    float solveButtonX = resetButtonX + buttonWidth + buttonSpacing;
    QRectF solveRect(solveButtonX, buttonY, buttonWidth, buttonHeight);
    painter.setBrush(hanoiGame.isAnimating ? QColor(150, 150, 150) : QColor(150, 255, 150));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(solveRect, 5, 5);
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 14));
    painter.drawText(solveRect, Qt::AlignCenter, hanoiGame.isAnimating ? "求解中..." : "自动求解");
    
    // 圆盘数量调整
    float diskCountX = solveButtonX + buttonWidth + buttonSpacing + 50;
    painter.setFont(QFont("Arial", 14));
    painter.setPen(Qt::black);
    painter.drawText(QRectF(diskCountX, buttonY, 100, buttonHeight), 
                    Qt::AlignVCenter, "圆盘数:");
    
    float countX = diskCountX + 80;
    QRectF countRect(countX, buttonY, 40, buttonHeight);
    painter.setBrush(Qt::white);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(countRect, 3, 3);
    painter.setPen(Qt::black);
    painter.drawText(countRect, Qt::AlignCenter, QString::number(hanoiDiskCount));
    
    // 减少按钮
    QRectF decRect(countX + 50, buttonY, 30, buttonHeight);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(decRect, 3, 3);
    painter.drawText(decRect, Qt::AlignCenter, "-");
    
    // 增加按钮
    QRectF incRect(countX + 90, buttonY, 30, buttonHeight);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(incRect, 3, 3);
    painter.drawText(incRect, Qt::AlignCenter, "+");
    
    // 显示完成提示
    if (hanoiGame.isComplete()) 
    {
        painter.setFont(QFont("Arial", 20));
        painter.setPen(QColor(0, 150, 0));
        painter.drawText(QRectF(startX, buttonY + 50, 400, 40), "恭喜！已完成！");
    }
}

void ColorBottleGame::drawSnakeGame(QPainter& painter)
{
    float startX = 100;
    float startY = menuBarHeight + 80;
    float cellSize = 20.0f;  // 每个格子的大小
    float gridWidth = snakeGame.gridWidth * cellSize;
    float gridHeight = snakeGame.gridHeight * cellSize;
    
    // 绘制标题
    painter.setPen(Qt::black);
    painter.setFont(QFont("Arial", 24));
    painter.drawText(QRectF(startX, startY - 60, 400, 40), "贪吃蛇游戏");
    
    // 绘制分数
    painter.setFont(QFont("Arial", 18));
    painter.drawText(QRectF(startX, startY - 30, 300, 30), 
                    "分数: " + QString::number(snakeGame.score));
    
    // 绘制游戏区域背景
    painter.setBrush(QColor(245, 245, 245));
    painter.setPen(QPen(Qt::black, 2));
    QRectF gameAreaRect(startX, startY, gridWidth, gridHeight);
    painter.drawRect(gameAreaRect);
    
    // 绘制网格线
    painter.setPen(QPen(QColor(220, 220, 220), 1));
    for (int i = 0; i <= snakeGame.gridWidth; i++)
    {
        float x = startX + i * cellSize;
        painter.drawLine(QPointF(x, startY), QPointF(x, startY + gridHeight));
    }
    for (int i = 0; i <= snakeGame.gridHeight; i++)
    {
        float y = startY + i * cellSize;
        painter.drawLine(QPointF(startX, y), QPointF(startX + gridWidth, y));
    }
    
    // 绘制食物
    if (!snakeGame.gameOver)
    {
        float foodX = startX + snakeGame.food.x * cellSize;
        float foodY = startY + snakeGame.food.y * cellSize;
        painter.setBrush(QColor(255, 0, 0));  // 红色食物
        painter.setPen(QPen(Qt::black, 1));
        QRectF foodRect(foodX + 2, foodY + 2, cellSize - 4, cellSize - 4);
        painter.drawEllipse(foodRect);
    }
    
    // 绘制蛇
    for (size_t i = 0; i < snakeGame.snake.size(); i++)
    {
        float snakeX = startX + snakeGame.snake[i].x * cellSize;
        float snakeY = startY + snakeGame.snake[i].y * cellSize;
        
        if (i == 0)
        {
            // 蛇头 - 绿色，稍大
            painter.setBrush(QColor(0, 200, 0));
            painter.setPen(QPen(Qt::black, 2));
            QRectF headRect(snakeX + 1, snakeY + 1, cellSize - 2, cellSize - 2);
            painter.drawRoundedRect(headRect, 3, 3);
            
            // 绘制眼睛
            painter.setBrush(Qt::white);
            painter.setPen(Qt::NoPen);
            float eyeSize = 3.0f;
            float eyeOffset = 5.0f;
            if (snakeGame.direction == SnakeGame::RIGHT)
            {
                painter.drawEllipse(QRectF(snakeX + cellSize - eyeOffset - eyeSize, snakeY + eyeOffset, eyeSize, eyeSize));
                painter.drawEllipse(QRectF(snakeX + cellSize - eyeOffset - eyeSize, snakeY + cellSize - eyeOffset - eyeSize, eyeSize, eyeSize));
            }
            else if (snakeGame.direction == SnakeGame::LEFT)
            {
                painter.drawEllipse(QRectF(snakeX + eyeOffset, snakeY + eyeOffset, eyeSize, eyeSize));
                painter.drawEllipse(QRectF(snakeX + eyeOffset, snakeY + cellSize - eyeOffset - eyeSize, eyeSize, eyeSize));
            }
            else if (snakeGame.direction == SnakeGame::UP)
            {
                painter.drawEllipse(QRectF(snakeX + eyeOffset, snakeY + eyeOffset, eyeSize, eyeSize));
                painter.drawEllipse(QRectF(snakeX + cellSize - eyeOffset - eyeSize, snakeY + eyeOffset, eyeSize, eyeSize));
            }
            else // DOWN
            {
                painter.drawEllipse(QRectF(snakeX + eyeOffset, snakeY + cellSize - eyeOffset - eyeSize, eyeSize, eyeSize));
                painter.drawEllipse(QRectF(snakeX + cellSize - eyeOffset - eyeSize, snakeY + cellSize - eyeOffset - eyeSize, eyeSize, eyeSize));
            }
        }
        else
        {
            // 蛇身 - 深绿色
            painter.setBrush(QColor(0, 150, 0));
            painter.setPen(QPen(Qt::black, 1));
            QRectF bodyRect(snakeX + 2, snakeY + 2, cellSize - 4, cellSize - 4);
            painter.drawRoundedRect(bodyRect, 2, 2);
        }
    }
    
    // 绘制游戏状态信息
    float infoY = startY + gridHeight + 30;
    painter.setFont(QFont("Arial", 16));
    painter.setPen(Qt::black);
    
    if (snakeGame.gameOver)
    {
        painter.setPen(Qt::red);
        painter.setFont(QFont("Arial", 24));
        painter.drawText(QRectF(startX, infoY, 400, 40), "游戏结束！按空格键重新开始");
    }
    else if (snakeGame.paused)
    {
        painter.setPen(Qt::blue);
        painter.setFont(QFont("Arial", 20));
        painter.drawText(QRectF(startX, infoY, 400, 40), "游戏暂停 - 按空格键继续");
    }
    else
    {
        painter.drawText(QRectF(startX, infoY, 400, 30), "使用方向键控制蛇的移动");
        painter.drawText(QRectF(startX, infoY + 25, 400, 30), "按空格键暂停/继续");
    }
}

void ColorBottleGame::drawNonogramGame(QPainter& painter) 
{
    float startX = 100;
    float startY = menuBarHeight + 60;
    float cellSize = 40.0f;  // 增大单元格尺寸，为编辑按钮提供更多空间
    
    int rows = nonogramGame.rows;
    int cols = nonogramGame.cols;
    
    // 计算线索区域大小
    float clueSpacing = 30.0f;  // 约束条件间距（增大以适应更大的单元格）
    float maxRowClueWidth = 0;
    float maxColClueHeight = 0;
    for (int r = 0; r < rows; r++) 
    {
        float width = static_cast<float>(nonogramGame.rowClues[r].size()) * clueSpacing;
        if (width > maxRowClueWidth) maxRowClueWidth = width;
    }
    for (int c = 0; c < cols; c++)
     {
        float height = static_cast<float>(nonogramGame.colClues[c].size()) * clueSpacing;
        if (height > maxColClueHeight) maxColClueHeight = height;
    }
    
    float gridStartX = startX + maxRowClueWidth + 10;
    float gridStartY = startY + maxColClueHeight + 10;
    
    // 绘制列线索（上方）- 检查是否满足约束条件并变色
    painter.setFont(QFont("Arial", 12));
    for (int c = 0; c < cols; c++)
     {
        float x = gridStartX + c * cellSize;
        float y = startY;
        bool isSatisfied = !nonogramEditorMode && nonogramGame.checkColClue(c);
        for (size_t i = 0; i < nonogramGame.colClues[c].size(); i++) 
        {
            QColor textColor = isSatisfied ? QColor(0, 200, 0) : Qt::black;  // 满足条件为绿色
            painter.setPen(textColor);
            painter.drawText(QRectF(x, y + i * clueSpacing, cellSize, clueSpacing), 
                           Qt::AlignCenter, QString::number(nonogramGame.colClues[c][i]));
        }
        
        // 编辑器模式：在相邻两列之间添加编辑按钮（放在当前列右侧，下一列左侧之间）
        if (nonogramEditorMode && c < cols ) 
        {
            float buttonX = x + cellSize + (0*cellSize - 16.0f) * 0.5f;  // 放在两列之间中间位置
            float buttonSize = 16.0f;
            
            // 为每个约束项绘制+/-按钮
            for (size_t i = 0; i < nonogramGame.colClues[c].size(); i++) 
            {
                float buttonY = y + i * clueSpacing;
                
                // +按钮
                QRectF plusRect(buttonX, buttonY, buttonSize, clueSpacing * 0.4f);
                painter.setBrush(QColor(200, 255, 200));
                painter.setPen(QPen(Qt::black, 1));
                painter.drawRoundedRect(plusRect, 3, 3);
                painter.setPen(Qt::black);
                painter.drawText(plusRect, Qt::AlignCenter, "+");
                
                // -按钮
                QRectF minusRect(buttonX, buttonY + clueSpacing * 0.42f, buttonSize, clueSpacing * 0.4f);
                painter.setBrush(QColor(255, 200, 200));
                painter.setPen(QPen(Qt::black, 1));
                painter.drawRoundedRect(minusRect, 3, 3);
                painter.setPen(Qt::black);
                painter.drawText(minusRect, Qt::AlignCenter, "-");
            }
            
            // 添加新项的按钮
            float addButtonY = y + static_cast<float>(nonogramGame.colClues[c].size()) * clueSpacing;
            QRectF addRect(buttonX, addButtonY, buttonSize, clueSpacing * 0.5f);
            painter.setBrush(QColor(200, 200, 255));
            painter.setPen(QPen(Qt::black, 1));
            painter.drawRoundedRect(addRect, 2, 2);
            painter.setPen(Qt::black);
            painter.drawText(addRect, Qt::AlignCenter, "+N");
        }
    }
    
    // 绘制行线索（左侧）- 检查是否满足约束条件并变色
    painter.setFont(QFont("Arial", 12));
    float gridRightX = gridStartX + cols * cellSize;  // 棋盘右侧位置
    for (int r = 0; r < rows; r++)
     {
        float x = startX;
        float y = gridStartY + r * cellSize;
        float textX = x;
        bool isSatisfied = !nonogramEditorMode && nonogramGame.checkRowClue(r);
        QColor textColor = isSatisfied ? QColor(0, 200, 0) : Qt::black;  // 满足条件为绿色
        painter.setPen(textColor);
        for (int i = static_cast<int>(nonogramGame.rowClues[r].size()) - 1; i >= 0; i--)
         {
            textX -= clueSpacing;
            painter.drawText(QRectF(textX, y, clueSpacing, cellSize), 
                           Qt::AlignCenter, QString::number(nonogramGame.rowClues[r][i]));
        }
        
        // 编辑器模式：在棋盘右侧添加编辑按钮（水平排列，从左到右对应约束索引0到size-1）
        if (nonogramEditorMode) 
        {
            float buttonStartX = gridRightX + 5;  // 放在棋盘右侧
            float buttonY = y;
            float buttonSize = 16.0f;
            float buttonHeight = cellSize * 0.4f;  // 按钮高度
            float buttonSpacing = 3.0f;  // 按钮之间的间距
            
            int clueCount = static_cast<int>(nonogramGame.rowClues[r].size());
            
            // 为每个约束项绘制+/-按钮（水平排列，从左到右对应约束索引0到size-1）
            for (int i = 0; i < clueCount; i++)
             {
                float currentButtonX = buttonStartX + i * (buttonSize + buttonSpacing);
                
                // +按钮（上方）
                QRectF plusRect(currentButtonX, buttonY, buttonSize, buttonHeight);
                painter.setBrush(QColor(200, 255, 200));
                painter.setPen(QPen(Qt::black, 1));
                painter.drawRoundedRect(plusRect, 3, 3);
                painter.setPen(Qt::black);
                painter.drawText(plusRect, Qt::AlignCenter, "+");
                
                // -按钮（下方）
                QRectF minusRect(currentButtonX, buttonY + buttonHeight + 2, buttonSize, buttonHeight);
                painter.setBrush(QColor(255, 200, 200));
                painter.setPen(QPen(Qt::black, 1));
                painter.drawRoundedRect(minusRect, 3, 3);
                painter.setPen(Qt::black);
                painter.drawText(minusRect, Qt::AlignCenter, "-");
            }
            
            // 添加新项的按钮（放在最右侧）
            float addButtonX = buttonStartX + clueCount * (buttonSize + buttonSpacing);
            QRectF addRect(addButtonX, buttonY, buttonSize, cellSize * 0.2f);
            painter.setBrush(QColor(200, 200, 255));
            painter.setPen(QPen(Qt::black, 1));
            painter.drawRoundedRect(addRect, 2, 2);
            painter.setPen(Qt::black);
            painter.drawText(addRect, Qt::AlignCenter, "+N");
        }
    }
    
    // 绘制棋盘网格
    painter.setPen(QPen(Qt::black, 1));
    painter.setBrush(Qt::NoBrush);
    
    // 绘制网格线
    for (int i = 0; i <= rows; i++) 
    {
        float y = gridStartY + i * cellSize;
        painter.drawLine(QPointF(gridStartX, y), QPointF(gridStartX + cols * cellSize, y));
    }
    for (int i = 0; i <= cols; i++) 
    {
        float x = gridStartX + i * cellSize;
        painter.drawLine(QPointF(x, gridStartY), QPointF(x, gridStartY + rows * cellSize));
    }
    
    // 绘制单元格
    std::vector<std::vector<int>>* gridToDraw = nonogramShowSolution ? &nonogramGame.solution : &nonogramGame.grid;
    
    for (int r = 0; r < rows; r++) 
    {
        for (int c = 0; c < cols; c++) 
        {
            float x = gridStartX + c * cellSize + 1;
            float y = gridStartY + r * cellSize + 1;
            QRectF cellRect(x, y, cellSize - 2, cellSize - 2);
            
            int cellValue = (*gridToDraw)[r][c];
            if (cellValue == 1) 
            {
                // 涂色
                painter.setBrush(Qt::black);
                painter.setPen(Qt::NoPen);
                painter.drawRect(cellRect);
            } else if (cellValue == -1) 
            {
                // X标记（不涂色）
                painter.setBrush(Qt::white);
                painter.setPen(QPen(Qt::red, 2));
                painter.drawRect(cellRect);
                // 绘制X
                painter.drawLine(cellRect.topLeft(), cellRect.bottomRight());
                painter.drawLine(cellRect.topRight(), cellRect.bottomLeft());
            } else 
            {
                // 未涂色
                painter.setBrush(Qt::white);
                painter.setPen(Qt::NoPen);
                painter.drawRect(cellRect);
            }
        }
    }
    
    // 绘制编辑器按钮和控制
    float buttonY = gridStartY + rows * cellSize + 30;
    painter.setFont(QFont("Arial", 12));
    
    // 编辑器模式按钮
    QRectF editorButtonRect(gridStartX, buttonY, 100, 30);
    QColor editorButtonColor = nonogramEditorMode ? QColor(150, 200, 150) : QColor(200, 200, 200);
    painter.setBrush(editorButtonColor);
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(editorButtonRect, 3, 3);
    painter.drawText(editorButtonRect, Qt::AlignCenter, nonogramEditorMode ? "退出编辑" : "编辑模式");
    
    // 求解按钮
    QRectF solveButtonRect(gridStartX + 120, buttonY, 100, 30);
    painter.setBrush(QColor(200, 200, 255));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(solveButtonRect, 3, 3);
    painter.drawText(solveButtonRect, Qt::AlignCenter, "自动求解");
    
    // 显示求解结果按钮
    if (!nonogramGame.solution.empty() && nonogramGame.solution[0].size() == cols) 
    {
        QRectF showSolutionRect(gridStartX + 240, buttonY, 100, 30);
        QColor showSolutionColor = nonogramShowSolution ? QColor(255, 200, 200) : QColor(200, 200, 200);
        painter.setBrush(showSolutionColor);
        painter.setPen(QPen(Qt::black, 1));
        painter.drawRoundedRect(showSolutionRect, 3, 3);
        painter.drawText(showSolutionRect, Qt::AlignCenter, "显示答案");
    }
}

void ColorBottleGame::drawNonogramEditor(QPainter& painter)
 {
    // 编辑器界面：显示行数和列数调整按钮
    // 计算棋盘底部位置，确保编辑器界面在棋盘下方
    float gameStartY = menuBarHeight + 60;
    float cellSize = 40.0f;  // 增大单元格尺寸，与drawNonogramGame保持一致
    float clueSpacing = 30.0f;  // 约束条件间距（与drawNonogramGame保持一致）
    float maxColClueHeight = 0;
    for (int c = 0; c < nonogramGame.cols; c++)
    {
        float height = static_cast<float>(nonogramGame.colClues[c].size()) * clueSpacing;
        if (height > maxColClueHeight) maxColClueHeight = height;
    }
    float gridStartY = gameStartY + maxColClueHeight + 10;
    float buttonY = gridStartY + nonogramGame.rows * cellSize + 30;
    
    float startX = 100;
    float startY = buttonY + 50;  // 在棋盘按钮下方50像素处
    
    painter.setFont(QFont("Arial", 12));
    painter.setPen(Qt::black);
    painter.drawText(QRectF(startX, startY, 500, 30), 
                    "编辑器模式：点击棋盘单元格切换状态（未涂色→涂色→X→未涂色）");
    
    // 行数调整
    float rowControlY = startY + 40;
    painter.drawText(QRectF(startX, rowControlY, 80, 30), "行数:");
    painter.drawText(QRectF(startX + 80, rowControlY, 40, 30), 
                    Qt::AlignCenter, QString::number(nonogramGame.rows));
    
    // 行数减少按钮
    QRectF rowDecreaseRect(startX + 130, rowControlY, 30, 30);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(rowDecreaseRect, 3, 3);
    painter.drawText(rowDecreaseRect, Qt::AlignCenter, "-");
    
    // 行数增加按钮
    QRectF rowIncreaseRect(startX + 170, rowControlY, 30, 30);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(rowIncreaseRect, 3, 3);
    painter.drawText(rowIncreaseRect, Qt::AlignCenter, "+");
    
    // 列数调整
    float colControlY = startY + 80;
    painter.drawText(QRectF(startX, colControlY, 80, 30), "列数:");
    painter.drawText(QRectF(startX + 80, colControlY, 40, 30), 
                    Qt::AlignCenter, QString::number(nonogramGame.cols));
    
    // 列数减少按钮
    QRectF colDecreaseRect(startX + 130, colControlY, 30, 30);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(colDecreaseRect, 3, 3);
    painter.drawText(colDecreaseRect, Qt::AlignCenter, "-");
    
    // 列数增加按钮
    QRectF colIncreaseRect(startX + 170, colControlY, 30, 30);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(colIncreaseRect, 3, 3);
    painter.drawText(colIncreaseRect, Qt::AlignCenter, "+");
}

int ColorBottleGame::getNonogramCellAt(float x, float y) 
{
    float startX = 100;
    float startY = menuBarHeight + 60;
    float cellSize = 40.0f;  // 增大单元格尺寸，与drawNonogramGame保持一致
    
    int rows = nonogramGame.rows;
    int cols = nonogramGame.cols;
    
    // 计算线索区域大小
    float clueSpacing = 30.0f;  // 约束条件间距（与drawNonogramGame保持一致）
    float maxRowClueWidth = 0;
    float maxColClueHeight = 0;
    for (int r = 0; r < rows; r++) 
    {
        float width = static_cast<float>(nonogramGame.rowClues[r].size()) * clueSpacing;
        if (width > maxRowClueWidth) maxRowClueWidth = width;
    }
    for (int c = 0; c < cols; c++) 
    {
        float height = static_cast<float>(nonogramGame.colClues[c].size()) * clueSpacing;
        if (height > maxColClueHeight) maxColClueHeight = height;
    }
    
    float gridStartX = startX + maxRowClueWidth + 10;
    float gridStartY = startY + maxColClueHeight + 10;
    
    if (x < gridStartX || x > gridStartX + cols * cellSize ||
        y < gridStartY || y > gridStartY + rows * cellSize)
         {
        return -1;  // 不在棋盘范围内
    }
    
    int col = static_cast<int>((x - gridStartX) / cellSize);
    int row = static_cast<int>((y - gridStartY) / cellSize);
    
    if (row >= 0 && row < rows && col >= 0 && col < cols) 
    {
        return row * cols + col;
    }
    return -1;
}

int ColorBottleGame::picnicSidebarX() const
{
    // 与中级/高级一致：始终按 6 列宽度定位侧栏
    return picnicBoardX + 6 * picnicCellSize + 30;
}

bool ColorBottleGame::isPicnicBoardPoint(int x, int y) const
{
    if (x >= picnicSidebarX())
        return false;
    const int boardW = picnicGame.getBoardCols() * picnicCellSize;
    const int boardH = picnicGame.getBoardRows() * picnicCellSize;
    return x >= picnicBoardX && x < picnicBoardX + boardW &&
           y >= picnicBoardY && y < picnicBoardY + boardH;
}

bool ColorBottleGame::isPicnicPanelPoint(int x, int y) const
{
    if (x >= picnicSidebarX())
        return false;
    const int boardW = picnicGame.getBoardCols() * picnicCellSize;
    const int boardH = picnicGame.getBoardRows() * picnicCellSize;
    const int panelTop = picnicBoardY + boardH;
    const int panelHeight = 220;
    return x >= picnicBoardX && x < picnicBoardX + boardW &&
           y >= panelTop && y < panelTop + panelHeight;
}

std::shared_ptr<Shape> ColorBottleGame::getPicnicShapeFromPanel(int x, int y)
{
    const int targetCellSize = 12;
    const int margin = 5;
    const int boardW = picnicGame.getBoardCols() * picnicCellSize;
    const int boardH = picnicGame.getBoardRows() * picnicCellSize;
    int currentX = picnicBoardX + margin;
    int currentY = picnicBoardY + boardH + 25;
    int maxHeightInRow = 0;
    const int panelRight = picnicBoardX + boardW;

    auto shapes = picnicGame.getAvailableShapesForPanel();
    for (const auto& shape : shapes)
    {
        int rotation = picnicGame.getPanelRotation(shape->getId());
        auto pattern = shape->getRotatedPattern(rotation);
        if (pattern.empty()) continue;

        int shapeWidth = static_cast<int>(pattern[0].size()) * targetCellSize;
        int shapeHeight = static_cast<int>(pattern.size()) * targetCellSize;

        if (currentX + shapeWidth + margin > panelRight)
        {
            currentX = picnicBoardX + margin;
            currentY += maxHeightInRow + margin;
            maxHeightInRow = shapeHeight;
        }
        else
        {
            maxHeightInRow = (std::max)(maxHeightInRow, shapeHeight);
        }

        if (x >= currentX && x < currentX + shapeWidth &&
            y >= currentY && y < currentY + shapeHeight)
        {
            return shape;
        }
        currentX += shapeWidth + margin;
    }
    return nullptr;
}

void ColorBottleGame::setPicnicFeedback(const QString& message, int durationMs)
{
    picnicFeedback = message;
    picnicFeedbackUntilMs = QDateTime::currentMSecsSinceEpoch() + durationMs;
}

QString ColorBottleGame::findPicnicResourcesPath() const
{
    const QStringList candidates = {
        QCoreApplication::applicationDirPath() + "/resources",
        QDir::currentPath() + "/resources",
        QCoreApplication::applicationDirPath() + "/../resources",
        QString::fromUtf8("D:/MyProjects/color_bottle/build/Release/resources")
    };
    for (const QString& path : candidates)
    {
        if (QDir(path).exists() && QDir(path + "/shapes").exists())
            return QDir(path).absolutePath();
    }
    return QString();
}

void ColorBottleGame::initializePicnicTextures()
{
    picnicBaseTextures.clear();
    picnicRotatedTextures.clear();
    picnicTexturesLoaded = 0;
    picnicResourcesPath = findPicnicResourcesPath();
    picnicUseTextures = !picnicResourcesPath.isEmpty();

    if (!picnicUseTextures)
    {
        setPicnicFeedback(QString::fromUtf8("未找到 resources 贴图目录，使用纯色绘制"), 3000);
        return;
    }

    const auto names = picnicGame.getAllShapeNames();
    for (const auto& name : names)
    {
        QPixmap pm = loadPicnicBaseTexture(name);
        if (!pm.isNull())
        {
            picnicBaseTextures.insert(QString::fromStdString(name), pm);
            picnicTexturesLoaded++;
        }
    }
    setPicnicFeedback(QString::fromUtf8("已加载 %1 张图块贴图（%2）")
                          .arg(picnicTexturesLoaded)
                          .arg(picnicResourcesPath), 3500);
}

QPixmap ColorBottleGame::loadPicnicBaseTexture(const std::string& shapeName)
{
    if (picnicResourcesPath.isEmpty())
        return QPixmap();

    const QString name = QString::fromStdString(shapeName);
    // sexy 目录部分文件名与形状名不完全一致
    QStringList sexyAliases;
    if (name == "C-shape")
        sexyAliases << "Big_c" << "Big-C" << "big_c" << "C-shape";
    else if (name == "Line5")
        sexyAliases << "line5" << "Line5";
    else if (name == "Big-L")
        sexyAliases << "3x3L" << "3x3 L" << "Big-L" << "Big_L";
    else if (name == "Cross")
        sexyAliases << "Cross" << "cross";
    else
        sexyAliases << name;

    QStringList paths;
    // 优先使用高质量 sexy 贴图，其次 shapes 根目录 / rotated 0° / basic
    for (const QString& alias : sexyAliases)
    {
        paths << picnicResourcesPath + "/shapes/sexy/" + alias + ".png";
        paths << picnicResourcesPath + "/shapes/" + alias + ".png";
        paths << picnicResourcesPath + "/shapes/rotated/" + alias + "_0deg.png";
        paths << picnicResourcesPath + "/shapes/basic/" + alias + ".png";
    }

    for (const QString& path : paths)
    {
        if (QFileInfo::exists(path))
        {
            QPixmap pm(path);
            if (!pm.isNull())
                return pm;
        }
    }
    return QPixmap();
}

QPixmap ColorBottleGame::getPicnicShapeTexture(const std::string& shapeName, int rotation)
{
    if (!picnicUseTextures)
        return QPixmap();

    rotation = ((rotation % 4) + 4) % 4;
    const QString cacheKey = QString::fromStdString(shapeName) + "_rot" + QString::number(rotation);
    auto it = picnicRotatedTextures.find(cacheKey);
    if (it != picnicRotatedTextures.end())
        return it.value();

    // 若存在预生成的旋转变体文件则优先加载（文件实为副本时仍再旋转以保证方向正确）
    QPixmap base;
    auto baseIt = picnicBaseTextures.find(QString::fromStdString(shapeName));
    if (baseIt != picnicBaseTextures.end())
        base = baseIt.value();
    else
    {
        base = loadPicnicBaseTexture(shapeName);
        if (!base.isNull())
            picnicBaseTextures.insert(QString::fromStdString(shapeName), base);
    }

    if (base.isNull())
        return QPixmap();

    QPixmap result = base;
    if (rotation != 0)
    {
        QTransform transform;
        transform.rotate(90.0 * rotation);
        result = base.transformed(transform, Qt::SmoothTransformation);
    }
    picnicRotatedTextures.insert(cacheKey, result);
    return result;
}

void ColorBottleGame::drawPicnicShapePixmap(QPainter& painter, int x, int y,
                                            const std::string& shapeName, int rotation,
                                            int cellSize, qreal opacity)
{
    auto shape = picnicGame.getShapeByName(shapeName);
    if (!shape)
        return;

    auto pattern = shape->getRotatedPattern(rotation);
    if (pattern.empty())
        return;

    const int shapeWidth = static_cast<int>(pattern[0].size()) * cellSize;
    const int shapeHeight = static_cast<int>(pattern.size()) * cellSize;
    const QRect dest(x, y, shapeWidth, shapeHeight);

    QPixmap texture = getPicnicShapeTexture(shapeName, rotation);
    if (!texture.isNull())
    {
        const qreal oldOpacity = painter.opacity();
        painter.setOpacity(opacity);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawPixmap(dest, texture);
        painter.setOpacity(oldOpacity);
        // 细描边，方便辨认格子边界
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(QColor(0, 0, 0, 80), 1));
        painter.drawRect(dest.adjusted(0, 0, -1, -1));
        return;
    }

    // 回退：纯色格子
    const Color& c = shape->getColor();
    QColor qc(c.r, c.g, c.b);
    qc.setAlphaF(opacity);
    painter.setBrush(qc);
    painter.setPen(QPen(Qt::black, 1));
    for (size_t i = 0; i < pattern.size(); ++i)
    {
        for (size_t j = 0; j < pattern[i].size(); ++j)
        {
            if (pattern[i][j])
            {
                painter.drawRect(x + static_cast<int>(j) * cellSize,
                                 y + static_cast<int>(i) * cellSize,
                                 cellSize, cellSize);
            }
        }
    }
}

void ColorBottleGame::initializePicnicShapeEditor()
{
    picnicShapeInfo.clear();
    picnicEditorSelectedIndex = -1;
    auto shapeNames = picnicGame.getAllShapeNames();
    std::sort(shapeNames.begin(), shapeNames.end(),
              [](const std::string& a, const std::string& b) {
                  return QString::fromStdString(a).compare(
                             QString::fromStdString(b), Qt::CaseInsensitive) < 0;
              });

    // 编辑器显示顺序：L-shape 在 L-mirror 前，Z-shape 在 Z-mirror 前
    auto swapByName = [&](const std::string& a, const std::string& b) {
        auto ia = std::find(shapeNames.begin(), shapeNames.end(), a);
        auto ib = std::find(shapeNames.begin(), shapeNames.end(), b);
        if (ia != shapeNames.end() && ib != shapeNames.end())
            std::iter_swap(ia, ib);
    };
    swapByName("L-mirror", "L-shape");
    swapByName("Z-mirror", "Z-shape");

    for (const std::string& shapeName : shapeNames)
    {
        Color color(128, 128, 128);
        if (auto shape = picnicGame.getShapeByName(shapeName))
            color = shape->getColor();

        std::array<int, 4> angleCounts = {
            picnicGame.getShapeRotationCount(shapeName, 0),
            picnicGame.getShapeRotationCount(shapeName, 90),
            picnicGame.getShapeRotationCount(shapeName, 180),
            picnicGame.getShapeRotationCount(shapeName, 270)
        };
        picnicShapeInfo.emplace_back(shapeName, angleCounts, color);
    }

    // 按图块列表行数自适应编辑器高度：标题区 + 行列表 + 底部按钮区
    const int listTop = 45;
    const int itemHeight = 25;
    const int bottomBar = 50;
    picnicEditorHeight = listTop
        + static_cast<int>(picnicShapeInfo.size()) * itemHeight
        + bottomBar;

    const int maxHeight = (std::max)(200, kDesignHeight - picnicEditorY - 10);
    if (picnicEditorHeight > maxHeight)
        picnicEditorHeight = maxHeight;
    if (picnicEditorHeight < listTop + itemHeight + bottomBar)
        picnicEditorHeight = listTop + itemHeight + bottomBar;
}

void ColorBottleGame::applyPicnicShapeEditorChanges()
{
    for (const auto& item : picnicShapeInfo)
    {
        const std::string& shapeName = std::get<0>(item);
        const auto& angleCounts = std::get<1>(item);
        for (int angle = 0; angle < 360; angle += 90)
            picnicGame.updateShapeRotation(shapeName, angle, 0);
        int totalCount = 0;
        for (int angle = 0; angle < 4; ++angle)
        {
            if (angleCounts[angle] > 0)
                picnicGame.updateShapeRotation(shapeName, angle * 90, angleCounts[angle]);
            totalCount += angleCounts[angle];
        }
        picnicGame.updateShapeCount(shapeName, totalCount);
    }
    picnicGame.rebuildShapes();
    setPicnicFeedback(QString::fromUtf8("图块角度配置已应用"), 2500);
}

bool ColorBottleGame::getPicnicSnapPlacement(int& outX, int& outY, bool& canPlace) const
{
    canPlace = false;
    if (!picnicDragging || !picnicGame.getIsDragging() || !picnicGame.getSelectedShape())
        return false;

    auto shape = picnicGame.getSelectedShape();
    int rotation = picnicGame.getDraggedOutRotation();
    auto pattern = shape->getRotatedPattern(rotation);
    if (pattern.empty())
        return false;

    const int cell = picnicCellSize;
    const int boardCols = picnicGame.getBoardCols();
    const int boardRows = picnicGame.getBoardRows();
    int shapeWidth = static_cast<int>(pattern[0].size()) * cell;
    int shapeHeight = static_cast<int>(pattern.size()) * cell;
    int relX = picnicMouseX - picnicBoardX;
    int relY = picnicMouseY - picnicBoardY;

    if (relX < 0 || relY < 0 || relX >= boardCols * cell || relY >= boardRows * cell)
        return false;

    float preciseX = static_cast<float>(relX - shapeWidth / 2) / static_cast<float>(cell);
    float preciseY = static_cast<float>(relY - shapeHeight / 2) / static_cast<float>(cell);
    outX = static_cast<int>(std::round(preciseX));
    outY = static_cast<int>(std::round(preciseY));
    outX = (std::max)(0, (std::min)(outX, boardCols - 1));
    outY = (std::max)(0, (std::min)(outY, boardRows - 1));
    canPlace = picnicGame.canPlaceShapeConst(shape, outX, outY, rotation);
    return true;
}

void ColorBottleGame::handlePicnicShapeEditorClick(int x, int y)
{
    if (x < picnicEditorX || x > picnicEditorX + picnicEditorWidth ||
        y < picnicEditorY || y > picnicEditorY + picnicEditorHeight)
        return;

    QRect closeRect(picnicEditorX + picnicEditorWidth - 35, picnicEditorY + 5, 30, 25);
    if (closeRect.contains(x, y))
    {
        picnicShowShapeEditor = false;
        return;
    }

    QRect confirmRect(picnicEditorX + picnicEditorWidth - 160,
                      picnicEditorY + picnicEditorHeight - 40, 150, 30);
    if (confirmRect.contains(x, y))
    {
        applyPicnicShapeEditorChanges();
        picnicShowShapeEditor = false;
        return;
    }

    const int listY = picnicEditorY + 45;
    const int listX = picnicEditorX + 10;
    const int ITEM_HEIGHT = 25;
    const int BUTTON_WIDTH = 25;
    const int LABEL_WIDTH = 35;
    const int BUTTON_SPACING = 6;
    const int COUNT_WIDTH = 25;
    const int GROUP_SPACING = 15;
    const int groupWidth = LABEL_WIDTH + BUTTON_SPACING + BUTTON_WIDTH + BUTTON_SPACING +
                           COUNT_WIDTH + BUTTON_SPACING + BUTTON_WIDTH + GROUP_SPACING;
    const int rowControlsWidth = 150 + 4 * groupWidth;

    for (size_t i = 0; i < picnicShapeInfo.size(); ++i)
    {
        auto& angleCounts = std::get<1>(picnicShapeInfo[i]);
        int rowY = listY + static_cast<int>(i) * ITEM_HEIGHT;
        QRect rowRect(listX, rowY, rowControlsWidth, ITEM_HEIGHT);

        // 点击行（名称或空白）选中该图块
        if (rowRect.contains(x, y))
            picnicEditorSelectedIndex = static_cast<int>(i);

        int angleX = listX + 150;
        for (int angle = 0; angle < 4; ++angle)
        {
            int minusX = angleX + LABEL_WIDTH + BUTTON_SPACING;
            QRect minusRect(minusX, rowY + 2, BUTTON_WIDTH, 20);
            if (minusRect.contains(x, y))
            {
                picnicEditorSelectedIndex = static_cast<int>(i);
                if (angleCounts[angle] > 0)
                    angleCounts[angle]--;
                return;
            }

            int countX = minusX + BUTTON_WIDTH + BUTTON_SPACING;
            QRect countRect(countX, rowY + 2, COUNT_WIDTH, 20);
            if (countRect.contains(x, y))
            {
                picnicEditorSelectedIndex = static_cast<int>(i);
                return;
            }

            int plusX = countX + COUNT_WIDTH + BUTTON_SPACING;
            QRect plusRect(plusX, rowY + 2, BUTTON_WIDTH, 20);
            if (plusRect.contains(x, y))
            {
                picnicEditorSelectedIndex = static_cast<int>(i);
                angleCounts[angle]++;
                return;
            }

            angleX += groupWidth;
        }

        if (rowRect.contains(x, y))
            return;
    }
}

void ColorBottleGame::solvePicnicPuzzle()
{
    picnicGame.resetGame();
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    picnicGame.setSolveUpdateCallback([this]() {
        update();
        QApplication::processEvents();
    });
    bool algoOk = picnicGame.solveFillEntireBoard();
    picnicGame.setSolveUpdateCallback(nullptr);
    const double solveSec = (QDateTime::currentMSecsSinceEpoch() - startMs) / 1000.0;
    const bool filled = picnicGame.isBoardCompletelyFilled();
    Q_UNUSED(algoOk);
    if (filled)
    {
        const PicnicSolveRecord rec = capturePicnicSolveRecord();
        if (picnicSolveRecordMeetsLimits(rec))
            appendPicnicSolveRecord(rec);
        setPicnicFeedback(QString::fromUtf8("自动求解成功！耗时 %1 秒，棋盘已填满")
                              .arg(QString::number(solveSec, 'f', 2)), 4000);
    }
    else
    {
        setPicnicFeedback(QString::fromUtf8("自动求解失败（%1 秒），棋盘未完全填满")
                              .arg(QString::number(solveSec, 'f', 2)), 4000);
    }
}

void ColorBottleGame::enumeratePicnicFillCombinations()
{
    if (picnicEnumerateRunning)
        return;

    picnicEnumerateRunning = true;
    picnicEnumerateCancel = false;
    picnicDragging = false;
    picnicStoneEditMode = false;
    picnicGame.resetGame();

    const int beforeCount = static_cast<int>(picnicSolveRecordIndicesFor(picnicDifficulty).size());
    int added = 0;
    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();

    setPicnicFeedback(QString::fromUtf8("正在穷举可填满组合…（Esc 取消）"), 2000);
    update();
    QApplication::processEvents();

    picnicGame.enumerateFillCombinations(
        [this, &added](const std::map<std::string, int>& combo) {
            PicnicSolveRecord rec;
            rec.difficulty = picnicDifficulty;
            for (int y = 0; y < picnicGame.getBoardRows(); ++y)
            {
                for (int x = 0; x < picnicGame.getBoardCols(); ++x)
                {
                    if (picnicGame.isStone(x, y))
                        rec.stones.emplace_back(x, y);
                }
            }
            rec.shapeRotations = combo;
            const int before = static_cast<int>(picnicSolveRecords.size());
            appendPicnicSolveRecord(rec);
            if (static_cast<int>(picnicSolveRecords.size()) > before)
                ++added;
        },
        [this]() {
            QApplication::processEvents(QEventLoop::AllEvents, 16);
            return picnicEnumerateCancel;
        },
        [this, startMs](int attempts, int found) {
            const double sec = (QDateTime::currentMSecsSinceEpoch() - startMs) / 1000.0;
            setPicnicFeedback(QString::fromUtf8("穷举中… 尝试 %1，已发现 %2 种（%3 秒，Esc 取消）")
                                  .arg(attempts)
                                  .arg(found)
                                  .arg(QString::number(sec, 'f', 1)), 1500);
            update();
        });

    picnicEnumerateRunning = false;
    const int afterCount = static_cast<int>(picnicSolveRecordIndicesFor(picnicDifficulty).size());
    const double sec = (QDateTime::currentMSecsSinceEpoch() - startMs) / 1000.0;

    if (picnicEnumerateCancel)
    {
        setPicnicFeedback(QString::fromUtf8("穷举已取消：新增 %1 条，当前难度共 %2 条（%3 秒）")
                              .arg(added)
                              .arg(afterCount)
                              .arg(QString::number(sec, 'f', 1)), 4000);
    }
    else
    {
        setPicnicFeedback(QString::fromUtf8("穷举完成：新增 %1 条（本难度 %2→%3），耗时 %4 秒")
                              .arg(added)
                              .arg(beforeCount)
                              .arg(afterCount)
                              .arg(QString::number(sec, 'f', 1)), 5000);
    }
    update();
}

QString ColorBottleGame::picnicSolveRecordsFilePath() const
{
    if (!picnicSolveRecordsPath.isEmpty())
        return picnicSolveRecordsPath;
    return QCoreApplication::applicationDirPath() + "/picnic_solve_records.txt";
}

QByteArray ColorBottleGame::picnicSolveRecordFingerprint(const PicnicSolveRecord& record) const
{
    QByteArray key;
    key.reserve(96);
    key.append(static_cast<char>(static_cast<int>(record.difficulty) + '0'));
    key.append('|');
    for (const auto& s : record.stones)
    {
        key.append(QByteArray::number(s.first));
        key.append(',');
        key.append(QByteArray::number(s.second));
        key.append(';');
    }
    key.append('|');
    for (const auto& kv : record.shapeRotations)
    {
        key.append(kv.first.data(), static_cast<int>(kv.first.size()));
        key.append('=');
        key.append(QByteArray::number(kv.second));
        key.append(';');
    }
    return key;
}

void ColorBottleGame::ensurePicnicSolveRecordsLoaded()
{
    if (picnicSolveRecordsLoaded)
        return;
    loadPicnicSolveRecords();
}

ColorBottleGame::PicnicSolveRecord ColorBottleGame::capturePicnicSolveRecord() const
{
    // 按棋盘上实际放置的图块类型/角度统计，构成“棋盘分割”记录
    PicnicSolveRecord rec;
    rec.difficulty = picnicDifficulty;
    for (int y = 0; y < picnicGame.getBoardRows(); ++y)
    {
        for (int x = 0; x < picnicGame.getBoardCols(); ++x)
        {
            if (picnicGame.isStone(x, y))
                rec.stones.emplace_back(x, y);
        }
    }

    for (const auto& ps : picnicGame.getPlacedShapes())
    {
        if (!ps.shape)
            continue;
        const int angle = (((ps.rotation % 4) + 4) % 4) * 90;
        const std::string key = ps.shape->getName() + "_" + std::to_string(angle);
        rec.shapeRotations[key] += 1;
    }

    if (rec.shapeRotations.empty())
    {
        for (const auto& kv : picnicGame.shapeRotations)
        {
            if (kv.second > 0)
                rec.shapeRotations[kv.first] = kv.second;
        }
    }
    return rec;
}

bool ColorBottleGame::picnicSolveRecordMeetsLimits(const PicnicSolveRecord& record) const
{
    int ones = 0;
    int line2 = 0;
    for (const auto& kv : record.shapeRotations)
    {
        if (kv.second <= 0)
            continue;
        const size_t pos = kv.first.find('_');
        const std::string name = (pos == std::string::npos)
            ? kv.first
            : kv.first.substr(0, pos);
        if (name == "1x1")
            ones += kv.second;
        else if (name == "Line2")
            line2 += kv.second;
    }
    return ones <= 2 && line2 <= 2;
}

bool ColorBottleGame::picnicSolveRecordsEqual(const PicnicSolveRecord& a,
                                              const PicnicSolveRecord& b) const
{
    return a.difficulty == b.difficulty
        && a.stones == b.stones
        && a.shapeRotations == b.shapeRotations;
}

void ColorBottleGame::loadPicnicSolveRecords()
{
    picnicSolveRecords.clear();
    picnicSolveRecordIndexByFp.clear();
    picnicSolveRecordIndex = -1;
    picnicSolveRecordsPath = picnicSolveRecordsFilePath();
    picnicSolveRecordsLoaded = true;

    QFile file(picnicSolveRecordsPath);
    if (!file.open(QIODevice::ReadOnly))
        return;

    const QByteArray data = file.readAll();
    file.close();
    if (data.isEmpty())
        return;

    // 粗估：约 160 字节/条，预分配减少扩容
    picnicSolveRecords.reserve(static_cast<size_t>(data.size() / 160 + 64));
    picnicSolveRecordIndexByFp.reserve(static_cast<int>(picnicSolveRecords.capacity()));

    PicnicSolveRecord cur;
    bool inRecord = false;
    auto flushRecord = [&]() {
        if (inRecord && !cur.shapeRotations.empty() && picnicSolveRecordMeetsLimits(cur))
        {
            const QByteArray fp = picnicSolveRecordFingerprint(cur);
            if (!picnicSolveRecordIndexByFp.contains(fp))
            {
                picnicSolveRecordIndexByFp.insert(fp, static_cast<int>(picnicSolveRecords.size()));
                picnicSolveRecords.push_back(std::move(cur));
            }
        }
        cur = PicnicSolveRecord();
        inRecord = false;
    };

    auto tokenize = [](const QByteArray& line) {
        QList<QByteArray> parts;
        int p = 0;
        const int n = line.size();
        while (p < n)
        {
            while (p < n && (line[p] == ' ' || line[p] == '\t'))
                ++p;
            const int s = p;
            while (p < n && line[p] != ' ' && line[p] != '\t')
                ++p;
            if (p > s)
                parts.append(line.mid(s, p - s));
        }
        return parts;
    };

    const QByteArray easyCn = QString::fromUtf8("初级").toUtf8();
    const QByteArray mediumCn = QString::fromUtf8("中级").toUtf8();

    int i = 0;
    const int n = data.size();
    while (i < n)
    {
        const int lineStart = i;
        while (i < n && data.at(i) != '\n')
            ++i;
        QByteArray line = data.mid(lineStart, i - lineStart);
        if (i < n)
            ++i;
        if (!line.isEmpty() && line.endsWith('\r'))
            line.chop(1);

        // trim
        int a = 0;
        int b = line.size();
        while (a < b && (line.at(a) == ' ' || line.at(a) == '\t'))
            ++a;
        while (b > a && (line.at(b - 1) == ' ' || line.at(b - 1) == '\t'))
            --b;
        if (a > 0 || b < line.size())
            line = line.mid(a, b - a);

        if (line.isEmpty() || line.startsWith('#'))
            continue;
        if (line == "---")
        {
            flushRecord();
            inRecord = true;
            continue;
        }
        if (!inRecord)
            continue;

        const QList<QByteArray> parts = tokenize(line);
        if (parts.isEmpty())
            continue;

        if (parts[0] == "difficulty" && parts.size() >= 2)
        {
            const QByteArray d = parts[1].toLower();
            if (d == "easy" || d == easyCn)
                cur.difficulty = PicnicDifficulty::Easy;
            else if (d == "medium" || d == mediumCn)
                cur.difficulty = PicnicDifficulty::Medium;
            else
                cur.difficulty = PicnicDifficulty::Hard;
        }
        else if (parts[0] == "stone" && parts.size() >= 3)
        {
            cur.stones.emplace_back(parts[1].toInt(), parts[2].toInt());
        }
        else if (parts.size() >= 2)
        {
            bool ok = false;
            const int count = parts[1].toInt(&ok);
            if (ok && count > 0)
                cur.shapeRotations[QString::fromUtf8(parts[0]).toStdString()] = count;
        }
    }
    flushRecord();
}

void ColorBottleGame::appendPicnicSolveRecord(const PicnicSolveRecord& record)
{
    if (record.shapeRotations.empty())
        return;
    if (!picnicSolveRecordMeetsLimits(record))
        return;

    ensurePicnicSolveRecordsLoaded();

    const QByteArray fp = picnicSolveRecordFingerprint(record);
    if (picnicSolveRecordIndexByFp.contains(fp))
    {
        picnicSolveRecordIndex = picnicSolveRecordIndexByFp.value(fp);
        return;
    }

    picnicSolveRecordIndexByFp.insert(fp, static_cast<int>(picnicSolveRecords.size()));
    picnicSolveRecords.push_back(record);
    picnicSolveRecordIndex = static_cast<int>(picnicSolveRecords.size()) - 1;

    picnicSolveRecordsPath = picnicSolveRecordsFilePath();
    QFile file(picnicSolveRecordsPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        // 内存已追加，但文件写入失败时回滚内存并提示
        picnicSolveRecords.pop_back();
        picnicSolveRecordIndexByFp.remove(fp);
        picnicSolveRecordIndex = picnicSolveRecords.empty()
            ? -1
            : static_cast<int>(picnicSolveRecords.size()) - 1;
        setPicnicFeedback(QString::fromUtf8("追加求解记录失败：无法写入文件\n%1")
                              .arg(picnicSolveRecordsPath), 3500);
        return;
    }
    QTextStream out(&file);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif
    out << "---\n";
    out << "# " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    const char* diffName = "Hard";
    if (record.difficulty == PicnicDifficulty::Easy) diffName = "Easy";
    else if (record.difficulty == PicnicDifficulty::Medium) diffName = "Medium";
    out << "difficulty " << diffName << "\n";
    for (const auto& s : record.stones)
        out << "stone " << s.first << " " << s.second << "\n";
    for (const auto& kv : record.shapeRotations)
        out << QString::fromStdString(kv.first) << " " << kv.second << "\n";
    file.close();
}

void ColorBottleGame::applyPicnicSolveRecord(int index)
{
    if (index < 0 || index >= static_cast<int>(picnicSolveRecords.size()))
        return;

    const PicnicSolveRecord& rec = picnicSolveRecords[static_cast<size_t>(index)];
    picnicSolveRecordIndex = index;
    picnicDragging = false;
    picnicStoneEditMode = false;

    picnicDifficulty = rec.difficulty;
    picnicGame.setCellPixelSize(picnicCellSize);
    picnicGame.setDifficulty(rec.difficulty);

    picnicGame.clearStones();
    for (const auto& s : rec.stones)
        picnicGame.setStone(s.first, s.second, true);

    picnicGame.shapeRotations.clear();
    picnicGame.shapeCounts.clear();
    for (const auto& kv : rec.shapeRotations)
        picnicGame.shapeRotations[kv.first] = kv.second;
    picnicGame.rebuildShapes();
    picnicGame.resetGame();
    initializePicnicShapeEditor();
}

void ColorBottleGame::navigatePicnicSolveRecord(int delta)
{
    ensurePicnicSolveRecordsLoaded();
    if (picnicSolveRecords.empty())
    {
        setPicnicFeedback(QString::fromUtf8("暂无求解记录（自动求解成功后会写入文件）"), 2500);
        return;
    }

    const std::vector<int> matches = picnicSolveRecordIndicesFor(picnicDifficulty);
    if (matches.empty())
    {
        setPicnicFeedback(QString::fromUtf8("当前难度暂无求解记录"), 2000);
        return;
    }

    int curLocal = -1;
    for (int i = 0; i < static_cast<int>(matches.size()); ++i)
    {
        if (matches[static_cast<size_t>(i)] == picnicSolveRecordIndex)
        {
            curLocal = i;
            break;
        }
    }

    const int n = static_cast<int>(matches.size());
    int nextLocal = curLocal;
    if (nextLocal < 0)
        nextLocal = (delta > 0) ? 0 : (n - 1);
    else
        nextLocal = (nextLocal + delta + n) % n;

    applyPicnicSolveRecord(matches[static_cast<size_t>(nextLocal)]);
}

std::vector<int> ColorBottleGame::picnicSolveRecordIndicesFor(PicnicDifficulty difficulty) const
{
    std::vector<int> matches;
    for (int i = 0; i < static_cast<int>(picnicSolveRecords.size()); ++i)
    {
        if (picnicSolveRecords[static_cast<size_t>(i)].difficulty == difficulty)
            matches.push_back(i);
    }
    return matches;
}

void ColorBottleGame::drawPicnicShapeEditor(QPainter& painter)
{
    if (!picnicShowShapeEditor)
        return;

    QRect editorRect(picnicEditorX, picnicEditorY, picnicEditorWidth, picnicEditorHeight);
    painter.setBrush(QColor(250, 250, 250));
    painter.setPen(QPen(Qt::black, 2));
    painter.drawRect(editorRect);

    QRect titleRect(picnicEditorX, picnicEditorY, picnicEditorWidth, 35);
    painter.setBrush(QColor(200, 200, 200));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRect(titleRect);
    painter.setFont(QFont("Microsoft YaHei", 11));
    painter.drawText(titleRect.adjusted(10, 0, -40, 0), Qt::AlignVCenter | Qt::AlignLeft,
                     QString::fromUtf8("图块角度数量编辑器（拖拽标题栏移动，E 关闭）"));

    QRect closeRect(picnicEditorX + picnicEditorWidth - 35, picnicEditorY + 5, 30, 25);
    painter.setBrush(QColor(200, 100, 100));
    painter.drawRect(closeRect);
    painter.drawText(closeRect, Qt::AlignCenter, "X");

    const int listY = picnicEditorY + 45;
    const int listX = picnicEditorX + 10;
    const int ITEM_HEIGHT = 25;
    const int BUTTON_WIDTH = 25;
    const int LABEL_WIDTH = 35;
    const int BUTTON_SPACING = 6;
    const int COUNT_WIDTH = 25;
    const int GROUP_SPACING = 15;

    painter.setFont(QFont("Consolas", 9));
    for (size_t i = 0; i < picnicShapeInfo.size(); ++i)
    {
        const auto& item = picnicShapeInfo[i];
        const std::string& name = std::get<0>(item);
        const auto& angleCounts = std::get<1>(item);
        int rowY = listY + static_cast<int>(i) * ITEM_HEIGHT;

        painter.setPen(Qt::black);
        painter.setFont(QFont("Consolas", 9));
        painter.drawText(listX, rowY + 16, QString::fromStdString(name));

        int angleX = listX + 150;
        for (int angle = 0; angle < 4; ++angle)
        {
            painter.setFont(QFont("Consolas", 9));
            painter.setPen(Qt::black);
            painter.drawText(angleX, rowY + 16, QString("%1deg").arg(angle * 90));
            int minusX = angleX + LABEL_WIDTH + BUTTON_SPACING;
            QRect minusRect(minusX, rowY + 2, BUTTON_WIDTH, 20);
            painter.setBrush(QColor(230, 230, 230));
            painter.setPen(QPen(Qt::black, 1));
            painter.drawRect(minusRect);
            painter.drawText(minusRect, Qt::AlignCenter, "-");

            int countX = minusX + BUTTON_WIDTH + BUTTON_SPACING;
            QRect countRect(countX, rowY + 2, COUNT_WIDTH, 20);
            const bool nonzero = angleCounts[angle] > 0;
            if (nonzero)
            {
                painter.setBrush(QColor(255, 193, 7));
                painter.setPen(QPen(QColor(180, 90, 0), 2));
                painter.drawRoundedRect(countRect, 3, 3);
                painter.setFont(QFont("Consolas", 11, QFont::Bold));
                painter.setPen(QColor(120, 40, 0));
            }
            else
            {
                painter.setFont(QFont("Consolas", 9));
                painter.setPen(QColor(140, 140, 140));
            }
            painter.drawText(countRect, Qt::AlignCenter, QString::number(angleCounts[angle]));

            int plusX = countX + COUNT_WIDTH + BUTTON_SPACING;
            QRect plusRect(plusX, rowY + 2, BUTTON_WIDTH, 20);
            painter.setFont(QFont("Consolas", 9));
            painter.setBrush(QColor(230, 230, 230));
            painter.setPen(QPen(Qt::black, 1));
            painter.drawRect(plusRect);
            painter.drawText(plusRect, Qt::AlignCenter, "+");

            angleX += LABEL_WIDTH + BUTTON_SPACING + BUTTON_WIDTH + BUTTON_SPACING +
                      COUNT_WIDTH + BUTTON_SPACING + BUTTON_WIDTH + GROUP_SPACING;
        }
    }

    // 右侧预览
    QRect previewRect(picnicEditorX + 750, picnicEditorY + 45,
                      picnicEditorWidth - 760, picnicEditorHeight - 95);
    painter.setBrush(QColor(240, 240, 240));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRect(previewRect);
    painter.setFont(QFont("Microsoft YaHei", 10));
    painter.drawText(previewRect.left() + 5, previewRect.top() + 16,
                     QString::fromUtf8("图块预览"));

    const int SHAPE_CELL_SIZE = 10;
    const int MARGIN = 18;
    int currentX = previewRect.left() + 10;
    int currentY = previewRect.top() + 28;
    int maxHeightInRow = 0;
    for (const auto& item : picnicShapeInfo)
    {
        const std::string& name = std::get<0>(item);
        const auto& angleCounts = std::get<1>(item);
        int totalCount = angleCounts[0] + angleCounts[1] + angleCounts[2] + angleCounts[3];
        if (totalCount == 0)
            continue;
        auto shape = picnicGame.getShapeByName(name);
        if (!shape)
            continue;

        for (int angle = 0; angle < 4; ++angle)
        {
            if (angleCounts[angle] == 0)
                continue;
            auto pattern = shape->getRotatedPattern(angle);
            if (pattern.empty())
                continue;
            int shapeWidth = static_cast<int>(pattern[0].size()) * SHAPE_CELL_SIZE;
            int shapeHeight = static_cast<int>(pattern.size()) * SHAPE_CELL_SIZE;
            if (currentX + shapeWidth + MARGIN > previewRect.right() - 10)
            {
                currentX = previewRect.left() + 10;
                currentY += maxHeightInRow + MARGIN;
                maxHeightInRow = shapeHeight;
            }
            else
            {
                maxHeightInRow = (std::max)(maxHeightInRow, shapeHeight);
            }
            if (currentY + shapeHeight + 20 > previewRect.bottom())
                break;

            drawPicnicShapePixmap(painter, currentX, currentY, name, angle, SHAPE_CELL_SIZE);
            painter.setPen(Qt::black);
            painter.setFont(QFont("Consolas", 8));
            painter.drawText(currentX, currentY + shapeHeight + 12,
                             QString("x%1@%2").arg(angleCounts[angle]).arg(angle * 90));
            currentX += shapeWidth + MARGIN;
        }
        currentX = previewRect.left() + 10;
        currentY += maxHeightInRow + MARGIN + 12;
        maxHeightInRow = 0;
        if (currentY > previewRect.bottom() - 30)
            break;
    }

    QRect confirmRect(picnicEditorX + picnicEditorWidth - 160,
                      picnicEditorY + picnicEditorHeight - 40, 150, 30);
    painter.setBrush(QColor(100, 200, 100));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(confirmRect, 4, 4);
    painter.setFont(QFont("Microsoft YaHei", 11));
    painter.drawText(confirmRect, Qt::AlignCenter, QString::fromUtf8("应用更改"));
}

void ColorBottleGame::setPicnicDifficulty(PicnicDifficulty difficulty)
{
    picnicDragging = false;
    picnicStoneEditMode = false;

    ensurePicnicSolveRecordsLoaded();

    if (tryApplyRandomPicnicSolveRecord(difficulty))
    {
        QString label;
        switch (difficulty)
        {
        case PicnicDifficulty::Easy: label = QString::fromUtf8("初级 4×4"); break;
        case PicnicDifficulty::Medium: label = QString::fromUtf8("中级 4×6"); break;
        default: label = QString::fromUtf8("高级 6×6"); break;
        }
        setPicnicFeedback(QString::fromUtf8("已切换难度：%1").arg(label), 1500);
        return;
    }

    picnicDifficulty = difficulty;
    picnicGame.setCellPixelSize(picnicCellSize);
    picnicGame.setDifficulty(difficulty);
    initializePicnicShapeEditor();
    QString label;
    switch (difficulty)
    {
    case PicnicDifficulty::Easy:
        label = QString::fromUtf8("初级 4×4");
        break;
    case PicnicDifficulty::Medium:
        label = QString::fromUtf8("中级 4×6");
        break;
    case PicnicDifficulty::Hard:
    default:
        label = QString::fromUtf8("高级 6×6");
        break;
    }
    setPicnicFeedback(QString::fromUtf8("已切换难度：%1（该难度暂无求解记录，使用默认图块）")
                          .arg(label), 2500);
}

bool ColorBottleGame::tryApplyRandomPicnicSolveRecord(PicnicDifficulty difficulty)
{
    const std::vector<int> matches = picnicSolveRecordIndicesFor(difficulty);
    if (matches.empty())
        return false;

    std::uniform_int_distribution<size_t> dist(0, matches.size() - 1);
    applyPicnicSolveRecord(matches[dist(rng)]);
    return true;
}

bool ColorBottleGame::handlePicnicSidebarClick(int x, int y)
{
    const int sx = picnicSidebarX();
    const QPoint pt(x, y);

    auto diffRect = [&](int relY) {
        return QRect(sx, picnicBoardY + relY, 130, 28);
    };
    if (diffRect(30).contains(pt))
    {
        setPicnicDifficulty(PicnicDifficulty::Easy);
        return true;
    }
    if (diffRect(64).contains(pt))
    {
        setPicnicDifficulty(PicnicDifficulty::Medium);
        return true;
    }
    if (diffRect(98).contains(pt))
    {
        setPicnicDifficulty(PicnicDifficulty::Hard);
        return true;
    }

    if (QRect(sx, picnicBoardY + 385, 62, 30).contains(pt))
    {
        solvePicnicPuzzle();
        return true;
    }
    if (QRect(sx + 68, picnicBoardY + 385, 62, 30).contains(pt))
    {
        enumeratePicnicFillCombinations();
        return true;
    }
    if (QRect(sx, picnicBoardY + 425, 130, 30).contains(pt))
    {
        picnicGame.resetGame();
        setPicnicFeedback(QString::fromUtf8("已重置棋盘（石头保留）"), 1500);
        return true;
    }
    if (QRect(sx, picnicBoardY + 465, 130, 30).contains(pt))
    {
        picnicStoneEditMode = !picnicStoneEditMode;
        if (picnicStoneEditMode)
        {
            picnicDragging = false;
            setPicnicFeedback(QString::fromUtf8("摆石头模式：左键切换格子，再次点按钮退出"), 2500);
        }
        else
        {
            setPicnicFeedback(QString::fromUtf8("已退出摆石头模式"), 1500);
        }
        return true;
    }
    if (QRect(sx, picnicBoardY + 505, 130, 30).contains(pt))
    {
        initializePicnicShapeEditor();
        picnicShowShapeEditor = true;
        picnicStoneEditMode = false;
        return true;
    }
    if (QRect(sx, picnicBoardY + 545, 130, 30).contains(pt))
    {
        if (picnicUseTextures)
        {
            picnicUseTextures = false;
            setPicnicFeedback(QString::fromUtf8("已切换为纯色绘制"), 1500);
        }
        else
        {
            if (picnicResourcesPath.isEmpty())
                initializePicnicTextures();
            else
            {
                picnicUseTextures = true;
                if (picnicBaseTextures.isEmpty())
                    initializePicnicTextures();
                setPicnicFeedback(QString::fromUtf8("已启用贴图（%1 张）")
                                      .arg(picnicTexturesLoaded), 1500);
            }
        }
        return true;
    }
    if (QRect(sx, picnicBoardY + 585, 130, 28).contains(pt) && picnicGame.getStoneCount() > 0)
    {
        picnicGame.clearStones();
        setPicnicFeedback(QString::fromUtf8("已清除全部石头"), 1500);
        return true;
    }
    return false;
}

void ColorBottleGame::drawPicnicGame(QPainter& painter)
{
    const int cell = picnicCellSize;
    const int boardCols = picnicGame.getBoardCols();
    const int boardRows = picnicGame.getBoardRows();
    const int boardW = boardCols * cell;
    const int boardH = boardRows * cell;
    const int panelTop = picnicBoardY + boardH;
    const int panelHeight = 220;
    const int sidebarX = picnicSidebarX();

    painter.setPen(Qt::black);
    painter.setFont(QFont("Microsoft YaHei", 22));
    QString diffText;
    switch (picnicDifficulty)
    {
    case PicnicDifficulty::Easy: diffText = QString::fromUtf8("初级"); break;
    case PicnicDifficulty::Medium: diffText = QString::fromUtf8("中级"); break;
    default: diffText = QString::fromUtf8("高级"); break;
    }
    painter.drawText(QRectF(picnicBoardX, menuBarHeight + 5, 500, 40),
                     QString::fromUtf8("野餐日 · %1（%2×%3）")
                         .arg(diffText).arg(boardRows).arg(boardCols));

    painter.setBrush(QColor(255, 255, 255));
    painter.setPen(QPen(Qt::black, 2));
    painter.drawRect(picnicBoardX, picnicBoardY, boardW, boardH);

    painter.setPen(QPen(QColor(200, 200, 200), 1));
    for (int i = 0; i <= boardCols; ++i)
    {
        painter.drawLine(picnicBoardX + i * cell, picnicBoardY,
                         picnicBoardX + i * cell, picnicBoardY + boardH);
    }
    for (int i = 0; i <= boardRows; ++i)
    {
        painter.drawLine(picnicBoardX, picnicBoardY + i * cell,
                         picnicBoardX + boardW, picnicBoardY + i * cell);
    }

    // 石头占格
    for (int y = 0; y < boardRows; ++y)
    {
        for (int x = 0; x < boardCols; ++x)
        {
            if (!picnicGame.isStone(x, y))
                continue;
            const int cx = picnicBoardX + x * cell;
            const int cy = picnicBoardY + y * cell;
            painter.setPen(QPen(QColor(80, 70, 60), 1));
            painter.setBrush(QColor(120, 110, 100));
            painter.drawRect(cx + 2, cy + 2, cell - 4, cell - 4);
            painter.setBrush(QColor(150, 140, 128));
            painter.drawEllipse(cx + cell / 4, cy + cell / 4, cell / 2, cell / 2);
            painter.setPen(QColor(60, 50, 40));
            painter.setFont(QFont("Microsoft YaHei", 8));
            painter.drawText(QRect(cx, cy, cell, cell), Qt::AlignCenter, QString::fromUtf8("石"));
        }
    }

    auto selected = picnicGame.getSelectedShape();
    int selectedId = selected ? selected->getId() : -1;
    for (const auto& placed : picnicGame.getPlacedShapes())
    {
        if (picnicDragging && picnicGame.getIsDragging() && placed.shapeId == selectedId)
            continue;

        drawPicnicShapePixmap(painter,
                              picnicBoardX + placed.x * cell,
                              picnicBoardY + placed.y * cell,
                              placed.shape->getName(),
                              placed.rotation,
                              cell);
    }

    painter.setBrush(QColor(245, 245, 245));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRect(picnicBoardX, panelTop, boardW, panelHeight);
    painter.setFont(QFont("Microsoft YaHei", 12));
    painter.drawText(picnicBoardX + 10, panelTop + 18,
                     QString::fromUtf8("图块面板（左键拖拽，右键旋转）"));

    const int targetCellSize = 12;
    const int margin = 5;
    int px = picnicBoardX + margin;
    int py = panelTop + 25;
    int maxHeightInRow = 0;
    auto panelShapes = picnicGame.getAvailableShapesForPanel();
    for (const auto& shape : panelShapes)
    {
        if (picnicDragging && picnicGame.getIsDragging() &&
            selected && shape->getId() == selected->getId())
            continue;

        int rotation = picnicGame.getPanelRotation(shape->getId());
        auto pattern = shape->getRotatedPattern(rotation);
        if (pattern.empty()) continue;

        int shapeWidth = static_cast<int>(pattern[0].size()) * targetCellSize;
        int shapeHeight = static_cast<int>(pattern.size()) * targetCellSize;
        if (px + shapeWidth + margin > picnicBoardX + boardW)
        {
            px = picnicBoardX + margin;
            py += maxHeightInRow + margin;
            maxHeightInRow = shapeHeight;
        }
        else
        {
            maxHeightInRow = (std::max)(maxHeightInRow, shapeHeight);
        }

        drawPicnicShapePixmap(painter, px, py, shape->getName(), rotation, targetCellSize);
        px += shapeWidth + margin;
    }

    // 侧边栏
    painter.setPen(Qt::black);
    painter.setFont(QFont("Microsoft YaHei", 12));
    painter.drawText(sidebarX, picnicBoardY + 20, QString::fromUtf8("难度等级"));

    auto drawDiffBtn = [&](int y, PicnicDifficulty d, const QString& text) {
        QRect r(sidebarX, picnicBoardY + y, 130, 28);
        const bool on = (picnicDifficulty == d);
        painter.setBrush(on ? QColor(255, 220, 140) : QColor(230, 230, 230));
        painter.setPen(QPen(on ? QColor(180, 100, 0) : Qt::black, on ? 2 : 1));
        painter.drawRoundedRect(r, 4, 4);
        painter.setPen(Qt::black);
        painter.drawText(r, Qt::AlignCenter, text);
    };
    drawDiffBtn(30, PicnicDifficulty::Easy, QString::fromUtf8("初级 4×4"));
    drawDiffBtn(64, PicnicDifficulty::Medium, QString::fromUtf8("中级 4×6"));
    drawDiffBtn(98, PicnicDifficulty::Hard, QString::fromUtf8("高级 6×6"));

    painter.setPen(Qt::black);
    painter.drawText(sidebarX, picnicBoardY + 145, QString::fromUtf8("操作说明"));
    painter.drawText(sidebarX, picnicBoardY + 167, QString::fromUtf8("左键：拖拽图块"));
    painter.drawText(sidebarX, picnicBoardY + 189, QString::fromUtf8("右键：旋转 / 取回"));
    painter.drawText(sidebarX, picnicBoardY + 211, QString::fromUtf8("R：拖拽中旋转"));
    painter.drawText(sidebarX, picnicBoardY + 233, QString::fromUtf8("E：编辑 / Pg：记录"));
    painter.drawText(sidebarX, picnicBoardY + 255,
                     picnicStoneEditMode
                         ? QString::fromUtf8("摆石头：开（点格子）")
                         : (picnicUseTextures
                                ? QString::fromUtf8("贴图：开（%1）").arg(picnicTexturesLoaded)
                                : QString::fromUtf8("贴图：关（纯色）")));

    painter.drawText(sidebarX, picnicBoardY + 285, QString::fromUtf8("游戏状态："));
    if (picnicGame.isGameWon())
    {
        painter.setPen(QColor(0, 140, 0));
        painter.drawText(sidebarX, picnicBoardY + 307, QString::fromUtf8("恭喜通关！"));
    }
    else
    {
        painter.setPen(Qt::black);
        painter.drawText(sidebarX, picnicBoardY + 307, QString::fromUtf8("进行中…"));
    }

    painter.setPen(Qt::black);
    if (picnicDragging && picnicGame.getIsDragging())
    {
        painter.drawText(sidebarX, picnicBoardY + 333, QString::fromUtf8("拖拽中：是"));
        painter.drawText(sidebarX, picnicBoardY + 355,
                         QString::fromUtf8("旋转：%1°").arg(picnicGame.getDraggedOutRotation() * 90));
    }
    else
    {
        painter.drawText(sidebarX, picnicBoardY + 333, QString::fromUtf8("拖拽中：否"));
    }

    QRect solveRect(sidebarX, picnicBoardY + 385, 62, 30);
    painter.setBrush(QColor(100, 200, 100));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(solveRect, 4, 4);
    painter.drawText(solveRect, Qt::AlignCenter, QString::fromUtf8("求解"));

    QRect enumRect(sidebarX + 68, picnicBoardY + 385, 62, 30);
    painter.setBrush(picnicEnumerateRunning ? QColor(255, 200, 120) : QColor(120, 180, 220));
    painter.drawRoundedRect(enumRect, 4, 4);
    painter.drawText(enumRect, Qt::AlignCenter,
                     picnicEnumerateRunning ? QString::fromUtf8("穷举中")
                                            : QString::fromUtf8("穷举"));

    QRect resetRect(sidebarX, picnicBoardY + 425, 130, 30);
    painter.setBrush(QColor(200, 200, 200));
    painter.drawRoundedRect(resetRect, 4, 4);
    painter.drawText(resetRect, Qt::AlignCenter, QString::fromUtf8("重置"));

    QRect stoneBtnRect(sidebarX, picnicBoardY + 465, 130, 30);
    painter.setBrush(picnicStoneEditMode ? QColor(180, 160, 140) : QColor(220, 220, 220));
    painter.setPen(QPen(picnicStoneEditMode ? QColor(100, 70, 40) : Qt::black,
                        picnicStoneEditMode ? 2 : 1));
    painter.drawRoundedRect(stoneBtnRect, 4, 4);
    painter.setPen(Qt::black);
    painter.drawText(stoneBtnRect, Qt::AlignCenter,
                     picnicStoneEditMode ? QString::fromUtf8("退出摆石头")
                                         : QString::fromUtf8("摆石头"));

    QRect editorBtnRect(sidebarX, picnicBoardY + 505, 130, 30);
    painter.setBrush(picnicShowShapeEditor ? QColor(150, 200, 150) : QColor(220, 220, 220));
    painter.setPen(QPen(Qt::black, 1));
    painter.drawRoundedRect(editorBtnRect, 4, 4);
    painter.drawText(editorBtnRect, Qt::AlignCenter, QString::fromUtf8("图块编辑"));

    QRect texBtnRect(sidebarX, picnicBoardY + 545, 130, 30);
    painter.setBrush(picnicUseTextures ? QColor(180, 220, 255) : QColor(220, 220, 220));
    painter.drawRoundedRect(texBtnRect, 4, 4);
    painter.drawText(texBtnRect, Qt::AlignCenter,
                     picnicUseTextures ? QString::fromUtf8("关闭贴图") : QString::fromUtf8("启用贴图"));

    if (picnicGame.getStoneCount() > 0)
    {
        QRect clearStoneRect(sidebarX, picnicBoardY + 585, 130, 28);
        painter.setBrush(QColor(210, 190, 170));
        painter.drawRoundedRect(clearStoneRect, 4, 4);
        painter.drawText(clearStoneRect, Qt::AlignCenter, QString::fromUtf8("清石头"));
    }

    painter.setPen(Qt::black);
    auto available = picnicGame.getAvailableShapesForPanel();
    const int statsY = picnicGame.getStoneCount() > 0 ? 625 : 595;
    painter.drawText(sidebarX, picnicBoardY + statsY,
                     QString::fromUtf8("可用图块: %1").arg(available.size()));
    painter.drawText(sidebarX, picnicBoardY + statsY + 22,
                     QString::fromUtf8("已放置: %1").arg(picnicGame.getPlacedShapes().size()));
    painter.drawText(sidebarX, picnicBoardY + statsY + 44,
                     QString::fromUtf8("石头: %1 / 可放: %2")
                         .arg(picnicGame.getStoneCount())
                         .arg(picnicGame.getPlayableArea()));
    {
        const std::vector<int> diffRecords = picnicSolveRecordIndicesFor(picnicDifficulty);
        int localPos = -1;
        for (int i = 0; i < static_cast<int>(diffRecords.size()); ++i)
        {
            if (diffRecords[static_cast<size_t>(i)] == picnicSolveRecordIndex)
            {
                localPos = i;
                break;
            }
        }
        painter.drawText(sidebarX, picnicBoardY + statsY + 66,
                         localPos >= 0
                             ? QString::fromUtf8("求解记录: %1/%2")
                                   .arg(localPos + 1)
                                   .arg(diffRecords.size())
                             : QString::fromUtf8("求解记录: %1")
                                   .arg(diffRecords.size()));
    }

    // 反馈横幅
    if (!picnicFeedback.isEmpty() &&
        QDateTime::currentMSecsSinceEpoch() < picnicFeedbackUntilMs)
    {
        QRect feedbackRect(sidebarX, picnicBoardY + statsY + 92, 280, 50);
        painter.setBrush(QColor(255, 255, 200));
        painter.setPen(QPen(Qt::black, 1));
        painter.drawRect(feedbackRect);
        painter.setFont(QFont("Microsoft YaHei", 10));
        painter.drawText(feedbackRect.adjusted(6, 4, -6, -4),
                         Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignVCenter,
                         picnicFeedback);
    }

    // 吸附落点预览（绿可放 / 红不可放）
    int snapX = 0, snapY = 0;
    bool canPlace = false;
    if (getPicnicSnapPlacement(snapX, snapY, canPlace))
    {
        auto shape = picnicGame.getSelectedShape();
        int rotation = picnicGame.getDraggedOutRotation();
        auto pattern = shape->getRotatedPattern(rotation);
        QColor snapColor = canPlace ? QColor(0, 200, 0, 90) : QColor(220, 40, 40, 90);
        painter.setBrush(snapColor);
        painter.setPen(QPen(canPlace ? QColor(0, 140, 0) : QColor(180, 0, 0), 2, Qt::DashLine));
        for (size_t i = 0; i < pattern.size(); ++i)
        {
            for (size_t j = 0; j < pattern[i].size(); ++j)
            {
                if (pattern[i][j])
                {
                    painter.drawRect(picnicBoardX + (snapX + static_cast<int>(j)) * cell,
                                     picnicBoardY + (snapY + static_cast<int>(i)) * cell,
                                     cell, cell);
                }
            }
        }
    }

    // 跟随鼠标的拖拽幽灵
    if (picnicDragging && picnicGame.getIsDragging() && picnicGame.getSelectedShape())
    {
        auto shape = picnicGame.getSelectedShape();
        int rotation = picnicGame.getDraggedOutRotation();
        auto pattern = shape->getRotatedPattern(rotation);
        if (!pattern.empty())
        {
            int shapeWidth = static_cast<int>(pattern[0].size()) * cell;
            int shapeHeight = static_cast<int>(pattern.size()) * cell;
            int offsetX = picnicMouseX - shapeWidth / 2;
            int offsetY = picnicMouseY - shapeHeight / 2;
            drawPicnicShapePixmap(painter, offsetX, offsetY, shape->getName(),
                                  rotation, cell, 0.7);
        }
    }

    drawPicnicShapeEditor(painter);
}

ColorBottleGame::ColorBottleGame(QWidget* parent) 
    : QWidget(parent), currentMode(MODE_BOTTLE_COLOR), currentLevel(1), maxLevels(15), 
      rng(std::random_device()()), nonogramGame(11, 8), nonogramEditorMode(false), 
      nonogramShowSolution(false), hanoiGame(3), hanoiAnimationTimer(nullptr), 
      hanoiDiskCount(3), snakeGame(30, 20), snakeGameTimer(nullptr), 
#ifdef QT_MULTIMEDIA_AVAILABLE
      foodSoundEffect(nullptr), gameOverSoundEffect(nullptr), 
#endif
      picnicCellSize(60), picnicBoardX(40), picnicBoardY(0), picnicMouseX(0), picnicMouseY(0),
      picnicDragging(false), picnicFeedbackUntilMs(0), picnicShowShapeEditor(false),
      picnicStoneEditMode(false), picnicEnumerateCancel(false), picnicEnumerateRunning(false),
      picnicEditorDragging(false), picnicEditorX(20), picnicEditorY(50),
      picnicEditorWidth(1180), picnicEditorHeight(720),
      picnicEditorDragOffsetX(0), picnicEditorDragOffsetY(0),
      picnicEditorSelectedIndex(-1),
      picnicUseTextures(false), picnicTexturesLoaded(0),
      picnicSolveRecordsLoaded(false),
      picnicSolveRecordIndex(-1),
      picnicDifficulty(PicnicDifficulty::Hard), menuBarHeight(40) 
      {
    setMinimumSize(700, 450);
    resize(kDesignWidth, kDesignHeight);
    setWindowTitle("Game collection");
    setMouseTracking(true);
    picnicBoardY = menuBarHeight + 50;
    picnicGame.setCellPixelSize(picnicCellSize);
    initializePicnicTextures();
    picnicSolveRecordsLoaded = false;
    initializePicnicShapeEditor();  // 求解记录延迟到进入野餐日时再加载
    
    // 初始化所有可用颜色
    allColors = 
    {
        Qt::red,
        Qt::blue,
        Qt::green,
        Qt::yellow,
        QColor(255, 0, 255),  // Magenta
        Qt::cyan,
        QColor(255, 165, 0),  // Orange
        QColor(128, 0, 128),   // Purple
        QColor(255, 192, 203), // Pink
        QColor(0, 128, 128),    // Teal
        QColor(255, 255, 0),   // Yellow (bright)
        QColor(0, 255, 255)    // Cyan (bright)
    };
    
    // 初始化UI位置
    bottleSize = 60.0f;
    bottleSpacing = 16.0f;
    startX = 50.0f;
    startY = 185.0f;  // 与 paint 中隐藏瓶行对齐（Attempts 与 Hidden Bottles 已拉开）
    colorPaletteX = 1100.0f;  // 右侧位置
    colorPaletteY = 150.0f;   // 从顶部开始
    colorPaletteColumns = 1;
    colorPaletteFirstColRows = 1;
    colorPaletteStartCount = 0;
    
    // 加载SVG油漆刷图标
    brushRenderer = new QSvgRenderer(this);
    if (!brushRenderer->load(QString("3139463_37491-.svg"))) 
    {
        // 如果加载失败，尝试其他路径
        brushRenderer->load(QString(":/3139463_37491-.svg"));
    }
    
    // 加载刷子 PNG（猜瓶子颜色面板底图）
    const QStringList brushPaths = {
        QStringLiteral(":/resources/brush.png"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/resources/brush.png"),
        QStringLiteral("resources/brush.png"),
        QStringLiteral("brush.png"),
        QStringLiteral("./brush.png")
    };
    brushPixmap = QPixmap();
    for (const QString& path : brushPaths)
    {
        if (brushPixmap.load(path) && !brushPixmap.isNull())
            break;
    }

    // 加载瓶子 PNG（猜瓶子位置底图）
    const QStringList bottlePaths = {
        QStringLiteral(":/resources/bottle.png"),
        QCoreApplication::applicationDirPath() + QStringLiteral("/resources/bottle.png"),
        QStringLiteral("resources/bottle.png"),
        QStringLiteral("bottle.png"),
        QStringLiteral("./bottle.png")
    };
    bottlePixmap = QPixmap();
    for (const QString& path : bottlePaths)
    {
        if (bottlePixmap.load(path) && !bottlePixmap.isNull())
            break;
    }
    
    // 初始化非数组游戏（11×8棋盘）
    // 上方的约束条件：1；1，6；11；1，1，4；7；6，3；1，2，3；4
    nonogramGame.colClues[0] = {1};
    nonogramGame.colClues[1] = {1, 6};
    nonogramGame.colClues[2] = {11};
    nonogramGame.colClues[3] = {1, 1, 4};
    nonogramGame.colClues[4] = {7};
    nonogramGame.colClues[5] = {6, 3};
    nonogramGame.colClues[6] = {1, 2, 3};
    nonogramGame.colClues[7] = {4};
    
    // 左边的约束条件：3，3；1，1，1；6；1，4；4；5；4；4；2，3；2，2；3，2
    nonogramGame.rowClues[0] = {3, 3};
    nonogramGame.rowClues[1] = {1, 1, 1};
    nonogramGame.rowClues[2] = {6};
    nonogramGame.rowClues[3] = {1, 4};
    nonogramGame.rowClues[4] = {4};
    nonogramGame.rowClues[5] = {5};
    nonogramGame.rowClues[6] = {4};
    nonogramGame.rowClues[7] = {4};
    nonogramGame.rowClues[8] = {2, 3};
    nonogramGame.rowClues[9] = {2, 2};
    nonogramGame.rowClues[10] = {3, 2};
    
    // 初始化汉诺塔游戏
    hanoiAnimationTimer = new QTimer(this);
    connect(hanoiAnimationTimer, &QTimer::timeout, this, [this]() {
        if (hanoiGame.isAnimating && hanoiGame.currentMoveIndex < static_cast<int>(hanoiGame.solutionMoves.size())) 
        {
            hanoiGame.executeNextMove();
            update();
            if (hanoiGame.currentMoveIndex >= static_cast<int>(hanoiGame.solutionMoves.size())) 
            {
                hanoiGame.isAnimating = false;
                hanoiAnimationTimer->stop();
            }
        }
    });
    
    // 初始化贪吃蛇游戏
    snakeGameTimer = new QTimer(this);
    connect(snakeGameTimer, &QTimer::timeout, this, [this]() {
        if (currentMode == MODE_SNAKE && !snakeGame.gameOver && !snakeGame.paused)
        {
            snakeGame.update();
            update();
        }
    });
    snakeGameTimer->start(150);  // 每150毫秒更新一次
    
    // 初始化音效（如果 Multimedia 模块可用）
#ifdef QT_MULTIMEDIA_AVAILABLE
    // 尝试从多个路径加载音效文件
    QString soundBasePath;
    QStringList possiblePaths = {
        QDir::currentPath() + "/sounds",
        QDir::currentPath() + "/../sounds",
        QCoreApplication::applicationDirPath() + "/sounds",
        QCoreApplication::applicationDirPath() + "/../sounds",
        "sounds"
    };
    
    for (const QString& path : possiblePaths)
    {
        QFileInfo foodFile(path + "/food.wav");
        QFileInfo gameoverFile(path + "/gameover.wav");
        if (foodFile.exists() && gameoverFile.exists())
        {
            soundBasePath = path;
            break;
        }
    }
    
    foodSoundEffect = new QSoundEffect(this);
    if (!soundBasePath.isEmpty())
    {
        QString foodPath = QDir(soundBasePath).absoluteFilePath("food.wav");
        foodSoundEffect->setSource(QUrl::fromLocalFile(foodPath));
    }
    foodSoundEffect->setVolume(0.5f);
    
    gameOverSoundEffect = new QSoundEffect(this);
    if (!soundBasePath.isEmpty())
    {
        QString gameoverPath = QDir(soundBasePath).absoluteFilePath("gameover.wav");
        gameOverSoundEffect->setSource(QUrl::fromLocalFile(gameoverPath));
    }
    gameOverSoundEffect->setVolume(0.5f);
    
    // 设置贪吃蛇游戏的回调函数
    snakeGame.onFoodEaten = [this]() {
        if (foodSoundEffect && foodSoundEffect->status() == QSoundEffect::Ready)
        {
            foodSoundEffect->play();
        }
    };
    
    snakeGame.onGameOver = [this]() {
        if (gameOverSoundEffect && gameOverSoundEffect->status() == QSoundEffect::Ready)
        {
            gameOverSoundEffect->play();
        }
    };
#else
    // Multimedia 模块不可用，设置空回调
    snakeGame.onFoodEaten = nullptr;
    snakeGame.onGameOver = nullptr;
#endif
    
    // 设置焦点策略，以便接收键盘事件
    setFocusPolicy(Qt::StrongFocus);
    
    initializeLevel();
}

ColorBottleGame::~ColorBottleGame() 
{
    if (brushRenderer) 
    {
        delete brushRenderer;
    }
    if (hanoiAnimationTimer) 
    {
        hanoiAnimationTimer->stop();
    }
    if (snakeGameTimer) 
    {
        snakeGameTimer->stop();
    }
    // 音效对象由Qt的父子关系自动清理，无需手动删除
}

// ---------- UI 缩放：设计分辨率 1400×900，窗口等比缩放并居中 ----------
qreal ColorBottleGame::uiScale() const
{
    // 取宽高比中较小者，避免内容被裁切
    return qMin(static_cast<qreal>(width()) / kDesignWidth,
                static_cast<qreal>(height()) / kDesignHeight);
}

QPointF ColorBottleGame::uiOrigin() const
{
    // 缩放后在窗口内居中的左上角偏移（可能出现灰边）
    const qreal s = uiScale();
    return QPointF((width() - kDesignWidth * s) * 0.5,
                   (height() - kDesignHeight * s) * 0.5);
}

QPointF ColorBottleGame::mapToDesign(const QPointF& widgetPos) const
{
    // 控件像素 → 设计坐标（供点击/拖拽与绘制使用同一套坐标系）
    const qreal s = uiScale();
    if (s <= 0.0)
        return widgetPos;
    const QPointF o = uiOrigin();
    return QPointF((widgetPos.x() - o.x()) / s,
                   (widgetPos.y() - o.y()) / s);
}

void ColorBottleGame::applyUiTransform(QPainter& painter) const
{
    painter.translate(uiOrigin());
    painter.scale(uiScale(), uiScale());
}

void ColorBottleGame::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();  // 触发重绘以应用新缩放
}

// ---------- 输入：鼠标坐标先转设计坐标系再分发各模式 ----------
void ColorBottleGame::mousePressEvent(QMouseEvent* event) 
{
    const QPointF design = mapToDesign(event->position());
    float mouseX = static_cast<float>(design.x());
    float mouseY = static_cast<float>(design.y());

    // 野餐日：右键旋转 / 取回图块
    if (event->button() == Qt::RightButton && currentMode == MODE_PICNIC)
    {
        if (picnicShowShapeEditor)
            return;

        if (picnicDragging && picnicGame.getIsDragging())
        {
            picnicGame.rotateSelectedShape();
            update();
            return;
        }
        if (isPicnicBoardPoint(static_cast<int>(mouseX), static_cast<int>(mouseY)))
        {
            int cellX = (static_cast<int>(mouseX) - picnicBoardX) / picnicCellSize;
            int cellY = (static_cast<int>(mouseY) - picnicBoardY) / picnicCellSize;
            auto boardShape = picnicGame.getBoardCell(cellX, cellY);
            if (boardShape)
            {
                picnicGame.removeShapeAt(cellX, cellY);
                update();
            }
            return;
        }
        if (isPicnicPanelPoint(static_cast<int>(mouseX), static_cast<int>(mouseY)))
        {
            auto clickedShape = getPicnicShapeFromPanel(static_cast<int>(mouseX), static_cast<int>(mouseY));
            if (clickedShape)
            {
                picnicGame.incrementPanelRotation(clickedShape->getId());
                update();
            }
            return;
        }
    }

    if (event->button() == Qt::LeftButton) 
    {
        // 处理菜单栏点击
        if (mouseY <= menuBarHeight) 
        {
            float menu1X = 20, menu2X = 200, menu3X = 380, menu4X = 560, menu5X = 740;
            float menuWidth = 150;
            if (mouseX >= menu1X && mouseX <= menu1X + menuWidth)
             {
                currentMode = MODE_BOTTLE_COLOR;
                update();
                return;
            } else if (mouseX >= menu2X && mouseX <= menu2X + menuWidth) 
            {
                currentMode = MODE_NONOGRAM;
                update();
                return;
            } else if (mouseX >= menu3X && mouseX <= menu3X + menuWidth) 
            {
                currentMode = MODE_HANOI;
                update();
                return;
            } else if (mouseX >= menu4X && mouseX <= menu4X + menuWidth) 
            {
                currentMode = MODE_SNAKE;
                // 切换到贪吃蛇模式时，重置游戏
                if (snakeGame.gameOver)
                {
                    snakeGame.reset();
                }
                update();
                return;
            } else if (mouseX >= menu5X && mouseX <= menu5X + menuWidth)
            {
                currentMode = MODE_PICNIC;
                ensurePicnicSolveRecordsLoaded();
                if (picnicSolveRecordIndex < 0)
                    tryApplyRandomPicnicSolveRecord(picnicDifficulty);
                update();
                return;
            }
        }

        // 野餐日模式
        if (currentMode == MODE_PICNIC)
        {
            // 编辑器优先拦截点击
            if (picnicShowShapeEditor)
            {
                QRect titleRect(picnicEditorX, picnicEditorY, picnicEditorWidth, 35);
                if (titleRect.contains(static_cast<int>(mouseX), static_cast<int>(mouseY)))
                {
                    picnicEditorDragging = true;
                    picnicEditorDragOffsetX = static_cast<int>(mouseX) - picnicEditorX;
                    picnicEditorDragOffsetY = static_cast<int>(mouseY) - picnicEditorY;
                    update();
                    return;
                }
                handlePicnicShapeEditorClick(static_cast<int>(mouseX), static_cast<int>(mouseY));
                update();
                return;
            }

            // 侧栏按钮：绘制与点击共用 picnicSidebarX()，避免初级棋盘变窄后命中区错位
            if (handlePicnicSidebarClick(static_cast<int>(mouseX), static_cast<int>(mouseY)))
            {
                update();
                return;
            }

            if (isPicnicBoardPoint(static_cast<int>(mouseX), static_cast<int>(mouseY)))
            {
                int cellX = (static_cast<int>(mouseX) - picnicBoardX) / picnicCellSize;
                int cellY = (static_cast<int>(mouseY) - picnicBoardY) / picnicCellSize;

                if (picnicStoneEditMode)
                {
                    if (picnicGame.getBoardCell(cellX, cellY))
                    {
                        setPicnicFeedback(QString::fromUtf8("该格已有图块，请先取回再放石头"), 1800);
                    }
                    else
                    {
                        const bool nowStone = picnicGame.isStone(cellX, cellY);
                        picnicGame.toggleStone(cellX, cellY);
                        setPicnicFeedback(nowStone
                                              ? QString::fromUtf8("已移除石头")
                                              : QString::fromUtf8("已放置石头"),
                                          1200);
                    }
                    update();
                    return;
                }

                if (picnicGame.isStone(cellX, cellY))
                {
                    setPicnicFeedback(QString::fromUtf8("石头格不可放置图块"), 1200);
                    update();
                    return;
                }

                auto boardShape = picnicGame.getBoardCell(cellX, cellY);
                if (boardShape)
                {
                    // 先 startDrag 记录原位置，再移除，便于放置失败时恢复
                    picnicGame.startDrag(boardShape, static_cast<int>(mouseX), static_cast<int>(mouseY), false);
                    picnicGame.removeShapeAt(cellX, cellY);
                    picnicDragging = true;
                    picnicMouseX = static_cast<int>(mouseX);
                    picnicMouseY = static_cast<int>(mouseY);
                }
            }
            else if (!picnicStoneEditMode &&
                     isPicnicPanelPoint(static_cast<int>(mouseX), static_cast<int>(mouseY)))
            {
                auto clickedShape = getPicnicShapeFromPanel(static_cast<int>(mouseX), static_cast<int>(mouseY));
                if (clickedShape)
                {
                    picnicGame.startDrag(clickedShape, static_cast<int>(mouseX), static_cast<int>(mouseY), true);
                    picnicDragging = true;
                    picnicMouseX = static_cast<int>(mouseX);
                    picnicMouseY = static_cast<int>(mouseY);
                }
            }
            update();
            return;
        }
        
        // 棋盘填色游戏模式
        if (currentMode == MODE_NONOGRAM)
         {
            float startX = 100;
            float startY = menuBarHeight + 60;
            float cellSize = 40.0f;  // 增大单元格尺寸，与drawNonogramGame保持一致
            int rows = nonogramGame.rows;
            int cols = nonogramGame.cols;
            
            // 计算线索区域大小（与drawNonogramGame保持一致）
            float clueSpacingForLayout = 30.0f;  // 约束条件间距（与drawNonogramGame保持一致）
            float maxRowClueWidth = 0;
            float maxColClueHeight = 0;
            for (int r = 0; r < rows; r++) 
            {
                float width = static_cast<float>(nonogramGame.rowClues[r].size()) * clueSpacingForLayout;
                if (width > maxRowClueWidth) maxRowClueWidth = width;
            }
            for (int c = 0; c < cols; c++) 
            {
                float height = static_cast<float>(nonogramGame.colClues[c].size()) * clueSpacingForLayout;
                if (height > maxColClueHeight) maxColClueHeight = height;
            }
            
            float gridStartX = startX + maxRowClueWidth + 10;
            float gridStartY = startY + maxColClueHeight + 10;
            float buttonY = gridStartY + rows * cellSize + 30;
            
            // 检查编辑器按钮
            if (mouseY >= buttonY && mouseY <= buttonY + 30) 
            {
                if (mouseX >= gridStartX && mouseX <= gridStartX + 100)
                 {
                    nonogramEditorMode = !nonogramEditorMode;
                    update();
                    return;
                }
                // 检查求解按钮
                if (mouseX >= gridStartX + 120 && mouseX <= gridStartX + 220)
                 {
                    nonogramGame.solve();
                    // 求解后自动显示答案
                    if (!nonogramGame.solution.empty()) 
                    {
                        nonogramShowSolution = true;
                    }
                    update();
                    return;
                }
                // 检查显示答案按钮
                if (!nonogramGame.solution.empty() && 
                    mouseX >= gridStartX + 240 && mouseX <= gridStartX + 340) 
                    {
                    nonogramShowSolution = !nonogramShowSolution;
                    update();
                    return;
                }
            }
            
            // 编辑器模式：检查约束条件编辑按钮
            if (nonogramEditorMode) 
            {
                float clueSpacing = 30.0f;  // 约束条件间距（与drawNonogramGame保持一致）
                float buttonSize = 18.0f;  // 按钮大小（与drawNonogramGame保持一致）
                
                // 检查列约束的编辑按钮（放在相邻两列之间）
                for (int c = 0; c < cols ; c++) 
                {
                    float x = gridStartX + c * cellSize;
                    float y = startY;
                    
                    // 按钮放在两列之间中间位置
                    float buttonX = x + cellSize + (0*cellSize - 16.0f) * 0.5f;  // 与绘制代码保持一致
                    float btnSize = 16.0f;  // 与绘制代码保持一致
                    
                    // 检查每个约束项的+/-按钮（与绘制代码保持一致）
                    for (size_t i = 0; i < nonogramGame.colClues[c].size(); i++) 
                    {
                        float buttonY = y + i * clueSpacing;
                        
                        // +按钮
                        if (mouseX >= buttonX && mouseX <= buttonX + btnSize &&
                            mouseY >= buttonY && mouseY <= buttonY + clueSpacing * 0.4f) 
                        {
                            nonogramGame.colClues[c][i]++;
                            update();
                            return;
                        }
                        
                        // -按钮
                        if (mouseX >= buttonX && mouseX <= buttonX + btnSize &&
                            mouseY >= buttonY + clueSpacing * 0.42f && mouseY <= buttonY + clueSpacing * 0.82f) 
                        {
                            if (nonogramGame.colClues[c][i] > 0) 
                            {
                                nonogramGame.colClues[c][i]--;
                                if (nonogramGame.colClues[c][i] == 0 && nonogramGame.colClues[c].size() > 1) 
                                {
                                    nonogramGame.colClues[c].erase(nonogramGame.colClues[c].begin() + i);
                                }
                            }
                            update();
                            return;
                        }
                    }
                    
                    // 检查添加新项的按钮（与绘制代码保持一致）
                    float addButtonY = y + static_cast<float>(nonogramGame.colClues[c].size()) * clueSpacing;
                    if (mouseX >= buttonX && mouseX <= buttonX + btnSize &&
                        mouseY >= addButtonY && mouseY <= addButtonY + clueSpacing * 0.5f) 
                    {
                        nonogramGame.colClues[c].push_back(1);
                        update();
                        return;
                    }
                }
                
                // 检查行约束的编辑按钮（放在棋盘右侧，水平排列）
                float gridRightX = gridStartX + cols * cellSize;  // 棋盘右侧位置（与绘制代码保持一致）
                for (int r = 0; r < rows; r++) 
                {
                    float y = gridStartY + r * cellSize;
                    float buttonStartX = gridRightX + 5;  // 放在棋盘右侧，与绘制代码保持一致
                    float buttonY = y;
                    float btnSize = 16.0f;  // 与绘制代码保持一致
                    float buttonHeight = cellSize * 0.4f;  // 与绘制代码保持一致
                    float buttonSpacing = 3.0f;  // 与绘制代码保持一致
                    
                    int clueCount = static_cast<int>(nonogramGame.rowClues[r].size());
                    
                    // 为每个约束项检查+/-按钮（水平排列，从左到右对应约束索引0到size-1）
                    for (int i = 0; i < clueCount; i++)
                     {
                        float currentButtonX = buttonStartX + i * (btnSize + buttonSpacing);
                        int clueIndex = i;  // 按钮索引i对应约束项索引i（从左到右）
                        
                        // +按钮（上方）
                        if (mouseX >= currentButtonX && mouseX <= currentButtonX + btnSize &&
                            mouseY >= buttonY && mouseY <= buttonY + buttonHeight) 
                        {
                            nonogramGame.rowClues[r][clueIndex]++;
                            update();
                            return;
                        }
                        
                        // -按钮（下方）
                        if (mouseX >= currentButtonX && mouseX <= currentButtonX + btnSize &&
                            mouseY >= buttonY + buttonHeight + 2 && 
                            mouseY <= buttonY + buttonHeight * 2 + 2) 
                        {
                            if (nonogramGame.rowClues[r][clueIndex] > 0) 
                            {
                                nonogramGame.rowClues[r][clueIndex]--;
                                if (nonogramGame.rowClues[r][clueIndex] == 0 && nonogramGame.rowClues[r].size() > 1) 
                                {
                                    nonogramGame.rowClues[r].erase(nonogramGame.rowClues[r].begin() + clueIndex);
                                }
                            }
                            update();
                            return;
                        }
                    }
                    
                    // 检查添加新项的按钮（放在最右侧）
                    float addButtonX = buttonStartX + clueCount * (btnSize + buttonSpacing);
                    float addButtonHeight = cellSize * 0.2f;
                    if (mouseX >= addButtonX && mouseX <= addButtonX + btnSize &&
                        mouseY >= buttonY && mouseY <= buttonY + addButtonHeight) 
                    {
                        nonogramGame.rowClues[r].push_back(1);  // 添加到末尾，绘制时显示在最右侧
                        update();
                        return;
                    }
                }
            }
            
            // 编辑器模式：检查行数和列数调整按钮
            if (nonogramEditorMode) 
            {
                // 计算编辑器位置（与drawNonogramEditor中的计算保持一致）
                float gameStartY = menuBarHeight + 60;
                float cellSizeEditor = 40.0f;  // 增大单元格尺寸，与drawNonogramGame保持一致
                float clueSpacingEditor = 30.0f;  // 约束条件间距（与drawNonogramGame保持一致）
                float maxColClueHeight = 0;
                for (int c = 0; c < cols; c++)
                {
                    float height = static_cast<float>(nonogramGame.colClues[c].size()) * clueSpacingEditor;
                    if (height > maxColClueHeight) maxColClueHeight = height;
                }
                float gridStartYEditor = gameStartY + maxColClueHeight + 10;
                float buttonYEditor = gridStartYEditor + rows * cellSizeEditor + 30;
                
                float editorStartX = 100;
                float editorStartY = buttonYEditor + 50;  // 在棋盘按钮下方50像素处
                float rowControlY = editorStartY + 40;
                float colControlY = editorStartY + 80;
                
                // 检查行数减少按钮
                if (mouseY >= rowControlY && mouseY <= rowControlY + 30 &&
                    mouseX >= editorStartX + 130 && mouseX <= editorStartX + 160) 
                {
                    if (nonogramGame.rows > 1) 
                    {
                        nonogramGame.setSize(nonogramGame.rows - 1, nonogramGame.cols);
                        update();
                    }
                    return;
                }
                
                // 检查行数增加按钮
                if (mouseY >= rowControlY && mouseY <= rowControlY + 30 &&
                    mouseX >= editorStartX + 170 && mouseX <= editorStartX + 200) 
                {
                    if (nonogramGame.rows < 50) 
                    {
                        nonogramGame.setSize(nonogramGame.rows + 1, nonogramGame.cols);
                        update();
                    }
                    return;
                }
                
                // 检查列数减少按钮
                if (mouseY >= colControlY && mouseY <= colControlY + 30 &&
                    mouseX >= editorStartX + 130 && mouseX <= editorStartX + 160) 
                {
                    if (nonogramGame.cols > 1) 
                    {
                        nonogramGame.setSize(nonogramGame.rows, nonogramGame.cols - 1);
                        update();
                    }
                    return;
                }
                
                // 检查列数增加按钮
                if (mouseY >= colControlY && mouseY <= colControlY + 30 &&
                    mouseX >= editorStartX + 170 && mouseX <= editorStartX + 200) 
                {
                    if (nonogramGame.cols < 50) 
                    {
                        nonogramGame.setSize(nonogramGame.rows, nonogramGame.cols + 1);
                        update();
                    }
                    return;
                }
            }
            
            // 处理棋盘单元格点击
            int cellIndex = getNonogramCellAt(mouseX, mouseY);
            if (cellIndex >= 0) 
            {
                int row = cellIndex / cols;
                int col = cellIndex % cols;
                if (nonogramEditorMode)
                 {
                    // 编辑器模式：切换单元格状态（未涂色→涂色→X→未涂色）
                    if (nonogramGame.grid[row][col] == 0) 
                    {
                        nonogramGame.grid[row][col] = 1;  // 未涂色→涂色
                    } else if (nonogramGame.grid[row][col] == 1) 
                    {
                        nonogramGame.grid[row][col] = -1;  // 涂色→X
                    } else 
                    {
                        nonogramGame.grid[row][col] = 0;  // X→未涂色
                    }
                    nonogramGame.generateClues();
                } else {
                    // 游戏模式：切换单元格状态（未涂色→涂色→X→未涂色）
                    if (nonogramGame.grid[row][col] == 0) 
                    {
                        nonogramGame.grid[row][col] = 1;  // 未涂色→涂色
                    } else if (nonogramGame.grid[row][col] == 1) 
                    {
                        nonogramGame.grid[row][col] = -1;  // 涂色→X
                    } else 
                    {
                        nonogramGame.grid[row][col] = 0;  // X→未涂色
                    }
                }
                update();
                return;
            }
            return;
        }
        
        // 汉诺塔游戏模式
        if (currentMode == MODE_HANOI) 
        {
            float startX = 100;
            float startY = menuBarHeight + 80;
            float rodWidth = 15.0f;
            float rodHeight = 400.0f;
            float baseHeight = 30.0f;
            float baseWidth = 900.0f;
            float rodSpacing = 300.0f;
            float rod1X = startX + 150;
            float baseY = startY + rodHeight;
            float buttonY = baseY + baseHeight + 60;
            float buttonWidth = 120;
            float buttonHeight = 35;
            float buttonSpacing = 20;
            
            // 检查重置按钮
            float resetButtonX = startX;
            if (mouseY >= buttonY && mouseY <= buttonY + buttonHeight &&
                mouseX >= resetButtonX && mouseX <= resetButtonX + buttonWidth) 
            {
                hanoiGame.reset(hanoiDiskCount);
                hanoiGame.isAnimating = false;
                if (hanoiAnimationTimer) 
                {
                    hanoiAnimationTimer->stop();
                }
                update();
                return;
            }
            
            // 检查自动求解按钮
            float solveButtonX = resetButtonX + buttonWidth + buttonSpacing;
            if (!hanoiGame.isAnimating &&
                mouseY >= buttonY && mouseY <= buttonY + buttonHeight &&
                mouseX >= solveButtonX && mouseX <= solveButtonX + buttonWidth) 
            {
                hanoiGame.reset(hanoiDiskCount);
                hanoiGame.generateSolution();
                hanoiGame.isAnimating = true;
                hanoiGame.currentMoveIndex = 0;
                if (hanoiAnimationTimer) 
                {
                    hanoiAnimationTimer->start(500);  // 每500ms执行一步
                }
                update();
                return;
            }
            
            // 检查圆盘数量调整按钮
            float diskCountX = solveButtonX + buttonWidth + buttonSpacing + 50;
            float countX = diskCountX + 80;
            float decX = countX + 50;
            float incX = countX + 90;
            
            if (mouseY >= buttonY && mouseY <= buttonY + buttonHeight) 
            {
                // 减少按钮
                if (mouseX >= decX && mouseX <= decX + 30) 
                {
                    if (hanoiDiskCount > 1 && !hanoiGame.isAnimating) 
                    {
                        hanoiDiskCount--;
                        hanoiGame.reset(hanoiDiskCount);
                        update();
                    }
                    return;
                }
                // 增加按钮
                if (mouseX >= incX && mouseX <= incX + 30) 
                {
                    if (hanoiDiskCount < 8 && !hanoiGame.isAnimating) 
                    {
                        hanoiDiskCount++;
                        hanoiGame.reset(hanoiDiskCount);
                        update();
                    }
                    return;
                }
            }
            
            // 检查柱子点击（用于手动移动）
            if (!hanoiGame.isAnimating) 
            {
                float diskHeight = 30.0f;
                float maxDiskWidth = 200.0f;
                float minDiskWidth = 60.0f;
                
                for (int rodIndex = 0; rodIndex < 3; rodIndex++) 
                {
                    float rodX = rod1X + rodIndex * rodSpacing;
                    float rodLeft = rodX - rodWidth / 2 - 10;
                    float rodRight = rodX + rodWidth / 2 + 10;
                    float rodTop = startY - 10;
                    float rodBottom = baseY + 10;
                    
                    if (mouseX >= rodLeft && mouseX <= rodRight &&
                        mouseY >= rodTop && mouseY <= rodBottom) 
                    {
                        if (hanoiGame.selectedRod == -1) 
                        {
                            // 选择柱子
                            if (!hanoiGame.rods[rodIndex].empty()) 
                            {
                                hanoiGame.selectedRod = rodIndex;
                                update();
                            }
                        } else 
                        {
                            // 移动圆盘
                            if (hanoiGame.selectedRod != rodIndex) 
                            {
                                hanoiGame.move(hanoiGame.selectedRod, rodIndex);
                            }
                            hanoiGame.selectedRod = -1;
                            update();
                        }
                        return;
                    }
                }
            }
            return;
        }
        
        // 瓶子游戏模式（原有逻辑）
        if (gameWon || gameOver)
         {
            // 点击任意位置进入下一关或重新开始
            if (gameWon && currentLevel < maxLevels) 
            {
                currentLevel++;
                initializeLevel();
            } else if (gameWon && currentLevel >= maxLevels)
             {
                // 游戏完成
                currentLevel = 1;
                initializeLevel();
            } else if (gameOver)
             {
                // 重新开始当前关卡
                initializeLevel();
            }
        } else if (showingResult)
         {
            // 点击继续下一轮，新起一行
            showingResult = false;
            currentAttemptRow++;
            
            // 保存当前尝试的历史（记录未匹配瓶子的位置，用于显示透明瓶子）
            std::vector<QColor> attemptRow(bottleCount, QColor(0, 0, 0, 0));  // 初始化为不显示
            for (int i = 0; i < bottleCount; i++) 
            {
                if (!matchedBottles[i]) 
                {
                    attemptRow[i] = QColor(Qt::transparent);  // 未匹配位置显示透明瓶子
                }
            }
            attemptHistory.push_back(attemptRow);
            
            // 保存当前尝试的错误涂色（一滩保留在上一次尝试的那一行，不能被擦除）
            std::vector<QColor> attemptWrong = wrongColors;
            attemptWrongColors.push_back(attemptWrong);
            
            // 清除当前错误涂色记录（但已保存在attemptWrongColors中）
            for (int i = 0; i < bottleCount; i++) 
            {
                if (!matchedBottles[i]) 
                {
                    wrongColors[i] = QColor(Qt::transparent);
                }
            }
            
            // 刷新颜色选择面板：未匹配的颜色重新刷新在选择面板
            refreshAvailableColors();
        } else
         {
            // 选择颜色
            int colorIndex = getColorIndexAt(mouseX, mouseY);
            if (colorIndex >= 0 && colorIndex < static_cast<int>(availableColors.size())) 
            {
                // 找到第一个未上色且未匹配的瓶子
                for (int i = 0; i < bottleCount; i++) 
                {
                    if (userBottles[i] == QColor(Qt::transparent) && !matchedBottles[i]) 
                    {
                        userBottles[i] = availableColors[colorIndex];
                        break;
                    }
                }
                
                // 检查是否所有未匹配的瓶子都已上色
                if (isAllBottlesColored()) 
                {
                    checkMatch();
                }
            }
        }
        
        update();  // 触发重绘
    }
}

void ColorBottleGame::keyPressEvent(QKeyEvent* event)
{
    if (currentMode == MODE_PICNIC)
    {
        if (event->key() == Qt::Key_Escape && picnicEnumerateRunning)
        {
            picnicEnumerateCancel = true;
            return;
        }
        if (event->key() == Qt::Key_E)
        {
            if (picnicShowShapeEditor)
            {
                picnicShowShapeEditor = false;
            }
            else
            {
                initializePicnicShapeEditor();
                picnicShowShapeEditor = true;
            }
            update();
            return;
        }
        if (event->key() == Qt::Key_R && picnicDragging && picnicGame.getIsDragging())
        {
            picnicGame.rotateSelectedShape();
            update();
            return;
        }
        if (event->key() == Qt::Key_PageUp)
        {
            navigatePicnicSolveRecord(-1);
            update();
            return;
        }
        if (event->key() == Qt::Key_PageDown)
        {
            navigatePicnicSolveRecord(+1);
            update();
            return;
        }
    }
    if (currentMode == MODE_SNAKE)
    {
        switch (event->key())
        {
            case Qt::Key_Up:
            case Qt::Key_W:
                snakeGame.setDirection(SnakeGame::UP);
                break;
            case Qt::Key_Down:
            case Qt::Key_S:
                snakeGame.setDirection(SnakeGame::DOWN);
                break;
            case Qt::Key_Left:
            case Qt::Key_A:
                snakeGame.setDirection(SnakeGame::LEFT);
                break;
            case Qt::Key_Right:
            case Qt::Key_D:
                snakeGame.setDirection(SnakeGame::RIGHT);
                break;
            case Qt::Key_Space:
                if (snakeGame.gameOver)
                {
                    snakeGame.reset();
                }
                else
                {
                    snakeGame.paused = !snakeGame.paused;
                }
                update();
                break;
        }
    }
    QWidget::keyPressEvent(event);
}

void ColorBottleGame::mouseMoveEvent(QMouseEvent* event)
{
    const QPointF design = mapToDesign(event->position());
    picnicMouseX = static_cast<int>(design.x());
    picnicMouseY = static_cast<int>(design.y());
    if (currentMode == MODE_PICNIC && picnicEditorDragging && picnicShowShapeEditor)
    {
        picnicEditorX = picnicMouseX - picnicEditorDragOffsetX;
        picnicEditorY = picnicMouseY - picnicEditorDragOffsetY;
        update();
    }
    else if (currentMode == MODE_PICNIC && picnicDragging && picnicGame.getIsDragging())
    {
        picnicGame.updateDrag(picnicMouseX, picnicMouseY);
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void ColorBottleGame::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && currentMode == MODE_PICNIC)
    {
        if (picnicEditorDragging)
        {
            picnicEditorDragging = false;
            update();
            return;
        }
        if (picnicDragging)
        {
            const QPointF design = mapToDesign(event->position());
            int mx = static_cast<int>(design.x());
            int my = static_cast<int>(design.y());
            if (isPicnicBoardPoint(mx, my))
            {
                // endDrag 使用棋盘相对像素坐标
                bool ok = picnicGame.endDrag(mx - picnicBoardX, my - picnicBoardY);
                if (!ok)
                    setPicnicFeedback(QString::fromUtf8("无法放置到该位置"), 1500);
            }
            else
            {
                picnicGame.cancelDrag();
                setPicnicFeedback(QString::fromUtf8("已取消放置"), 1200);
            }
            picnicDragging = false;
            update();
        }
    }
    QWidget::mouseReleaseEvent(event);
}

// ---------- 主绘制：窗口底色 → 设计区变换 → 菜单 → 当前游戏模式 ----------
void ColorBottleGame::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    
    // 窗口背景（等比缩放时两侧/上下可能留边）
    painter.fillRect(rect(), QColor(210, 210, 210));

    applyUiTransform(painter);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    // 设计分辨率内容区背景
    painter.fillRect(QRectF(0, 0, kDesignWidth, kDesignHeight), QColor(240, 240, 240));
    
    // 绘制菜单栏
    drawMenuBar(painter);
    
    // 根据模式绘制不同游戏
    if (currentMode == MODE_NONOGRAM) 
    {
        drawNonogramGame(painter);
        if (nonogramEditorMode) 
        {
            drawNonogramEditor(painter);
        }
    } else if (currentMode == MODE_HANOI) 
    {
        drawHanoiGame(painter);
    } else if (currentMode == MODE_SNAKE)
    {
        drawSnakeGame(painter);
    } else if (currentMode == MODE_PICNIC)
    {
        drawPicnicGame(painter);
    } else 
    {
        // 瓶子游戏模式
        // 绘制标题和关卡信息
        painter.setPen(Qt::black);
        painter.setFont(QFont("Arial", 30));
        painter.drawText(QRectF(50, menuBarHeight + 20, 600, 40), 
                        "Color Bottle Game - Level " + QString::number(currentLevel));
    
        // 绘制尝试次数（与下方「Hidden Bottles」拉开间距，避免遮挡）
        painter.setFont(QFont("Arial", 20));
        const float attemptsTextY = menuBarHeight + 65;
        painter.drawText(QRectF(50, attemptsTextY, 300, 30), 
                        "Attempts: " + QString::number(remainingAttempts) + "/" + QString::number(maxAttempts));
        
        // 绘制隐藏瓶子（顶部）- 成功时显示真实颜色
        const float hiddenLabelY = attemptsTextY + 45;
        float adjustedStartY = hiddenLabelY + 32;
        painter.setFont(QFont("Arial", 18));
        painter.drawText(QRectF(startX, hiddenLabelY, 200, 30), "Hidden Bottles:");
    
        for (int i = 0; i < bottleCount; i++) 
        {
            float x = startX + i * (bottleSize + bottleSpacing);
            // 如果游戏胜利，显示真实颜色
            bool showReal = gameWon;
            drawBottle(painter, x, adjustedStartY, hiddenBottles[i], matchedBottles[i], !showReal, showReal);
        }
        
        // 尝试区布局：
        //   rowPitch      —— 行距（瓶高 + 8，比早期更紧）
        //   attemptColWidth —— 一列宽度（整排瓶子 + 列间距）
        //   超出 maxAttemptBottom 时 attemptCol++，从顶部另开一列向右排
        const float rowPitch = bottleSize * 1.5f + 8.0f;
        const float labelOffset = 22.0f;
        const float attemptColWidth =
            bottleCount * (bottleSize + bottleSpacing) + 36.0f;
        const float attemptsTop = adjustedStartY + bottleSize * 1.5f + 36.0f;
        const float maxAttemptBottom = static_cast<float>(kDesignHeight) - 90.0f;

        int attemptCol = 0;
        float currentY = attemptsTop;
        auto attemptBaseX = [&]() {
            return startX + attemptCol * attemptColWidth;
        };
        auto advanceAttemptRow = [&]() {
            currentY += rowPitch;
            if (currentY + bottleSize * 1.5f > maxAttemptBottom)
            {
                ++attemptCol;
                currentY = attemptsTop;
            }
        };

        // 历史尝试：仅在仍未匹配的列位置画透明瓶或错误飞溅（已匹配列留空对齐）
        for (size_t attempt = 0; attempt < attemptHistory.size(); attempt++)
        {
            const float baseX = attemptBaseX();
            painter.setPen(QColor(100, 100, 100));
            painter.setFont(QFont("Arial", 14));
            painter.drawText(QRectF(baseX, currentY - labelOffset, 200, 22),
                             "Attempt " + QString::number(attempt + 1) + ":");

            for (int i = 0; i < bottleCount; i++)
            {
                if (!matchedBottles[i] && attemptHistory[attempt][i] == QColor(Qt::transparent))
                {
                    float x = baseX + i * (bottleSize + bottleSpacing);
                    if (attempt < attemptWrongColors.size() &&
                        attemptWrongColors[attempt][i] != QColor(Qt::transparent))
                    {
                        drawWrongColorPuddle(painter, x, currentY, attemptWrongColors[attempt][i]);
                    }
                    else
                    {
                        drawBottle(painter, x, currentY, QColor(Qt::transparent), false, false, false);
                    }
                }
            }
            advanceAttemptRow();
        }

        // 绘制用户瓶子（当前尝试）
        {
            const float baseX = attemptBaseX();
            painter.setPen(Qt::black);
            painter.setFont(QFont("Arial", 16));
            painter.drawText(QRectF(baseX, currentY - labelOffset, 200, 22), "Current Attempt:");

            for (int i = 0; i < bottleCount; i++)
            {
                float x = baseX + i * (bottleSize + bottleSpacing);
                if (matchedBottles[i])
                {
                    drawBottle(painter, x, currentY, userBottles[i], true, false, false);
                }
                else if (wrongColors[i] != QColor(Qt::transparent))
                {
                    drawWrongColorPuddle(painter, x, currentY, wrongColors[i]);
                }
                else
                {
                    drawBottle(painter, x, currentY, userBottles[i], false, false, false);
                }
            }
        }

        // 绘制颜色选择面板
        drawColorPalette(painter);
    
    // 状态提示：底部半透明横幅，避免与瓶子/尝试行互相遮挡
    auto drawStatusBanner = [&](const QString& text, const QColor& textColor, int pointSize) {
        painter.setFont(QFont("Arial", pointSize, QFont::Bold));
        const QFontMetrics fm(painter.font());
        const int padX = 28;
        const int padY = 14;
        const int textW = fm.horizontalAdvance(text) + padX * 2;
        const int textH = fm.height() + padY * 2;
        const int bx = qMax(20, (kDesignWidth - textW) / 2);
        const int by = kDesignHeight - textH - 36;
        painter.setPen(QPen(QColor(0, 0, 0, 50), 1));
        painter.setBrush(QColor(255, 255, 255, 235));
        painter.drawRoundedRect(bx, by, textW, textH, 10, 10);
        painter.setPen(textColor);
        painter.drawText(QRect(bx, by, textW, textH), Qt::AlignCenter, text);
    };

    if (gameWon)
    {
        if (currentLevel >= maxLevels)
            drawStatusBanner(QStringLiteral("Congratulations! All levels complete! Click to restart..."),
                             QColor(30, 90, 220), 26);
        else
            drawStatusBanner(QStringLiteral("Level Complete! Click to continue..."),
                             QColor(0, 140, 40), 28);
    }
    else if (gameOver)
    {
        drawStatusBanner(QStringLiteral("Game Over! Click to retry..."),
                         QColor(200, 30, 30), 28);
    }
    else if (showingResult)
    {
        bool hasMatch = false;
        for (bool matched : matchedBottles)
        {
            if (matched) { hasMatch = true; break; }
        }
        if (hasMatch)
            drawStatusBanner(QStringLiteral("Some bottles matched! Click to continue..."),
                             QColor(0, 140, 40), 22);
        else
            drawStatusBanner(QStringLiteral("No matches! Click to try again..."),
                             QColor(200, 30, 30), 22);
    }
    }
}