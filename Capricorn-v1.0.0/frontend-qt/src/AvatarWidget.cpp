#include "AvatarWidget.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDir>
#include <QEasingCurve>
#include <QEnterEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QJsonArray>
#include <QJsonValue>
#include <QMouseEvent>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPointer>
#include <QStandardPaths>
#include <QVariantAnimation>
#include <utility>

namespace {
QNetworkAccessManager *networkManager() {
    static QNetworkAccessManager *manager = new QNetworkAccessManager(qApp);
    return manager;
}

QString bundledAvatarPath(const QString &code) {
    const QString normalized = QDir::cleanPath(code).replace(u'\\', u'/');
    if (normalized.isEmpty() || QDir::isAbsolutePath(normalized)
        || normalized == QStringLiteral("..") || normalized.startsWith(QStringLiteral("../"))
        || normalized.contains(QStringLiteral("/../"))) {
        return {};
    }
    const QString relative = QStringLiteral("assets/avatars/%1.svg").arg(normalized);
    const QString runtimePath = QDir(QCoreApplication::applicationDirPath()).filePath(relative);
    if (QFileInfo::exists(runtimePath)) return runtimePath;
    const QString sourceTreePath = QDir(QDir::currentPath())
        .filePath(QStringLiteral("frontend-qt/resources/avatars/%1.svg").arg(normalized));
    if (QFileInfo::exists(sourceTreePath)) return sourceTreePath;
    return runtimePath;
}
}

AvatarWidget::AvatarWidget(const QJsonObject &avatar, QWidget *parent) : QWidget(parent), m_avatar(avatar) {
    setCursor(Qt::PointingHandCursor);
    setMinimumSize(92, 110);
    setAttribute(Qt::WA_Hover, true);
    setAccessibleName(displayName());

    m_hoverAnimation = new QVariantAnimation(this);
    m_hoverAnimation->setDuration(150);
    m_hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_hoverAnimation, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_hoverProgress = value.toReal();
        update();
    });
    ensureLoaded();
}

QString AvatarWidget::displayName() const {
    QString value = avatarName();
    value.replace(u'_', u' ');
    return value.simplified();
}

QList<QByteArray> AvatarWidget::frameSvgData() const {
    QList<QByteArray> frames;
    if (isUserAvatar()) {
        QStringList paths;
        for (const QJsonValue &value : m_avatar.value(QStringLiteral("framePaths")).toArray()) {
            const QString path = value.toString();
            if (!path.isEmpty()) paths << path;
        }
        if (paths.isEmpty() && !avatarFilePath().isEmpty()) paths << avatarFilePath();
        for (const QString &path : std::as_const(paths)) {
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = file.readAll();
                if (!bytes.isEmpty()) frames << bytes;
            }
        }
        return frames;
    }

    QStringList codes;
    for (const QJsonValue &value : m_avatar.value(QStringLiteral("frameCodes")).toArray()) {
        const QString code = value.toString();
        if (!code.isEmpty()) codes << code;
    }
    if (codes.isEmpty() && !avatarCode().isEmpty()) codes << avatarCode();
    for (const QString &code : std::as_const(codes)) {
        QFile bundled(bundledAvatarPath(code));
        if (bundled.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = bundled.readAll();
            if (!bytes.isEmpty()) frames << bytes;
        }
    }
    return frames;
}

QString AvatarWidget::cachePath() const {
    const QString root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/avatars-v97");
    QDir().mkpath(root);
    const QByteArray hash = QCryptographicHash::hash(avatarUrl().toUtf8(), QCryptographicHash::Sha256).toHex();
    return root + u'/' + QString::fromLatin1(hash) + QStringLiteral(".svg");
}

void AvatarWidget::loadBytes(const QByteArray &bytes) {
    if (bytes.isEmpty()) return;
    m_svg = bytes;
    m_renderer.load(m_svg);
    update();
    emit loaded(this);
}

void AvatarWidget::ensureLoaded() {
    if (!m_svg.isEmpty() || m_loading) return;

    if (isUserAvatar()) {
        QFile local(avatarFilePath());
        if (local.open(QIODevice::ReadOnly)) loadBytes(local.readAll());
        else update();
        return;
    }

    QFile bundled(bundledAvatarPath(avatarCode()));
    if (bundled.open(QIODevice::ReadOnly)) { loadBytes(bundled.readAll()); return; }

    if (avatarUrl().trimmed().isEmpty()) { update(); return; }
    QFile cache(cachePath());
    if (cache.open(QIODevice::ReadOnly)) { loadBytes(cache.readAll()); return; }
    m_loading = true;
    QNetworkReply *reply = networkManager()->get(QNetworkRequest(QUrl(avatarUrl())));
    QPointer<AvatarWidget> self(this);
    connect(this, &QObject::destroyed, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, reply, [self, reply] {
        const QByteArray bytes = reply->error() == QNetworkReply::NoError ? reply->readAll() : QByteArray{};
        reply->deleteLater();
        if (!self) return;
        self->m_loading = false;
        if (!bytes.isEmpty()) {
            QFile cache(self->cachePath()); if (cache.open(QIODevice::WriteOnly)) cache.write(bytes);
            self->loadBytes(bytes);
        } else self->update();
    });
}

void AvatarWidget::setSelected(bool value) {
    if (m_selected == value) return;
    m_selected = value;
    update();
}

QSize AvatarWidget::sizeHint() const { return {102, 116}; }

QRectF AvatarWidget::deleteButtonRect() const {
    return QRectF(width() - 26.0, 5.0, 19.0, 19.0);
}

QRectF AvatarWidget::renameButtonRect() const {
    return QRectF(width() - 50.0, 5.0, 19.0, 19.0);
}

