#include "ui/PasteColumnResolver.h"

#include "core/CalculationService.h"
#include "ui/models/EntryTableModel.h"

#include <QRegularExpression>

namespace cartonledger {

namespace {

bool looksLikePlainInteger(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^\d+$)"));
    return pattern.match(text.trimmed()).hasMatch();
}

QVector<int> contiguousColumns(int startColumn, qsizetype count)
{
    QVector<int> columns;
    columns.reserve(count);
    for (qsizetype index = 0; index < count; ++index) {
        columns.append(startColumn + static_cast<int>(index));
    }
    return columns;
}

} // namespace

QVector<int> resolvePasteTargetColumns(int startColumn, const QStringList &cells)
{
    QVector<int> columns = contiguousColumns(startColumn, cells.size());

    if (startColumn != EntryTableModel::SpecificationColumn || cells.size() != 3) {
        return columns;
    }

    const QString specificationText = cells.at(0).trimmed();
    const QString pricePerSquareMeterText = cells.at(1).trimmed();
    const QString unitPriceText = cells.at(2).trimmed();

    if (!CalculationService::parseSpecification(specificationText).has_value()) {
        return columns;
    }

    if (!CalculationService::parsePriceValue(pricePerSquareMeterText).has_value()
        || !CalculationService::parsePriceValue(unitPriceText).has_value()) {
        return columns;
    }

    if (looksLikePlainInteger(pricePerSquareMeterText)) {
        return columns;
    }

    return {
        EntryTableModel::SpecificationColumn,
        EntryTableModel::PricePerSquareMeterColumn,
        EntryTableModel::UnitPriceColumn,
    };
}

} // namespace cartonledger