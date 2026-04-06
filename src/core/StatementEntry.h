#pragma once

#include <QDate>
#include <QString>

#include <optional>

namespace cartonledger {

enum class FormulaType {
    A,
    B,
    C,
    Manual,
};

struct StatementEntry {
    qint64 id = -1;
    QDate deliveryDate;
    QString orderNumber;
    QString specification;
    double lengthCm = 0.0;
    double widthCm = 0.0;
    double heightCm = 0.0;
    int quantity = 1;
    FormulaType formulaType = FormulaType::A;
    double pricePerSquareMeter = 2.15;
    int pricePrecision = 2;
    double manualUnitPrice = 0.0;
    int manualUnitPricePrecision = -1;
};

inline QString formulaTypeDisplay(FormulaType formulaType)
{
    switch (formulaType) {
    case FormulaType::A:
        return QStringLiteral("A");
    case FormulaType::B:
        return QStringLiteral("B");
    case FormulaType::C:
        return QStringLiteral("C");
    case FormulaType::Manual:
        return QStringLiteral("手动");
    }

    return QStringLiteral("A");
}

inline QString formulaTypeHintText()
{
    return QStringLiteral(
        "A = ((长 + 宽 + 4) * (宽 + 高 + 2) * 2) * 每平方单价 / 10000\n"
        "B = ((长 + 宽 + 4) * (宽 + 高 + 宽 + 2) * 2) * 每平方单价 / 10000\n"
        "C = 长 * 宽 * 每平方单价 / 10000\n"
        "手动 = 直接输入单价，总价按 单价 * 数量 计算");
}

inline std::optional<FormulaType> formulaTypeFromString(const QString &value)
{
    const QString normalized = value.trimmed().toUpper();
    if (normalized == QStringLiteral("A")) {
        return FormulaType::A;
    }
    if (normalized == QStringLiteral("B")) {
        return FormulaType::B;
    }
    if (normalized == QStringLiteral("C")) {
        return FormulaType::C;
    }
    if (normalized == QStringLiteral("手动") || normalized == QStringLiteral("MANUAL")) {
        return FormulaType::Manual;
    }

    return std::nullopt;
}

} // namespace cartonledger
