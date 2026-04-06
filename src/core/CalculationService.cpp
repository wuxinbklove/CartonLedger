#include "core/CalculationService.h"

#include <QRegularExpression>

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace cartonledger {

namespace {

constexpr int kAutomaticAmountPrecision = 2;
constexpr int kMaxStoredPricePrecision = 12;

int automaticAmountPrecision()
{
    return kAutomaticAmountPrecision;
}

double sanitizeDimension(double value)
{
    return value < 0.0 ? 0.0 : value;
}

int sanitizeQuantity(int value)
{
    return value < 0 ? 0 : value;
}

QString formatDimensionToken(double value)
{
    QString token = QString::number(CalculationService::roundTo(value, 2), 'f', 2);
    while (token.endsWith(QLatin1Char('0'))) {
        token.chop(1);
    }
    if (token.endsWith(QLatin1Char('.'))) {
        token.chop(1);
    }
    return token;
}

int sanitizePricePrecision(int precision)
{
    return std::clamp(precision, 0, kMaxStoredPricePrecision);
}

std::optional<double> rawCalculatedUnitPrice(const StatementEntry &entry, FormulaType formulaType)
{
    if (formulaType == FormulaType::Manual) {
        return sanitizeDimension(entry.manualUnitPrice);
    }

    double length = sanitizeDimension(entry.lengthCm);
    double width = sanitizeDimension(entry.widthCm);
    double height = sanitizeDimension(entry.heightCm);
    const double pricePerSquareMeter = sanitizeDimension(entry.pricePerSquareMeter);

    const QString specification = CalculationService::normalizeSpecification(entry.specification);
    if (!specification.isEmpty()) {
        const auto parsed = CalculationService::parseSpecification(specification);
        if (!parsed.has_value() || !CalculationService::isFormulaAllowed(specification, formulaType)) {
            return std::nullopt;
        }

        length = parsed->lengthCm;
        width = parsed->widthCm;
        height = parsed->heightCm.value_or(0.0);
    }

    if (length <= 0.0 || width <= 0.0) {
        return std::nullopt;
    }

    double areaFactor = 0.0;
    switch (formulaType) {
    case FormulaType::A:
        areaFactor = (length + width + 4.0) * (width + height + 2.0) * 2.0;
        break;
    case FormulaType::B:
        areaFactor = (length + width + 4.0) * (width + height + width + 2.0) * 2.0;
        break;
    case FormulaType::C:
        areaFactor = length * width;
        break;
    case FormulaType::Manual:
        return sanitizeDimension(entry.manualUnitPrice);
    }

    return areaFactor * pricePerSquareMeter / 10000.0;
}

bool matchesAtRetainedPrecision(double exactValue, double enteredValue, int maxPrecision)
{
    const int normalizedPrecision = sanitizePricePrecision(maxPrecision);
    const double roundedEntered = CalculationService::roundTo(enteredValue, normalizedPrecision);
    const double roundedExact = CalculationService::roundTo(exactValue, normalizedPrecision);
    if (qAbs(roundedExact - roundedEntered) < 1e-9) {
        return true;
    }

    if (normalizedPrecision <= 0) {
        return false;
    }

    const double step = std::pow(10.0, -static_cast<double>(normalizedPrecision));
    return qAbs((roundedExact - step) - roundedEntered) < 1e-9;
}

} // namespace

std::optional<ParsedSpecification> CalculationService::parseSpecification(const QString &value)
{
    const QString normalized = normalizeSpecification(value);
    if (normalized.isEmpty()) {
        return std::nullopt;
    }

    const QStringList parts = normalized.split(QLatin1Char('*'), Qt::KeepEmptyParts);
    if (parts.size() != 2 && parts.size() != 3) {
        return std::nullopt;
    }

    ParsedSpecification parsed;
    bool ok = false;
    parsed.lengthCm = parts.at(0).toDouble(&ok);
    if (!ok || parsed.lengthCm <= 0.0) {
        return std::nullopt;
    }

    parsed.widthCm = parts.at(1).toDouble(&ok);
    if (!ok || parsed.widthCm <= 0.0) {
        return std::nullopt;
    }

    if (parts.size() == 3) {
        const double heightCm = parts.at(2).toDouble(&ok);
        if (!ok || heightCm <= 0.0) {
            return std::nullopt;
        }
        parsed.heightCm = heightCm;
    }

    return parsed;
}

std::optional<ParsedPriceValue> CalculationService::parsePriceValue(const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }

    bool ok = false;
    const double parsedValue = trimmed.toDouble(&ok);
    if (!ok || parsedValue < 0.0) {
        return std::nullopt;
    }

    int precision = 0;
    const int dotIndex = trimmed.indexOf(QLatin1Char('.'));
    if (dotIndex >= 0) {
        precision = trimmed.size() - dotIndex - 1;
    }

    return ParsedPriceValue { parsedValue, sanitizePricePrecision(precision) };
}

