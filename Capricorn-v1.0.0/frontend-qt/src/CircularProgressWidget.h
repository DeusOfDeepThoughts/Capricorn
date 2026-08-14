#pragma once

#include <QColor>
#include <QWidget>

class CircularProgressWidget final : public QWidget {
public:
    explicit CircularProgressWidget(QWidget *parent = nullptr);

    void setProgress(int progress);
    void setPrimaryText(const QString &text);
    void setSecondaryText(const QString &text);
    void setAccentColor(const QColor &color);
    void setTrackColor(const QColor &color);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    int m_progress{0};
    QString m_primaryText{QStringLiteral("0 / 80")};
    QString m_secondaryText{};
    QColor m_accent{QStringLiteral("#657f72")};
    QColor m_track{QStringLiteral("#dfe7e2")};
};
