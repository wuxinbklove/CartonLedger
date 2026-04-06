#pragma once

#include "core/AppSettings.h"

#include <QDialog>

class QDoubleSpinBox;

namespace cartonledger {

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(AppSettings &settings, QWidget *parent = nullptr);

protected:
    void accept() override;

private:
    AppSettings &m_settings;
    QDoubleSpinBox *m_defaultPriceSpinBox = nullptr;
};

} // namespace cartonledger