QString CalculationService::normalizeSpecification(const QString &value)
{
    QString normalized = value.trimmed();
    normalized.replace(QRegularExpression(QStringLiteral("\\s*[*xX×]\\s*")), QStringLiteral("*"));
    return normalized;
}

QString CalculationService::specificationFromDimensions(double lengthCm, double widthCm, double heightCm)
{
    const double length = sanitizeDimension(lengthCm);
    const double width = sanitizeDimension(widthCm);
    const double height = sanitizeDimension(heightCm);

    if (length <= 0.0 || width <= 0.0) {
        return {};
    }

    QString specification = formatDimensionToken(length) + QStringLiteral("*") + formatDimensionToken(width);
    if (height > 0.0) {
        specification += QStringLiteral("*") + formatDimensionToken(height);
    }

    return specification;
}

QStringList CalculationService::allowedFormulaOptions(const QString &specification)
{
    if (normalizeSpecification(specification).isEmpty()) {
        return {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C"), formulaTypeDisplay(FormulaType::Manual)};
    }

    const auto parsed = parseSpecification(specification);
    if (!parsed.has_value()) {
        return {};
    }

    if (parsed->dimensionCount() == 2) {
        return {QStringLiteral("C"), formulaTypeDisplay(FormulaType::Manual)};
    }

    return {QStringLiteral("A"), QStringLiteral("B"), formulaTypeDisplay(FormulaType::Manual)};
}

bool CalculationService::isFormulaAllowed(const QString &specification, FormulaType formulaType)
{
    if (normalizeSpecification(specification).isEmpty()) {
        return true;
    }

    const auto parsed = parseSpecification(specification);
    if (!parsed.has_value()) {
        return false;
    }

    if (formulaType == FormulaType::Manual) {
        return true;
    }

    if (parsed->dimensionCount() == 2) {
        return formulaType == FormulaType::C;
    }

    return formulaType == FormulaType::A || formulaType == FormulaType::B;
}

std::optional<FormulaType> CalculationService::inferFormulaTypeForUnitPrice(const StatementEntry &entry, double unitPrice)
{
    const int enteredPrecision = entry.manualUnitPricePrecision >= 0
        ? sanitizePricePrecision(entry.manualUnitPricePrecision)
        : inferPricePrecision(unitPrice);

    const auto matchesFormula = [&entry, unitPrice, enteredPrecision](FormulaType formulaType) {
        const auto expectedUnitPrice = rawCalculatedUnitPrice(entry, formulaType);
        if (!expectedUnitPrice.has_value()) {
            return false;
        }

        return matchesAtRetainedPrecision(expectedUnitPrice.value(), unitPrice, enteredPrecision);
    };

    const QString specification = normalizeSpecification(entry.specification);
    if (!specification.isEmpty()) {
        const auto parsed = parseSpecification(specification);
        if (!parsed.has_value()) {
            return std::nullopt;
        }

        if (parsed->dimensionCount() == 2) {
            return matchesFormula(FormulaType::C) ? std::optional<FormulaType>(FormulaType::C) : std::nullopt;
        }

        if (matchesFormula(FormulaType::A)) {
            return FormulaType::A;
        }
        if (matchesFormula(FormulaType::B)) {
            return FormulaType::B;
        }
        return std::nullopt;
    }

    const double length = sanitizeDimension(entry.lengthCm);
    const double width = sanitizeDimension(entry.widthCm);
    const double height = sanitizeDimension(entry.heightCm);
    if (length <= 0.0 || width <= 0.0) {
        return std::nullopt;
    }

    if (height <= 0.0) {
        return matchesFormula(FormulaType::C) ? std::optional<FormulaType>(FormulaType::C) : std::nullopt;
    }

    if (matchesFormula(FormulaType::A)) {
        return FormulaType::A;
    }
    if (matchesFormula(FormulaType::B)) {
        return FormulaType::B;
    }

    return std::nullopt;
}

bool CalculationService::isSpecificationValid(const QString &specification)
{
    return parseSpecification(specification).has_value();
}

int CalculationService::effectivePricePrecision(const StatementEntry &entry)
{
    if (entry.pricePrecision >= 0) {
        return sanitizePricePrecision(entry.pricePrecision);
    }

    return inferPricePrecision(entry.pricePerSquareMeter);
}

int CalculationService::effectiveManualUnitPricePrecision(const StatementEntry &entry)
{
    if (entry.manualUnitPricePrecision >= 0) {
        return sanitizePricePrecision(entry.manualUnitPricePrecision);
    }

    return inferPricePrecision(entry.manualUnitPrice);
}

int CalculationService::effectiveAmountPrecision(const StatementEntry &entry)
{
    if (entry.formulaType == FormulaType::Manual) {
        return effectiveManualUnitPricePrecision(entry);
    }

    if (entry.manualUnitPricePrecision > automaticAmountPrecision()) {
        return sanitizePricePrecision(entry.manualUnitPricePrecision);
    }

    return automaticAmountPrecision();
}

int CalculationService::inferPricePrecision(double value)
{
    QString text = QString::number(roundTo(value, kMaxStoredPricePrecision), 'f', kMaxStoredPricePrecision);
    while (text.endsWith(QLatin1Char('0'))) {
        text.chop(1);
    }
    if (text.endsWith(QLatin1Char('.'))) {
        text.chop(1);
    }

    const int dotIndex = text.indexOf(QLatin1Char('.'));
    if (dotIndex < 0) {
        return 0;
    }

    return sanitizePricePrecision(text.size() - dotIndex - 1);
}

QString CalculationService::formatAmount(double value, int decimals)
{
    const int normalizedDecimals = sanitizePricePrecision(decimals);
    return QString::number(roundTo(value, normalizedDecimals), 'f', normalizedDecimals);
}

double CalculationService::calculateUnitPrice(const StatementEntry &entry)
{
    if (entry.formulaType == FormulaType::Manual) {
        return roundTo(sanitizeDimension(entry.manualUnitPrice), effectiveAmountPrecision(entry));
    }

    const auto rawUnitPrice = rawCalculatedUnitPrice(entry, entry.formulaType);
    if (!rawUnitPrice.has_value()) {
        return 0.0;
    }

    return roundTo(rawUnitPrice.value(), effectiveAmountPrecision(entry));
}

double CalculationService::calculateTotalPrice(const StatementEntry &entry)
{
    const double unitPrice = calculateUnitPrice(entry);
    const int quantity = sanitizeQuantity(entry.quantity);
    return roundTo(unitPrice * static_cast<double>(quantity), effectiveAmountPrecision(entry));
}

QString CalculationService::formatSpecification(const StatementEntry &entry)
{
    const QString specification = normalizeSpecification(entry.specification);
    if (!specification.isEmpty()) {
        return specification;
    }

    return specificationFromDimensions(entry.lengthCm, entry.widthCm, entry.heightCm);
}

double CalculationService::roundTo(double value, int decimals)
{
    const double factor = std::pow(10.0, static_cast<double>(decimals));
    return std::round(value * factor) / factor;
}

} // namespace cartonledger
