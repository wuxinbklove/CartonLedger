#include "ui/models/EntryTableModel.h"

#include "core/CalculationService.h"
#include "ui/commands/ModelStateCommand.h"

#include <QBrush>
#include <QColor>
#include <QUndoStack>

namespace cartonledger {

namespace {

QBrush unsavedRowBackground()
{
    return QBrush(QColor(255, 248, 225));
}

QDate parseEditableDate(const QVariant &value)
{
    if (value.canConvert<QDate>()) {
        const QDate date = value.toDate();
        if (date.isValid()) {
            return date;
        }
    }

    const QString text = value.toString().trimmed();
    if (text.isEmpty()) {
        return {};
    }

    QDate date = QDate::fromString(text, Qt::ISODate);
    if (date.isValid()) {
        return date;
    }

    const QStringList formats = {
        QStringLiteral("yyyy-M-d"),
        QStringLiteral("yyyy/M/d"),
        QStringLiteral("yyyy.M.d"),
        QStringLiteral("yyyy年M月d日"),
        QStringLiteral("M/d/yyyy"),
        QStringLiteral("M-d-yyyy"),
    };
    for (const QString &format : formats) {
        date = QDate::fromString(text, format);
        if (date.isValid()) {
            return date;
        }
    }

    return {};
}

} // namespace

EntryTableModel::EntryTableModel(QObject *parent)
    : QAbstractTableModel(parent)
{
}

int EntryTableModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return m_entries.size();
}

int EntryTableModel::columnCount(const QModelIndex &parent) const
{
    if (parent.isValid()) {
        return 0;
    }

    return ColumnCount;
}

QVariant EntryTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size()) {
        return {};
    }

    const StatementEntry &entry = m_entries.at(index.row());
    const bool isComputedColumn = (index.column() == UnitPriceColumn && entry.formulaType != FormulaType::Manual)
        || index.column() == TotalPriceColumn;

    if (role == SpecificationSuggestionsRole && index.column() == SpecificationColumn) {
        return m_specificationSuggestions;
    }

    if (role == AllowedFormulaOptionsRole && index.column() == FormulaColumn) {
        return CalculationService::allowedFormulaOptions(entry.specification);
    }

    if (role == Qt::ToolTipRole) {
        if (index.column() == SpecificationColumn) {
            return QStringLiteral("规格格式只允许：长*宽 或 长*宽*高，输入时会联想最近使用过的规格");
        }
        if (index.column() == FormulaColumn) {
            const auto parsed = CalculationService::parseSpecification(entry.specification);
            if (parsed.has_value() && parsed->dimensionCount() == 2) {
                return QStringLiteral("规格为长*宽时，可选择计算公式 C 或 手动");
            }
            if (parsed.has_value() && parsed->dimensionCount() == 3) {
                return QStringLiteral("规格为长*宽*高时，可选择计算公式 A、B 或 手动");
            }
        }
        if (index.column() == UnitPriceColumn && entry.formulaType == FormulaType::Manual) {
            return QStringLiteral("手动模式下可直接输入单价，不自动计算");
        }
    }

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == OrderNumberColumn) {
            return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
        }

        return static_cast<int>(Qt::AlignCenter);
    }

    if (role == Qt::ForegroundRole && isComputedColumn) {
        return QBrush(Qt::darkBlue);
    }

    if (role == Qt::BackgroundRole && isRowDirty(index.row())) {
        return unsavedRowBackground();
    }

    if (role != Qt::DisplayRole && role != Qt::EditRole) {
        return {};
    }

    switch (index.column()) {
    case FormulaColumn:
        return formulaTypeDisplay(entry.formulaType);
    case DeliveryDateColumn:
        return entry.deliveryDate.isValid()
            ? entry.deliveryDate.toString(QStringLiteral("yyyy-MM-dd"))
            : QString();
    case OrderNumberColumn:
        return entry.orderNumber;
    case SpecificationColumn:
        return CalculationService::formatSpecification(entry);
    case QuantityColumn:
        return entry.quantity;
    case PricePerSquareMeterColumn:
        if (role == Qt::EditRole) {
            return CalculationService::formatAmount(entry.pricePerSquareMeter, CalculationService::effectivePricePrecision(entry));
        }
        return CalculationService::formatAmount(entry.pricePerSquareMeter, CalculationService::effectivePricePrecision(entry));
    case UnitPriceColumn:
        if (role == Qt::EditRole && entry.formulaType == FormulaType::Manual) {
            return CalculationService::formatAmount(entry.manualUnitPrice, CalculationService::effectiveManualUnitPricePrecision(entry));
        }
        return CalculationService::formatAmount(CalculationService::calculateUnitPrice(entry), CalculationService::effectiveAmountPrecision(entry));
    case TotalPriceColumn:
        return CalculationService::formatAmount(CalculationService::calculateTotalPrice(entry), CalculationService::effectiveAmountPrecision(entry));
    case ColumnCount:
        break;
    }

    return {};
}

