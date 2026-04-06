#include "ui/commands/ModelStateCommand.h"

#include "ui/models/EntryTableModel.h"

namespace cartonledger {

ModelStateCommand::ModelStateCommand(EntryTableModel *model,
                                     ModelSnapshot before,
                                     ModelSnapshot after,
                                     const QString &text,
                                     QUndoCommand *parent)
    : QUndoCommand(text, parent)
    , m_model(model)
    , m_before(std::move(before))
    , m_after(std::move(after))
{
}

void ModelStateCommand::undo()
{
    m_model->restoreSnapshot(m_before);
}

void ModelStateCommand::redo()
{
    if (m_firstRedo) {
        // The action was already performed before the command was pushed onto the stack.
        m_firstRedo = false;
        return;
    }
    m_model->restoreSnapshot(m_after);
}

} // namespace cartonledger
