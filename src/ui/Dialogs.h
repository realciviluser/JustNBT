#pragma once

#include "core/Nbt.h"

#include <QDialog>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;

class AddTagDialog : public QDialog {
    Q_OBJECT

public:
    AddTagDialog(const nbt::Tag* container, QWidget* parent = nullptr);

    nbt::TagType type() const;
    QString name() const;

    void accept() override;

private:
    const nbt::Tag* container_;
    QComboBox* type_;
    QLineEdit* name_ = nullptr;
};

class ArrayEditDialog : public QDialog {
    Q_OBJECT

public:
    ArrayEditDialog(const nbt::Tag* tag, QWidget* parent = nullptr);

    const nbt::Tag& result() const { return result_; }
    void accept() override;

private:
    nbt::Tag result_;
    QPlainTextEdit* edit_;
    QLabel* info_;
};
