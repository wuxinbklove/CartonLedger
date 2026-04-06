#pragma once

#include "core/StatementEntry.h"

#include <QString>
#include <QStringList>

#include <optional>

namespace cartonledger {

struct ParsedSpecification {
    double lengthCm = 0.0;
    double widthCm = 0.0;
    std::optional<double> heightCm;

    int dimensionCount() const
    {
        return heightCm.has_value() ? 3 : 2;
    }
};

struct ParsedPriceValue {
    double value = 0.0;
    int precision = 0;
};

class CalculationService {
public:
    static std::optional<ParsedSpecification> parseSpecification(const QString &value);
    static std::optional<ParsedPriceValue> parsePriceValue(const QString &value);
    static QString normalizeSpecification(const QString &value);
    static QString specificationFromDimensions(double lengthCm, double widthCm, double heightCm);
    static QStringList allowedFormulaOptions(const QString &specification);
    static bool isFormulaAllowed(const QString &specification, FormulaType formulaType);
    static std::optional<FormulaType> inferFormulaTypeForUnitPrice(const StatementEntry &entry, double unitPrice);
    static bool isSpecificationValid(const QString &specification);
    static int effectivePricePrecision(const StatementEntry &entry);
    static int effectiveManualUnitPricePrecision(const StatementEntry &entry);
    static int effectiveAmountPrecision(const StatementEntry &entry);
    static int inferPricePrecision(double value);
    static QString formatAmount(double value, int decimals);
    static double calculateUnitPrice(const StatementEntry &entry);
    static double calculateTotalPrice(const StatementEntry &entry);
    static QString formatSpecification(const StatementEntry &entry);
    static double roundTo(double value, int decimals);
};

} // namespace cartonledger
