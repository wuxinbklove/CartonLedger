#include "export/EntryExportFormatter.h"

#include "core/CalculationService.h"

namespace cartonledger {

QStringList exportColumnHeaders()
{
    return {
        QStringLiteral("计算公式"),
        QStringLiteral("送货日期"),
        QStringLiteral("订单号"),
        QStringLiteral("规格"),
        QStringLiteral("数量"),
        QStringLiteral("每平方单价"),
        QStringLiteral("单价"),
        QStringLiteral("总价"),
    };
}

QVector<ExportRow> buildExportRows(const QVector<StatementEntry> &entries)
{
    QVector<ExportRow> rows;
    rows.reserve(entries.size());

    for (const StatementEntry &entry : entries) {
        const int pricePerSquareMeterDecimals = CalculationService::effectivePricePrecision(entry);
        const int amountDecimals = CalculationService::effectiveAmountPrecision(entry);
        const QString backgroundColorHex = normalizeBackgroundColorHex(entry.backgroundColorHex);
        rows.append({
            {formulaTypeDisplay(entry.formulaType), ExportCellType::Text, backgroundColorHex},
            {entry.deliveryDate.isValid() ? entry.deliveryDate.toString(QStringLiteral("yyyy-MM-dd")) : QString(), ExportCellType::Text, backgroundColorHex},
            {entry.orderNumber, ExportCellType::Text, backgroundColorHex},
            {CalculationService::formatSpecification(entry), ExportCellType::Text, backgroundColorHex},
            {QString::number(entry.quantity), ExportCellType::Number, backgroundColorHex},
            {CalculationService::formatAmount(entry.pricePerSquareMeter, pricePerSquareMeterDecimals), ExportCellType::Number, backgroundColorHex},
            {CalculationService::formatAmount(CalculationService::calculateUnitPrice(entry), amountDecimals), ExportCellType::Number, backgroundColorHex},
            {CalculationService::formatAmount(CalculationService::calculateTotalPrice(entry), amountDecimals), ExportCellType::Number, backgroundColorHex},
        });
    }

    return rows;
}

} // namespace cartonledger
