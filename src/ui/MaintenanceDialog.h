#pragma once

#include <QDialog>

class QCheckBox;
class QLabel;
class QSpinBox;

class MaintenanceDialog : public QDialog {
    Q_OBJECT

public:
    explicit MaintenanceDialog(QWidget* parent = nullptr);

private:
    void refreshStats();
    void savePolicy();
    void report(const QString& what, int removed, qint64 bytes);

    QLabel* backupStats_;
    QLabel* cacheStats_;
    QLabel* result_;
    QCheckBox* autoCleanup_;
    QSpinBox* keepPerFile_;
    QSpinBox* keepDays_;
    QSpinBox* cacheLimitMb_;
};
