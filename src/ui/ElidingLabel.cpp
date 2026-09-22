#include "ui/ElidingLabel.h"

#include <QFontMetrics>
#include <QPainter>

ElidingLabel::ElidingLabel(QWidget* parent) : QLabel(parent)
{
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    setMinimumWidth(0);
}

void ElidingLabel::setText(const QString& text)
{
    full_ = text;
    setToolTip(text);
    QLabel::setText(text);
    update();
}

QSize ElidingLabel::minimumSizeHint() const
{
    const QSize base = QLabel::minimumSizeHint();
    return {0, base.height()};
}

QSize ElidingLabel::sizeHint() const
{
    const QSize base = QLabel::sizeHint();
    return {std::min(base.width(), 400), base.height()};
}

void ElidingLabel::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    const QRect r = contentsRect();
    const QString shown = fontMetrics().elidedText(full_, Qt::ElideRight, r.width());
    p.setPen(palette().color(foregroundRole()));
    p.drawText(r, int(alignment()), shown);
}