QVariant EntryTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation == Qt::Vertical) {
        if (role == Qt::DisplayRole) {
            return section + 1;
        }
        if (role == Qt::TextAlignmentRole) {
            return static_cast<int>(Qt::AlignCenter);
        }
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    if (role == Qt::ToolTipRole && section == FormulaColumn) {
        return QStringLiteral("计算公式说明：\n%1").arg(formulaTypeHintText());
    }

    if (role != Qt::DisplayRole) {
        return QAbstractTableModel::headerData(section, orientation, role);
    }

    switch (section) {
    case FormulaColumn:
        return QStringLiteral("计算公式");
    case DeliveryDateColumn:
        return QStringLiteral("送货日期");
    case OrderNumberColumn:
        return QStringLiteral("订单号");
    case SpecificationColumn:
        return QStringLiteral("规格");
    case QuantityColumn:
        return QStringLiteral("数量");
    case PricePerSquareMeterColumn:
        return QStringLiteral("每平方单价");
    case UnitPriceColumn:
        return QStringLiteral("单价");
    case TotalPriceColumn:
        return QStringLiteral("总价");
    case ColumnCount:
        break;
    }

    return {};
}

Qt::ItemFlags EntryTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid()) {
        return Qt::NoItemFlags;
    }

    Qt::ItemFlags itemFlags = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
    if (index.column() != TotalPriceColumn) {
        itemFlags |= Qt::ItemIsEditable;
    }

    return itemFlags;
}

