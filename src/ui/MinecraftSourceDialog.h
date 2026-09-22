#pragma once

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;

class MinecraftSourceDialog : public QDialog {
    Q_OBJECT

public:
    explicit MinecraftSourceDialog(QWidget* parent = nullptr);

    QString chosenDir() const { return dir_; }
    QString chosenVersion() const;

    bool setFolder(const QString& dir);

private:
    void chooseFolder();
    void fillVersions(const QString& selectVersion);

    QString dir_;
    QLineEdit* folder_;
    QComboBox* version_;
    QLabel* status_;
};
