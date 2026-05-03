#pragma once

#include "core/StatementEntry.h"
#include "ui/commands/ModelStateCommand.h"

#include <QAbstractTableModel>
#include <QHash>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QVector>

class QUndoStack;
class QColor;

namespace cartonledger {

class EntryTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        FormulaColumn = 0,
        DeliveryDateColumn,
        OrderNumberColumn,
        SpecificationColumn,
        QuantityColumn,
        PricePerSquareMeterColumn,
        UnitPriceColumn,
        TotalPriceColumn,
        ColumnCount,
    };

    enum Role {
        AllowedFormulaOptionsRole = Qt::UserRole + 1,
        SpecificationSuggestionsRole,
    };

    explicit EntryTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role = Qt::EditRole) override;
    bool insertRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;
    bool removeRows(int row, int count, const QModelIndex &parent = QModelIndex()) override;

    void setEntries(QVector<StatementEntry> entries);
    void insertEntry(int row, const StatementEntry &entry);
    void appendEntry(const StatementEntry &entry);
    void setRecentSpecificationTemplates(const QVector<StatementEntry> &entries);
    void setSearchHighlightRows(const QSet<int> &rows);
    void clearSearchHighlights();
    bool setRowBackgroundColor(int row, const QColor &color);
    bool clearRowBackgroundColor(int row);

    void setUndoStack(QUndoStack *stack);
    void setSuppressUndo(bool suppress);
    ModelSnapshot captureSnapshot() const;
    void restoreSnapshot(const ModelSnapshot &snapshot);
    void markAllRowsClean(const QMap<int, qint64> &idMap);

    const QVector<StatementEntry> &entries() const;
    const QVector<qint64> &deletedIds() const;
    bool isDirty() const;

private:
    bool isRowDirty(int row) const;
    bool setRowBackgroundColorHex(int row, const QString &colorHex, const QString &commandText);
    void markRowDirty(int row);
    void rebuildSpecificationTemplates();
    void recalculateRow(int row);
    void markDirty();

    QVector<StatementEntry> m_entries;
    QVector<bool> m_dirtyRows;
    QVector<qint64> m_deletedIds;
    QVector<StatementEntry> m_persistedTemplates;
    QHash<QString, StatementEntry> m_recentTemplatesBySpecification;
    QStringList m_specificationSuggestions;
    QSet<int> m_searchHighlightRows;
    bool m_dirty = false;
    QUndoStack *m_undoStack = nullptr;
    bool m_suppressUndo = false;
};

} // namespace cartonledger
