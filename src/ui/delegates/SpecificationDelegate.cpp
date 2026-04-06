#include "ui/delegates/SpecificationDelegate.h"

#include "ui/models/EntryTableModel.h"

#include <QCompleter>
#include <QLineEdit>

namespace cartonledger {

SpecificationDelegate::SpecificationDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *SpecificationDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const
{
    auto *editor = new QLineEdit(parent);
    editor->setClearButtonEnabled(true);
    editor->setPlaceholderText(QStringLiteral("长*宽 或 长*宽*高"));

    const QStringList suggestions = index.model()->data(index, EntryTableModel::SpecificationSuggestionsRole).toStringList();
    auto *completer = new QCompleter(suggestions, editor);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCompletionMode(QCompleter::PopupCompletion);
    editor->setCompleter(completer);
    return editor;
}

void SpecificationDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *lineEdit = qobject_cast<QLineEdit *>(editor);
    if (lineEdit == nullptr) {
        return;
    }

    lineEdit->setText(index.model()->data(index, Qt::EditRole).toString());
}

void SpecificationDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    auto *lineEdit = qobject_cast<QLineEdit *>(editor);
    if (lineEdit == nullptr) {
        return;
    }

    model->setData(index, lineEdit->text(), Qt::EditRole);
}

} // namespace cartonledger