bool EntryTableModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || role != Qt::EditRole || index.row() < 0 || index.row() >= m_entries.size()) {
        return false;
    }

    ModelSnapshot before;
    if (m_undoStack && !m_suppressUndo) {
        before = captureSnapshot();
    }

    StatementEntry &entry = m_entries[index.row()];
    bool changed = false;

    switch (index.column()) {
    case FormulaColumn: {
        const auto parsed = formulaTypeFromString(value.toString());
        if (!parsed.has_value()) {
            return false;
        }
        if (!CalculationService::isFormulaAllowed(entry.specification, parsed.value())) {
            return false;
        }

        if (parsed.value() == FormulaType::Manual && entry.formulaType != FormulaType::Manual) {
            entry.manualUnitPrice = CalculationService::calculateUnitPrice(entry);
            entry.manualUnitPricePrecision = CalculationService::effectiveAmountPrecision(entry);
        } else if (parsed.value() != FormulaType::Manual) {
            entry.manualUnitPricePrecision = -1;
        }

        entry.formulaType = parsed.value();
        changed = true;
        break;
    }
    case DeliveryDateColumn: {
        const QString text = value.toString().trimmed();
        if (text.isEmpty()) {
            entry.deliveryDate = {};
            changed = true;
            break;
        }

        const QDate date = parseEditableDate(value);
        if (!date.isValid()) {
            return false;
        }
        entry.deliveryDate = date;
        changed = true;
        break;
    }
    case OrderNumberColumn:
        entry.orderNumber = value.toString().trimmed();
        changed = true;
        break;
    case SpecificationColumn: {
        const QString specification = value.toString().trimmed();
        if (specification.isEmpty()) {
            entry.specification.clear();
            entry.lengthCm = 0.0;
            entry.widthCm = 0.0;
            entry.heightCm = 0.0;
            changed = true;
            break;
        }

        const auto parsed = CalculationService::parseSpecification(specification);
        if (!parsed.has_value()) {
            return false;
        }

        entry.specification = CalculationService::normalizeSpecification(specification);
        entry.lengthCm = parsed->lengthCm;
        entry.widthCm = parsed->widthCm;
        entry.heightCm = parsed->heightCm.value_or(0.0);

        const auto recentTemplate = m_recentTemplatesBySpecification.constFind(entry.specification);
        if (recentTemplate != m_recentTemplatesBySpecification.cend()) {
            entry.quantity = recentTemplate->quantity >= 0 ? recentTemplate->quantity : entry.quantity;
            entry.pricePerSquareMeter = recentTemplate->pricePerSquareMeter;
            entry.pricePrecision = CalculationService::effectivePricePrecision(*recentTemplate);
            entry.formulaType = recentTemplate->formulaType;
            entry.manualUnitPrice = recentTemplate->manualUnitPrice;
            entry.manualUnitPricePrecision = CalculationService::effectiveManualUnitPricePrecision(*recentTemplate);
        } else if (entry.formulaType != FormulaType::Manual) {
            if (parsed->dimensionCount() == 2) {
                entry.formulaType = FormulaType::C;
            } else if (entry.formulaType == FormulaType::C) {
                entry.formulaType = FormulaType::A;
            }
            entry.manualUnitPricePrecision = -1;
        }
        changed = true;
        break;
    }
    case QuantityColumn: {
        bool ok = false;
        const int parsed = value.toInt(&ok);
        if (!ok || parsed < 0) {
            return false;
        }
        entry.quantity = parsed;
        changed = true;
        break;
    }
    case PricePerSquareMeterColumn: {
        const auto parsed = CalculationService::parsePriceValue(value.toString());
        if (!parsed.has_value()) {
            return false;
        }
        entry.pricePerSquareMeter = parsed->value;
        entry.pricePrecision = parsed->precision;
        changed = true;
        break;
    }
    case UnitPriceColumn: {
        const auto parsed = CalculationService::parsePriceValue(value.toString());
        if (!parsed.has_value()) {
            return false;
        }

        entry.manualUnitPrice = parsed->value;
        entry.manualUnitPricePrecision = parsed->precision;
        entry.formulaType = CalculationService::inferFormulaTypeForUnitPrice(entry, parsed->value).value_or(FormulaType::Manual);
        changed = true;
        break;
    }
    case TotalPriceColumn:
    case ColumnCount:
        return false;
    }

    if (!changed) {
        return false;
    }

    recalculateRow(index.row());
    rebuildSpecificationTemplates();
    markRowDirty(index.row());
    markDirty();

    if (m_undoStack && !m_suppressUndo) {
        const auto columnName = headerData(index.column(), Qt::Horizontal, Qt::DisplayRole).toString();
        const QString cmdText = QStringLiteral("编辑「%1」").arg(columnName);
        m_undoStack->push(new ModelStateCommand(this, std::move(before), captureSnapshot(), cmdText));
    }

    return true;
}

bool EntryTableModel::insertRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || row > m_entries.size() || count <= 0) {
        return false;
    }

    beginInsertRows(QModelIndex(), row, row + count - 1);
    for (int index = 0; index < count; ++index) {
        m_entries.insert(row, StatementEntry {});
        m_dirtyRows.insert(row, true);
    }
    endInsertRows();
    rebuildSpecificationTemplates();
    markDirty();
    return true;
}

bool EntryTableModel::removeRows(int row, int count, const QModelIndex &parent)
{
    if (parent.isValid() || row < 0 || count <= 0 || row + count > m_entries.size()) {
        return false;
    }

    beginRemoveRows(QModelIndex(), row, row + count - 1);
    for (int index = 0; index < count; ++index) {
        const StatementEntry entry = m_entries.takeAt(row);
        m_dirtyRows.removeAt(row);
        if (entry.id >= 0) {
            m_deletedIds.append(entry.id);
        }
    }
    endRemoveRows();
    rebuildSpecificationTemplates();
    markDirty();
    return true;
}

void EntryTableModel::setEntries(QVector<StatementEntry> entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    m_dirtyRows = QVector<bool>(m_entries.size(), false);
    m_deletedIds.clear();
    rebuildSpecificationTemplates();
    m_dirty = false;
    endResetModel();
}

