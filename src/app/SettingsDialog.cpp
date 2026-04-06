#include "app/SettingsDialog.h"

#include "core/CalculationService.h"

#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace cartonledger {

SettingsDialog::SettingsDialog(AppSettings &settings, QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
{
    setWindowTitle(QStringLiteral("设置"));
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(QStringLiteral("设置新建行默认使用的每平方单价。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);

    auto *formLayout = new QFormLayout();
    m_defaultPriceSpinBox = new QDoubleSpinBox(this);
    m_defaultPriceSpinBox->setDecimals(4);
    m_defaultPriceSpinBox->setRange(0.0, 9999.9999);
    m_defaultPriceSpinBox->setSingleStep(0.01);
    m_defaultPriceSpinBox->setValue(m_settings.defaultPricePerSquareMeter());
    formLayout->addRow(QStringLiteral("默认每平方单价"), m_defaultPriceSpinBox);
    layout->addLayout(formLayout);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttonBox);
}

void SettingsDialog::accept()
{
    m_settings.setDefaultPricePerSquareMeter(
        m_defaultPriceSpinBox->value(),
        CalculationService::inferPricePrecision(m_defaultPriceSpinBox->value()));
    QDialog::accept();
}

} // namespace cartonledger
