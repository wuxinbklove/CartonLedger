#pragma once

#include "core/StatementEntry.h"

#include <QUndoCommand>
#include <QVector>

namespace cartonledger {

class EntryTableModel;

struct ModelSnapshot {
    QVector<StatementEntry> entries;
    QVector<bool> dirtyRows;
    QVector<qint64> deletedIds;
};

class ModelStateCommand : public QUndoCommand {
public:
    ModelStateCommand(EntryTableModel *model,
                      ModelSnapshot before,
                      ModelSnapshot after,
                      const QString &text,
                      QUndoCommand *parent = nullptr);

    void undo() override;
    void redo() override;

private:
    EntryTableModel *m_model;
    ModelSnapshot m_before;
    ModelSnapshot m_after;
    bool m_firstRedo = true;
};

} // namespace cartonledger