void EntryTableModel::insertEntry(int row, const StatementEntry &entry)
{
    beginInsertRows(QModelIndex(), row, row);
    m_entries.insert(row, entry);
    m_dirtyRows.insert(row, true);
    endInsertRows();
    rebuildSpecificationTemplates();
    markDirty();
}

void EntryTableModel::appendEntry(const StatementEntry &entry)
{
    insertEntry(m_entries.size(), entry);
}

void EntryTableModel::setRecentSpecificationTemplates(const QVector<StatementEntry> &entries)
{
    m_persistedTemplates = entries;
    rebuildSpecificationTemplates();

    if (rowCount() > 0) {
        emit dataChanged(
            index(0, FormulaColumn),
            index(rowCount() - 1, SpecificationColumn),
            {SpecificationSuggestionsRole, AllowedFormulaOptionsRole, Qt::ToolTipRole});
    }
}

void EntryTableModel::rebuildSpecificationTemplates()
{
    m_recentTemplatesBySpecification.clear();
    m_specificationSuggestions.clear();

    auto appendTemplateIfAbsent = [this](const StatementEntry &entry) {
        const QString specification = CalculationService::formatSpecification(entry);
        if (specification.isEmpty() || m_recentTemplatesBySpecification.contains(specification)) {
            return;
        }

        m_recentTemplatesBySpecification.insert(specification, entry);
        m_specificationSuggestions.append(specification);
    };

    for (qsizetype index = m_entries.size(); index > 0; --index) {
        appendTemplateIfAbsent(m_entries.at(index - 1));
    }

    for (const StatementEntry &entry : m_persistedTemplates) {
        appendTemplateIfAbsent(entry);
    }
}

const QVector<StatementEntry> &EntryTableModel::entries() const
{
    return m_entries;
}

const QVector<qint64> &EntryTableModel::deletedIds() const
{
    return m_deletedIds;
}

bool EntryTableModel::isDirty() const
{
    return m_dirty;
}

bool EntryTableModel::isRowDirty(int row) const
{
    return row >= 0 && row < m_dirtyRows.size() && m_dirtyRows.at(row);
}

void EntryTableModel::markRowDirty(int row)
{
    if (row < 0 || row >= m_dirtyRows.size() || m_dirtyRows.at(row)) {
        return;
    }

    m_dirtyRows[row] = true;
    emit dataChanged(index(row, FormulaColumn), index(row, TotalPriceColumn), {Qt::BackgroundRole});
}

void EntryTableModel::recalculateRow(int row)
{
    emit dataChanged(index(row, FormulaColumn), index(row, TotalPriceColumn));
}

void EntryTableModel::markDirty()
{
    m_dirty = true;
}

void EntryTableModel::setUndoStack(QUndoStack *stack)
{
    m_undoStack = stack;
}

void EntryTableModel::setSuppressUndo(bool suppress)
{
    m_suppressUndo = suppress;
}

ModelSnapshot EntryTableModel::captureSnapshot() const
{
    return ModelSnapshot { m_entries, m_dirtyRows, m_deletedIds };
}

void EntryTableModel::restoreSnapshot(const ModelSnapshot &snapshot)
{
    beginResetModel();
    m_entries = snapshot.entries;
    m_dirtyRows = snapshot.dirtyRows;
    m_deletedIds = snapshot.deletedIds;
    m_dirty = (std::any_of(m_dirtyRows.cbegin(), m_dirtyRows.cend(), [](bool v) { return v; })
               || !m_deletedIds.isEmpty());
    rebuildSpecificationTemplates();
    endResetModel();
}

void EntryTableModel::markAllRowsClean(const QMap<int, qint64> &idMap)
{
    for (auto it = idMap.cbegin(); it != idMap.cend(); ++it) {
        const int row = it.key();
        if (row >= 0 && row < m_entries.size()) {
            m_entries[row].id = it.value();
        }
    }
    m_dirtyRows.fill(false);
    m_deletedIds.clear();
    m_dirty = false;

    if (!m_entries.isEmpty()) {
        emit dataChanged(index(0, FormulaColumn), index(m_entries.size() - 1, TotalPriceColumn), { Qt::BackgroundRole });
    }
}

} // namespace cartonledger
