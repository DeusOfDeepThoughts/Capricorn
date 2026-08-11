#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QStringList>
#include <QRectF>
#include <QSvgRenderer>
#include <QWidget>

class QEnterEvent;
class QEvent;
class QMouseEvent;
class QPaintEvent;
class QVariantAnimation;

class AvatarWidget final : public QWidget {
    Q_OBJECT
public:
    explicit AvatarWidget(const QJsonObject &avatar, QWidget *parent = nullptr);
    QString avatarId() const { return m_avatar.value(QStringLiteral("id")).toString(); }
    QString avatarName() const { return m_avatar.value(QStringLiteral("name")).toString(); }
    QString displayName() const;
    QString avatarUrl() const { return m_avatar.value(QStringLiteral("url")).toString(); }
    QString avatarCode() const { return m_avatar.value(QStringLiteral("code")).toString(); }
    QString avatarFilePath() const { return m_avatar.value(QStringLiteral("filePath")).toString(); }
    QString interactionProfile() const { return m_avatar.value(QStringLiteral("interactionProfile")).toString(); }
    QList<QByteArray> frameSvgData() const;
    bool isUserAvatar() const { return m_avatar.value(QStringLiteral("source")).toString() == QStringLiteral("user"); }
    bool isDeletable() const { return isUserAvatar() && m_avatar.value(QStringLiteral("deletable")).toBool(false); }
    QByteArray svgData() const { return m_svg; }
    void setSelected(bool selected);
    bool isSelected() const { return m_selected; }
    void ensureLoaded();

signals:
    void clicked(AvatarWidget *widget);
    void loaded(AvatarWidget *widget);
    void deleteRequested(AvatarWidget *widget);
    void renameRequested(AvatarWidget *widget);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    QSize sizeHint() const override;

private:
    QString cachePath() const;
    QRectF deleteButtonRect() const;
    QRectF renameButtonRect() const;
    QRectF fittedSvgRect(const QRectF &bounds) const;
    void loadBytes(const QByteArray &bytes);
    void animateHoverTo(qreal target);

    QJsonObject m_avatar;
    QByteArray m_svg;
    QSvgRenderer m_renderer;
    QVariantAnimation *m_hoverAnimation{};
    bool m_selected{false};
    bool m_loading{false};
    bool m_hovered{false};
    qreal m_hoverProgress{0.0};
};