QRectF AvatarWidget::fittedSvgRect(const QRectF &bounds) const {
    QRectF source = m_renderer.viewBoxF();
    if (!source.isValid() || source.width() <= 0.0 || source.height() <= 0.0) {
        const QSize size = m_renderer.defaultSize();
        source = QRectF(0.0, 0.0, qMax(1, size.width()), qMax(1, size.height()));
    }
    const qreal scale = qMin(bounds.width() / source.width(), bounds.height() / source.height());
    const QSizeF fitted(source.width() * scale, source.height() * scale);
    return QRectF(bounds.center().x() - fitted.width() / 2.0,
                  bounds.center().y() - fitted.height() / 2.0,
                  fitted.width(), fitted.height());
}

void AvatarWidget::animateHoverTo(qreal target) {
    if (!m_hoverAnimation) return;
    m_hoverAnimation->stop();
    m_hoverAnimation->setStartValue(m_hoverProgress);
    m_hoverAnimation->setEndValue(qBound(0.0, target, 1.0));
    m_hoverAnimation->start();
}

void AvatarWidget::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    const qreal hover = m_hoverProgress;
    const QRectF cardRect(2.0, 2.0, width() - 4.0, height() - 4.0);
    if (m_selected || hover > 0.01) {
        const int alpha = m_selected ? 190 : qRound(115.0 * hover);
        const int fillAlpha = m_selected ? 170 : qRound(105.0 * hover);
        painter.setPen(QPen(QColor(151, 177, 163, alpha), m_selected ? 1.25 : 1.0));
        painter.setBrush(QColor(242, 247, 244, fillAlpha));
        painter.drawRoundedRect(cardRect, 13.0, 13.0);
    }

    painter.save();
    const QPointF center = cardRect.center();
    const qreal scale = 1.0 + hover * 0.018;
    painter.translate(center.x(), center.y() - hover * 1.5);
    painter.scale(scale, scale);
    painter.translate(-center.x(), -center.y());

    const QRectF imageBounds((width() - 76.0) / 2.0, 8.0, 76.0, 76.0);
    if (m_selected) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(95, 129, 111, 18));
        painter.drawEllipse(imageBounds.adjusted(-2, -2, 2, 2));
    }
    if (m_renderer.isValid()) m_renderer.render(&painter, fittedSvgRect(imageBounds));
    else {
        painter.setPen(QColor(QStringLiteral("#938DA1")));
        painter.drawText(imageBounds, Qt::AlignCenter, QStringLiteral("SVG"));
    }
    painter.restore();

    if (isDeletable() && (m_hovered || m_selected)) {
        const QColor outline(188, 204, 196, 205);
        const QColor ink(QStringLiteral("#6C6876"));
        painter.setPen(QPen(outline, 1.0));
        painter.setBrush(QColor(250, 252, 251, 235));

        const QRectF editRect = renameButtonRect();
        painter.drawEllipse(editRect);
        painter.save();
        painter.setPen(QPen(ink, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF a(editRect.left() + 5.4, editRect.bottom() - 5.2);
        const QPointF b(editRect.right() - 5.0, editRect.top() + 5.3);
        painter.drawLine(a, b);
        painter.drawLine(QPointF(a.x() - 0.6, a.y() + 2.0), QPointF(a.x() + 1.6, a.y() + 1.4));
        painter.drawLine(QPointF(b.x() - 1.4, b.y() - 1.5), QPointF(b.x() + 0.7, b.y() + 0.5));
        painter.restore();

        const QRectF closeRect = deleteButtonRect();
        painter.setPen(QPen(outline, 1.0));
        painter.setBrush(QColor(250, 252, 251, 235));
        painter.drawEllipse(closeRect);
        QFont closeFont = painter.font();
        closeFont.setPointSizeF(9.5);
        closeFont.setWeight(QFont::DemiBold);
        painter.setFont(closeFont);
        painter.setPen(ink);
        painter.drawText(closeRect, Qt::AlignCenter, QStringLiteral("×"));
    }

    QFont labelFont;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    labelFont.setFamilies({QStringLiteral("STKaiti"), QStringLiteral("KaiTi"),
                           QStringLiteral("Microsoft YaHei UI"), QStringLiteral("Segoe UI")});
#else
    labelFont.setFamily(QStringLiteral("KaiTi"));
#endif
    labelFont.setPointSizeF(9.4);
    labelFont.setWeight(m_selected ? QFont::DemiBold : QFont::Medium);
    labelFont.setLetterSpacing(QFont::AbsoluteSpacing, 0.25);
    painter.setFont(labelFont);
    const QRectF labelRect(5, height() - 29, width() - 10, 23);
    painter.setPen(QColor(255, 255, 255, 175));
    painter.drawText(labelRect.translated(0.0, 0.8), Qt::AlignCenter | Qt::TextSingleLine, displayName());
    painter.setPen(QColor(m_selected ? QStringLiteral("#5848CE") : (m_hovered ? QStringLiteral("#6757E8") : QStringLiteral("#667085"))));
    painter.drawText(labelRect, Qt::AlignCenter | Qt::TextSingleLine, displayName());
}

void AvatarWidget::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) {
        if (isDeletable() && (m_hovered || m_selected) && renameButtonRect().contains(event->position()))
            emit renameRequested(this);
        else if (isDeletable() && (m_hovered || m_selected) && deleteButtonRect().contains(event->position()))
            emit deleteRequested(this);
        else
            emit clicked(this);
    }
    QWidget::mousePressEvent(event);
}

void AvatarWidget::enterEvent(QEnterEvent *event) {
    m_hovered = true;
    animateHoverTo(1.0);
    QWidget::enterEvent(event);
}

void AvatarWidget::leaveEvent(QEvent *event) {
    m_hovered = false;
    animateHoverTo(0.0);
    QWidget::leaveEvent(event);
}
