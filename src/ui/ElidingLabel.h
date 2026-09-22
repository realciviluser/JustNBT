#pragma once

#include <QLabel>

class ElidingLabel : public QLabel {
    Q_OBJECT

public:
    explicit ElidingLabel(QWidget* parent = nullptr);

    void setText(const QString& text);
    QString fullText() const { return full_; }

    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString full_;
};
