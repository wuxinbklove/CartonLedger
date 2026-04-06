#include "ui/delegates/FormulaTypeDelegate.h"

#include "ui/models/EntryTableModel.h"

#include <QComboBox>
#include <QStyleFactory>

namespace cartonledger {

FormulaTypeDelegate::FormulaTypeDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

QWidget *FormulaTypeDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &index) const
{
    auto *editor = new QComboBox(parent);
    editor->setStyle(QStyleFactory::create("Fusion"));
    QStringList options = index.model()->data(index, EntryTableModel::AllowedFormulaOptionsRole).toStringList();
    if (options.isEmpty()) {
        options << "A" << "B" << "C" << "手动";
    }
    editor->addItems(options);
    return editor;
}

void FormulaTypeDelegate::setEditorData(QWidget *editor, const QModelIndex &index) const
{
    auto *comboBox = qobject_cast<QComboBox *>(editor);
    if (comboBox == nullptr) {
        return;
    }

    const QString currentValue = index.model()->data(index, Qt::EditRole).toString();
    const int comboIndex = comboBox->findText(currentValue);
    comboBox->setCurrentIndex(comboIndex >= 0 ? comboIndex : 0);
}

void FormulaTypeDelegate::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
    auto *comboBox = qobject_cast<QComboBox *>(editor);
    if (comboBox == nullptr) {
        return;
    }

    model->setData(index, comboBox->currentText(), Qt::EditRole);
}

} // namespace cartonledger
