#include "MainWindow.h"

#include "AppData.h"
#include "AvatarWidget.h"
#include "ChatWindow.h"
#include "CircularProgressWidget.h"
#include "CoreClient.h"
#include "PetWindow.h"
#include "ProcessSupervisor.h"
#include "VoiceRecognitionClient.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCheckBox>
#include <QColor>
#include <QCloseEvent>
#include <QDateTime>
#include <QEasingCurve>
#include <QDir>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QList>
#include <QListWidget>
#include <QMenu>
#include <QMouseEvent>
#include <QPixmap>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QRadioButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QScrollBar>
#include <QScreen>
#include <QSet>
#include <QSlider>
#include <QStackedWidget>
#include <QStringList>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTextEdit>
#include <QTextCursor>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>
#include <QUrl>
#include <QUuid>
#include <QtMath>
#include <utility>
#include <algorithm>
#include <functional>
#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
class DeselectableRadioButton final : public QRadioButton {
public:
    explicit DeselectableRadioButton(const QString &text, QWidget *parent = nullptr)
        : QRadioButton(text, parent) {}

    bool wasCheckedOnPress() const noexcept { return m_wasCheckedOnPress; }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event && event->button() == Qt::LeftButton)
            m_wasCheckedOnPress = isChecked();
        QRadioButton::mousePressEvent(event);
    }

private:
    bool m_wasCheckedOnPress{false};
};

void forceUncheckRadioButton(QRadioButton *radio) {
    if (!radio || !radio->isChecked()) return;
    QButtonGroup *buttonGroup = radio->group();
    const bool groupWasExclusive = buttonGroup && buttonGroup->exclusive();
    const bool autoWasExclusive = radio->autoExclusive();
    if (groupWasExclusive) buttonGroup->setExclusive(false);
    radio->setAutoExclusive(false);
    radio->setChecked(false);
    radio->setAutoExclusive(autoWasExclusive);
    if (groupWasExclusive) buttonGroup->setExclusive(true);
}

class InteractiveMotionFilter final : public QObject {
public:
    explicit InteractiveMotionFilter(QObject *parent = nullptr) : QObject(parent) {}

    void attach(QWidget *widget, const QColor &shadow = QColor(67, 91, 78, 45)) {
        if (!widget || widget->property("interactiveMotion").toBool()) return;
        widget->setProperty("interactiveMotion", true);
        widget->setAttribute(Qt::WA_Hover, true);
        auto *effect = new QGraphicsDropShadowEffect(widget);
        effect->setBlurRadius(0.0);
        effect->setOffset(0.0, 0.0);
        QColor transparent = shadow;
        transparent.setAlpha(0);
        effect->setColor(transparent);
        widget->setGraphicsEffect(effect);
        widget->setProperty("motionShadow", shadow);
        widget->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        auto *widget = qobject_cast<QWidget *>(watched);
        auto *effect = widget ? qobject_cast<QGraphicsDropShadowEffect *>(widget->graphicsEffect()) : nullptr;
        if (!widget || !effect) return QObject::eventFilter(watched, event);

        // Opted-in disabled controls are visually inert. The V70 save action mirrors
        // the persona management “使用” action: no hover lift, press shadow or residual
        // animation while the button cannot be clicked. EnabledChange is
        // handled as well so a button disabled while hovered is reset at once.
        if (!widget->isEnabled() && widget->property("motionDisabledInert").toBool()) {
            const auto runningAnimations = effect->findChildren<QPropertyAnimation *>();
            for (QPropertyAnimation *animation : runningAnimations) {
                animation->stop();
                animation->deleteLater();
            }
            effect->setBlurRadius(0.0);
            effect->setOffset(0.0, 0.0);
            QColor transparent = widget->property("motionShadow").value<QColor>();
            transparent.setAlpha(0);
            effect->setColor(transparent);
            return QObject::eventFilter(watched, event);
        }

        qreal blur = effect->blurRadius();
        qreal offset = effect->yOffset();
        int duration = 120;
        QColor target = widget->property("motionShadow").value<QColor>();
        switch (event->type()) {
        case QEvent::Enter:
            blur = 18.0; offset = 3.0; target.setAlpha(48); duration = 150; break;
        case QEvent::Leave:
            blur = 0.0; offset = 0.0; target.setAlpha(0); duration = 180; break;
        case QEvent::MouseButtonPress:
            blur = 7.0; offset = 1.0; target.setAlpha(36); duration = 70; break;
        case QEvent::MouseButtonRelease:
            blur = widget->underMouse() ? 18.0 : 0.0;
            offset = widget->underMouse() ? 3.0 : 0.0;
            target.setAlpha(widget->underMouse() ? 48 : 0);
            duration = 110;
            break;
        default:
            return QObject::eventFilter(watched, event);
        }
        auto animate = [effect, duration](const QByteArray &property, const QVariant &endValue) {
            auto *animation = new QPropertyAnimation(effect, property, effect);
            animation->setDuration(duration);
            animation->setStartValue(effect->property(property.constData()));
            animation->setEndValue(endValue);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            animation->start(QAbstractAnimation::DeleteWhenStopped);
        };
        animate(QByteArrayLiteral("blurRadius"), blur);
        animate(QByteArrayLiteral("yOffset"), offset);
        animate(QByteArrayLiteral("color"), target);
        return QObject::eventFilter(watched, event);
    }
};

class WindowResizeHandle final : public QWidget {
public:
    WindowResizeHandle(QWidget *target, Qt::Edges edges, QWidget *parent = nullptr)
        : QWidget(parent), m_target(target), m_edges(edges) {
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_StyledBackground, false);
        setMouseTracking(true);
        setCursor(cursorForEdges(m_edges));
    }

protected:
    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !m_target || m_target->isMaximized()) {
            QWidget::mousePressEvent(event);
            return;
        }

        // V90 deliberately owns the resize gesture instead of relying only on
        // startSystemResize()/WM_NCHITTEST.  On some Qt 6 + frameless Windows
        // combinations the OS cursor changes correctly while the native resize
        // loop never starts.  Capturing the mouse and applying geometry ourselves
        // makes every exposed edge/corner deterministic.
        m_dragging = true;
        m_pressGlobal = event->globalPosition().toPoint();
        m_pressGeometry = m_target->geometry();
        grabMouse();
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        setCursor(cursorForEdges(m_edges));
        if (!m_dragging || !m_target) {
            QWidget::mouseMoveEvent(event);
            return;
        }

        const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
        const QRect initial = m_pressGeometry;
        constexpr int kManualMaxWidth = 1760;
        constexpr int kManualMaxHeight = 1120;
        const int minWidth = qMax(1, m_target->minimumWidth());
        const int minHeight = qMax(1, m_target->minimumHeight());
        const int maxWidth = qMax(minWidth, kManualMaxWidth);
        const int maxHeight = qMax(minHeight, kManualMaxHeight);

        int x = initial.x();
        int y = initial.y();
        int w = initial.width();
        int h = initial.height();

        if (m_edges.testFlag(Qt::LeftEdge)) {
            const int nextWidth = qBound(minWidth, initial.width() - delta.x(), maxWidth);
            x = initial.x() + initial.width() - nextWidth;
            w = nextWidth;
        } else if (m_edges.testFlag(Qt::RightEdge)) {
            w = qBound(minWidth, initial.width() + delta.x(), maxWidth);
        }

        if (m_edges.testFlag(Qt::TopEdge)) {
            const int nextHeight = qBound(minHeight, initial.height() - delta.y(), maxHeight);
            y = initial.y() + initial.height() - nextHeight;
            h = nextHeight;
        } else if (m_edges.testFlag(Qt::BottomEdge)) {
            h = qBound(minHeight, initial.height() + delta.y(), maxHeight);
        }

        m_target->setGeometry(x, y, w, h);
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (m_dragging && event->button() == Qt::LeftButton) {
            m_dragging = false;
            releaseMouse();
            event->accept();
            return;
        }
        QWidget::mouseReleaseEvent(event);
    }

    void hideEvent(QHideEvent *event) override {
        if (m_dragging) {
            m_dragging = false;
            releaseMouse();
        }
        QWidget::hideEvent(event);
    }

private:
    static Qt::CursorShape cursorForEdges(Qt::Edges edges) {
        const bool left = edges.testFlag(Qt::LeftEdge);
        const bool right = edges.testFlag(Qt::RightEdge);
        const bool top = edges.testFlag(Qt::TopEdge);
        const bool bottom = edges.testFlag(Qt::BottomEdge);
        if ((left && top) || (right && bottom)) return Qt::SizeFDiagCursor;
        if ((right && top) || (left && bottom)) return Qt::SizeBDiagCursor;
        if (left || right) return Qt::SizeHorCursor;
        if (top || bottom) return Qt::SizeVerCursor;
        return Qt::ArrowCursor;
    }

    QWidget *m_target{};
    Qt::Edges m_edges{};
    bool m_dragging{false};
    QPoint m_pressGlobal;
    QRect m_pressGeometry;
};

class WindowOutlineOverlay final : public QWidget {
public:
    explicit WindowOutlineOverlay(QWidget *parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        if (!isVisible()) return;
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(QColor(QStringLiteral("#BDB6CC")), 1.8);
        pen.setJoinStyle(Qt::RoundJoin);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        const QRectF outline = QRectF(rect()).adjusted(1.25, 1.25, -1.25, -1.25);
        painter.drawRoundedRect(outline, 22.5, 22.5);
    }
};

class DetectionIndicator final : public QWidget {
public:
    explicit DetectionIndicator(QWidget *parent = nullptr) : QWidget(parent), timer_(new QTimer(this)) {
        setFixedSize(22, 22);
        timer_->setInterval(85);
        connect(timer_, &QTimer::timeout, this, [this] { phase_ = (phase_ + 1) % 8; update(); });
    }
    void setState(const QString &state) {
        state_ = state;
        if (state_ == QStringLiteral("running")) timer_->start(); else timer_->stop();
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPointF c(width() / 2.0, height() / 2.0);
        if (state_ == QStringLiteral("running")) {
            for (int i = 0; i < 8; ++i) {
                const qreal a = (i * 45.0 - 90.0) * 3.14159265358979323846 / 180.0;
                QColor color(QStringLiteral("#6757E8"));
                const int distance = (i - phase_ + 8) % 8;
                color.setAlpha(qBound(45, 230 - distance * 25, 230));
                painter.setPen(Qt::NoPen); painter.setBrush(color);
                painter.drawEllipse(c + QPointF(qCos(a) * 7.0, qSin(a) * 7.0), 2.0, 2.0);
            }
        } else if (state_ == QStringLiteral("success") || state_ == QStringLiteral("done")) {
            QPen pen(QColor(QStringLiteral("#35A98F")), 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            painter.setPen(pen); painter.setBrush(Qt::NoBrush);
            painter.drawLine(QPointF(5.0, 11.5), QPointF(9.3, 15.2));
            painter.drawLine(QPointF(9.3, 15.2), QPointF(17.0, 6.8));
        } else if (state_ == QStringLiteral("error") || state_ == QStringLiteral("failed")) {
            QPen pen(QColor(QStringLiteral("#D95C68")), 1.9, Qt::SolidLine, Qt::RoundCap);
            painter.setPen(pen); painter.drawLine(QPointF(6.0, 6.0), QPointF(16.0, 16.0));
            painter.drawLine(QPointF(16.0, 6.0), QPointF(6.0, 16.0));
        } else {
            painter.setPen(QPen(QColor(QStringLiteral("#C8C3CF")), 1.4));
            painter.setBrush(Qt::NoBrush); painter.drawEllipse(c, 4.2, 4.2);
        }
    }
private:
    QString state_{QStringLiteral("idle")};
    QTimer *timer_{};
    int phase_{0};
};

class ClickableFrame final : public QFrame {
public:
    explicit ClickableFrame(QWidget *parent = nullptr) : QFrame(parent) { setCursor(Qt::PointingHandCursor); }
    void setActivated(std::function<void()> callback) { m_callback = std::move(callback); }
protected:
    void mouseReleaseEvent(QMouseEvent *event) override {
        QFrame::mouseReleaseEvent(event);
        if (event->button() == Qt::LeftButton && rect().contains(event->position().toPoint()) && m_callback) m_callback();
    }
private:
    std::function<void()> m_callback;
};

void clearLayout(QLayout *layout) {
    if (!layout) return;
    while (QLayoutItem *item = layout->takeAt(0)) {
        if (item->widget()) delete item->widget();
        if (item->layout()) {
            clearLayout(item->layout());
            delete item->layout();
        }
        delete item;
    }
}

QLabel *statusLabel(QWidget *parent = nullptr) {
    auto *label = new QLabel(parent);
    label->setObjectName(QStringLiteral("status"));
    label->setWordWrap(true);
    return label;
}


QString sourceKind(const QString &source) {
    if (source == QStringLiteral("builtin")) return QStringLiteral("内置");
    if (source == QStringLiteral("created")) return QString();
    return QStringLiteral("导入");
}


QWidget *scrollPage(QWidget *content) {
    // The page viewport always owns the available width. Never let a child sizeHint
    // shrink the active page and leave a blank canvas on the right.
    auto *scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("pageScroll"));
    scroll->setWidgetResizable(true);
    scroll->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    scroll->setMinimumWidth(0);
    scroll->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    scroll->viewport()->setMinimumWidth(0);
    scroll->viewport()->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    // V70: never let the QAbstractScrollArea viewport or its page content
    // autofill a rectangular palette background over the appShell corner.
    scroll->viewport()->setAutoFillBackground(false);
    content->setAutoFillBackground(false);
    content->setMinimumWidth(0);
    content->setMaximumWidth(QWIDGETSIZE_MAX);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    if (content->layout()) content->layout()->setSizeConstraint(QLayout::SetDefaultConstraint);
    scroll->setWidget(content);
    return scroll;
}

int percent(int value, int total) {
    return total > 0 ? qBound(0, qRound((double(value) / double(total)) * 100.0), 100) : 0;
}


void setDynamicProperty(QWidget *widget, const char *name, const QVariant &value) {
    if (!widget || widget->property(name) == value) return;
    widget->setProperty(name, value);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    widget->update();
}

QLabel *smallMuted(const QString &text, QWidget *parent = nullptr) {
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("mutedSmall"));
    label->setWordWrap(true);
    return label;
}

QJsonObject canonicalModelConfig(const QJsonObject &config) {
    return QJsonObject{
        {QStringLiteral("provider"), config.value(QStringLiteral("provider")).toString(QStringLiteral("custom"))},
        {QStringLiteral("providerName"), config.value(QStringLiteral("providerName")).toString(QStringLiteral("自定义服务"))},
        {QStringLiteral("baseUrl"), config.value(QStringLiteral("baseUrl")).toString().trimmed()},
        {QStringLiteral("modelId"), config.value(QStringLiteral("modelId")).toString().trimmed()},
        {QStringLiteral("apiKey"), config.value(QStringLiteral("apiKey")).toString().trimmed()}
    };
}

bool validModelBaseUrl(const QString &value) {
    const QString text = value.trimmed();
    if (text.isEmpty() || text.contains(QRegularExpression(QStringLiteral("\\s")))) return false;
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || url.isRelative() || url.host().trimmed().isEmpty()) return false;
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("https") || scheme == QStringLiteral("http");
}

bool validModelId(const QString &value) {
    const QString text = value.trimmed();
    if (text.size() < 1 || text.size() > 200) return false;
    static const QRegularExpression pattern(QStringLiteral(R"(^[A-Za-z0-9][A-Za-z0-9._:/@+\-]{0,199}$)"));
    return pattern.match(text).hasMatch();
}

bool validApiKey(const QString &value) {
    const QString text = value.trimmed();
    if (text.size() < 8 || text.size() > 512) return false;
    static const QRegularExpression pattern(QStringLiteral(R"(^\S{8,512}$)"));
    return pattern.match(text).hasMatch();
}

QString modelConfigValidationError(const QJsonObject &config) {
    const QString baseUrl = config.value(QStringLiteral("baseUrl")).toString().trimmed();
    const QString modelId = config.value(QStringLiteral("modelId")).toString().trimmed();
    const QString apiKey = config.value(QStringLiteral("apiKey")).toString().trimmed();
    if (baseUrl.isEmpty()) return QStringLiteral("请填写 Base URL。");
    if (!validModelBaseUrl(baseUrl)) return QStringLiteral("Base URL 格式无效，请填写完整的 http:// 或 https:// 地址。");
    if (modelId.isEmpty()) return QStringLiteral("请填写模型 ID。");
    if (!validModelId(modelId)) return QStringLiteral("模型 ID 格式无效，仅支持字母、数字及 . _ : / @ + -，且不能包含空格。");
    if (apiKey.isEmpty()) return QStringLiteral("请填写 API 密钥。");
    if (!validApiKey(apiKey)) return QStringLiteral("API 密钥格式无效：长度应为 8–512 个字符，且不能包含空格或换行。");
    return {};
}


QJsonObject canonicalVoiceConfig(const QJsonObject &config) {
    return QJsonObject{
        {QStringLiteral("provider"), QStringLiteral("baidu-realtime")},
        {QStringLiteral("appId"), config.value(QStringLiteral("appId")).toString().trimmed()},
        {QStringLiteral("apiKey"), config.value(QStringLiteral("apiKey")).toString().trimmed()}
    };
}

QString voiceConfigValidationError(const QJsonObject &config) {
    const QString appId = config.value(QStringLiteral("appId")).toString().trimmed();
    const QString apiKey = config.value(QStringLiteral("apiKey")).toString().trimmed();
    static const QRegularExpression appIdPattern(QStringLiteral(R"(^[0-9]{1,18}$)"));
    bool appIdNumberOk = false;
    const qlonglong appIdNumber = appId.toLongLong(&appIdNumberOk);
    if (appId.isEmpty()) return QStringLiteral("请填写 AppID。");
    if (!appIdPattern.match(appId).hasMatch() || !appIdNumberOk || appIdNumber <= 0)
        return QStringLiteral("AppID 格式无效，请填写百度语音应用的数字 AppID。");
    if (apiKey.isEmpty()) return QStringLiteral("请填写 API Key。");
    if (!validApiKey(apiKey)) return QStringLiteral("API Key 格式无效：长度应为 8–512 个字符，且不能包含空格或换行。");
    return {};
}


bool isTextMutationAttempt(QWidget *widget, QEvent *event) {
    if (!widget || !event) return false;
    if (event->type() == QEvent::InputMethod || event->type() == QEvent::Drop) return true;
    if (event->type() != QEvent::KeyPress) return false;

    auto *key = static_cast<QKeyEvent *>(event);
    if (key->matches(QKeySequence::Copy) || key->matches(QKeySequence::SelectAll)) return false;
    if (key->matches(QKeySequence::Paste) || key->matches(QKeySequence::Cut)
        || key->matches(QKeySequence::Undo) || key->matches(QKeySequence::Redo)) return true;

    switch (key->key()) {
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return qobject_cast<QTextEdit *>(widget) != nullptr;
    case Qt::Key_Left:
    case Qt::Key_Right:
    case Qt::Key_Up:
    case Qt::Key_Down:
    case Qt::Key_Home:
    case Qt::Key_End:
    case Qt::Key_PageUp:
    case Qt::Key_PageDown:
    case Qt::Key_Tab:
    case Qt::Key_Backtab:
    case Qt::Key_Escape:
        return false;
    default:
        break;
    }

    // Printable text without a command modifier would alter the editor if it
    // were writable. Keep Ctrl/Cmd navigation/copy shortcuts usable while the
    // running pet owns the configuration.
    const Qt::KeyboardModifiers commandModifiers = Qt::ControlModifier | Qt::MetaModifier | Qt::AltModifier;
    return !key->text().isEmpty() && !(key->modifiers() & commandModifiers);
}

bool isControlMutationAttempt(QWidget *widget, QEvent *event) {
    if (!widget || !event) return false;
    if (qobject_cast<AvatarWidget *>(widget)) {
        if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonDblClick) return false;
        return static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
    }
    if (qobject_cast<QLineEdit *>(widget) || qobject_cast<QTextEdit *>(widget))
        return isTextMutationAttempt(widget, event);
    if (auto *slider = qobject_cast<QSlider *>(widget)) {
        Q_UNUSED(slider)
        if (event->type() == QEvent::Wheel) return true;
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
            return static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton;
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            return key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down
                || key == Qt::Key_PageUp || key == Qt::Key_PageDown || key == Qt::Key_Home || key == Qt::Key_End;
        }
        return false;
    }
    if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
        if (!button->isCheckable()) return false;
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            if (static_cast<QMouseEvent *>(event)->button() != Qt::LeftButton) return false;
            // V126 radios are explicitly deselectable. Re-clicking a selected
            // radio is therefore a real mutation attempt, just like a check box.
            return true;
        }
        if (event->type() == QEvent::KeyPress) {
            const int key = static_cast<QKeyEvent *>(event)->key();
            if (key == Qt::Key_Space || key == Qt::Key_Return || key == Qt::Key_Enter)
                return true;
            if (qobject_cast<QRadioButton *>(button) && (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down))
                return true;
        }
    }
    return false;
}

QJsonArray sortedModelConfigs(const QJsonArray &source) {
    QList<QJsonObject> items;
    items.reserve(source.size());
    for (const QJsonValue &value : source) {
        if (value.isObject()) items.append(value.toObject());
    }
    std::stable_sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
        const qint64 aUpdated = a.value(QStringLiteral("updatedAt")).toInteger();
        const qint64 bUpdated = b.value(QStringLiteral("updatedAt")).toInteger();
        if (aUpdated != bUpdated) return aUpdated > bUpdated;
        return a.value(QStringLiteral("id")).toString() < b.value(QStringLiteral("id")).toString();
    });
    QJsonArray sorted;
    for (const QJsonObject &item : std::as_const(items)) sorted.append(item);
    return sorted;
}

QJsonArray newestPersonaPacksFirst(const QJsonArray &source) {
    QList<QJsonObject> items;
    items.reserve(source.size());
    for (const QJsonValue &value : source) {
        if (value.isObject()) items.append(value.toObject());
    }
    // Creation time, rather than update time, is intentional: editing a strength
    // slider must not unexpectedly move an existing card.  stable_sort preserves
    // the stored order for legacy/equal timestamps and keeps this display-only
    // ordering from rewriting the state file.
    std::stable_sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("createdAt")).toInteger()
            > b.value(QStringLiteral("createdAt")).toInteger();
    });
    QJsonArray sorted;
    for (const QJsonObject &item : std::as_const(items)) sorted.append(item);
    return sorted;
}
} // namespace

MainWindow::MainWindow(AppData *data, CoreClient *core, ProcessSupervisor *processes, QWidget *parent)
    : QMainWindow(parent), m_data(data), m_core(core), m_processes(processes) {
    setWindowTitle(QStringLiteral("Capricorn"));
    QIcon appIcon(QStringLiteral(":/resources/logo.svg"));
    if (appIcon.isNull()) appIcon = QIcon(QStringLiteral(":/resources/icon.ico"));
    setWindowIcon(appIcon);
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint |
                   Qt::WindowMinimizeButtonHint | Qt::WindowMaximizeButtonHint |
                   Qt::WindowCloseButtonHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    QSize initialSize(1180, 740);
    QSize startupMinimum(960, 640);
    if (QScreen *startupScreen = QGuiApplication::primaryScreen()) {
        const QSize workArea = startupScreen->availableGeometry().size() - QSize(16, 16);
        startupMinimum = QSize(qBound(320, workArea.width(), 960),
                               qBound(320, workArea.height(), 640));
        initialSize = QSize(qMax(startupMinimum.width(), qMin(1180, workArea.width())),
                            qMax(startupMinimum.height(), qMin(740, workArea.height())));
    }
    setMinimumSize(startupMinimum);
    resize(initialSize);

    m_motionFilter = new InteractiveMotionFilter(this);
    m_voiceClient = new VoiceRecognitionClient(this);
    // V90: resize is owned by eight explicit edge/corner handles plus native
    // Windows hit-testing.  The V76 application-wide event filter is intentionally
    // not installed: it could race Qt's window-state transition while maximizing,
    // stealing mouse events and repeatedly resetting cursors across every child.
    m_resizeFilter = nullptr;

    m_appShell = new QFrame(this);
    auto *root = m_appShell;
    root->setObjectName(QStringLiteral("appShell"));
    root->setProperty("maximized", false);
    auto *bodyLayout = new QHBoxLayout(root);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    m_sidebar = buildSidebar();
    bodyLayout->addWidget(m_sidebar);

    m_contentHost = new QWidget(root);
    m_contentHost->setObjectName(QStringLiteral("contentHost"));
    m_contentHost->setMinimumWidth(0);
    m_contentHost->installEventFilter(this);
    auto *contentLayout = new QVBoxLayout(m_contentHost);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    m_titleBar = buildWindowControls(m_contentHost);
    contentLayout->addWidget(m_titleBar, 0);

    m_views = new QStackedWidget(m_contentHost);
    m_views->setObjectName(QStringLiteral("mainPanel"));
    m_views->setMinimumWidth(0);
    m_views->setMaximumWidth(QWIDGETSIZE_MAX);
    m_views->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_personaPage = buildPersonaPage();
    m_questionPage = buildQuestionPage();
    for (QWidget *responsivePage : {m_personaPage, m_questionPage}) {
        if (auto *scroll = qobject_cast<QScrollArea *>(responsivePage); scroll && scroll->viewport()) {
            scroll->viewport()->setProperty("editPersonaResponsiveViewport", true);
            scroll->viewport()->installEventFilter(this);
        }
    }
    m_views->addWidget(m_personaPage);
    m_views->addWidget(buildPacksPage());
    m_views->addWidget(buildModelPage());
    m_views->addWidget(buildVoicePage());
    m_views->addWidget(m_questionPage);
    connect(m_voiceClient, &VoiceRecognitionClient::started, this, [this] {
        m_voiceStopPending = false;
        updateVoiceInputState();
    });
    connect(m_voiceClient, &VoiceRecognitionClient::interimText, this, &MainWindow::applyVoiceInterimText);
    connect(m_voiceClient, &VoiceRecognitionClient::finalText, this, &MainWindow::applyVoiceFinalText);
    connect(m_voiceClient, &VoiceRecognitionClient::errorOccurred, this, [this](const QString &message) {
        showModelNotice(QStringLiteral("语音识别"), message, true);
    });
    connect(m_voiceClient, &VoiceRecognitionClient::stopped, this, [this] {
        m_voiceStopPending = false;
        resetVoiceInterimTracking();
        if (m_views && m_views->currentIndex() == 4) {
            captureCurrentAnswer();
            saveDraft();
            renderProgress();
        }
        const int pendingView = m_pendingViewAfterVoiceStop;
        m_pendingViewAfterVoiceStop = -1;
        updateVoiceInputState();
        if (pendingView >= 0) QTimer::singleShot(0, this, [this, pendingView] { switchView(pendingView); });
    });
    contentLayout->addWidget(m_views, 1);
    bodyLayout->addWidget(m_contentHost, 1);
    setCentralWidget(root);
    m_windowOutline = new WindowOutlineOverlay(m_appShell);
    m_windowOutline->setObjectName(QStringLiteral("windowOutline"));
    m_windowOutline->setGeometry(m_appShell->rect());
    m_windowOutline->raise();

    const QList<QPair<QString, Qt::Edges>> resizeSpecs{
        {QStringLiteral("resizeLeft"), Qt::LeftEdge},
        {QStringLiteral("resizeRight"), Qt::RightEdge},
        {QStringLiteral("resizeTop"), Qt::TopEdge},
        {QStringLiteral("resizeBottom"), Qt::BottomEdge},
        {QStringLiteral("resizeTopLeft"), Qt::TopEdge | Qt::LeftEdge},
        {QStringLiteral("resizeTopRight"), Qt::TopEdge | Qt::RightEdge},
        {QStringLiteral("resizeBottomLeft"), Qt::BottomEdge | Qt::LeftEdge},
        {QStringLiteral("resizeBottomRight"), Qt::BottomEdge | Qt::RightEdge}
    };
    for (const auto &spec : resizeSpecs) {
        auto *handle = new WindowResizeHandle(this, spec.second, this);
        handle->setObjectName(spec.first);
        m_resizeHandles << handle;
    }
    updateResizeHandles();

    updateWindowControlIcons();
    setupTrayIcon();

    // V126: startup returns to the neutral "new persona" workspace instead of
    // restoring a viewed/edited saved persona, but an unfinished NEW-persona draft
    // is durable and must survive accidental exits. restoreDraft() loads only that
    // persistent draft and never restores m_editPackId / persona-view state.
    restoreDraft();

    connect(m_processes, &ProcessSupervisor::processError, this, [this](const QString &message) {
        QTimer::singleShot(0, this, [this, message] {
            if (isVisible()) showModelNotice(QStringLiteral("运行异常"), message, true);
        });
    });

    renderProgress();
    renderModules();
    renderPacks();
    loadModelConfig();
    loadVoiceConfig();
    updateVoiceInputState();
    switchView(0);
    QTimer::singleShot(0, this, [this] {
        applyResponsiveLayout();
        if (QWindow *handle = windowHandle()) {
            connect(handle, &QWindow::screenChanged, this, [this](QScreen *targetScreen) {
                m_personaMetricsScreenWidth = 0;
                m_personaMetricsScreenHeight = 0;
                applyResponsiveLayout();
                if (!targetScreen || actualWindowMaximized()) return;
                const QSize workArea = targetScreen->availableGeometry().size() - QSize(16, 16);
                const QSize bounded(qMin(width(), workArea.width()), qMin(height(), workArea.height()));
                resize(qMax(minimumWidth(), bounded.width()), qMax(minimumHeight(), bounded.height()));
            });
        }
    });
}

void MainWindow::startUiAuditCapture(const QString &directory) {
    QDir().mkpath(directory);

    QRect controlsGeometry;
    if (m_titleBar) {
        if (QWidget *controls = m_titleBar->findChild<QWidget *>(QStringLiteral("windowControls")))
            controlsGeometry = QRect(controls->mapTo(this, QPoint(0, 0)), controls->size());
    }
    const bool patrickPresent = !m_data->packById(QStringLiteral("patrick")).isEmpty();
    QJsonObject auditInfo{
        {QStringLiteral("version"), QStringLiteral("129.0.0")},
        {QStringLiteral("windowWidth"), width()},
        {QStringLiteral("windowHeight"), height()},
        {QStringLiteral("devicePixelRatio"), devicePixelRatioF()},
        {QStringLiteral("qtVersion"), QString::fromLatin1(qVersion())},
        {QStringLiteral("scaleFactor"), qEnvironmentVariable("QT_SCALE_FACTOR")},
        {QStringLiteral("fontDpi"), qEnvironmentVariable("QT_FONT_DPI")},
        {QStringLiteral("minimumWidth"), minimumWidth()},
        {QStringLiteral("minimumHeight"), minimumHeight()},
        {QStringLiteral("sidebarWidth"), m_sidebar ? m_sidebar->width() : 0},
        {QStringLiteral("windowControls"), QJsonObject{
            {QStringLiteral("x"), controlsGeometry.x()}, {QStringLiteral("y"), controlsGeometry.y()},
            {QStringLiteral("width"), controlsGeometry.width()}, {QStringLiteral("height"), controlsGeometry.height()}
        }},
        {QStringLiteral("patrickPresent"), patrickPresent},
        {QStringLiteral("policy"), QStringLiteral("Current audit is asynchronous; no nested processEvents or pre-event-loop capture.")}
    };
    QFile auditFile(QDir(directory).filePath(QStringLiteral("audit-info.json")));
    if (auditFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
        auditFile.write(QJsonDocument(auditInfo).toJson(QJsonDocument::Indented));

    struct AuditStep {
        QStringList windowNames;
        QString shellName;
        std::function<void()> action;
        int settleMs{320};
    };

    auto steps = std::make_shared<QList<AuditStep>>();
    steps->append(AuditStep{QStringList{QStringLiteral("v97-shell-normal.png")}, QString{}, [this] {
        setWindowMaximized(false); resize(1180, 740); switchView(0);
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("v97-shell-maximized.png")}, QString{}, [this] {
        setWindowMaximized(true);
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("v97-shell-minimum.png")}, QString{}, [this] {
        setWindowMaximized(false); resize(minimumSize());
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("persona-top.png")}, QStringLiteral("persona-shell-full.png"), [this] {
        setWindowMaximized(false); resize(1180, 740); switchView(0);
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("v95-persona-bottom-corner.png")}, QStringLiteral("v95-persona-bottom-shell.png"), [this] {
        switchView(0);
        if (auto *scroll = qobject_cast<QScrollArea *>(m_views ? m_views->currentWidget() : nullptr))
            scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("packs-top.png")}, QStringLiteral("packs-shell-full.png"), [this] { switchView(1); }});
    steps->append(AuditStep{QStringList{QStringLiteral("api-top.png")}, QStringLiteral("api-shell-full.png"), [this] { switchView(2); }});
    steps->append(AuditStep{QStringList{QStringLiteral("question-top.png"), QStringLiteral("01-module-open.png")}, QStringLiteral("question-shell-full.png"), [this] { openModule(0); }});
    steps->append(AuditStep{QStringList{QStringLiteral("02-option-selected.png")}, QString{}, [this] {
        const QList<QAbstractButton *> choices = m_optionsHost ? m_optionsHost->findChildren<QAbstractButton *>() : QList<QAbstractButton *>{};
        if (!choices.isEmpty()) choices.first()->click();
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("03-evidence-open.png")}, QString{}, [this] {
        if (m_evidenceToggle && !m_evidenceToggle->isChecked()) m_evidenceToggle->click();
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("04-next-question.png")}, QString{}, [this] {
        if (m_nextButton) m_nextButton->click();
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("05-pack-selected.png")}, QString{}, [this] { switchView(1); }});
    steps->append(AuditStep{QStringList{QStringLiteral("06-export-panel.png")}, QString{}, [this] {
        if (m_exportSelectedButton) m_exportSelectedButton->click();
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("07-api-custom.png")}, QString{}, [this] {
        switchView(2); if (m_providerButtons.size() > 4) m_providerButtons.at(4)->click();
    }});
    steps->append(AuditStep{QStringList{QStringLiteral("08-api-deepseek.png")}, QString{}, [this] {
        if (!m_providerButtons.isEmpty()) m_providerButtons.first()->click();
    }});

    auto index = std::make_shared<int>(0);
    auto runNext = std::make_shared<std::function<void()>>();
    QPointer<MainWindow> self(this);
    *runNext = [self, directory, steps, index, runNext] {
        if (!self) return;
        if (*index >= steps->size()) {
            auto *chat = new ChatWindow(self->m_core, self.data());
            chat->setAttribute(Qt::WA_DeleteOnClose);
            chat->setWindowFlag(Qt::Window, true);
            chat->setSession(QStringLiteral("ui-audit"), QStringLiteral("patrick"), QStringLiteral("派大星"));
            chat->setAuditConversation();
            chat->setAuditComposeText();
            chat->show();
            QPointer<ChatWindow> chatGuard(chat);
            QTimer::singleShot(320, self.data(), [chatGuard, directory] {
                if (!chatGuard) return;
                chatGuard->grab().save(QDir(directory).filePath(QStringLiteral("chat.png")), "PNG");
                chatGuard->close();
            });

            auto *pet = new PetWindow(self.data());
            pet->setAttribute(Qt::WA_DeleteOnClose);
            pet->setWindowFlag(Qt::Window, true);
            QFile logo(QStringLiteral(":/resources/logo.svg"));
            if (logo.open(QIODevice::ReadOnly)) pet->setAvatarFrames({logo.readAll()}, QStringLiteral("派大星"), QStringLiteral("builtin-avatar-01"), QStringLiteral("Capricorn"));
            pet->showAuditPrompt();
            pet->show();
            QPointer<PetWindow> petGuard(pet);
            QTimer::singleShot(320, self.data(), [petGuard, directory] {
                if (!petGuard) return;
                petGuard->grab().save(QDir(directory).filePath(QStringLiteral("pet-prompt.png")), "PNG");
                petGuard->close();
            });
            QTimer::singleShot(900, self.data(), [] { QCoreApplication::quit(); });
            return;
        }

        const AuditStep step = steps->at((*index)++);
        if (step.action) step.action();
        QTimer::singleShot(step.settleMs, self.data(), [self, directory, step, runNext] {
            if (!self || !self->isVisible()) return;
            const QPixmap windowImage = self->grab();
            for (const QString &name : step.windowNames)
                windowImage.save(QDir(directory).filePath(name), "PNG");
            if (!step.shellName.isEmpty() && self->m_appShell)
                self->m_appShell->grab().save(QDir(directory).filePath(step.shellName), "PNG");
            (*runNext)();
        });
    };
    QTimer::singleShot(250, this, [runNext] { (*runNext)(); });
}

MainWindow::~MainWindow() {
    if (m_voiceClient) m_voiceClient->stop();
}

bool MainWindow::actualWindowMaximized() const {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) return IsZoomed(hwnd) != FALSE;
#endif
    return isMaximized() || windowState().testFlag(Qt::WindowMaximized);
}

void MainWindow::updateWindowControlIcons() {
    if (!m_minimizeControl || !m_maximizeControl || !m_closeControl) return;
    const bool maximized = m_windowMaximizedState;
    m_minimizeControl->setIcon(QIcon(QStringLiteral(":/resources/window-minimize.svg")));
    m_maximizeControl->setIcon(QIcon(maximized
                                        ? QStringLiteral(":/resources/window-restore.svg")
                                        : QStringLiteral(":/resources/window-maximize.svg")));
    m_maximizeControl->setToolTip(maximized ? QStringLiteral("还原") : QStringLiteral("最大化"));
    m_maximizeControl->setAccessibleName(maximized ? QStringLiteral("还原窗口") : QStringLiteral("最大化窗口"));
    m_maximizeControl->setProperty("windowIsMaximized", maximized);
    m_maximizeControl->update();
    m_closeControl->setIcon(QIcon(QStringLiteral(":/resources/window-close.svg")));
    setDynamicProperty(m_appShell, "maximized", maximized);
    if (m_appShell && m_appShell->layout())
        m_appShell->layout()->setContentsMargins(0, 0, 0, 0);
    if (m_windowOutline) {
        m_windowOutline->setVisible(!maximized);
        m_windowOutline->raise();
        m_windowOutline->update();
    }
    updateWindowShape();
}

void MainWindow::synchronizeWindowState() {
    m_windowMaximizedState = actualWindowMaximized();
    updateWindowControlIcons();
    applyResponsiveLayout();
    updateResizeHandles();
}

void MainWindow::setWindowMaximized(bool maximized) {
    // One transition path for the button and all programmatic requests. On
    // Windows, use the native ShowWindow state change used by mainstream desktop
    // apps; the WM_SIZE/WindowStateChange notifications remain the source of truth.
    m_windowMaximizedState = maximized;
    updateWindowControlIcons();
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    if (hwnd) ShowWindow(hwnd, maximized ? SW_MAXIMIZE : SW_RESTORE);
    else if (maximized) showMaximized(); else showNormal();
#else
    if (maximized) showMaximized(); else showNormal();
#endif
    // WindowStateChange is the normal synchronization source.  One queued pass is
    // enough for programmatic maximize/restore and avoids V76's repeated layout
    // bursts while Windows is still completing the transition.
    QTimer::singleShot(0, this, &MainWindow::synchronizeWindowState);
}

void MainWindow::toggleWindowMaximize() {
    setWindowMaximized(!actualWindowMaximized());
}

void MainWindow::updateWindowShape() {
    // V70 keeps QWidget masks disabled for Qt 6.11 stability. The rounded
    // appShell is the only full-window opaque surface. Every full-size child,
    // scroll viewport and scrollbar container remains transparent, so no child
    // can repaint the lower-right corner as a square when scrolled to the end.
    clearMask();
    if (m_appShell) m_appShell->clearMask();
}


void MainWindow::relayoutGrid(QGridLayout *layout, const QList<QWidget *> &widgets, int columns, bool spanLast) {
    if (!layout || widgets.isEmpty()) return;
    columns = qMax(1, columns);

    // V90: relayout only when the topology changes.  V76 removed and re-added
    // every child on every resize event; maximizing a frameless window can emit a
    // burst of resize/state messages and that unnecessary churn could stall the UI.
    const bool sameTopology = layout->property("responsiveColumns").toInt() == columns
        && layout->property("responsiveSpanLast").toBool() == spanLast
        && layout->property("responsiveItemCount").toInt() == widgets.size();
    if (sameTopology) return;

    for (QWidget *widget : widgets) layout->removeWidget(widget);
    for (int column = 0; column < 12; ++column) layout->setColumnStretch(column, 0);
    for (int index = 0; index < widgets.size(); ++index) {
        QWidget *widget = widgets.at(index);
        widget->setSizePolicy(QSizePolicy::Ignored, widget->sizePolicy().verticalPolicy());
        const bool finalSpan = spanLast && index == widgets.size() - 1 && (index % columns) == 0 && columns > 1;
        layout->addWidget(widget, index / columns, index % columns, 1, finalSpan ? columns : 1);
    }
    for (int column = 0; column < columns; ++column) layout->setColumnStretch(column, 1);
    layout->setProperty("responsiveColumns", columns);
    layout->setProperty("responsiveSpanLast", spanLast);
    layout->setProperty("responsiveItemCount", widgets.size());
    layout->invalidate();
}

void MainWindow::updatePersonaLayoutMetrics() {
    // Resolve stable card metrics from the monitor's logical work area. Normal
    // monitors keep the established V90 proportions; narrow/portrait monitors
    // reduce the column count instead of squeezing fixed card internals together.
    constexpr int kRegularSidebarWidth = 236;
    constexpr int kBaseCardWidth = 335;
    constexpr int kBaseGap = 18;
    constexpr int kBaseMaxContent = kBaseCardWidth * 3 + kBaseGap * 4;
    constexpr int kMinimumReadableCardWidth = 280;
    constexpr int kMaximumReadableCardWidth = 400;
    constexpr int kMaximumLayoutContentWidth = 1204;
    constexpr int kMinimumPersonaColumns = 2;

    QScreen *targetScreen = screen();
    if (!targetScreen) targetScreen = QGuiApplication::primaryScreen();
    const QRect workArea = targetScreen ? targetScreen->availableGeometry() : QRect(0, 0, qMax(width(), 1180), 740);
    const int screenWidth = qMax(320, workArea.width() - 16);
    if (screenWidth == m_personaMetricsScreenWidth && workArea.height() == m_personaMetricsScreenHeight
        && m_personaCardWidth > 0 && m_personaCardGap > 0) return;

    const int sidebarWidth = screenWidth < 900 ? qBound(184, screenWidth / 4, 220)
                                                : kRegularSidebarWidth;
    const int maxContent = qMax(1, screenWidth - sidebarWidth);
    const int layoutContent = qMin(maxContent, kMaximumLayoutContentWidth);
    const qreal scale = qreal(layoutContent) / qreal(kBaseMaxContent);
    const int chosenGap = qBound(12, qRound(kBaseGap * scale), 22);
    int maximumColumns = kMinimumPersonaColumns;
    if (layoutContent >= kMinimumReadableCardWidth * 3 + chosenGap * 4) maximumColumns = 3;
    const int rawCardWidth = (layoutContent - chosenGap * (maximumColumns + 1)) / maximumColumns;
    const int chosenCard = qBound(kMinimumReadableCardWidth, rawCardWidth, kMaximumReadableCardWidth);

    m_personaMetricsScreenWidth = screenWidth;
    m_personaMetricsScreenHeight = workArea.height();
    m_cachedSidebarWidth = sidebarWidth;
    m_personaCardGap = chosenGap;
    m_personaCardWidth = chosenCard;
    m_personaMaximumColumns = maximumColumns;
    // The lower resize boundary is derived from the existing card geometry; it
    // does not alter G.  At minimum width the row is exactly:
    // G + card + G + card + G, so both cards remain visible and all three gaps
    // are equal.  Do not clamp this invariant back to one column on a small screen.
    m_personaMinimumWindowWidth = sidebarWidth
        + m_personaCardWidth * kMinimumPersonaColumns
        + m_personaCardGap * (kMinimumPersonaColumns + 1);
    m_responsiveMinimumHeight = qBound(320, workArea.height() - 16, 640);

    if (minimumSize() != QSize(m_personaMinimumWindowWidth, m_responsiveMinimumHeight))
        setMinimumSize(m_personaMinimumWindowWidth, m_responsiveMinimumHeight);
}

void MainWindow::applyResponsiveLayout() {
    if (!m_contentHost || !m_sidebar || m_applyingResponsiveLayout) return;
    m_applyingResponsiveLayout = true;

    // The sidebar/divider is a stable visual anchor.  Keeping it fixed also keeps
    // the first persona card at one invariant distance from the divider throughout
    // live resizing, instead of drifting as a percentage of the window width.
    updatePersonaLayoutMetrics();
    const int sidebarWidth = m_cachedSidebarWidth;
    if (m_sidebar->width() != sidebarWidth) m_sidebar->setFixedWidth(sidebarWidth);

    const int contentWidth = qMax(1, width() - sidebarWidth);

    // V118: preserve the established persona-management gutter invariant while
    // using that same visual boundary for Edit Persona and Model Connection.
    // G never changes during live resize. Persona Management keeps a zero-margin
    // page because its grid owns the outer G gutters itself; every other workspace
    // uses G as its left/right page margin. At the minimum window width the first
    // persona row is therefore exactly: G + card + G + card + G.
    const int pageMargin = m_personaCardGap;
    for (QLayout *layout : std::as_const(m_pageLayouts)) {
        if (!layout || layout == m_packsPageLayout) continue;
        layout->setContentsMargins(pageMargin, 18, pageMargin, 28);
    }
    if (m_packsPageLayout) m_packsPageLayout->setContentsMargins(0, 18, 0, 28);
    if (m_packsHeaderLayout) m_packsHeaderLayout->setContentsMargins(pageMargin, 0, pageMargin, 0);
    if (m_exportPanel && m_exportPanel->parentWidget()
        && m_exportPanel->parentWidget()->objectName() == QStringLiteral("exportOuterHost")
        && m_exportPanel->parentWidget()->layout()) {
        m_exportPanel->parentWidget()->layout()->setContentsMargins(pageMargin, 10, pageMargin, 0);
    }

    // V129: page geometry must follow the real QScrollArea viewport, not the
    // theoretical main-window content width.  A vertical scrollbar and DPI
    // rounding can make the viewport narrower while the outer window width stays
    // unchanged; if the child keeps the larger width, every right-hand control is
    // clipped together.  Keep the page content hard-bounded to its current
    // viewport and use that same width for all Edit Persona responsive decisions.
    auto syncScrollViewportWidth = [](QWidget *page, int fallbackWidth) {
        auto *scroll = qobject_cast<QScrollArea *>(page);
        if (!scroll || !scroll->viewport() || !scroll->widget()) return qMax(1, fallbackWidth);
        const int viewportWidth = scroll->viewport()->width();
        if (viewportWidth <= 0) return qMax(1, fallbackWidth);
        QWidget *content = scroll->widget();
        content->setMinimumWidth(0);
        if (content->maximumWidth() != viewportWidth) content->setMaximumWidth(viewportWidth);
        if (content->width() > viewportWidth) content->resize(viewportWidth, content->height());
        return viewportWidth;
    };
    const int personaViewportWidth = syncScrollViewportWidth(m_personaPage, contentWidth);
    const int questionViewportWidth = syncScrollViewportWidth(m_questionPage, contentWidth);
    const int personaUsable = qMax(1, personaViewportWidth - pageMargin * 2);
    const int questionUsable = qMax(1, questionViewportWidth - pageMargin * 2);
    const int usable = qMax(1, contentWidth - pageMargin * 2);
    const bool compact = qMin(contentWidth, personaViewportWidth) < 820;
    const bool compactDensity = compact || height() < 680;
    setDynamicProperty(m_appShell, "uiDensity",
                       compactDensity ? QStringLiteral("compact") : QStringLiteral("comfortable"));

    for (QPushButton *button : std::as_const(m_navButtons)) {
        if (!button) continue;
        button->setFixedHeight(compactDensity ? 40 : 44);
        button->setIconSize(compactDensity ? QSize(18, 18) : QSize(20, 20));
    }
    if (QFrame *brand = m_sidebar->findChild<QFrame *>(QStringLiteral("brandMark")))
        brand->setFixedSize(compactDensity ? QSize(34, 34) : QSize(38, 38));
    for (QWidget *card : std::as_const(m_moduleCards))
        if (card) card->setFixedHeight(compactDensity ? 116 : 123);
    for (QPushButton *provider : std::as_const(m_providerButtons))
        if (provider) provider->setFixedHeight(compactDensity ? 46 : 50);
    if (m_savedModelConfigsScroll)
        m_savedModelConfigsScroll->setFixedHeight(compactDensity ? 180 : 200);

    const int personaProgressColumns = personaUsable >= 600 ? 3 : (personaUsable >= 390 ? 2 : 1);
    const int questionProgressColumns = questionUsable >= 600 ? 3 : (questionUsable >= 390 ? 2 : 1);
    const int ringSize = compact ? 64 : 78;
    for (CircularProgressWidget *ring : {m_professionalRing, m_privateRing, m_coreRing,
                                         m_questionProfessionalRing, m_questionPrivateRing, m_questionCoreRing})
        if (ring) ring->setFixedSize(ringSize, ringSize);
    relayoutGrid(m_personaProgressGrid, m_personaProgressCards, personaProgressColumns, false);
    relayoutGrid(m_questionProgressGrid, m_questionProgressCards, questionProgressColumns, false);
    // V91: keep the question-module overview at two columns even at the
    // main window's minimum resize boundary. The window minimum is already
    // derived from a two-card layout, so a live resize must not collapse this
    // page to one column and then disagree with the layout after navigation.
    relayoutGrid(m_moduleGrid, m_moduleCards, 2, false);

    for (QGridLayout *avatarGrid : std::as_const(m_avatarGrids)) {
        QList<QWidget *> avatars;
        for (int index = 0; index < avatarGrid->count(); ++index) {
            if (QWidget *widget = avatarGrid->itemAt(index)->widget()) avatars << widget;
        }
        relayoutGrid(avatarGrid, avatars, qBound(4, personaUsable / 108, 8), false);
    }

    if (m_packGrid) {
        const int fittingColumns = qBound(1,
            qMax(1, contentWidth - m_personaCardGap)
                / (qMax(1, m_personaCardWidth) + m_personaCardGap),
            m_personaMaximumColumns);
        const int desiredColumns = qMin(actualWindowMaximized() ? 3 : 2, fittingColumns);
        const int packColumns = qMax(1, qMin(desiredColumns, qMax(1, m_packCards.size())));

        if (m_packGridHost) {
            m_packGridHost->setMinimumWidth(0);
            m_packGridHost->setMaximumWidth(QWIDGETSIZE_MAX);
            m_packGridHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }

        // The outer gutters and the gap between cards are the same immutable G.
        // Cards absorb only the extra width; the gap itself is never stretched or
        // recomputed. This restores the established fixed-gutter rule and simultaneously gives the
        // page the same left/right visual boundary used by the other workspaces.
        m_packGrid->setContentsMargins(m_personaCardGap, 0, m_personaCardGap, 0);
        m_packGrid->setHorizontalSpacing(m_personaCardGap);
        m_packGrid->setVerticalSpacing(18);
        for (QWidget *card : std::as_const(m_packCards)) {
            if (!card) continue;
            card->setMinimumWidth(m_personaCardWidth);
            card->setMaximumWidth(QWIDGETSIZE_MAX);
            card->setFixedHeight(198);
            card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        }

        const int topologyKey = packColumns * 100000000
            + qMin(m_packCards.size(), 99) * 1000000
            + qMin(m_personaCardWidth, 9999) * 100
            + qMin(m_personaCardGap, 99);
        if (m_packGridColumns != topologyKey) {
            for (QWidget *widget : std::as_const(m_packCards)) m_packGrid->removeWidget(widget);
            for (int column = 0; column < 12; ++column) {
                m_packGrid->setColumnStretch(column, 0);
                m_packGrid->setColumnMinimumWidth(column, 0);
            }
            for (int column = 0; column < packColumns; ++column) {
                m_packGrid->setColumnStretch(column, 1);
                m_packGrid->setColumnMinimumWidth(column, m_personaCardWidth);
            }
            for (int index = 0; index < m_packCards.size(); ++index) {
                QWidget *card = m_packCards.at(index);
                m_packGrid->addWidget(card, index / packColumns, index % packColumns);
            }
            m_packGridColumns = topologyKey;
        }
        m_packGrid->setAlignment(Qt::AlignTop);
    }

    if (m_optionsLayout && !m_optionWidgets.isEmpty()) {
        int optionColumns = questionUsable >= 620 ? 2 : 1;
        if (m_currentQuestionType == QStringLiteral("scale"))
            optionColumns = questionUsable >= 700 ? 7 : (questionUsable >= 440 ? 4 : 3);
        relayoutGrid(m_optionsLayout, m_optionWidgets, optionColumns, false);
    }

    if (m_personaIdentityLayout) {
        const bool vertical = personaUsable < 440;
        m_personaIdentityLayout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        if (QWidget *rowHost = m_personaIdentityLayout->parentWidget()) {
            if (QWidget *identity = rowHost->parentWidget())
                identity->setFixedHeight(vertical ? 164 : 104);
        }
        if (m_savePersonaButton) {
            m_savePersonaButton->setMaximumWidth(vertical ? QWIDGETSIZE_MAX : 180);
            m_savePersonaButton->setSizePolicy(vertical ? QSizePolicy::Expanding : QSizePolicy::Preferred,
                                               QSizePolicy::Fixed);
        }
    }

    auto setHeaderDirection = [](QBoxLayout *layout, bool vertical) {
        if (!layout) return;
        layout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        layout->setAlignment(vertical ? Qt::AlignTop : Qt::Alignment{});
    };
    setHeaderDirection(m_personaHeaderLayout, personaUsable < 420);
    setHeaderDirection(m_packsHeaderLayout, usable < 560);
    setHeaderDirection(m_answerHeaderLayout, questionUsable < 470);
    setHeaderDirection(m_exportHeaderLayout, usable < 500);

    if (m_exportModulesLayout && m_exportModulesLayout->count() > 0) {
        QList<QWidget *> modules;
        for (int index = 0; index < m_exportModulesLayout->count(); ++index)
            if (QWidget *widget = m_exportModulesLayout->itemAt(index)->widget()) modules << widget;
        // V118: export-module selection is a stable three-column grid at all
        // supported main-window widths.  The minimum window width already leaves
        // enough room for three readable cards.
        relayoutGrid(m_exportModulesLayout, modules, 3, false);
    }

    if (m_providerLayout && !m_providerButtons.isEmpty()) {
        QList<QWidget *> providers;
        for (QPushButton *button : std::as_const(m_providerButtons)) providers << button;
        relayoutGrid(m_providerLayout, providers, usable >= 560 ? 2 : 1, true);
    }

    if (m_questionBodyLayout && m_questionBodyLayout->count() >= 2) {
        const bool vertical = questionUsable < 610;
        m_questionBodyLayout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        if (QWidget *rail = m_questionBodyLayout->itemAt(0)->widget()) {
            if (vertical) { rail->setMinimumWidth(0); rail->setMaximumWidth(QWIDGETSIZE_MAX); }
            else rail->setFixedWidth(222);
        }
    }
    if (m_modelColumnsLayout && m_modelColumnsLayout->count() >= 2) {
        const bool vertical = usable < 650;
        m_modelColumnsLayout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        if (QWidget *sync = m_modelColumnsLayout->itemAt(1)->widget()) {
            sync->setMinimumWidth(vertical ? 0 : 320);
            sync->setMaximumWidth(vertical ? QWIDGETSIZE_MAX : 320);
        }
    }
    if (m_exportColumnsLayout && m_exportColumnsLayout->count() >= 2) {
        const bool vertical = usable < 650;
        m_exportColumnsLayout->setDirection(vertical ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        if (QWidget *summary = m_exportColumnsLayout->itemAt(1)->widget())
            summary->setMaximumWidth(vertical ? QWIDGETSIZE_MAX : 360);
    }

    m_applyingResponsiveLayout = false;
}

void MainWindow::scrollCurrentPageToTop() {
    if (!m_views) return;
    if (auto *scroll = qobject_cast<QScrollArea *>(m_views->currentWidget()))
        scroll->verticalScrollBar()->setValue(0);
}

void MainWindow::updateResizeHandles() {
    if (m_resizeHandles.size() != 8) return;
    const bool available = !actualWindowMaximized();
    const int w = qMax(0, width());
    const int h = qMax(0, height());
    constexpr int edge = 16;
    constexpr int corner = 34;
    constexpr int controlWidth = 138;
    constexpr int controlHeight = 58;

    // The top-right 138x58 control cluster is an explicit no-resize zone.  The
    // top-right diagonal grip therefore lives immediately to its left; all other
    // edges/corners cover their entire exposed boundary continuously.
    const int topUsableWidth = qMax(0, w - controlWidth);
    const QList<QRect> geometries{
        QRect(0, 0, edge, h),
        QRect(qMax(0, w - edge), qMin(controlHeight, h), edge, qMax(0, h - controlHeight)),
        QRect(0, 0, topUsableWidth, edge),
        QRect(0, qMax(0, h - edge), w, edge),
        QRect(0, 0, corner, corner),
        QRect(qMax(0, topUsableWidth - corner), 0, corner, corner),
        QRect(0, qMax(0, h - corner), corner, corner),
        QRect(qMax(0, w - corner), qMax(0, h - corner), corner, corner)
    };
    for (int i = 0; i < m_resizeHandles.size(); ++i) {
        QWidget *handle = m_resizeHandles.at(i);
        handle->setGeometry(geometries.at(i));
        handle->setVisible(available && !geometries.at(i).isEmpty());
        if (handle->isVisible()) handle->raise();
    }
    if (available) {
        for (int i = 4; i < m_resizeHandles.size(); ++i) m_resizeHandles.at(i)->raise();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    applyResponsiveLayout();
    updateWindowShape();
    if (m_windowOutline && m_appShell) {
        m_windowOutline->setGeometry(m_appShell->rect());
        m_windowOutline->raise();
    }
    updateResizeHandles();
}



void MainWindow::changeEvent(QEvent *event) {
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        QTimer::singleShot(0, this, &MainWindow::synchronizeWindowState);
}

void MainWindow::setupTrayIcon() {
    m_trayIcon = new QSystemTrayIcon(QIcon(QStringLiteral(":/resources/logo.svg")), this);
    if (m_trayIcon->icon().isNull()) m_trayIcon->setIcon(QIcon(QStringLiteral(":/resources/icon.ico")));
    m_trayIcon->setToolTip(QStringLiteral("Capricorn"));

    m_trayMenu = new QMenu(this);
    QAction *restoreAction = m_trayMenu->addAction(QStringLiteral("打开 Capricorn"));
    m_trayMenu->addSeparator();
    QAction *exitAction = m_trayMenu->addAction(QStringLiteral("完全退出"));
    m_trayIcon->setContextMenu(m_trayMenu);

    connect(restoreAction, &QAction::triggered, this, &MainWindow::restoreFromTray);
    connect(exitAction, &QAction::triggered, this, &MainWindow::beginFastExit);
    connect(m_trayIcon, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            restoreFromTray();
    });
}

void MainWindow::minimizeToTray() {
    if (m_exitRequested) return;

    // V62: folding the main shell must not change the pet/chat state. A visible
    // pet or conversation remains visible and interactive; a hidden one stays hidden.
    if (QSystemTrayIcon::isSystemTrayAvailable() && m_trayIcon) {
        hide();
        m_trayIcon->show();
        return;
    }

    // Restricted sessions without a notification area use the taskbar fallback;
    // auxiliary windows remain untouched here as well.
    showMinimized();
}

void MainWindow::restoreFromTray() {
    if (m_exitRequested) return;
    if (m_trayIcon) m_trayIcon->hide();
    if (isMinimized()) showNormal();
    else show();
    raise();
    activateWindow();
}

void MainWindow::beginFastExit() {
    if (m_exitRequested) return;
    m_exitRequested = true;

    // The visual close happens first. Cleanup continues for only a short bounded
    // interval while every application window is already gone from the desktop.
    if (m_chatWindow) m_chatWindow->hide();
    if (m_petWindow) m_petWindow->hide();
    if (m_trayIcon) m_trayIcon->hide();
    hide();

    m_core->shutdown();
    connect(m_processes, &ProcessSupervisor::stopped, qApp, &QCoreApplication::quit,
            Qt::SingleShotConnection);
    m_processes->stopAsync(180, 120);

    // Hard UX deadline: even if a damaged child process never reports exit, the
    // Qt event loop ends promptly; the Windows Job Object then removes leftovers.
    QTimer::singleShot(500, qApp, &QCoreApplication::quit);
}

QWidget *MainWindow::buildWindowControls(QWidget *parent) {
    auto *chrome = new QWidget(parent);
    chrome->setObjectName(QStringLiteral("windowChrome"));
    chrome->setFixedHeight(58);

    auto *chromeLayout = new QHBoxLayout(chrome);
    chromeLayout->setContentsMargins(18, 0, 0, 0);
    chromeLayout->setSpacing(0);

    m_windowDragArea = new QWidget(chrome);
    m_windowDragArea->setObjectName(QStringLiteral("windowDragArea"));
    m_windowDragArea->setProperty("windowDragZone", true);
#ifndef Q_OS_WIN
    m_windowDragArea->installEventFilter(this);
#endif
    m_windowDragArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chromeLayout->addWidget(m_windowDragArea, 1);

    auto *cluster = new QWidget(chrome);
    m_windowControlsCluster = cluster;
    cluster->setObjectName(QStringLiteral("windowControls"));
    cluster->setProperty("excludeWindowDrag", true);
    cluster->setFixedSize(138, 58);
    auto *layout = new QHBoxLayout(cluster);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto makeControl = [cluster](const QString &name, const QString &tip) {
        auto *button = new QToolButton(cluster);
        button->setObjectName(name);
        button->setProperty("excludeWindowDrag", true);
        button->setToolTip(tip);
        button->setAutoRaise(true);
        button->setIconSize(QSize(16, 16));
        button->setFixedSize(46, 58);
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty("excludeWindowDrag", true);
        return button;
    };
    m_minimizeControl = makeControl(QStringLiteral("windowMinimize"), QStringLiteral("最小化"));
    m_maximizeControl = makeControl(QStringLiteral("windowMaximize"), QStringLiteral("最大化"));
    m_closeControl = makeControl(QStringLiteral("windowClose"), QStringLiteral("关闭"));
    layout->addWidget(m_minimizeControl);
    layout->addWidget(m_maximizeControl);
    layout->addWidget(m_closeControl);
    chromeLayout->addWidget(cluster, 0, Qt::AlignRight | Qt::AlignTop);
    cluster->raise();
    m_minimizeControl->raise();
    m_maximizeControl->raise();
    m_closeControl->raise();

    connect(m_minimizeControl, &QToolButton::clicked, this, &QWidget::showMinimized);
    connect(m_maximizeControl, &QToolButton::clicked, this, &MainWindow::toggleWindowMaximize);
    connect(m_closeControl, &QToolButton::clicked, this, &QWidget::close);
    return chrome;
}

QWidget *MainWindow::buildSidebar() {
    auto *sidebar = new QWidget(this);
    sidebar->setObjectName(QStringLiteral("sidebar"));
    sidebar->setFixedWidth(236);
    auto *layout = new QVBoxLayout(sidebar);
    layout->setContentsMargins(15, 22, 15, 20);
    layout->setSpacing(6);

    auto *brandHost = new QWidget(sidebar);
    brandHost->setObjectName(QStringLiteral("brand"));
    auto *brand = new QHBoxLayout(brandHost);
    brand->setContentsMargins(8, 4, 8, 25);
    brand->setSpacing(10);
    auto *mark = new QFrame(brandHost);
    mark->setObjectName(QStringLiteral("brandMark"));
    mark->setFixedSize(38, 38);
    mark->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    auto *markLayout = new QVBoxLayout(mark);
    markLayout->setContentsMargins(5, 5, 5, 5);
    auto *logo = new QLabel(mark);
    QIcon brandIcon(QStringLiteral(":/resources/logo.svg"));
    if (brandIcon.isNull()) brandIcon = QIcon(QStringLiteral(":/resources/icon.ico"));
    logo->setPixmap(brandIcon.pixmap(27, 27));
    logo->setAlignment(Qt::AlignCenter);
    logo->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    markLayout->addWidget(logo);
    auto *name = new QLabel(QStringLiteral("Capricorn"), brandHost);
    name->setObjectName(QStringLiteral("brandText"));
    name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    brand->addWidget(mark);
    brand->addWidget(name);
    brand->addStretch();
    layout->addWidget(brandHost);

    const QStringList labels{QStringLiteral("编辑人格"), QStringLiteral("人格管理"), QStringLiteral("构建宠物"), QStringLiteral("语音配置")};
    const QStringList icons{QStringLiteral(":/resources/edit-persona.svg"),
                            QStringLiteral(":/resources/persona-management.svg"),
                            QStringLiteral(":/resources/build-pet.svg"),
                            QStringLiteral(":/resources/voice-config.svg")};
    for (int index = 0; index < labels.size(); ++index) {
        // QPushButton's platform layout leaves only a very narrow icon/text gap.
        // One en-space adds a restrained visual pause without shifting the shared
        // icon column or changing the hit area shown in the sidebar.
        auto *button = new QPushButton(QString(QChar(0x2002)) + labels.at(index), sidebar);
        button->setAccessibleName(labels.at(index));
        button->setIcon(QIcon(icons.at(index)));
        button->setIconSize(QSize(20, 20));
        button->setObjectName(QStringLiteral("navButton"));
        button->setCheckable(true);
        button->setFixedHeight(40);
        button->setAutoExclusive(true);
        connect(button, &QPushButton::clicked, this, [this, index] {
            if (index == 0 && !m_editPackId.isEmpty()) restoreDraft();
            switchView(index);
        });
        layout->addWidget(button);
        m_navButtons << button;
    }

    // Decorative brand motto: use the user-approved artwork itself rather than
    // approximating its calligraphy with a locally installed font.
    layout->addStretch();
    auto *motto = new QLabel(sidebar);
    motto->setObjectName(QStringLiteral("sidebarMotto"));
    motto->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    motto->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    motto->setFixedHeight(66);
    motto->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    const QPixmap mottoArtwork(QStringLiteral(":/resources/sidebar-motto-art.png"));
    if (!mottoArtwork.isNull()) {
        motto->setPixmap(mottoArtwork.scaled(QSize(190, 60), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        motto->setText(QStringLiteral("Let it unfold"));
    }
    layout->addWidget(motto);
    layout->addSpacing(18);
    return sidebar;
}

QWidget *MainWindow::makeHeader(const QString &eyebrow, const QString &title, const QString &subtitle) {
    auto *host = new QWidget(this);
    host->setObjectName(QStringLiteral("pageHeader"));
    host->setMinimumHeight(87);
    auto *layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *e = new QLabel(eyebrow, host);
    e->setObjectName(QStringLiteral("eyebrow"));
    auto *t = new QLabel(title, host);
    t->setObjectName(QStringLiteral("pageTitle"));
    e->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    t->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    layout->addWidget(e);
    layout->addSpacing(6);
    layout->addWidget(t);
    if (!subtitle.trimmed().isEmpty()) {
        auto *s = new QLabel(subtitle, host);
        s->setObjectName(QStringLiteral("pageSubtitle"));
        s->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        s->setWordWrap(true);
        layout->addSpacing(6);
        layout->addWidget(s);
    } else {
        host->setMinimumHeight(66);
    }
    layout->addStretch(1);
    return host;
}

QWidget *MainWindow::makeProgressCard(const QString &title, QLabel **detail,
                                      CircularProgressWidget **ring, const QString &objectName,
                                      const QString &centerSuffix) {
    auto *card = new QFrame(this);
    card->setObjectName(objectName);
    card->setFixedHeight(112);
    auto *layout = new QHBoxLayout(card);
    layout->setContentsMargins(12, 14, 12, 14);
    layout->setSpacing(10);
    *ring = new CircularProgressWidget(card);
    (*ring)->setFixedSize(78, 78);
    (*ring)->setSecondaryText(centerSuffix);
    if (objectName == QStringLiteral("progressCardPrivate")) {
        (*ring)->setAccentColor(QColor(QStringLiteral("#F0A23A")));
        (*ring)->setTrackColor(QColor(QStringLiteral("#F8E8CE")));
    } else if (objectName == QStringLiteral("progressCardCore")) {
        (*ring)->setAccentColor(QColor(QStringLiteral("#4F8CFF")));
        (*ring)->setTrackColor(QColor(QStringLiteral("#DCE9FA")));
    }
    auto *copy = new QVBoxLayout;
    copy->setSpacing(5);
    auto *name = new QLabel(title, card);
    name->setObjectName(QStringLiteral("progressTitle"));
    *detail = new QLabel(card);
    (*detail)->setObjectName(QStringLiteral("progressDetail"));
    (*detail)->hide();
    copy->addStretch();
    copy->addWidget(name);
    copy->addStretch();
    layout->addWidget(*ring);
    layout->addLayout(copy, 1);
    static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(card);
    return card;
}

QWidget *MainWindow::buildAvatarStudio(QGridLayout **gridOut) {
    auto *panel = new QFrame(this);
    panel->setObjectName(QStringLiteral("personaImageStudio"));
    panel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(0);

    auto *title = new QLabel(QStringLiteral("桌宠形象"), panel);
    title->setObjectName(QStringLiteral("sectionTitle"));
    layout->addWidget(title);
    layout->addSpacing(5);
    auto *avatarHint = smallMuted(QStringLiteral("每个自定义槽位支持用户上传包含 1~10 张 SVG 的压缩包文件 或 单张 SVG"), panel);
    avatarHint->setWordWrap(true);
    layout->addWidget(avatarHint);
    layout->addSpacing(12);

    auto *canvas = new QFrame(panel);
    canvas->setObjectName(QStringLiteral("avatarCanvas"));
    auto *canvasLayout = new QVBoxLayout(canvas);
    canvasLayout->setContentsMargins(0, 0, 0, 0);
    canvasLayout->setSpacing(0);

    auto *grid = new QGridLayout;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(9);
    grid->setVerticalSpacing(9);
    const QJsonArray avatars = m_data->avatars();
    for (int index = 0; index < avatars.size(); ++index) {
        auto *avatar = new AvatarWidget(avatars.at(index).toObject(), canvas);
        avatar->setSelected(avatar->avatarId() == m_selectedAvatarId);
        connect(avatar, &AvatarWidget::clicked, this, &MainWindow::selectAvatar);
        registerRuntimePersonaEditControl(avatar);
        grid->addWidget(avatar, index / 8, index % 8);
        m_avatarWidgets << avatar;
    }
    canvasLayout->addLayout(grid);
    layout->addWidget(canvas);

    auto *status = statusLabel(panel);
    status->setObjectName(QStringLiteral("personaImageStatus"));
    status->setMinimumHeight(18);
    layout->addSpacing(6);
    layout->addWidget(status);

    m_avatarGrid = grid;
    m_avatarGrids << grid;
    refreshUserAvatarSlots();
    if (gridOut) *gridOut = grid;
    return panel;
}

QWidget *MainWindow::buildPersonaPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("page"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(46, 34, 46, 30);
    layout->setSpacing(0);
    m_pageLayouts << layout;

    m_personaHeaderLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *headerRow = m_personaHeaderLayout;
    headerRow->addWidget(makeHeader(QStringLiteral("EDIT PERSONA"), QStringLiteral("编辑人格"),
                                    QString()), 1);
    m_editorMode = new QLabel(page);
    m_editorMode->setObjectName(QStringLiteral("editorModeTag"));
    m_editorMode->setVisible(false);
    headerRow->addWidget(m_editorMode, 0, Qt::AlignTop);
    layout->addLayout(headerRow);
    layout->addSpacing(14);

    m_personaProgressGrid = new QGridLayout;
    auto *progress = m_personaProgressGrid;
    progress->setHorizontalSpacing(16);
    progress->setVerticalSpacing(12);
    auto *professionalCard = makeProgressCard(QStringLiteral("专业题库"), &m_professionalDetail,
                                              &m_professionalRing, QStringLiteral("progressCard"), QString());
    auto *privateCard = makeProgressCard(QStringLiteral("私人档案"), &m_privateDetail,
                                         &m_privateRing, QStringLiteral("progressCardPrivate"), QString());
    auto *coreCard = makeProgressCard(QStringLiteral("核心问题"), &m_coreDetail,
                                      &m_coreRing, QStringLiteral("progressCardCore"), QString());
    m_personaProgressCards = {professionalCard, privateCard, coreCard};
    progress->addWidget(professionalCard, 0, 0);
    progress->addWidget(privateCard, 0, 1);
    progress->addWidget(coreCard, 0, 2);
    for (int column = 0; column < 3; ++column) progress->setColumnStretch(column, 1);
    progress->setProperty("responsiveRole", QStringLiteral("personaProgress"));
    layout->addLayout(progress);
    layout->addSpacing(24);

    m_moduleGrid = new QGridLayout;
    m_moduleGrid->setHorizontalSpacing(10);
    m_moduleGrid->setVerticalSpacing(10);
    layout->addLayout(m_moduleGrid);

    layout->addSpacing(24);
    layout->addWidget(buildAvatarStudio(&m_avatarGrid));

    auto *identity = new QFrame(page);
    identity->setObjectName(QStringLiteral("personaIdentityFooter"));
    identity->setFixedHeight(104);

    // Keep the title on its own row, then place the name edit and save button
    // in the exact same layout row. This makes their visual center lines share
    // one geometry instead of trying to compensate across different parents.
    auto *identityRoot = new QVBoxLayout(identity);
    identityRoot->setContentsMargins(16, 14, 16, 14);
    identityRoot->setSpacing(7);
    auto *fieldLabel = new QLabel(QStringLiteral("人格名称"), identity);
    fieldLabel->setObjectName(QStringLiteral("fieldTitle"));
    identityRoot->addWidget(fieldLabel);

    auto *inputRowHost = new QWidget(identity);
    inputRowHost->setObjectName(QStringLiteral("personaIdentityInputRow"));
    inputRowHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_personaIdentityLayout = new QBoxLayout(QBoxLayout::LeftToRight, inputRowHost);
    auto *identityLayout = m_personaIdentityLayout;
    identityLayout->setContentsMargins(0, 0, 0, 0);
    identityLayout->setSpacing(16);

    m_personaName = new QLineEdit(inputRowHost);
    m_personaName->setMinimumWidth(0);
    m_personaName->setMinimumHeight(36);
    m_personaName->setMaxLength(40);
    m_personaName->setPlaceholderText(QStringLiteral("例如：工作中的我、温柔陪伴者"));
    registerRuntimePersonaEditControl(m_personaName);

    m_savePersonaButton = new QPushButton(QStringLiteral("保存人格"), inputRowHost);
    m_savePersonaButton->setObjectName(QStringLiteral("primary"));
    m_savePersonaButton->setMinimumWidth(112);
    m_savePersonaButton->setFixedHeight(40);
    connect(m_savePersonaButton, &QPushButton::clicked, this, &MainWindow::savePersona);
    connect(m_personaName, &QLineEdit::textChanged, this, [this] {
        if (!m_editorReadOnly && !isRunningPersonaEditorLocked()) saveDraft();
    });

    identityLayout->addWidget(m_personaName, 1, Qt::AlignVCenter);
    identityLayout->addWidget(m_savePersonaButton, 0, Qt::AlignVCenter);
    identityRoot->addWidget(inputRowHost, 1);
    layout->addSpacing(24);
    layout->addWidget(identity);
    m_personaStatus = statusLabel(page);
    m_personaStatus->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    layout->addWidget(m_personaStatus);
    layout->addStretch();
    return scrollPage(page);
}

QWidget *MainWindow::buildQuestionPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("page"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(46, 34, 46, 30);
    layout->setSpacing(0);
    m_pageLayouts << layout;

    layout->addWidget(makeHeader(QStringLiteral("EDIT PERSONA"), QStringLiteral("编辑人格"),
                                 QString()));
    layout->addSpacing(14);
    m_questionProgressGrid = new QGridLayout;
    auto *progress = m_questionProgressGrid;
    progress->setHorizontalSpacing(16);
    progress->setVerticalSpacing(12);
    auto *professionalCard = makeProgressCard(QStringLiteral("专业题库"), &m_questionProfessionalDetail,
                                              &m_questionProfessionalRing, QStringLiteral("progressCard"), QString());
    auto *privateCard = makeProgressCard(QStringLiteral("私人档案"), &m_questionPrivateDetail,
                                         &m_questionPrivateRing, QStringLiteral("progressCardPrivate"), QString());
    auto *coreCard = makeProgressCard(QStringLiteral("核心问题"), &m_questionCoreDetail,
                                      &m_questionCoreRing, QStringLiteral("progressCardCore"), QString());
    m_questionProgressCards = {professionalCard, privateCard, coreCard};
    progress->addWidget(professionalCard, 0, 0);
    progress->addWidget(privateCard, 0, 1);
    progress->addWidget(coreCard, 0, 2);
    for (int column = 0; column < 3; ++column) progress->setColumnStretch(column, 1);
    progress->setProperty("responsiveRole", QStringLiteral("questionProgress"));
    layout->addLayout(progress);
    layout->addSpacing(24);

    auto *top = new QHBoxLayout;
    auto *back = new QPushButton(QStringLiteral("← 返回模块"), page);
    back->setObjectName(QStringLiteral("ghost"));
    connect(back, &QPushButton::clicked, this, [this] {
        captureCurrentAnswer();
        saveDraft();
        switchView(0);
    });
    top->addWidget(back);
    top->addStretch();
    layout->addLayout(top);
    layout->addSpacing(18);

    m_questionBodyLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *body = m_questionBodyLayout;
    body->setSpacing(18);
    auto *rail = new QFrame(page);
    rail->setObjectName(QStringLiteral("questionRail"));
    rail->setFixedWidth(222);
    rail->setMinimumHeight(560);
    auto *railLayout = new QVBoxLayout(rail);
    railLayout->setContentsMargins(14, 14, 14, 14);
    railLayout->setSpacing(6);
    m_questionModuleTitle = new QLabel(rail);
    m_questionModuleTitle->setObjectName(QStringLiteral("railTitle"));
    m_questionModuleTitle->setWordWrap(true);
    m_questionModuleDescription = new QLabel(rail);
    m_questionModuleDescription->setObjectName(QStringLiteral("railDescription"));
    m_questionModuleDescription->setWordWrap(true);
    railLayout->addWidget(m_questionModuleTitle);
    railLayout->addWidget(m_questionModuleDescription);
    auto *indexHost = new QWidget(rail);
    m_questionIndexLayout = new QGridLayout(indexHost);
    m_questionIndexLayout->setContentsMargins(0, 6, 2, 0);
    m_questionIndexLayout->setHorizontalSpacing(6);
    m_questionIndexLayout->setVerticalSpacing(7);
    railLayout->addWidget(indexHost);
    auto *legend = new QFrame(rail);
    legend->setObjectName(QStringLiteral("questionLegend"));
    auto *legendLayout = new QVBoxLayout(legend);
    legendLayout->setContentsMargins(0, 12, 0, 0);
    legendLayout->setSpacing(9);
    m_coreLegend = smallMuted(QString(), legend);
    m_extensionLegend = smallMuted(QString(), legend);
    legendLayout->addWidget(m_coreLegend);
    legendLayout->addWidget(m_extensionLegend);
    railLayout->addWidget(legend);
    railLayout->addStretch();
    body->addWidget(rail);

    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("questionCard"));
    card->setMinimumHeight(560);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(21, 21, 21, 21);
    cardLayout->setSpacing(0);
    auto *meta = new QHBoxLayout;
    m_questionType = new QLabel(card);
    m_questionType->setObjectName(QStringLiteral("tag"));
    m_questionPosition = new QLabel(card);
    m_questionPosition->setObjectName(QStringLiteral("questionPosition"));
    meta->addWidget(m_questionType);
    meta->addStretch();
    meta->addWidget(m_questionPosition);
    cardLayout->addLayout(meta);
    m_questionText = new QLabel(card);
    m_questionText->setObjectName(QStringLiteral("questionText"));
    m_questionText->setWordWrap(true);
    m_questionHint = new QLabel(card);
    m_questionHint->setObjectName(QStringLiteral("questionHint"));
    m_questionHint->setWordWrap(true);
    cardLayout->addSpacing(16);
    cardLayout->addWidget(m_questionText);
    cardLayout->addSpacing(8);
    cardLayout->addWidget(m_questionHint);

    m_evidenceToggle = new QToolButton(card);
    m_evidenceToggle->setObjectName(QStringLiteral("evidenceToggle"));
    m_evidenceToggle->setText(QStringLiteral("查看本题的测量目标与研究依据"));
    m_evidenceToggle->setCheckable(true);
    m_evidenceToggle->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_evidenceToggle->setArrowType(Qt::RightArrow);
    m_questionEvidence = new QLabel(card);
    m_questionEvidence->setObjectName(QStringLiteral("evidenceBody"));
    m_questionEvidence->setWordWrap(true);
    m_questionEvidence->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_questionEvidence->setOpenExternalLinks(true);
    m_questionEvidence->setVisible(false);
    auto *evidenceGap = new QWidget(card);
    evidenceGap->setFixedHeight(4);
    evidenceGap->setVisible(false);
    connect(m_evidenceToggle, &QToolButton::toggled, this, [this, evidenceGap](bool checked) {
        m_evidenceToggle->setArrowType(checked ? Qt::DownArrow : Qt::RightArrow);
        evidenceGap->setVisible(checked);
        m_questionEvidence->setVisible(checked);
    });
    cardLayout->addSpacing(12);
    cardLayout->addWidget(m_evidenceToggle);
    cardLayout->addWidget(evidenceGap);
    cardLayout->addWidget(m_questionEvidence);

    m_optionHead = new QWidget(card);
    m_optionHead->setObjectName(QStringLiteral("optionHead"));
    auto *optionHeadLayout = new QHBoxLayout(m_optionHead);
    optionHeadLayout->setContentsMargins(0, 0, 0, 0);
    optionHeadLayout->setSpacing(12);
    auto *optionLabel = new QLabel(QStringLiteral("选择方式"), m_optionHead);
    optionLabel->setObjectName(QStringLiteral("mutedSmall"));
    m_optionMode = new QLabel(m_optionHead);
    m_optionMode->setObjectName(QStringLiteral("optionMode"));
    optionHeadLayout->addWidget(optionLabel);
    optionHeadLayout->addStretch();
    optionHeadLayout->addWidget(m_optionMode);
    // V101: keep the evidence section visually attached to the following
    // answer-mode controls. V100 still left a 10 px layout gap here, which
    // remained conspicuous even when the evidence body was collapsed.
    cardLayout->addSpacing(4);
    cardLayout->addWidget(m_optionHead);
    cardLayout->addSpacing(8);

    m_optionsHost = new QWidget(card);
    m_optionsHost->setObjectName(QStringLiteral("optionsHost"));
    m_optionsLayout = new QGridLayout(m_optionsHost);
    m_optionsLayout->setContentsMargins(0, 0, 0, 0);
    m_optionsLayout->setHorizontalSpacing(7);
    m_optionsLayout->setVerticalSpacing(7);
    cardLayout->addWidget(m_optionsHost);
    cardLayout->addSpacing(8);

    auto *scaleHost = new QWidget(card);
    scaleHost->setObjectName(QStringLiteral("scaleHost"));
    auto *scaleLayout = new QVBoxLayout(scaleHost);
    scaleLayout->setContentsMargins(0, 8, 0, 0);
    scaleLayout->setSpacing(8);
    auto *anchors = new QHBoxLayout;
    anchors->setContentsMargins(2, 0, 2, 0);
    m_scaleLow = new QLabel(scaleHost);
    m_scaleLow->setObjectName(QStringLiteral("mutedSmall"));
    m_scaleHigh = new QLabel(scaleHost);
    m_scaleHigh->setObjectName(QStringLiteral("mutedSmall"));
    anchors->addWidget(m_scaleLow);
    anchors->addStretch();
    anchors->addWidget(m_scaleHigh);
    m_scale = new QSlider(Qt::Horizontal, scaleHost);
    m_scale->setMinimumHeight(24);
    m_scale->setRange(1, 7);
    m_scale->setValue(4);
    m_scale->setTickPosition(QSlider::TicksBelow);
    m_scale->setTickInterval(1);
    m_scale->setVisible(false);
    registerRuntimePersonaEditControl(m_scale);
    scaleLayout->addLayout(anchors);
    scaleLayout->addWidget(m_scale);
    cardLayout->addWidget(scaleHost);

    auto *answerBlock = new QFrame(card);
    answerBlock->setObjectName(QStringLiteral("textAnswerBlock"));
    auto *answerLayout = new QVBoxLayout(answerBlock);
    answerLayout->setContentsMargins(14, 14, 14, 14);
    answerLayout->setSpacing(11);
    m_answerHeaderLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *answerHead = m_answerHeaderLayout;
    auto *answerCopy = new QVBoxLayout;
    auto *answerTitle = new QLabel(QStringLiteral("文字回答"), answerBlock);
    answerTitle->setObjectName(QStringLiteral("answerTitle"));
    answerCopy->addWidget(answerTitle);
    auto *answerHint = new QLabel(QStringLiteral("可写具体事件、当时原话或行为过程"), answerBlock);
    answerHint->setObjectName(QStringLiteral("mutedSmall"));
    answerHint->setWordWrap(false);
    answerHint->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    answerCopy->addWidget(answerHint);
    answerHead->addLayout(answerCopy);
    answerHead->addStretch();
    m_voiceInputButton = new QToolButton(answerBlock);
    m_voiceInputButton->setObjectName(QStringLiteral("voiceInputButton"));
    m_voiceInputButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_voiceInputButton->setIcon(QIcon(QStringLiteral(":/resources/voice-input.svg")));
    m_voiceInputButton->setIconSize(QSize(20, 20));
    m_voiceInputButton->setFixedSize(38, 38);
    m_voiceInputButton->setCursor(Qt::PointingHandCursor);
    connect(m_voiceInputButton, &QToolButton::clicked, this, &MainWindow::toggleVoiceInput);
    answerHead->addWidget(m_voiceInputButton, 0, Qt::AlignTop | Qt::AlignRight);
    answerLayout->addLayout(answerHead);
    m_answerText = new QTextEdit(answerBlock);
    m_answerText->setPlaceholderText(QStringLiteral("尽量写现实中真正发生过的做法、原话或具体例子……"));
    m_answerText->setMinimumHeight(180);
    registerRuntimePersonaEditControl(m_answerText);
    connect(m_answerText, &QTextEdit::textChanged, this, [this] {
        if (!m_updatingVoiceText && !m_voiceInterimText.isEmpty()) m_voiceInterimText.clear();
        if (m_editorReadOnly || isRunningPersonaEditorLocked() || m_views->currentIndex() != 4) return;
        captureCurrentAnswer();
        saveDraft();
        renderProgress();
    });
    answerLayout->addWidget(m_answerText);
    cardLayout->addSpacing(16);
    cardLayout->addWidget(answerBlock);

    cardLayout->addSpacing(14);

    m_questionStatus = statusLabel(card);
    cardLayout->addWidget(m_questionStatus);
    auto *actions = new QHBoxLayout;
    m_previousButton = new QPushButton(QStringLiteral("上一题"), card);
    m_nextButton = new QPushButton(QStringLiteral("下一题 →"), card);
    m_nextButton->setObjectName(QStringLiteral("primary"));
    connect(m_previousButton, &QPushButton::clicked, this, [this] {
        captureCurrentAnswer();
        if (m_currentQuestion > 0) --m_currentQuestion;
        saveDraft();
        renderQuestion();
    });
    connect(m_nextButton, &QPushButton::clicked, this, [this] {
        captureCurrentAnswer();
        const QJsonArray questions = m_data->modules().at(m_currentModule).toObject().value(QStringLiteral("questions")).toArray();
        if (m_currentQuestion + 1 < questions.size()) {
            ++m_currentQuestion;
            saveDraft();
            renderQuestion();
        } else {
            saveDraft();
            renderProgress();
            renderModules();
            switchView(0);
        }
    });
    actions->addWidget(m_previousButton);
    actions->addStretch();
    actions->addWidget(m_nextButton);
    cardLayout->addLayout(actions);
    body->addWidget(card, 1);
    layout->addLayout(body);

    // V62: both the desktop-pet appearance and persona-name/save footer exist only
    // on the New Persona home page. Question modules end after their own actions.
    layout->addSpacing(8);
    return scrollPage(page);
}

QWidget *MainWindow::buildPacksPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("page"));
    auto *layout = new QVBoxLayout(page);
    m_packsPageLayout = layout;
    layout->setContentsMargins(0, 18, 0, 28);
    layout->setSpacing(0);
    m_pageLayouts << layout;
    m_packsHeaderLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *head = m_packsHeaderLayout;
    head->setContentsMargins(18, 0, 18, 0);
    head->setSpacing(18);
    auto *packsHeader = makeHeader(QStringLiteral("PERSONA MANAGEMENT"), QStringLiteral("人格管理"),
                                   QString());
    packsHeader->setFixedHeight(112);
    packsHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    head->addWidget(packsHeader, 1, Qt::AlignTop);
    auto *headActions = new QHBoxLayout;
    headActions->setContentsMargins(0, 0, 0, 0);
    headActions->setSpacing(12);
    m_exportSelectedButton = new QPushButton(QStringLiteral("导出人格"), page);
    m_exportSelectedButton->setObjectName(QStringLiteral("exportToolbarAction"));
    m_exportSelectedButton->setEnabled(false);
    auto *importButton = new QPushButton(QStringLiteral("导入人格"), page);
    importButton->setObjectName(QStringLiteral("importToolbarAction"));
    connect(m_exportSelectedButton, &QPushButton::clicked, this, [this] { exportPack(m_selectedPackId); });
    connect(importButton, &QPushButton::clicked, this, &MainWindow::importPack);
    static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(m_exportSelectedButton, QColor(70, 111, 88, 55));
    static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(importButton, QColor(75, 104, 126, 50));
    headActions->addWidget(m_exportSelectedButton);
    headActions->addWidget(importButton);
    head->addLayout(headActions);
    layout->addLayout(head);
    layout->addSpacing(8);

    auto *listHost = new QWidget(page);
    listHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    m_packsLayout = new QVBoxLayout(listHost);
    m_packsLayout->setContentsMargins(0, 0, 0, 0);
    m_packsLayout->setSpacing(12);
    m_packsLayout->setAlignment(Qt::AlignTop);
    layout->addWidget(listHost, 0, Qt::AlignTop);
    // V90: the persona-card area has no persistent annotation line underneath.
    // Keep the label object for internal status routing, but never place it in the UI.
    m_packStatus = statusLabel(page);
    m_packStatus->setVisible(false);

    auto *exportOuterHost = new QWidget(page);
    exportOuterHost->setObjectName(QStringLiteral("exportOuterHost"));
    exportOuterHost->setVisible(false);
    auto *exportOuterLayout = new QHBoxLayout(exportOuterHost);
    exportOuterLayout->setContentsMargins(18, 10, 18, 0);
    exportOuterLayout->setSpacing(0);
    m_exportPanel = new QFrame(exportOuterHost);
    m_exportPanel->setObjectName(QStringLiteral("exportPanel"));
    m_exportPanel->setVisible(false);
    exportOuterLayout->addWidget(m_exportPanel);
    auto *exportLayout = new QVBoxLayout(m_exportPanel);
    // V118: the expanded export area has breathing room on every side instead
    // of touching the persona grid/page boundaries.
    exportLayout->setContentsMargins(18, 18, 18, 14);
    exportLayout->setSpacing(14);
    m_exportHeaderLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *exportHead = m_exportHeaderLayout;
    auto *exportHeadCopy = new QVBoxLayout;
    exportHeadCopy->setSpacing(3);
    auto *exportEyebrow = new QLabel(QStringLiteral("EXPORT PERSONA"), m_exportPanel);
    exportEyebrow->setObjectName(QStringLiteral("eyebrow"));
    m_exportTitle = new QLabel(QStringLiteral("导出人格包"), m_exportPanel);
    m_exportTitle->setObjectName(QStringLiteral("exportTitle"));
    exportHeadCopy->addWidget(exportEyebrow);
    exportHeadCopy->addWidget(m_exportTitle);
    auto *closeExport = new QPushButton(QStringLiteral("收起"), m_exportPanel);
    closeExport->setObjectName(QStringLiteral("ghost"));
    connect(closeExport, &QPushButton::clicked, this, [this, exportOuterHost] {
        m_exportPanel->setVisible(false);
        exportOuterHost->setVisible(false);
    });
    exportHead->addLayout(exportHeadCopy, 1);
    exportHead->addWidget(closeExport, 0, Qt::AlignTop);
    exportLayout->addLayout(exportHead);

    m_exportColumnsLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *columns = m_exportColumnsLayout;
    columns->setSpacing(0);
    auto *modulePanel = new QFrame(m_exportPanel);
    modulePanel->setObjectName(QStringLiteral("exportSelectionPanel"));
    auto *modulePanelLayout = new QVBoxLayout(modulePanel);
    modulePanelLayout->setContentsMargins(20, 20, 20, 14);
    modulePanelLayout->setSpacing(8);
    auto *packNameLabel = new QLabel(QStringLiteral("人格包名称"), modulePanel);
    packNameLabel->setObjectName(QStringLiteral("fieldTitle"));
    m_exportPackName = new QLineEdit(modulePanel);
    modulePanelLayout->addWidget(packNameLabel);
    modulePanelLayout->addWidget(m_exportPackName);

    auto *moduleTitleRow = new QHBoxLayout;
    auto *moduleTitle = new QLabel(QStringLiteral("选择要导出的模块"), modulePanel);
    moduleTitle->setObjectName(QStringLiteral("sectionTitle"));
    auto *toggleModules = new QPushButton(QStringLiteral("取消全选"), modulePanel);
    toggleModules->setObjectName(QStringLiteral("ghost"));
    connect(toggleModules, &QPushButton::clicked, this, [this, toggleModules] {
        bool anyChecked = false;
        for (QCheckBox *check : std::as_const(m_exportModuleChecks)) anyChecked = anyChecked || check->isChecked();
        for (QCheckBox *check : std::as_const(m_exportModuleChecks)) check->setChecked(!anyChecked);
        toggleModules->setText(anyChecked ? QStringLiteral("全部选中") : QStringLiteral("取消全选"));
        refreshExportPanel();
    });
    moduleTitleRow->addWidget(moduleTitle);
    moduleTitleRow->addStretch();
    moduleTitleRow->addWidget(toggleModules);
    modulePanelLayout->addSpacing(12);
    modulePanelLayout->addLayout(moduleTitleRow);
    auto *selectionHint = smallMuted(QStringLiteral("取消勾选后，该模块的问题和文字答案不会进入人格包。"), modulePanel);
    selectionHint->setObjectName(QStringLiteral("exportSelectionHint"));
    modulePanelLayout->addWidget(selectionHint);
    modulePanelLayout->addSpacing(6);

    auto *moduleChecksHost = new QWidget(modulePanel);
    moduleChecksHost->setObjectName(QStringLiteral("exportModuleChecksHost"));
    m_exportModulesLayout = new QGridLayout(moduleChecksHost);
    m_exportModulesLayout->setContentsMargins(0, 0, 0, 0);
    m_exportModulesLayout->setHorizontalSpacing(10);
    m_exportModulesLayout->setVerticalSpacing(10);
    modulePanelLayout->addWidget(moduleChecksHost);

    m_exportPackButton = new QPushButton(QStringLiteral("导出人格包"), modulePanel);
    m_exportPackButton->setObjectName(QStringLiteral("primary"));
    m_exportPackButton->setMinimumHeight(42);
    m_exportPackButton->setMinimumWidth(148);
    m_exportPackButton->setEnabled(false);
    connect(m_exportPackButton, &QPushButton::clicked, this, &MainWindow::performExport);
    modulePanelLayout->addSpacing(14);
    auto *exportActionRow = new QHBoxLayout;
    exportActionRow->setContentsMargins(0, 0, 0, 0);
    exportActionRow->addStretch();
    exportActionRow->addWidget(m_exportPackButton, 0, Qt::AlignRight);
    modulePanelLayout->addLayout(exportActionRow);

    // V118: privacy text is a normal annotation, matching the other muted hints;
    // no badge/panel fill competes with the primary action.
    auto *privacy = smallMuted(QStringLiteral("人格包可能包含高度敏感信息，分享前请确认所选模块。"), modulePanel);
    privacy->setWordWrap(true);
    privacy->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    modulePanelLayout->addWidget(privacy);
    columns->addWidget(modulePanel, 1);
    exportLayout->addLayout(columns);
    layout->addWidget(exportOuterHost);
    layout->addStretch();
    // V118: keep the persona-management scroll area so Export Persona can be
    // reached with a real animated scroll instead of an instantaneous jump.
    auto *packsScroll = qobject_cast<QScrollArea *>(scrollPage(page));
    m_desktopScroll = packsScroll;
    return packsScroll;
}

QWidget *MainWindow::buildModelPage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("page"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(46, 34, 46, 30);
    layout->setSpacing(20);
    m_pageLayouts << layout;
    layout->addWidget(makeHeader(QStringLiteral("BUILD PET"), QStringLiteral("构建宠物"), QString()));

    m_modelColumnsLayout = new QBoxLayout(QBoxLayout::LeftToRight);
    auto *columns = m_modelColumnsLayout;
    columns->setSpacing(16);

    auto *connection = new QFrame(page);
    connection->setObjectName(QStringLiteral("panel"));
    connection->setMinimumHeight(690);
    auto *connectionLayout = new QVBoxLayout(connection);
    connectionLayout->setContentsMargins(19, 19, 19, 19);
    connectionLayout->setSpacing(9);
    auto *title = new QLabel(QStringLiteral("选择模型服务"), connection);
    title->setObjectName(QStringLiteral("sectionTitle"));
    connectionLayout->addWidget(title);
    m_providerLayout = new QGridLayout;
    auto *providers = m_providerLayout;
    providers->setHorizontalSpacing(9);
    providers->setVerticalSpacing(9);
    struct Provider { const char *id; const char *name; const char *url; };
    const Provider definitions[]{
        {"deepseek", "DeepSeek", "https://api.deepseek.com"},
        {"qwen", "通义千问", "https://dashscope.aliyuncs.com/compatible-mode/v1"},
        {"glm", "智谱 GLM", "https://open.bigmodel.cn/api/paas/v4"},
        {"openai", "OpenAI", "https://api.openai.com/v1"},
        {"custom", "自定义", ""}
    };
    for (int index = 0; index < 5; ++index) {
        const Provider definition = definitions[index];
        auto *button = new QPushButton(QString::fromUtf8(definition.name), connection);
        button->setObjectName(QStringLiteral("providerCard"));
        static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(button);
        button->setCheckable(true);
        button->setFixedHeight(50);
        button->setProperty("providerIndex", index);
        button->setProperty("providerId", QString::fromLatin1(definition.id));
        button->setProperty("providerName", QString::fromUtf8(definition.name));
        button->setProperty("providerUrl", QString::fromLatin1(definition.url));
        connect(button, &QPushButton::clicked, this, [this, button] {
            if (m_loadingModelConfig) return;
            m_provider = button->property("providerId").toString();
            m_providerDisplay = button->property("providerName").toString();
            const QString url = button->property("providerUrl").toString();
            if (!url.isEmpty()) m_baseUrl->setText(url);
            else if (m_provider == QStringLiteral("custom")) m_baseUrl->clear();
            for (QPushButton *item : std::as_const(m_providerButtons)) item->setChecked(item == button);
            updateModelSaveState();
            invalidateModelVerification();
        });
        if (index == 4) providers->addWidget(button, 2, 0, 1, 2);
        else providers->addWidget(button, index / 2, index % 2);
        m_providerButtons << button;
    }
    connectionLayout->addLayout(providers);

    auto *form = new QVBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(10);
    m_baseUrl = new QLineEdit(connection);
    m_baseUrl->setPlaceholderText(QStringLiteral("例如 https://example.com/v1"));
    m_modelId = new QLineEdit(connection);
    m_modelId->setPlaceholderText(QStringLiteral("填写服务商提供的模型 ID"));
    m_apiKey = new QLineEdit(connection);
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("输入 API 密钥"));
    const auto addModelFieldRow = [connection, form](const QString &labelText, QLineEdit *field) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(12);
        auto *label = new QLabel(labelText, connection);
        label->setObjectName(QStringLiteral("formFieldLabel"));
        label->setFixedWidth(72);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        row->addWidget(label, 0, Qt::AlignVCenter);
        row->addWidget(field, 1);
        form->addLayout(row);
    };
    addModelFieldRow(QStringLiteral("Base URL"), m_baseUrl);
    addModelFieldRow(QStringLiteral("模型 ID"), m_modelId);
    addModelFieldRow(QStringLiteral("API 密钥"), m_apiKey);
    connectionLayout->addSpacing(4);
    connectionLayout->addLayout(form);
    auto *showKey = new QCheckBox(QStringLiteral("显示 API 密钥"), connection);
    showKey->setObjectName(QStringLiteral("modelShowApiKey"));
    showKey->setFixedHeight(32);
    connect(showKey, &QCheckBox::toggled, this, [this](bool checked) {
        m_apiKey->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });

    auto onFieldEdited = [this] {
        if (m_loadingModelConfig) return;
        updateModelSaveState();
        invalidateModelVerification();
    };
    connect(m_baseUrl, &QLineEdit::textChanged, this, [onFieldEdited](const QString &) { onFieldEdited(); });
    connect(m_modelId, &QLineEdit::textChanged, this, [onFieldEdited](const QString &) { onFieldEdited(); });
    connect(m_apiKey, &QLineEdit::textChanged, this, [onFieldEdited](const QString &) { onFieldEdited(); });

    // V70: one visually balanced action line.  Both controls use the same
    // 32px row height and explicit top alignment so their upper edges remain
    // flush at every DPI scale.  The save action is intentionally compact.
    auto *keyActionRow = new QHBoxLayout;
    keyActionRow->setContentsMargins(0, 0, 0, 0);
    keyActionRow->setSpacing(12);
    keyActionRow->setAlignment(Qt::AlignTop);
    keyActionRow->addWidget(showKey, 0, Qt::AlignTop);
    keyActionRow->addStretch();
    m_saveModelButton = new QPushButton(QStringLiteral("保存配置"), connection);
    m_saveModelButton->setObjectName(QStringLiteral("modelSaveButton"));
    m_saveModelButton->setProperty("saved", false);
    m_saveModelButton->setProperty("valid", false);
    m_saveModelButton->setProperty("motionDisabledInert", true);
    m_saveModelButton->setFixedSize(96, 32);
    static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(m_saveModelButton);
    connect(m_saveModelButton, &QPushButton::clicked, this, &MainWindow::saveModelConfig);
    keyActionRow->addWidget(m_saveModelButton, 0, Qt::AlignTop);
    connectionLayout->addLayout(keyActionRow);

    auto *savedTitle = new QLabel(QStringLiteral("已保存配置"), connection);
    savedTitle->setObjectName(QStringLiteral("subSectionTitle"));
    connectionLayout->addWidget(savedTitle);

    m_savedModelConfigsScroll = new QScrollArea(connection);
    m_savedModelConfigsScroll->setObjectName(QStringLiteral("modelConfigLibraryScroll"));
    m_savedModelConfigsScroll->setWidgetResizable(true);
    m_savedModelConfigsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_savedModelConfigsScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_savedModelConfigsScroll->setFrameShape(QFrame::NoFrame);
    m_savedModelConfigsScroll->viewport()->setAutoFillBackground(false);
    m_savedModelConfigsScroll->setFixedHeight(200);
    m_savedModelConfigsHost = new QWidget(m_savedModelConfigsScroll);
    m_savedModelConfigsHost->setAutoFillBackground(false);
    m_savedModelConfigsHost->setObjectName(QStringLiteral("modelConfigLibraryHost"));
    m_savedModelConfigsLayout = new QVBoxLayout(m_savedModelConfigsHost);
    m_savedModelConfigsLayout->setContentsMargins(8, 8, 8, 8);
    m_savedModelConfigsLayout->setSpacing(7);
    m_savedModelConfigsScroll->setWidget(m_savedModelConfigsHost);
    connectionLayout->addWidget(m_savedModelConfigsScroll);
    connectionLayout->addStretch();
    columns->addWidget(connection, 1);

    auto *sync = new QFrame(page);
    sync->setObjectName(QStringLiteral("apiSyncPanel"));
    sync->setMinimumWidth(300);
    sync->setMaximumWidth(420);
    sync->setMinimumHeight(470);
    auto *syncLayout = new QVBoxLayout(sync);
    syncLayout->setContentsMargins(19, 19, 19, 19);
    syncLayout->setSpacing(10);
    m_providerName = new QLabel(QStringLiteral("人格交接"), sync);
    m_providerName->setObjectName(QStringLiteral("syncTitle"));
    m_activePersonaLabel = new QLabel(QStringLiteral("当前人格：尚未选择"), sync);
    m_activePersonaLabel->setObjectName(QStringLiteral("activePersona"));
    m_activePersonaLabel->setWordWrap(true);
    syncLayout->addWidget(m_providerName);
    syncLayout->addWidget(m_activePersonaLabel);

    const QStringList stepLabels{
        QStringLiteral("模型连接"),
        QStringLiteral("文件完整性检查"),
        QStringLiteral("上传文件"),
        QStringLiteral("创建人格会话"),
        QStringLiteral("创建桌宠")
    };
    for (int index = 0; index < stepLabels.size(); ++index) {
        auto *row = new QFrame(sync);
        row->setObjectName(QStringLiteral("syncStepRow"));
        row->setProperty("state", QStringLiteral("idle"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 0, 10, 0);
        rowLayout->setSpacing(7);
        auto *step = new QLabel(stepLabels.at(index), row);
        step->setObjectName(QStringLiteral("syncStep"));
        rowLayout->addWidget(step, 1);
        auto *indicator = new DetectionIndicator(row);
        rowLayout->addWidget(indicator);
        syncLayout->addWidget(row);
        m_syncSteps << step;
        m_syncIndicators << indicator;
    }
    auto *autoSync = new QCheckBox(QStringLiteral("切换接入配置后自动同步人格"), sync);
    autoSync->setChecked(true);
    syncLayout->addWidget(autoSync);
    m_generatePetButton = new QPushButton(QStringLiteral("生成桌宠"), sync);
    m_generatePetButton->setObjectName(QStringLiteral("primary"));
    connect(m_generatePetButton, &QPushButton::clicked, this, &MainWindow::generatePet);
    syncLayout->addWidget(m_generatePetButton);
    syncLayout->addStretch();
    columns->addWidget(sync);
    layout->addLayout(columns, 1);

    refreshSavedModelConfigs();
    updateModelSaveState();
    updateGeneratePetAvailability();
    return scrollPage(page);
}


QWidget *MainWindow::buildVoicePage() {
    auto *page = new QWidget;
    page->setObjectName(QStringLiteral("page"));
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(46, 18, 46, 30);
    layout->setSpacing(0);
    m_pageLayouts << layout;
    auto *voiceHeader = makeHeader(QStringLiteral("VOICE CONFIGURATION"), QStringLiteral("语音配置"), QString());
    voiceHeader->setObjectName(QStringLiteral("voicePageHeader"));
    // Match Persona Management exactly: the same 112 px header block plus
    // the same 8 px gap before the first content card. This keeps the visible
    // title-to-card rhythm consistent across both pages at every DPI.
    voiceHeader->setFixedHeight(112);
    layout->addWidget(voiceHeader);
    layout->addSpacing(8);


    auto *card = new QFrame(page);
    card->setObjectName(QStringLiteral("voiceConfigCard"));
    card->setMinimumWidth(390);
    card->setMaximumWidth(510);
    card->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(20, 19, 20, 18);
    cardLayout->setSpacing(10);

    auto *title = new QLabel(QStringLiteral("百度实时语音识别"), card);
    title->setObjectName(QStringLiteral("sectionTitle"));
    cardLayout->addWidget(title);
    auto *hint = smallMuted(QStringLiteral("实时语音转文字 · 中文普通话"), card);
    hint->setWordWrap(false);
    cardLayout->addWidget(hint);
    cardLayout->addSpacing(6);

    auto *form = new QVBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(11);
    m_voiceAppId = new QLineEdit(card);
    m_voiceAppId->setPlaceholderText(QStringLiteral("输入百度语音应用 AppID"));
    m_voiceAppId->setMaxLength(20);
    m_voiceApiKey = new QLineEdit(card);
    m_voiceApiKey->setEchoMode(QLineEdit::Password);
    m_voiceApiKey->setPlaceholderText(QStringLiteral("输入 API Key"));
    const auto addVoiceFieldRow = [card, form](const QString &labelText, QLineEdit *field) {
        auto *row = new QHBoxLayout;
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(14);
        auto *label = new QLabel(labelText, card);
        label->setObjectName(QStringLiteral("formFieldLabel"));
        label->setFixedWidth(62);
        label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        row->addWidget(label, 0, Qt::AlignVCenter);
        row->addWidget(field, 1);
        form->addLayout(row);
    };
    addVoiceFieldRow(QStringLiteral("AppID"), m_voiceAppId);
    addVoiceFieldRow(QStringLiteral("API Key"), m_voiceApiKey);
    cardLayout->addLayout(form);

    auto *actionRow = new QHBoxLayout;
    actionRow->setContentsMargins(0, 2, 0, 0);
    actionRow->setSpacing(12);
    auto *showKey = new QCheckBox(QStringLiteral("显示 API Key"), card);
    showKey->setObjectName(QStringLiteral("voiceShowApiKey"));
    showKey->setFixedHeight(32);
    connect(showKey, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_voiceApiKey) m_voiceApiKey->setEchoMode(checked ? QLineEdit::Normal : QLineEdit::Password);
    });
    actionRow->addWidget(showKey);
    actionRow->addStretch();
    m_saveVoiceButton = new QPushButton(QStringLiteral("保存配置"), card);
    m_saveVoiceButton->setObjectName(QStringLiteral("voiceSaveButton"));
    m_saveVoiceButton->setProperty("saved", false);
    m_saveVoiceButton->setProperty("valid", false);
    m_saveVoiceButton->setProperty("motionDisabledInert", true);
    m_saveVoiceButton->setFixedSize(96, 32);
    static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(m_saveVoiceButton);
    connect(m_saveVoiceButton, &QPushButton::clicked, this, &MainWindow::saveVoiceConfig);
    actionRow->addWidget(m_saveVoiceButton);
    cardLayout->addLayout(actionRow);

    auto fieldEdited = [this] {
        if (!m_loadingVoiceConfig) updateVoiceSaveState();
    };
    connect(m_voiceAppId, &QLineEdit::textChanged, this, [fieldEdited](const QString &) { fieldEdited(); });
    connect(m_voiceApiKey, &QLineEdit::textChanged, this, [fieldEdited](const QString &) { fieldEdited(); });

    layout->addWidget(card, 0, Qt::AlignLeft | Qt::AlignTop);
    layout->addStretch();
    updateVoiceSaveState();
    return scrollPage(page);
}

void MainWindow::switchView(int index) {
    if (index < 0 || index > 3) index = 0;
    if (m_views && m_views->currentIndex() == 4 && m_voiceClient && m_voiceClient->isRunning() && index != 4) {
        m_pendingViewAfterVoiceStop = index;
        m_voiceStopPending = true;
        m_voiceClient->stop();
        updateVoiceInputState();
        return;
    }
    m_views->setCurrentIndex(index);
    if (m_views->currentWidget()) m_views->currentWidget()->updateGeometry();
    m_views->updateGeometry();
    scrollCurrentPageToTop();
    applyResponsiveLayout();
    for (int i = 0; i < m_navButtons.size(); ++i) m_navButtons.at(i)->setChecked(i == index);
    if (index == 0) {
        renderProgress();
        renderModules();
    } else if (index == 1) {
        renderPacks();
    } else if (index == 2) {
        loadModelConfig();
    } else if (index == 3) {
        loadVoiceConfig();
    }
}

bool MainWindow::hasQuestionResponse(int moduleIndex, int questionIndex) const {
    const QString key = QString::number(moduleIndex) + u':' + QString::number(questionIndex);
    const QJsonObject answer = m_answers.value(key).toObject();
    return answer.value(QStringLiteral("state")).toString() == QStringLiteral("answered");
}

void MainWindow::renderProgress() {
    int professional = 0;
    int privateCount = 0;
    int core = 0;
    int coreTotal = 0;
    const QJsonArray modules = m_data->modules();
    for (int moduleIndex = 0; moduleIndex < modules.size(); ++moduleIndex) {
        const QJsonObject module = modules.at(moduleIndex).toObject();
        const QJsonArray questions = module.value(QStringLiteral("questions")).toArray();
        const int coreCount = module.value(QStringLiteral("coreCount")).toInt(3);
        if (moduleIndex < modules.size() - 1) coreTotal += coreCount;
        for (int questionIndex = 0; questionIndex < questions.size(); ++questionIndex) {
            if (!hasQuestionResponse(moduleIndex, questionIndex)) continue;
            if (moduleIndex == modules.size() - 1) ++privateCount;
            else {
                ++professional;
                if (questionIndex < coreCount) ++core;
            }
        }
    }
    m_professionalRing->setPrimaryText(QStringLiteral("%1 / 80").arg(professional));
    m_professionalRing->setSecondaryText(QString());
    m_professionalRing->setProgress(percent(professional, 80));
    m_professionalDetail->hide();
    m_privateRing->setPrimaryText(QStringLiteral("%1 / 13").arg(privateCount));
    m_privateRing->setSecondaryText(QString());
    m_privateRing->setProgress(percent(privateCount, 13));
    m_privateDetail->hide();
    m_coreRing->setPrimaryText(QStringLiteral("%1 / %2").arg(core).arg(coreTotal));
    m_coreRing->setSecondaryText(QString());
    m_coreRing->setProgress(percent(core, coreTotal));
    m_coreDetail->hide();
    if (m_questionProfessionalRing) {
        m_questionProfessionalRing->setPrimaryText(QStringLiteral("%1 / 80").arg(professional));
        m_questionProfessionalRing->setSecondaryText(QString());
        m_questionProfessionalRing->setProgress(percent(professional, 80));
        m_questionProfessionalDetail->hide();
        m_questionPrivateRing->setPrimaryText(QStringLiteral("%1 / 13").arg(privateCount));
        m_questionPrivateRing->setSecondaryText(QString());
        m_questionPrivateRing->setProgress(percent(privateCount, 13));
        m_questionPrivateDetail->hide();
        m_questionCoreRing->setPrimaryText(QStringLiteral("%1 / %2").arg(core).arg(coreTotal));
        m_questionCoreRing->setSecondaryText(QString());
        m_questionCoreRing->setProgress(percent(core, coreTotal));
        m_questionCoreDetail->hide();
    }
}

void MainWindow::renderModules() {
    clearLayout(m_moduleGrid);
    m_moduleCards.clear();
    const QJsonArray modules = m_data->modules();
    for (int index = 0; index < modules.size(); ++index) {
        const QJsonObject module = modules.at(index).toObject();
        const int total = module.value(QStringLiteral("questions")).toArray().size();
        int completed = 0;
        for (int questionIndex = 0; questionIndex < total; ++questionIndex) {
            if (hasQuestionResponse(index, questionIndex)) ++completed;
        }
        const int coreCount = module.value(QStringLiteral("coreCount")).toInt(3);
        auto *card = new ClickableFrame;
        card->setObjectName(QStringLiteral("moduleButton"));
        card->setProperty("accent", index % 4);
        card->setProperty("completed", completed == total && total > 0);
        card->setProperty("privateModule", index == modules.size() - 1);
        card->setFixedHeight(123);
        card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        card->setActivated([this, index] { openModule(index); });
        auto *cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(15, 13, 15, 13);
        cardLayout->setSpacing(0);
        auto *head = new QHBoxLayout;
        head->setSpacing(8);
        auto *title = new QLabel(QStringLiteral("%1 · %2").arg(index + 1, 2, 10, QLatin1Char('0')).arg(module.value(QStringLiteral("name")).toString()), card);
        title->setObjectName(QStringLiteral("moduleTitle"));
        title->setMinimumWidth(0);
        title->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        const QString tagText = index == modules.size() - 1
            ? QStringLiteral("私人资料 · 共%1").arg(total)
            : QStringLiteral("核心%1 · 共%2").arg(coreCount).arg(total);
        auto *tag = new QLabel(tagText, card);
        tag->setObjectName(QStringLiteral("moduleTag"));
        head->addWidget(title, 1);
        head->addWidget(tag, 0, Qt::AlignTop);
        cardLayout->addLayout(head);
        cardLayout->addSpacing(8);
        auto *description = new QLabel(module.value(QStringLiteral("desc")).toString(), card);
        description->setObjectName(QStringLiteral("moduleDescription"));
        description->setWordWrap(true);
        cardLayout->addWidget(description);
        cardLayout->addStretch();
        auto *foot = new QLabel(QStringLiteral("已完成 %1 / %2").arg(completed).arg(total), card);
        foot->setObjectName(QStringLiteral("moduleFoot"));
        cardLayout->addWidget(foot);
        cardLayout->addSpacing(6);
        auto *track = new QProgressBar(card);
        track->setObjectName(QStringLiteral("moduleTrack"));
        track->setRange(0, qMax(1, total));
        track->setValue(completed);
        track->setTextVisible(false);
        track->setFixedHeight(4);
        cardLayout->addWidget(track);
        static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(card);
        m_moduleCards << card;
        m_moduleGrid->addWidget(card, index / 2, index % 2);
    }
    m_moduleGrid->setColumnStretch(0, 1);
    m_moduleGrid->setColumnStretch(1, 1);
    applyResponsiveLayout();
}

void MainWindow::openModule(int moduleIndex) {
    captureCurrentAnswer();
    m_currentModule = qBound(0, moduleIndex, m_data->modules().size() - 1);
    m_currentQuestion = 0;
    saveDraft();
    renderQuestion();
    m_views->setCurrentIndex(4);
    if (m_questionPage) m_questionPage->updateGeometry();
    m_views->updateGeometry();
    scrollCurrentPageToTop();
    applyResponsiveLayout();
    for (QPushButton *button : std::as_const(m_navButtons)) button->setChecked(false);
}

QString MainWindow::currentAnswerKey() const {
    return QString::number(m_currentModule) + u':' + QString::number(m_currentQuestion);
}

QJsonObject MainWindow::currentAnswer() const {
    return m_answers.value(currentAnswerKey()).toObject();
}

void MainWindow::captureCurrentAnswer() {
    if (!m_answerText || m_views->currentIndex() != 4 || m_editorReadOnly || isRunningPersonaEditorLocked()) return;
    QJsonObject answer;
    answer.insert(QStringLiteral("text"), m_answerText->toPlainText());
    QJsonArray selected;
    const QJsonObject module = m_data->modules().at(m_currentModule).toObject();
    const QJsonObject question = module.value(QStringLiteral("questions")).toArray().at(m_currentQuestion).toObject();
    const QString type = question.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("single")) {
        if (QAbstractButton *button = m_optionGroup ? m_optionGroup->checkedButton() : nullptr) selected.append(button->property("answerValue").toString());
    } else if (type == QStringLiteral("multi")) {
        for (QCheckBox *check : m_optionsHost->findChildren<QCheckBox *>()) if (check->isChecked()) selected.append(check->property("answerValue").toString());
    } else if (type == QStringLiteral("scale")) {
        if (QAbstractButton *button = m_optionGroup ? m_optionGroup->checkedButton() : nullptr) {
            selected.append(button->property("answerValue").toString());
        }
    }
    answer.insert(QStringLiteral("selected"), selected);
    answer.insert(QStringLiteral("state"), (!answer.value(QStringLiteral("text")).toString().trimmed().isEmpty() || !selected.isEmpty())
                      ? QStringLiteral("answered") : QStringLiteral("empty"));
    m_answers.insert(currentAnswerKey(), answer);
    m_draft.insert(QStringLiteral("answers"), m_answers);
}

void MainWindow::renderQuestion() {
    const QJsonObject module = m_data->modules().at(m_currentModule).toObject();
    const QJsonArray questions = module.value(QStringLiteral("questions")).toArray();
    m_currentQuestion = qBound(0, m_currentQuestion, questions.size() - 1);
    const QJsonObject question = questions.at(m_currentQuestion).toObject();
    const QJsonObject answer = currentAnswer();
    m_questionModuleTitle->setText(module.value(QStringLiteral("name")).toString());
    m_questionModuleDescription->setText(module.value(QStringLiteral("desc")).toString());
    m_questionPosition->setText(QStringLiteral("%1 / %2").arg(m_currentQuestion + 1).arg(questions.size()));
    const QString type = question.value(QStringLiteral("type")).toString();
    const int coreCount = module.value(QStringLiteral("coreCount")).toInt(3);
    m_questionType->setText(m_currentQuestion < coreCount ? QStringLiteral("核心问题") : QStringLiteral("延伸问题"));
    m_questionText->setText(question.value(QStringLiteral("t")).toString());
    QString visibleHint = question.value(QStringLiteral("h")).toString();
    if (visibleHint.contains(QStringLiteral("停顿、重音和叙述节奏")))
        visibleHint = QStringLiteral("请在文字回答中描述停顿、重音和叙述节奏。");
    m_questionHint->setText(visibleHint);
    const QJsonObject evidence = question.value(QStringLiteral("evidence")).toObject();
    const QJsonObject source = evidence.value(QStringLiteral("source")).toObject();
    m_questionEvidence->setText(QStringLiteral("<b>测量目标：</b>%1<br><br><b>设计理由：</b>%2<br><br><a href=\"%3\">%4</a>")
                                    .arg(evidence.value(QStringLiteral("construct")).toString().toHtmlEscaped(),
                                         evidence.value(QStringLiteral("reason")).toString().toHtmlEscaped(),
                                         source.value(QStringLiteral("url")).toString().toHtmlEscaped(),
                                         source.value(QStringLiteral("label")).toString().toHtmlEscaped()));

    clearLayout(m_questionIndexLayout);
    for (int index = 0; index < questions.size(); ++index) {
        const bool core = index < coreCount;
        const QString key = QString::number(m_currentModule) + u':' + QString::number(index);
        const bool completed = hasQuestionResponse(m_currentModule, index);
        auto *button = new QPushButton(QString::number(index + 1));
        button->setObjectName(QStringLiteral("indexButton"));
        button->setCheckable(true);
        button->setFixedSize(42, 42);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setChecked(index == m_currentQuestion);
        button->setProperty("completed", completed);
        button->setProperty("core", core);
        if (core) {
            auto *star = new QLabel(QStringLiteral("*"), button);
            star->setObjectName(QStringLiteral("coreStar"));
            star->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            star->setAlignment(Qt::AlignCenter);
            star->setFixedSize(12, 12);
            star->move(28, 1);
            star->setStyleSheet(index == m_currentQuestion
                                    ? QStringLiteral("color:#ffffff;background:transparent;border:0;font-size:11px;font-weight:800;")
                                    : QStringLiteral("color:#6757E8;background:transparent;border:0;font-size:11px;font-weight:800;"));
            star->raise();
        }
        connect(button, &QPushButton::clicked, this, [this, index] {
            captureCurrentAnswer();
            m_currentQuestion = index;
            saveDraft();
            renderQuestion();
        });
        m_questionIndexLayout->addWidget(button, index / 4, index % 4, Qt::AlignCenter);
    }
    for (int column = 0; column < 4; ++column) { m_questionIndexLayout->setColumnMinimumWidth(column, 42); m_questionIndexLayout->setColumnStretch(column, 1); }
    m_coreLegend->setText(QStringLiteral("核心问题：第 1～%1 题，建议用户详细回答，这些问题在塑造桌宠人格时占有极高的权重。").arg(coreCount));
    m_extensionLegend->setText(QStringLiteral("延伸问题：第 %1～%2 题，建议用户尽量回答，这些问题有助于完善桌宠人格细节。").arg(coreCount + 1).arg(questions.size()));

    clearLayout(m_optionsLayout);
    m_optionWidgets.clear();
    m_currentQuestionType = type;
    if (m_optionGroup) delete m_optionGroup;
    m_optionGroup = new QButtonGroup(m_optionsHost);
    m_optionGroup->setExclusive(type == QStringLiteral("single"));
    QStringList selectedValues;
    for (const QJsonValue &item : answer.value(QStringLiteral("selected")).toArray()) selectedValues << item.toString();
    const QJsonArray options = question.value(QStringLiteral("o")).toArray();
    const bool hasChoices = type == QStringLiteral("single") || type == QStringLiteral("multi") || type == QStringLiteral("scale");
    m_optionHead->setVisible(hasChoices);
    if (type == QStringLiteral("multi")) {
        const int maximum = question.value(QStringLiteral("max")).toInt(options.size());
        m_optionMode->setText(maximum < options.size() ? QStringLiteral("多选 · 最多 %1 项").arg(maximum)
                                                        : QStringLiteral("多选 · 数量不限"));
    } else if (type == QStringLiteral("scale")) {
        m_optionMode->setText(QStringLiteral("单选 · 7级量表"));
    } else if (type == QStringLiteral("single")) {
        m_optionMode->setText(QStringLiteral("单选"));
    } else {
        m_optionMode->clear();
    }
    if (type == QStringLiteral("single")) {
        for (int index = 0; index < options.size(); ++index) {
            const QString value = options.at(index).toString();
            auto *radio = new DeselectableRadioButton(value, m_optionsHost);
            radio->setObjectName(QStringLiteral("optionButton"));
            radio->setMinimumWidth(0);
            radio->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            radio->setProperty("answerValue", value);
            radio->setChecked(selectedValues.contains(value));
            radio->setEnabled(!m_editorReadOnly);
            registerRuntimePersonaEditControl(radio);
            connect(radio, &QRadioButton::clicked, this, [this, radio](bool) {
                if (m_editorReadOnly || isRunningPersonaEditorLocked()) return;
                if (radio->wasCheckedOnPress()) forceUncheckRadioButton(radio);
                captureCurrentAnswer();
                saveDraft();
                renderProgress();
            });
            m_optionGroup->addButton(radio);
            m_optionWidgets << radio;
            m_optionsLayout->addWidget(radio, index / 2, index % 2);
        }
    } else if (type == QStringLiteral("multi")) {
        const int maximum = question.value(QStringLiteral("max")).toInt(options.size());
        for (int index = 0; index < options.size(); ++index) {
            const QString value = options.at(index).toString();
            auto *check = new QCheckBox(value, m_optionsHost);
            check->setObjectName(QStringLiteral("optionButton"));
            check->setMinimumWidth(0);
            check->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            check->setProperty("answerValue", value);
            check->setChecked(selectedValues.contains(value));
            check->setEnabled(!m_editorReadOnly);
            registerRuntimePersonaEditControl(check);
            connect(check, &QCheckBox::toggled, this, [this, check, maximum](bool checked) {
                if (isRunningPersonaEditorLocked()) return;
                if (checked) {
                    int count = 0;
                    for (QCheckBox *item : m_optionsHost->findChildren<QCheckBox *>()) count += item->isChecked();
                    if (count > maximum) {
                        check->setChecked(false);
                        m_questionStatus->setText(QStringLiteral("本题最多选择 %1 项。").arg(maximum));
                        return;
                    }
                }
                captureCurrentAnswer();
                saveDraft();
                renderProgress();
            });
            m_optionWidgets << check;
            m_optionsLayout->addWidget(check, index / 2, index % 2);
        }
    } else if (type == QStringLiteral("scale")) {
        const int currentValue = selectedValues.isEmpty() ? 0 : selectedValues.first().toInt();
        m_scale->setValue(currentValue > 0 ? currentValue : 4);
        m_optionGroup->setExclusive(true);
        for (int value = 1; value <= 7; ++value) {
            auto *radio = new DeselectableRadioButton(QString::number(value), m_optionsHost);
            radio->setObjectName(QStringLiteral("optionButton"));
            radio->setMinimumWidth(0);
            radio->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
            radio->setProperty("scaleOption", true);
            radio->setProperty("answerValue", QString::number(value));
            radio->setChecked(currentValue > 0 && value == currentValue);
            radio->setEnabled(!m_editorReadOnly);
            registerRuntimePersonaEditControl(radio);
            m_optionGroup->addButton(radio);
            connect(radio, &QRadioButton::clicked, this, [this, radio, value](bool) {
                if (isRunningPersonaEditorLocked()) return;
                if (radio->wasCheckedOnPress()) {
                    forceUncheckRadioButton(radio);
                } else if (radio->isChecked()) {
                    m_scale->setValue(value);
                }
                captureCurrentAnswer();
                saveDraft();
                renderProgress();
            });
            m_optionWidgets << radio;
            m_optionsLayout->addWidget(radio, 0, value - 1);
        }
    }
    applyResponsiveLayout();
    m_optionsHost->setVisible(type == QStringLiteral("single") || type == QStringLiteral("multi") || type == QStringLiteral("scale"));
    QWidget *scaleHost = m_scale->parentWidget();
    scaleHost->setVisible(type == QStringLiteral("scale"));
    if (type == QStringLiteral("scale")) {
        m_scaleLow->setText(QStringLiteral("1 · %1").arg(question.value(QStringLiteral("lo")).toString()));
        m_scaleHigh->setText(QStringLiteral("7 · %1").arg(question.value(QStringLiteral("hi")).toString()));
        m_scale->setEnabled(!m_editorReadOnly);
    }
    m_answerText->setPlainText(answer.value(QStringLiteral("text")).toString());
    m_answerText->setReadOnly(m_editorReadOnly || isRunningPersonaEditorLocked());
    m_previousButton->setEnabled(m_currentQuestion > 0);
    m_nextButton->setText(m_currentQuestion + 1 < questions.size() ? QStringLiteral("下一题 →") : QStringLiteral("完成模块"));
    updateVoiceInputState();
    m_questionStatus->clear();
}

void MainWindow::saveDraft() {
    if (!m_personaName || m_editorReadOnly || isRunningPersonaEditorLocked()) return;
    m_draft.insert(QStringLiteral("name"), m_personaName->text().trimmed());
    m_draft.insert(QStringLiteral("answers"), m_answers);
    m_draft.insert(QStringLiteral("avatarPresetId"), m_selectedAvatarId);
    m_draft.insert(QStringLiteral("currentModule"), m_currentModule);
    m_draft.insert(QStringLiteral("currentQuestion"), m_currentQuestion);
    m_draft.insert(QStringLiteral("draftKind"), QStringLiteral("new"));
    // Only the unsaved NEW-persona workspace is a crash-recovery draft. Editing
    // or viewing an existing saved persona must never overwrite that recovery copy.
    if (m_editPackId.isEmpty()) m_data->setDraft(m_draft);
}

void MainWindow::restoreDraft() {
    QJsonObject persisted = m_data->draft();
    const QJsonObject persistedAnswers = persisted.value(QStringLiteral("answers")).toObject();
    const QString persistedName = persisted.value(QStringLiteral("name")).toString().trimmed();
    const QString persistedAvatar = persisted.value(QStringLiteral("avatarPresetId")).toString();

    // Legacy builds could persist the temporary snapshot used while viewing/editing
    // an existing persona. If the stored draft is byte-for-byte equivalent to a
    // saved persona, treat it as leaked view state rather than a user-created draft.
    bool leakedSavedPersonaSnapshot = false;
    const bool explicitlyNewDraft = persisted.value(QStringLiteral("draftKind")).toString() == QStringLiteral("new");
    if (!explicitlyNewDraft && (!persistedName.isEmpty() || !persistedAnswers.isEmpty() || !persistedAvatar.isEmpty())) {
        for (const QJsonValue &value : m_data->packs()) {
            const QJsonObject pack = value.toObject();
            if (pack.value(QStringLiteral("name")).toString().trimmed() == persistedName
                && pack.value(QStringLiteral("answers")).toObject() == persistedAnswers
                && pack.value(QStringLiteral("avatarPresetId")).toString() == persistedAvatar) {
                leakedSavedPersonaSnapshot = true;
                break;
            }
        }
    }
    if (leakedSavedPersonaSnapshot) {
        persisted = QJsonObject{{QStringLiteral("answers"), QJsonObject{}}, {QStringLiteral("draftKind"), QStringLiteral("new")}};
        m_data->setDraft(persisted);
    }

    m_draft = persisted;
    m_answers = persisted.value(QStringLiteral("answers")).toObject();
    m_selectedAvatarId = persisted.value(QStringLiteral("avatarPresetId")).toString();
    m_editPackId.clear();
    m_editorReadOnly = false;
    m_builtinAvatarOnly = false;
    m_personaName->setReadOnly(false);
    m_savePersonaButton->setVisible(true);
    m_savePersonaButton->setText(QStringLiteral("保存人格"));
    for (AvatarWidget *avatar : std::as_const(m_avatarWidgets)) {
        avatar->setEnabled(true);
        avatar->setSelected(avatar->avatarId() == m_selectedAvatarId);
    }

    const int moduleCount = m_data->modules().size();
    m_currentModule = moduleCount > 0
        ? qBound(0, persisted.value(QStringLiteral("currentModule")).toInt(0), moduleCount - 1)
        : 0;
    int questionCount = 0;
    if (moduleCount > 0)
        questionCount = m_data->modules().at(m_currentModule).toObject().value(QStringLiteral("questions")).toArray().size();
    m_currentQuestion = questionCount > 0
        ? qBound(0, persisted.value(QStringLiteral("currentQuestion")).toInt(0), questionCount - 1)
        : 0;
    m_personaName->setText(persisted.value(QStringLiteral("name")).toString());
    m_editorMode->clear();
    m_editorMode->setVisible(false);
    setPersonaStatusMessage(QString());
    updatePersonaEditorRuntimeLock();
    renderProgress();
    renderModules();
}

void MainWindow::resetDraft() {
    m_draft = QJsonObject{{QStringLiteral("answers"), QJsonObject{}}, {QStringLiteral("draftKind"), QStringLiteral("new")}};
    m_answers = QJsonObject{};
    m_selectedAvatarId.clear();
    m_editPackId.clear();
    m_editorReadOnly = false;
    m_builtinAvatarOnly = false;
    m_personaName->setReadOnly(false);
    m_savePersonaButton->setVisible(true);
    m_savePersonaButton->setText(QStringLiteral("保存人格"));
    for (AvatarWidget *avatar : std::as_const(m_avatarWidgets)) avatar->setEnabled(true);
    m_currentModule = 0;
    m_currentQuestion = 0;
    m_personaName->clear();
    m_editorMode->clear();
    m_editorMode->setVisible(false);
    for (AvatarWidget *avatar : std::as_const(m_avatarWidgets)) avatar->setSelected(false);
    updatePersonaEditorRuntimeLock();
    m_data->setDraft(m_draft);
    renderProgress();
    renderModules();
}

void MainWindow::setPersonaStatusMessage(const QString &message) {
    if (m_personaStatus) m_personaStatus->setText(message);
}

void MainWindow::savePersona() {
    if (isRunningPersonaEditorLocked()) {
        showRunningPetConfigLockedNotice();
        return;
    }
    if (m_editorReadOnly) {
        if (!m_builtinAvatarOnly || m_editPackId.isEmpty()) {
            setPersonaStatusMessage(QStringLiteral("当前人格为只读状态。"));
            return;
        }
        QJsonArray packs = m_data->packs();
        for (int index = 0; index < packs.size(); ++index) {
            QJsonObject pack = packs.at(index).toObject();
            if (pack.value(QStringLiteral("id")).toString() != m_editPackId) continue;
            if (m_selectedAvatarId.isEmpty()) {
                setPersonaStatusMessage(QStringLiteral("请先选择一个默认形象。"));
                return;
            }
            pack.insert(QStringLiteral("avatarPresetId"), m_selectedAvatarId);
            pack.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
            packs.replace(index, pack);
            m_data->setPacks(packs);
            invalidateActivePersona(m_editPackId, QStringLiteral("人格配置已修改，请返回人格管理重新点击“使用”。"));
            setPersonaStatusMessage(QStringLiteral("默认人格的桌宠形象已保存；如原本正在使用，请重新启用。"));
            renderPacks();
            // Keep built-in avatar editing consistent with saving a newly created
            // persona: after a successful save, leave the inspected persona and
            // return Edit Persona to the blank new-persona workspace.
            resetDraft();
            return;
        }
        return;
    }
    captureCurrentAnswer();
    const QJsonArray modules = m_data->modules();
    bool hasCoreResponse = false;
    for (int moduleIndex = 0; moduleIndex < modules.size() - 1 && !hasCoreResponse; ++moduleIndex) {
        const QJsonObject module = modules.at(moduleIndex).toObject();
        const int coreCount = module.value(QStringLiteral("coreCount")).toInt(3);
        const int total = module.value(QStringLiteral("questions")).toArray().size();
        const int coreLimit = qMin(coreCount, total);
        for (int questionIndex = 0; questionIndex < coreLimit; ++questionIndex) {
            if (hasQuestionResponse(moduleIndex, questionIndex)) { hasCoreResponse = true; break; }
        }
    }
    if (!hasCoreResponse) {
        showModelNotice(QStringLiteral("保存人格"), QStringLiteral("请至少回答一道核心问题"), false);
        return;
    }
    if (m_selectedAvatarId.isEmpty()) {
        showModelNotice(QStringLiteral("保存人格"), QStringLiteral("请选择一个桌宠形象"), false);
        return;
    }
    const QString name = m_personaName->text().trimmed();
    if (name.isEmpty()) {
        showModelNotice(QStringLiteral("保存人格"), QStringLiteral("请填写人格名称"), false);
        return;
    }
    QJsonArray packs = m_data->packs();
    QJsonObject pack;
    int replaceAt = -1;
    for (int index = 0; index < packs.size(); ++index) {
        if (packs.at(index).toObject().value(QStringLiteral("id")).toString() == m_editPackId) {
            pack = packs.at(index).toObject();
            replaceAt = index;
            break;
        }
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (pack.isEmpty()) {
        pack.insert(QStringLiteral("id"), QStringLiteral("persona-%1-%2").arg(now).arg(QUuid::createUuid().toString(QUuid::WithoutBraces).left(6)));
        pack.insert(QStringLiteral("createdAt"), now);
        pack.insert(QStringLiteral("source"), QStringLiteral("created"));
        pack.insert(QStringLiteral("author"), QStringLiteral("本机创建"));
        pack.insert(QStringLiteral("version"), QStringLiteral("1.0"));
        pack.insert(QStringLiteral("editable"), true);
        pack.insert(QStringLiteral("exportable"), true);
        pack.insert(QStringLiteral("deletable"), true);
        pack.insert(QStringLiteral("strength"), 80);
    }
    pack.insert(QStringLiteral("name"), name);
    pack.insert(QStringLiteral("answers"), m_answers);
    pack.insert(QStringLiteral("avatarPresetId"), m_selectedAvatarId);
    pack.insert(QStringLiteral("updatedAt"), now);
    QJsonArray moduleNames;
    for (const QJsonValue &value : m_data->modules()) moduleNames.append(value.toObject().value(QStringLiteral("name")));
    pack.insert(QStringLiteral("modules"), moduleNames);
    if (replaceAt >= 0) packs.replace(replaceAt, pack); else packs.append(pack);
    m_data->setPacks(packs);
    if (replaceAt >= 0)
        invalidateActivePersona(pack.value(QStringLiteral("id")).toString(), QStringLiteral("人格配置已修改，请重新点击“使用”。"));
    setPersonaStatusMessage(QStringLiteral("“%1”已保存。 ").arg(name));
    renderPacks();
    resetDraft();
}

void MainWindow::selectAvatar(AvatarWidget *avatar) {
    if (!avatar) return;
    if (isRunningPersonaEditorLocked()) { showRunningPetConfigLockedNotice(); return; }
    if (m_editorReadOnly && !m_builtinAvatarOnly) return;
    const bool clear = m_selectedAvatarId == avatar->avatarId();
    m_selectedAvatarId = clear ? QString() : avatar->avatarId();
    for (AvatarWidget *item : std::as_const(m_avatarWidgets)) item->setSelected(item->avatarId() == m_selectedAvatarId);
    setPersonaStatusMessage(QString());
    if (!m_editorReadOnly) saveDraft();
}

AvatarWidget *MainWindow::avatarById(const QString &id) const {
    for (AvatarWidget *avatar : m_avatarWidgets) if (avatar->avatarId() == id) return avatar;
    return nullptr;
}

void MainWindow::refreshUserAvatarSlots() {
    if (!m_avatarGrid) return;

    for (AvatarWidget *avatar : std::as_const(m_userAvatarWidgets)) {
        m_avatarGrid->removeWidget(avatar);
        m_avatarWidgets.removeAll(avatar);
        avatar->deleteLater();
    }
    m_userAvatarWidgets.clear();
    for (QWidget *slot : std::as_const(m_userAvatarSlotWidgets)) {
        m_avatarGrid->removeWidget(slot);
        slot->deleteLater();
    }
    m_userAvatarSlotWidgets.clear();

    const QJsonArray userAvatars = m_data->userAvatars();
    const int slotCount = m_data->maxUserAvatarCount();
    const int baseIndex = m_data->avatars().size();
    QWidget *host = m_avatarGrid->parentWidget();

    for (int slot = 0; slot < slotCount; ++slot) {
        QWidget *slotWidget = nullptr;
        if (slot < userAvatars.size()) {
            auto *avatar = new AvatarWidget(userAvatars.at(slot).toObject(), host);
            avatar->setSelected(avatar->avatarId() == m_selectedAvatarId);
            connect(avatar, &AvatarWidget::clicked, this, &MainWindow::selectAvatar);
            connect(avatar, &AvatarWidget::deleteRequested, this, &MainWindow::deleteUserAvatar);
            connect(avatar, &AvatarWidget::renameRequested, this, &MainWindow::renameUserAvatar);
            registerRuntimePersonaEditControl(avatar);
            m_avatarWidgets << avatar;
            m_userAvatarWidgets << avatar;
            slotWidget = avatar;
        } else {
            auto *empty = new QPushButton(QStringLiteral("＋"), host);
            empty->setObjectName(QStringLiteral("userAvatarSlot"));
            empty->setCursor(Qt::PointingHandCursor);
            empty->setMinimumSize(92, 110);
            empty->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
            empty->setToolTip(QStringLiteral("上传单张 SVG 或包含 1~10 张 SVG 的压缩包"));
            connect(empty, &QPushButton::clicked, this, &MainWindow::uploadUserAvatar);
            static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(empty, QColor(91, 123, 107, 28));
            m_userAvatarSlotWidgets << empty;
            slotWidget = empty;
        }
        const int index = baseIndex + slot;
        m_avatarGrid->addWidget(slotWidget, index / 8, index % 8);
    }

    QTimer::singleShot(0, this, &MainWindow::applyResponsiveLayout);
}

void MainWindow::uploadUserAvatar() {
    if (m_data->userAvatars().size() >= m_data->maxUserAvatarCount()) {
        showModelNotice(QStringLiteral("导入形象"), QStringLiteral("自定义形象已满，请先删除一个。"), true);
        return;
    }

    QFileDialog dialog(this, QStringLiteral("上传桌宠形象"), QString(),
                       QStringLiteral("桌宠形象 (*.svg *.zip *.7z *.rar *.tar *.tar.gz *.tgz);;SVG 图像 (*.svg);;压缩包 (*.zip *.7z *.rar *.tar *.tar.gz *.tgz)"));
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (execTopmostDialog(dialog) != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;

    QString error;
    const QJsonObject avatar = m_data->importUserAvatar(dialog.selectedFiles().constFirst(), &error);
    if (avatar.isEmpty()) {
        showModelNotice(QStringLiteral("导入形象"), error.isEmpty() ? QStringLiteral("导入失败。") : error, true);
        return;
    }
    refreshUserAvatarSlots();
    setPersonaStatusMessage(QStringLiteral("“%1”已加入自定义形象。")
                            .arg(avatar.value(QStringLiteral("name")).toString()));
}

void MainWindow::renameUserAvatar(AvatarWidget *avatar) {
    if (!avatar || !avatar->isUserAvatar()) return;
    const QJsonObject runningPack = m_runningPersonaId.isEmpty() ? QJsonObject{} : m_data->packById(m_runningPersonaId);
    if (!m_petWindow.isNull() && runningPack.value(QStringLiteral("avatarPresetId")).toString() == avatar->avatarId()) {
        showRunningPetConfigLockedNotice();
        return;
    }
    if (isRunningPersonaEditorLocked()) { showRunningPetConfigLockedNotice(); return; }

    QInputDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("重命名形象"));
    dialog.setLabelText(QStringLiteral("形象名称"));
    dialog.setInputMode(QInputDialog::TextInput);
    dialog.setTextValue(avatar->avatarName());
    dialog.setOption(QInputDialog::UsePlainTextEditForTextInput, false);
    dialog.resize(360, dialog.sizeHint().height());
    if (execTopmostDialog(dialog) != QDialog::Accepted) return;

    QString error;
    if (!m_data->renameUserAvatar(avatar->avatarId(), dialog.textValue(), &error)) {
        showModelNotice(QStringLiteral("重命名形象"), error.isEmpty() ? QStringLiteral("重命名失败。") : error, true);
        return;
    }
    const QString selectedId = m_selectedAvatarId;
    refreshUserAvatarSlots();
    for (AvatarWidget *item : std::as_const(m_avatarWidgets)) item->setSelected(item->avatarId() == selectedId);
    renderPacks();
    setPersonaStatusMessage(QStringLiteral("形象已重命名。"));
}

void MainWindow::deleteUserAvatar(AvatarWidget *avatar) {
    if (!avatar || !avatar->isDeletable()) return;
    const QJsonObject runningPack = m_runningPersonaId.isEmpty() ? QJsonObject{} : m_data->packById(m_runningPersonaId);
    if (!m_petWindow.isNull() && runningPack.value(QStringLiteral("avatarPresetId")).toString() == avatar->avatarId()) {
        showRunningPetConfigLockedNotice();
        return;
    }
    if (isRunningPersonaEditorLocked()) { showRunningPetConfigLockedNotice(); return; }
    if (!askTopmostQuestion(QStringLiteral("删除自定义形象"),
                            QStringLiteral("确定删除“%1”吗？").arg(avatar->avatarName()),
                            QStringLiteral("删除"), QStringLiteral("取消"))) return;

    const QString id = avatar->avatarId();
    const bool wasSelected = m_selectedAvatarId == id;
    QString error;
    if (!m_data->removeUserAvatar(id, &error)) {
        setPersonaStatusMessage(QStringLiteral("删除失败：%1").arg(error));
        return;
    }

    if (wasSelected) {
        m_selectedAvatarId = m_builtinAvatarOnly ? QStringLiteral("builtin-avatar-01") : QString();
        m_draft.insert(QStringLiteral("avatarPresetId"), m_selectedAvatarId);
        if (!m_editorReadOnly) saveDraft();
    }
    refreshUserAvatarSlots();
    for (AvatarWidget *item : std::as_const(m_avatarWidgets))
        item->setSelected(item->avatarId() == m_selectedAvatarId);
    renderPacks();
    setPersonaStatusMessage(QStringLiteral("自定义形象“%1”已删除。") .arg(avatar->avatarName()));
}

void MainWindow::renderPacks() {
    updatePersonaLayoutMetrics();
    clearLayout(m_packsLayout);
    m_packCards.clear();
    m_packGrid = nullptr;
    m_packGridHost = nullptr;
    m_packGridColumns = 0;
    const QString active = m_data->activePersonaId();
    const QJsonArray allPacks = newestPersonaPacksFirst(m_data->packs());
    if (m_selectedPackId.isEmpty() && !allPacks.isEmpty()) {
        m_selectedPackId = !active.isEmpty() ? active : allPacks.first().toObject().value(QStringLiteral("id")).toString();
    }

    m_packGridHost = new QWidget;
    m_packGridHost->setMinimumWidth(0);
    m_packGridHost->setMaximumWidth(QWIDGETSIZE_MAX);
    m_packGridHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *gridHost = m_packGridHost;
    m_packGrid = new QGridLayout(gridHost);
    auto *grid = m_packGrid;
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(0);
    grid->setVerticalSpacing(18);

    constexpr int kPersonaCardHeight = 198;
    constexpr int kAvatarSize = 48;
    int index = 0;
    for (const QJsonValue &value : allPacks) {
        const QJsonObject pack = value.toObject();
        const QString id = pack.value(QStringLiteral("id")).toString();
        const QString source = pack.value(QStringLiteral("source")).toString();
        const bool deletable = pack.value(QStringLiteral("deletable")).toBool();

        auto *card = new QFrame;
        card->setObjectName(QStringLiteral("packCard"));
        card->setProperty("source", source);
        card->setProperty("selected", id == m_selectedPackId);
        card->setProperty("inUse", active == id);
        card->setFixedSize(m_personaCardWidth, kPersonaCardHeight);
        card->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        card->setCursor(Qt::PointingHandCursor);

        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(9);

        // Header geometry is shared by every built-in/future built-in/user pack.
        // The title/badge share the exact same top/bottom; metadata ends on the
        // same baseline as the avatar block.
        auto *top = new QHBoxLayout;
        top->setContentsMargins(0, 0, 0, 0);
        top->setSpacing(12);
        top->setAlignment(Qt::AlignTop);

        const QString packName = pack.value(QStringLiteral("name")).toString();
        auto *avatar = new QLabel(packName.isEmpty() ? QStringLiteral("人") : packName.left(1), card);
        avatar->setObjectName(QStringLiteral("packAvatar"));
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setFixedSize(kAvatarSize, kAvatarSize);

        auto *copyHost = new QWidget(card);
        copyHost->setFixedHeight(kAvatarSize);
        copyHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *copy = new QVBoxLayout(copyHost);
        copy->setContentsMargins(0, 0, 0, 0);
        copy->setSpacing(2);

        auto *titleRow = new QHBoxLayout;
        titleRow->setContentsMargins(0, 0, 0, 0);
        titleRow->setSpacing(8);
        auto *name = new QLabel(packName, card);
        name->setObjectName(QStringLiteral("packName"));
        name->setFixedHeight(24);
        name->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        titleRow->addWidget(name, 1, Qt::AlignVCenter);

        if (deletable) {
            auto *removeCorner = new QToolButton(card);
            removeCorner->setObjectName(QStringLiteral("packDeleteCorner"));
            removeCorner->setText(QStringLiteral("×"));
            removeCorner->setToolTip(QStringLiteral("删除人格"));
            removeCorner->setAccessibleName(QStringLiteral("删除人格"));
            removeCorner->setProperty("excludeWindowDrag", true);
            removeCorner->setFixedSize(24, 24);
            connect(removeCorner, &QToolButton::clicked, this, [this, id] { deletePack(id); });
            titleRow->addWidget(removeCorner, 0, Qt::AlignTop);
        } else {
            auto *kind = new QLabel(sourceKind(source), card);
            kind->setObjectName(QStringLiteral("packKind"));
            kind->setFixedHeight(24);
            kind->setAlignment(Qt::AlignCenter);
            titleRow->addWidget(kind, 0, Qt::AlignTop);
        }
        copy->addLayout(titleRow);

        int answeredCount = 0;
        const QJsonObject packAnswers = pack.value(QStringLiteral("answers")).toObject();
        for (auto iterator = packAnswers.constBegin(); iterator != packAnswers.constEnd(); ++iterator) {
            if (iterator.value().toObject().value(QStringLiteral("state")).toString() == QStringLiteral("answered")) ++answeredCount;
        }
        if (id == QStringLiteral("patrick") && answeredCount == 0) answeredCount = 36;
        const QString metaText = QStringLiteral("已填写 %1 项 · %2")
            .arg(answeredCount).arg(pack.value(QStringLiteral("author")).toString());
        auto *meta = smallMuted(metaText, card);
        meta->setFixedHeight(22);
        meta->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
        copy->addWidget(meta, 0, Qt::AlignBottom);

        top->addWidget(avatar, 0, Qt::AlignTop);
        top->addWidget(copyHost, 1, Qt::AlignTop);
        layout->addLayout(top);

        auto *strengthBox = new QFrame(card);
        strengthBox->setObjectName(QStringLiteral("packStrength"));
        auto *strengthLayout = new QVBoxLayout(strengthBox);
        strengthLayout->setContentsMargins(10, 9, 10, 9);
        auto *strengthHead = new QHBoxLayout;
        strengthHead->addWidget(smallMuted(QStringLiteral("人格强度"), strengthBox));
        auto *strengthValue = new QLabel(QStringLiteral("%1%").arg(pack.value(QStringLiteral("strength")).toInt(80)), strengthBox);
        strengthValue->setObjectName(QStringLiteral("strengthValue"));
        strengthHead->addStretch();
        strengthHead->addWidget(strengthValue);
        auto *slider = new QSlider(Qt::Horizontal, strengthBox);
        slider->setMinimumHeight(24);
        slider->setRange(0, 100);
        slider->setValue(pack.value(QStringLiteral("strength")).toInt(80));
        slider->setEnabled(true);
        registerRuntimePersonaEditControl(slider, id);
        connect(slider, &QSlider::valueChanged, this, [this, id, strengthValue, card](int value) {
            if (!m_petWindow.isNull() && id == m_runningPersonaId) return;
            QJsonArray packs = m_data->packs();
            for (int i = 0; i < packs.size(); ++i) {
                QJsonObject item = packs.at(i).toObject();
                if (item.value(QStringLiteral("id")).toString() == id) {
                    item.insert(QStringLiteral("strength"), value);
                    item.insert(QStringLiteral("updatedAt"), QDateTime::currentMSecsSinceEpoch());
                    packs.replace(i, item);
                    break;
                }
            }
            m_data->setPacks(packs);
            strengthValue->setText(QStringLiteral("%1%").arg(value));
            invalidateActivePersona(id, QStringLiteral("人格强度已修改，请重新点击“使用”。"));
            if (auto *useButton = card->findChild<QPushButton *>(QStringLiteral("useAction"))) {
                useButton->setText(QStringLiteral("使用"));
                useButton->setEnabled(true);
                setDynamicProperty(useButton, "active", false);
            }
        });
        strengthLayout->addLayout(strengthHead);
        strengthLayout->addWidget(slider);
        layout->addWidget(strengthBox);

        auto *actions = new QHBoxLayout;
        actions->setContentsMargins(0, 0, 0, 0);
        actions->setSpacing(12);
        auto *view = new QPushButton(QStringLiteral("查看"), card);
        view->setObjectName(QStringLiteral("packAction"));
        auto *use = new QPushButton(QStringLiteral("使用"), card);
        use->setObjectName(QStringLiteral("useAction"));
        use->setProperty("active", active == id);
        use->setEnabled(active != id);
        use->setProperty("excludeWindowDrag", true);
        view->setProperty("excludeWindowDrag", true);
        connect(use, &QPushButton::clicked, this, [this, id] { usePack(id); });
        connect(view, &QPushButton::clicked, this, [this, id] { viewPack(id); });
        actions->addWidget(view, 1);
        actions->addWidget(use, 1);
        layout->addLayout(actions);

        card->installEventFilter(this);
        card->setProperty("packId", id);
        for (QWidget *child : card->findChildren<QWidget *>()) child->setProperty("packId", id);
        m_packCards << card;
        // Temporary placement; applyResponsiveLayout() immediately rebuilds the
        // current 2/3-column grid using the fixed outer/inter-card gutter.
        grid->addWidget(card, index / 2, index % 2, Qt::AlignHCenter | Qt::AlignTop);
        ++index;
    }
    if (index == 0) {
        auto *empty = new QLabel(QStringLiteral("尚未创建人格。"), gridHost);
        empty->setObjectName(QStringLiteral("emptyPack"));
        grid->addWidget(empty, 0, 0);
    }
    m_packsLayout->addWidget(gridHost);
    selectPack(m_selectedPackId, false);
    applyResponsiveLayout();
}

void MainWindow::selectPack(const QString &id, bool announce) {
    const QJsonObject pack = m_data->packById(id);
    m_selectedPackId = pack.isEmpty() ? QString() : id;
    m_exportSelectedButton->setEnabled(!pack.isEmpty() && pack.value(QStringLiteral("exportable")).toBool());
    Q_UNUSED(announce);
    if (!pack.isEmpty()) {
        for (QFrame *card : m_packsLayout->parentWidget()->findChildren<QFrame *>(QStringLiteral("packCard"))) {
            setDynamicProperty(card, "selected", card->property("packId").toString() == id);
        }
    }
}

void MainWindow::invalidateActivePersona(const QString &id, const QString &reason) {
    if (id.isEmpty() || m_data->activePersonaId() != id) return;
    m_data->setActivePersonaId(QString());
    for (QWidget *widget : std::as_const(m_packCards)) {
        auto *card = qobject_cast<QFrame *>(widget);
        if (!card) continue;
        const bool changedCard = card->property("packId").toString() == id;
        setDynamicProperty(card, "inUse", false);
        if (changedCard) {
            if (auto *button = card->findChild<QPushButton *>(QStringLiteral("useAction"))) {
                button->setText(QStringLiteral("使用"));
                button->setEnabled(true);
                setDynamicProperty(button, "active", false);
            }
        }
    }
    if (m_packStatus) m_packStatus->setText(reason);
    loadModelConfig();
}

void MainWindow::usePack(const QString &id) {
    const QJsonObject pack = m_data->packById(id);
    if (pack.isEmpty()) return;

    // V90: "使用" is also a pet switch boundary. Selecting persona B while
    // persona A has a live desktop pet closes A (and its chat/session) first;
    // the user can then generate B with a clean runtime state.
    if (!m_petWindow.isNull() && !m_runningPersonaId.isEmpty() && m_runningPersonaId != id)
        closePetAndChat(false);

    m_data->setActivePersonaId(id);
    m_selectedPackId = id;
    if (m_packStatus) m_packStatus->clear();
    renderPacks();
    loadModelConfig();
}

void MainWindow::viewPack(const QString &id) {
    const QJsonObject pack = m_data->packById(id);
    if (pack.isEmpty()) return;
    m_editPackId = id;
    m_editorReadOnly = !pack.value(QStringLiteral("editable")).toBool();
    m_builtinAvatarOnly = pack.value(QStringLiteral("source")).toString() == QStringLiteral("builtin");
    m_answers = pack.value(QStringLiteral("answers")).toObject();
    m_draft = QJsonObject{{QStringLiteral("answers"), m_answers},
                          {QStringLiteral("name"), pack.value(QStringLiteral("name"))},
                          {QStringLiteral("avatarPresetId"), pack.value(QStringLiteral("avatarPresetId"))}};
    m_selectedAvatarId = pack.value(QStringLiteral("avatarPresetId")).toString();
    m_personaName->setText(pack.value(QStringLiteral("name")).toString());
    m_personaName->setReadOnly(m_editorReadOnly || isRunningPersonaEditorLocked());
    m_savePersonaButton->setVisible(!m_editorReadOnly || m_builtinAvatarOnly);
    m_savePersonaButton->setText(m_builtinAvatarOnly ? QStringLiteral("保存形象") : QStringLiteral("保存人格"));
    m_editorMode->setText(m_builtinAvatarOnly ? QString()
                                               : (m_editorReadOnly ? QStringLiteral("查看模式") : QStringLiteral("编辑模式")));
    m_editorMode->setVisible(!m_editorMode->text().isEmpty());
    for (AvatarWidget *avatar : std::as_const(m_avatarWidgets)) {
        avatar->setSelected(avatar->avatarId() == m_selectedAvatarId);
        avatar->setEnabled(!m_editorReadOnly || m_builtinAvatarOnly);
    }
    updatePersonaEditorRuntimeLock();
    renderProgress();
    renderModules();
    switchView(0);
}

void MainWindow::deletePack(const QString &id) {
    if (!m_petWindow.isNull() && id == m_runningPersonaId) { showRunningPetConfigLockedNotice(); return; }
    if (!askTopmostQuestion(QStringLiteral("删除人格"), QStringLiteral("确定删除该人格吗？"), QStringLiteral("删除"), QStringLiteral("取消"))) return;
    QJsonArray kept;
    for (const QJsonValue &value : m_data->packs()) if (value.toObject().value(QStringLiteral("id")).toString() != id) kept.append(value);
    m_data->setPacks(kept);
    if (m_data->activePersonaId() == id) m_data->setActivePersonaId(QString());
    if (m_selectedPackId == id) m_selectedPackId.clear();
    renderPacks();
}

void MainWindow::exportPack(const QString &id) {
    const QJsonObject pack = m_data->packById(id);
    if (pack.isEmpty() || !pack.value(QStringLiteral("exportable")).toBool()) {
        m_packStatus->setText(QStringLiteral("当前选中的人格不能导出。"));
        return;
    }
    m_exportTargetPackId = id;
    m_exportTitle->setText(QStringLiteral("导出 · %1").arg(pack.value(QStringLiteral("name")).toString()));
    m_exportPackName->setText(QStringLiteral("%1 · 分享版").arg(pack.value(QStringLiteral("name")).toString()));
    if (m_exportPanel->parentWidget()) m_exportPanel->parentWidget()->setVisible(true);
    m_exportPanel->setVisible(true);
    clearLayout(m_exportModulesLayout);
    m_exportModulesLayout->setProperty("responsiveColumns", -1);
    m_exportModulesLayout->setProperty("responsiveItemCount", -1);
    m_exportModuleChecks.clear();
    const QJsonArray modules = m_data->modules();
    for (int index = 0; index < modules.size(); ++index) {
        const QJsonObject module = modules.at(index).toObject();
        auto *moduleCard = new ClickableFrame(m_exportPanel);
        moduleCard->setObjectName(QStringLiteral("exportModuleCard"));
        moduleCard->setProperty("checked", true);
        moduleCard->setMinimumHeight(68);
        auto *row = new QHBoxLayout(moduleCard);
        row->setContentsMargins(12, 10, 12, 10);
        row->setSpacing(10);
        auto *check = new QCheckBox(moduleCard);
        check->setObjectName(QStringLiteral("moduleCheck"));
        check->setChecked(true);
        check->setProperty("moduleIndex", index);
        check->setToolTip(QStringLiteral("包含此模块"));
        auto *copy = new QVBoxLayout;
        copy->setSpacing(3);
        auto *moduleName = new QLabel(module.value(QStringLiteral("name")).toString(), moduleCard);
        moduleName->setObjectName(QStringLiteral("exportModuleName"));
        auto *moduleDescription = new QLabel(module.value(QStringLiteral("desc")).toString(), moduleCard);
        moduleDescription->setObjectName(QStringLiteral("exportModuleDescription"));
        moduleDescription->setWordWrap(true);
        copy->addWidget(moduleName);
        copy->addWidget(moduleDescription);
        row->addLayout(copy, 1);
        // Keep every checkbox pinned to the same right-edge anchor regardless of
        // title length so all rows/columns form a visually clean selection grid.
        row->addWidget(check, 0, Qt::AlignRight | Qt::AlignVCenter);
        moduleCard->setActivated([check] { check->toggle(); });
        static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(moduleCard);
        connect(check, &QCheckBox::toggled, this, [this, moduleCard](bool checked) {
            setDynamicProperty(moduleCard, "checked", checked);
            refreshExportPanel();
        });
        m_exportModuleChecks << check;
        m_exportModulesLayout->addWidget(moduleCard, index / 3, index % 3);
    }
    refreshExportPanel();
    if (m_views && m_views->currentWidget()) m_views->currentWidget()->updateGeometry();
    if (m_views) m_views->updateGeometry();

    // V118: clicking “导出人格” should visually carry the user to Export Persona.
    // Wait for the newly-expanded panel to participate in layout, then animate the
    // page scrollbar with an eased curve.  Never call ensureWidgetVisible() here:
    // that API jumps immediately and is exactly the flash-like transition we want
    // to avoid.
    QTimer::singleShot(0, this, [this] {
        if (!m_desktopScroll || !m_exportPanel || !m_desktopScroll->widget()) return;
        if (QLayout *layout = m_desktopScroll->widget()->layout()) layout->activate();
        m_exportPanel->updateGeometry();
        QTimer::singleShot(0, this, [this] {
            if (!m_desktopScroll || !m_exportPanel || !m_desktopScroll->widget()) return;
            QScrollBar *bar = m_desktopScroll->verticalScrollBar();
            if (!bar) return;
            const int panelTop = m_exportPanel->mapTo(m_desktopScroll->widget(), QPoint(0, 0)).y();
            const int target = qBound(bar->minimum(), panelTop - 18, bar->maximum());
            const int start = bar->value();
            if (target == start) return;

            if (auto *previous = bar->findChild<QPropertyAnimation *>(QStringLiteral("exportPersonaScrollAnimation"),
                                                                      Qt::FindDirectChildrenOnly)) {
                previous->stop();
                previous->deleteLater();
            }
            auto *animation = new QPropertyAnimation(bar, "value", bar);
            animation->setObjectName(QStringLiteral("exportPersonaScrollAnimation"));
            animation->setDuration(520);
            animation->setStartValue(start);
            animation->setEndValue(target);
            animation->setEasingCurve(QEasingCurve::OutCubic);
            connect(animation, &QPropertyAnimation::finished, animation, &QObject::deleteLater);
            connect(bar, &QScrollBar::sliderPressed, animation, [animation] { animation->stop(); animation->deleteLater(); });
            animation->start();
        });
    });
}

void MainWindow::refreshExportPanel() {
    bool anyChecked = false;
    for (QCheckBox *check : std::as_const(m_exportModuleChecks)) {
        if (check && check->isChecked()) {
            anyChecked = true;
            break;
        }
    }
    // V118: export is a stateful primary action. With no selected module it is
    // visibly disabled instead of accepting a click and showing a warning dialog.
    if (m_exportPackButton) m_exportPackButton->setEnabled(anyChecked && !m_exportTargetPackId.isEmpty());
}

void MainWindow::performExport() {
    const QJsonObject original = m_data->packById(m_exportTargetPackId);
    if (original.isEmpty()) return;
    QSet<int> selectedModules;
    QJsonArray selectedNames;
    for (QCheckBox *check : std::as_const(m_exportModuleChecks)) {
        if (!check->isChecked()) continue;
        const int moduleIndex = check->property("moduleIndex").toInt();
        selectedModules.insert(moduleIndex);
        selectedNames.append(m_data->modules().at(moduleIndex).toObject().value(QStringLiteral("name")));
    }
    // The button is disabled when this is empty. Keep the guard for defensive
    // programmatic calls, but do not surface a second click-time warning.
    if (selectedModules.isEmpty()) return;
    QJsonObject pack = original;
    pack.insert(QStringLiteral("name"), m_exportPackName->text().trimmed().isEmpty() ? original.value(QStringLiteral("name")) : QJsonValue(m_exportPackName->text().trimmed()));
    QJsonObject filteredAnswers;
    const QJsonObject originalAnswers = original.value(QStringLiteral("answers")).toObject();
    for (auto iterator = originalAnswers.constBegin(); iterator != originalAnswers.constEnd(); ++iterator) {
        const int separator = iterator.key().indexOf(u':');
        const int moduleIndex = separator > 0 ? iterator.key().left(separator).toInt() : -1;
        if (selectedModules.contains(moduleIndex)) filteredAnswers.insert(iterator.key(), iterator.value());
    }
    pack.insert(QStringLiteral("answers"), filteredAnswers);
    pack.insert(QStringLiteral("modules"), selectedNames);
    QJsonArray selectedModuleIndexes;
    QList<int> sortedModuleIndexes = selectedModules.values();
    std::sort(sortedModuleIndexes.begin(), sortedModuleIndexes.end());
    for (int moduleIndex : std::as_const(sortedModuleIndexes)) selectedModuleIndexes.append(moduleIndex);
    // V118: this transient field tells the Markdown generator exactly which
    // sections may be emitted. It is stripped from the exported manifest later.
    pack.insert(QStringLiteral("exportModuleIndexes"), selectedModuleIndexes);
    QFileDialog saveDialog(this, QStringLiteral("导出人格包"),
                           pack.value(QStringLiteral("name")).toString() + QStringLiteral(".zip"),
                           QStringLiteral("Capricorn 人格包 (*.zip *.petpack)"));
    saveDialog.setAcceptMode(QFileDialog::AcceptSave);
    saveDialog.setFileMode(QFileDialog::AnyFile);
    saveDialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (execTopmostDialog(saveDialog) != QDialog::Accepted || saveDialog.selectedFiles().isEmpty()) return;
    const QString path = saveDialog.selectedFiles().constFirst();
    QString error;
    if (!m_data->exportPack(pack, path, &error)) {
        showModelNotice(QStringLiteral("导出人格包"), QStringLiteral("导出失败：") + error, true);
        return;
    }
    showModelNotice(QStringLiteral("导出完成"), QStringLiteral("人格包已成功导出。"), false);
}

void MainWindow::importPack() {
    QFileDialog openDialog(this, QStringLiteral("导入人格包"), QString(),
                           QStringLiteral("Capricorn 人格包 (*.petpack *.zip)"));
    openDialog.setAcceptMode(QFileDialog::AcceptOpen);
    openDialog.setFileMode(QFileDialog::ExistingFile);
    openDialog.setOption(QFileDialog::DontUseNativeDialog, true);
    if (execTopmostDialog(openDialog) != QDialog::Accepted || openDialog.selectedFiles().isEmpty()) return;
    const QString path = openDialog.selectedFiles().constFirst();
    QString error;
    const QJsonObject pack = m_data->importPack(path, &error);
    if (pack.isEmpty()) {
        m_packStatus->setText(QStringLiteral("导入失败：") + error);
        return;
    }
    QJsonArray packs = m_data->packs();
    packs.append(pack);
    m_data->setPacks(packs);
    m_selectedPackId = pack.value(QStringLiteral("id")).toString();
    m_packStatus->setText(QStringLiteral("人格包已导入，可像本机新增人格一样查看和编辑。"));
    renderPacks();
}

QJsonObject MainWindow::collectModelConfig() const {
    return QJsonObject{{QStringLiteral("provider"), m_provider},
                       {QStringLiteral("providerName"), m_providerDisplay},
                       {QStringLiteral("baseUrl"), m_baseUrl->text().trimmed()},
                       {QStringLiteral("modelId"), m_modelId->text().trimmed()},
                       {QStringLiteral("apiKey"), m_apiKey->text().trimmed()}};
}

void MainWindow::loadModelConfig() {
    if (!m_baseUrl || !m_modelId || !m_apiKey) return;
    const QJsonArray configs = sortedModelConfigs(m_data->modelConfigs());
    QJsonObject config = m_data->modelConfig();

    // Repair a legacy/current selection that does not carry its saved item ID.
    if (config.value(QStringLiteral("id")).toString().isEmpty() && !config.isEmpty()) {
        const QJsonObject target = canonicalModelConfig(config);
        for (const QJsonValue &value : configs) {
            const QJsonObject saved = value.toObject();
            if (canonicalModelConfig(saved) == target) {
                config = saved;
                break;
            }
        }
    }
    if (config.isEmpty() && !configs.isEmpty()) config = configs.first().toObject();

    m_loadingModelConfig = true;
    m_provider = config.value(QStringLiteral("provider")).toString(QStringLiteral("custom"));
    m_providerDisplay = config.value(QStringLiteral("providerName")).toString(QStringLiteral("自定义服务"));
    m_baseUrl->setText(config.value(QStringLiteral("baseUrl")).toString());
    m_modelId->setText(config.value(QStringLiteral("modelId")).toString());
    m_apiKey->setText(config.value(QStringLiteral("apiKey")).toString());
    for (QPushButton *button : std::as_const(m_providerButtons))
        button->setChecked(button->property("providerId").toString() == m_provider);
    m_loadedModelConfigId = config.value(QStringLiteral("id")).toString();
    m_lastSavedModelConfig = m_loadedModelConfigId.isEmpty() ? QJsonObject{} : canonicalModelConfig(config);
    m_loadingModelConfig = false;

    if (m_providerName) m_providerName->setText(QStringLiteral("人格交接"));
    const QJsonObject active = m_data->packById(m_data->activePersonaId());
    m_activePersonaLabel->setText(active.isEmpty() ? QStringLiteral("当前人格：尚未选择")
                                                   : QStringLiteral("当前人格：%1 · 强度 %2%")
                                                         .arg(active.value(QStringLiteral("name")).toString())
                                                         .arg(active.value(QStringLiteral("strength")).toInt(80)));
    refreshSavedModelConfigs();
    updateModelSaveState();
    invalidateModelVerification();
    if (m_activeSessionId.isEmpty()) resetGenerationSteps();
    updateGeneratePetAvailability();
}

int MainWindow::execTopmostDialog(QDialog &dialog) {
    if (m_modalActive) return QDialog::Rejected;
    m_modalActive = true;
    if (m_petWindow) m_petWindow->setModalBlocked(true);
    if (m_chatWindow) m_chatWindow->setModalBlocked(true);

    dialog.setWindowModality(Qt::ApplicationModal);
    dialog.setModal(true);
    dialog.setWindowFlag(Qt::WindowStaysOnTopHint, true);
    dialog.setAttribute(Qt::WA_DeleteOnClose, false);
    QTimer::singleShot(0, &dialog, [&dialog] {
        dialog.raise();
        dialog.activateWindow();
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(dialog.winId());
        if (hwnd) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOOWNERZORDER);
            SetForegroundWindow(hwnd);
        }
#endif
    });
    const int result = dialog.exec();

    if (m_chatWindow) m_chatWindow->setModalBlocked(false);
    if (m_petWindow) m_petWindow->setModalBlocked(false);
    m_modalActive = false;
    return result;
}

bool MainWindow::askTopmostQuestion(const QString &title, const QString &message,
                                    const QString &acceptText, const QString &rejectText) {
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("modelNoticeDialog"));
    const bool avatarDeleteDialog = title == QStringLiteral("删除自定义形象");
    const bool personaDeleteDialog = title == QStringLiteral("删除人格");
    const bool destructiveDialog = avatarDeleteDialog || personaDeleteDialog;
    dialog.setProperty("avatarDelete", avatarDeleteDialog);
    dialog.setProperty("personaDelete", personaDeleteDialog);
    dialog.setProperty("destructiveDialog", destructiveDialog);
    dialog.setProperty("capricornUnified", true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground, true);
    dialog.setMinimumWidth(destructiveDialog ? 360 : 380);
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(12, 12, 12, 12);
    auto *card = new QFrame(&dialog);
    card->setObjectName(QStringLiteral("modelNoticeCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, avatarDeleteDialog ? 20 : 18, 20, 16);
    layout->setSpacing(avatarDeleteDialog ? 14 : 12);
    auto *heading = new QLabel(title, card);
    heading->setObjectName(QStringLiteral("modelNoticeTitle"));
    auto *body = new QLabel(message, card);
    body->setObjectName(QStringLiteral("modelNoticeBody"));
    body->setWordWrap(true);
    if (avatarDeleteDialog) body->setContentsMargins(0, 2, 0, 4);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    auto *reject = new QPushButton(rejectText, card);
    reject->setObjectName(QStringLiteral("modelNoticeSecondary"));
    reject->setFixedSize(76, 34);
    auto *accept = new QPushButton(acceptText, card);
    accept->setObjectName(QStringLiteral("modelNoticeOk"));
    accept->setFixedSize(76, 34);
    connect(reject, &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(accept, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttons->addWidget(reject);
    buttons->addWidget(accept);
    layout->addWidget(heading);
    layout->addWidget(body);
    layout->addLayout(buttons);
    outer->addWidget(card);
    return execTopmostDialog(dialog) == QDialog::Accepted;
}

void MainWindow::showModelNotice(const QString &title, const QString &message, bool error) {
    if (m_exitRequested) return;
    if (m_modalActive) {
        QPointer<MainWindow> self(this);
        QTimer::singleShot(120, this, [self, title, message, error] {
            if (self) self->showModelNotice(title, message, error);
        });
        return;
    }
    QDialog dialog(this);
    dialog.setObjectName(QStringLiteral("modelNoticeDialog"));
    dialog.setProperty("exportSuccess", !error && title == QStringLiteral("导出完成"));
    dialog.setProperty("personaValidation", title == QStringLiteral("保存人格"));
    dialog.setProperty("capricornUnified", true);
    dialog.setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    dialog.setAttribute(Qt::WA_TranslucentBackground, true);
    dialog.setModal(true);
    dialog.setMinimumWidth(360);
    auto *outer = new QVBoxLayout(&dialog);
    outer->setContentsMargins(12, 12, 12, 12);
    auto *card = new QFrame(&dialog);
    card->setObjectName(QStringLiteral("modelNoticeCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(11);
    auto *heading = new QLabel(title, card);
    heading->setObjectName(error ? QStringLiteral("modelNoticeTitleError") : QStringLiteral("modelNoticeTitle"));
    auto *body = new QLabel(message, card);
    body->setObjectName(QStringLiteral("modelNoticeBody"));
    body->setWordWrap(true);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    auto *ok = new QPushButton(QStringLiteral("知道了"), card);
    ok->setObjectName(QStringLiteral("modelNoticeOk"));
    ok->setFixedSize(82, 34);
    connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
    buttonRow->addWidget(ok);
    layout->addWidget(heading);
    if (!message.trimmed().isEmpty()) layout->addWidget(body);
    layout->addLayout(buttonRow);
    outer->addWidget(card);
    execTopmostDialog(dialog);
}


void MainWindow::saveModelConfig() {
    const QJsonObject current = canonicalModelConfig(collectModelConfig());
    const QString validationError = modelConfigValidationError(current);
    if (!validationError.isEmpty()) {
        updateModelSaveState();
        showModelNotice(QStringLiteral("保存配置"), validationError, true);
        return;
    }

    QJsonArray configs = m_data->modelConfigs();
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QJsonObject saved = current;
    QString id;
    int existingIndex = -1;
    // Saving identical content refreshes/moves that item to the top. Any changed
    // configuration becomes a new item, so the user can keep multiple APIs
    // without needing a separate “new configuration” button.
    for (int index = 0; index < configs.size(); ++index) {
        const QJsonObject candidate = configs.at(index).toObject();
        if (canonicalModelConfig(candidate) == current) {
            existingIndex = index;
            id = candidate.value(QStringLiteral("id")).toString();
            break;
        }
    }
    if (existingIndex >= 0) {
        const QJsonObject old = configs.at(existingIndex).toObject();
        if (id.isEmpty()) id = QStringLiteral("cfg-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        saved.insert(QStringLiteral("id"), id);
        saved.insert(QStringLiteral("createdAt"), old.value(QStringLiteral("createdAt")).toVariant().toLongLong());
        saved.insert(QStringLiteral("updatedAt"), now);
        configs.replace(existingIndex, saved);
    } else {
        id = QStringLiteral("cfg-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
        saved.insert(QStringLiteral("id"), id);
        saved.insert(QStringLiteral("createdAt"), now);
        saved.insert(QStringLiteral("updatedAt"), now);
        configs.append(saved);
    }

    configs = sortedModelConfigs(configs);
    m_data->setModelConfigs(configs);
    m_data->setModelConfig(saved);
    m_loadedModelConfigId = id;
    m_lastSavedModelConfig = current;
    refreshSavedModelConfigs();
    updateModelSaveState();
    invalidateModelVerification();
}

void MainWindow::refreshSavedModelConfigs() {
    if (!m_savedModelConfigsLayout || !m_savedModelConfigsHost) return;
    clearLayout(m_savedModelConfigsLayout);
    const QJsonArray configs = sortedModelConfigs(m_data->modelConfigs());
    if (configs.isEmpty()) {
        auto *empty = new QLabel(QStringLiteral("尚未保存 API 配置"), m_savedModelConfigsHost);
        empty->setObjectName(QStringLiteral("modelConfigEmpty"));
        empty->setAlignment(Qt::AlignCenter);
        empty->setFixedHeight(166);
        m_savedModelConfigsLayout->addWidget(empty);
        return;
    }

    for (int index = 0; index < configs.size(); ++index) {
        const QJsonObject config = configs.at(index).toObject();
        const QString id = config.value(QStringLiteral("id")).toString();
        auto *item = new ClickableFrame(m_savedModelConfigsHost);
        item->setObjectName(QStringLiteral("modelConfigItem"));
        item->setProperty("selected", id == m_loadedModelConfigId);
        item->setFixedHeight(56);
        static_cast<InteractiveMotionFilter *>(m_motionFilter)->attach(item, QColor(67, 91, 78, 28));
        auto *itemLayout = new QHBoxLayout(item);
        itemLayout->setContentsMargins(8, 6, 7, 6);
        itemLayout->setSpacing(9);

        auto *order = new QLabel(QStringLiteral("%1").arg(index + 1, 2, 10, QLatin1Char('0')), item);
        order->setObjectName(QStringLiteral("modelConfigIndex"));
        order->setFixedSize(38, 38);
        order->setAlignment(Qt::AlignCenter);
        itemLayout->addWidget(order);

        auto *textHost = new QWidget(item);
        auto *textLayout = new QVBoxLayout(textHost);
        textLayout->setContentsMargins(0, 0, 0, 0);
        textLayout->setSpacing(1);
        const QString provider = config.value(QStringLiteral("providerName")).toString(QStringLiteral("自定义服务"));
        const QString model = config.value(QStringLiteral("modelId")).toString();
        auto *name = new QLabel(provider + QStringLiteral(" · ") + model, textHost);
        name->setObjectName(QStringLiteral("modelConfigName"));
        name->setToolTip(name->text());
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        auto *detail = new QLabel(config.value(QStringLiteral("baseUrl")).toString(), textHost);
        detail->setObjectName(QStringLiteral("modelConfigDetail"));
        detail->setToolTip(detail->text());
        detail->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        textLayout->addWidget(name);
        textLayout->addWidget(detail);
        itemLayout->addWidget(textHost, 1);

        auto *remove = new QPushButton(QStringLiteral("×"), item);
        remove->setObjectName(QStringLiteral("modelConfigDelete"));
        remove->setFixedSize(34, 34);
        remove->setToolTip(QStringLiteral("删除配置"));
        connect(remove, &QPushButton::clicked, this, [this, id] { deleteSavedModelConfig(id); });
        itemLayout->addWidget(remove);

        item->setActivated([this, config] { applySavedModelConfig(config, true); });
        m_savedModelConfigsLayout->addWidget(item);
    }
    m_savedModelConfigsLayout->addStretch();
}

void MainWindow::applySavedModelConfig(const QJsonObject &config, bool persistSelection) {
    if (!m_baseUrl || !m_modelId || !m_apiKey) return;
    m_loadingModelConfig = true;
    m_provider = config.value(QStringLiteral("provider")).toString(QStringLiteral("custom"));
    m_providerDisplay = config.value(QStringLiteral("providerName")).toString(QStringLiteral("自定义服务"));
    m_baseUrl->setText(config.value(QStringLiteral("baseUrl")).toString());
    m_modelId->setText(config.value(QStringLiteral("modelId")).toString());
    m_apiKey->setText(config.value(QStringLiteral("apiKey")).toString());
    for (QPushButton *button : std::as_const(m_providerButtons))
        button->setChecked(button->property("providerId").toString() == m_provider);
    m_loadedModelConfigId = config.value(QStringLiteral("id")).toString();
    m_lastSavedModelConfig = canonicalModelConfig(config);
    m_loadingModelConfig = false;
    if (persistSelection) m_data->setModelConfig(config);
    updateModelSaveState();
    invalidateModelVerification();
    updateGeneratePetAvailability();
    QTimer::singleShot(0, this, [this] { refreshSavedModelConfigs(); });
}

void MainWindow::deleteSavedModelConfig(const QString &id) {
    if (id.isEmpty()) return;
    QJsonArray kept;
    const QJsonArray configs = sortedModelConfigs(m_data->modelConfigs());
    for (const QJsonValue &value : configs) {
        if (value.toObject().value(QStringLiteral("id")).toString() != id) kept.append(value);
    }
    kept = sortedModelConfigs(kept);
    m_data->setModelConfigs(kept);

    if (m_loadedModelConfigId == id) {
        if (!kept.isEmpty()) {
            const QJsonObject next = kept.first().toObject();
            m_loadingModelConfig = true;
            m_provider = next.value(QStringLiteral("provider")).toString(QStringLiteral("custom"));
            m_providerDisplay = next.value(QStringLiteral("providerName")).toString(QStringLiteral("自定义服务"));
            m_baseUrl->setText(next.value(QStringLiteral("baseUrl")).toString());
            m_modelId->setText(next.value(QStringLiteral("modelId")).toString());
            m_apiKey->setText(next.value(QStringLiteral("apiKey")).toString());
            for (QPushButton *button : std::as_const(m_providerButtons))
                button->setChecked(button->property("providerId").toString() == m_provider);
            m_loadedModelConfigId = next.value(QStringLiteral("id")).toString();
            m_lastSavedModelConfig = canonicalModelConfig(next);
            m_loadingModelConfig = false;
            m_data->setModelConfig(next);
        } else {
            m_loadingModelConfig = true;
            m_provider = QStringLiteral("custom");
            m_providerDisplay = QStringLiteral("自定义服务");
            m_baseUrl->clear();
            m_modelId->clear();
            m_apiKey->clear();
            for (QPushButton *button : std::as_const(m_providerButtons))
                button->setChecked(button->property("providerId").toString() == m_provider);
            m_loadedModelConfigId.clear();
            m_lastSavedModelConfig = {};
            m_loadingModelConfig = false;
            m_data->setModelConfig(QJsonObject{{QStringLiteral("provider"), QStringLiteral("custom")},
                                               {QStringLiteral("providerName"), QStringLiteral("自定义服务")},
                                               {QStringLiteral("baseUrl"), QStringLiteral("")},
                                               {QStringLiteral("modelId"), QStringLiteral("")},
                                               {QStringLiteral("apiKey"), QStringLiteral("")}});
        }
        invalidateModelVerification();
    }
    updateModelSaveState();
    updateGeneratePetAvailability();
    QTimer::singleShot(0, this, [this] { refreshSavedModelConfigs(); });
}

void MainWindow::updateModelSaveState() {
    if (!m_saveModelButton) return;
    const QJsonObject current = canonicalModelConfig(collectModelConfig());
    const QString validationError = modelConfigValidationError(current);
    const bool valid = validationError.isEmpty();
    const bool saved = valid && !m_lastSavedModelConfig.isEmpty() && current == m_lastSavedModelConfig;
    const bool canSave = valid && !saved;

    setDynamicProperty(m_saveModelButton, "valid", valid);
    setDynamicProperty(m_saveModelButton, "saved", saved);
    m_saveModelButton->setEnabled(canSave);

    if (saved) {
        m_saveModelButton->setToolTip(QStringLiteral("当前配置已保存；修改任一配置项后可再次保存"));
    } else if (!valid) {
        m_saveModelButton->setToolTip(validationError);
    } else {
        m_saveModelButton->setToolTip(QStringLiteral("保存当前 API 配置"));
    }
}

void MainWindow::invalidateModelVerification() {
    const QJsonObject current = canonicalModelConfig(collectModelConfig());
    if (!m_verifiedModelConfig.isEmpty() && current == m_verifiedModelConfig) {
        updateGeneratePetAvailability();
        return;
    }
    m_verifiedModelConfig = {};
    m_modelVerificationRunning = false;
    if (!m_syncSteps.isEmpty()) setSyncStep(0, QStringLiteral("idle"));
    resetGenerationSteps();
    updateGeneratePetAvailability();
}

void MainWindow::updateGeneratePetAvailability() {
    if (!m_generatePetButton) return;
    const bool petExists = !m_petWindow.isNull();
    const bool busy = m_generationInProgress || m_modelVerificationRunning;
    const bool enabled = !petExists && !busy;
    m_generatePetButton->setEnabled(enabled);
    if (petExists) m_generatePetButton->setToolTip(QStringLiteral("请先关闭当前桌宠"));
    else if (busy) m_generatePetButton->setToolTip(QStringLiteral("正在检测模型连接并生成桌宠"));
    else m_generatePetButton->setToolTip(QStringLiteral("生成时会先自动检测当前模型连接"));
}


void MainWindow::loadVoiceConfig() {
    if (!m_voiceAppId || !m_voiceApiKey) return;
    const QJsonObject saved = canonicalVoiceConfig(m_data->voiceConfig());
    m_loadingVoiceConfig = true;
    m_voiceAppId->setText(saved.value(QStringLiteral("appId")).toString());
    m_voiceApiKey->setText(saved.value(QStringLiteral("apiKey")).toString());
    m_loadingVoiceConfig = false;
    m_lastSavedVoiceConfig = voiceConfigValidationError(saved).isEmpty() ? saved : QJsonObject{};
    updateVoiceSaveState();
    updateVoiceInputState();
}

void MainWindow::saveVoiceConfig() {
    if (!m_voiceAppId || !m_voiceApiKey) return;
    const QJsonObject config = canonicalVoiceConfig(QJsonObject{
        {QStringLiteral("appId"), m_voiceAppId->text()},
        {QStringLiteral("apiKey"), m_voiceApiKey->text()}
    });
    const QString validationError = voiceConfigValidationError(config);
    if (!validationError.isEmpty()) {
        updateVoiceSaveState();
        showModelNotice(QStringLiteral("语音配置"), validationError, true);
        return;
    }
    m_data->setVoiceConfig(config);
    m_lastSavedVoiceConfig = config;
    updateVoiceSaveState();
    updateVoiceInputState();
}

void MainWindow::updateVoiceSaveState() {
    if (!m_saveVoiceButton || !m_voiceAppId || !m_voiceApiKey) return;
    const QJsonObject current = canonicalVoiceConfig(QJsonObject{
        {QStringLiteral("appId"), m_voiceAppId->text()},
        {QStringLiteral("apiKey"), m_voiceApiKey->text()}
    });
    const bool valid = voiceConfigValidationError(current).isEmpty();
    const bool saved = valid && !m_lastSavedVoiceConfig.isEmpty() && current == m_lastSavedVoiceConfig;
    setDynamicProperty(m_saveVoiceButton, "valid", valid);
    setDynamicProperty(m_saveVoiceButton, "saved", saved);
    m_saveVoiceButton->setEnabled(valid && !saved);
    if (saved) m_saveVoiceButton->setToolTip(QStringLiteral("当前语音配置已保存"));
    else if (!valid) m_saveVoiceButton->setToolTip(voiceConfigValidationError(current));
    else m_saveVoiceButton->setToolTip(QStringLiteral("保存百度实时语音识别配置"));
}

void MainWindow::updateVoiceInputState() {
    if (!m_voiceInputButton) return;
    const QJsonObject saved = canonicalVoiceConfig(m_data->voiceConfig());
    const bool configured = voiceConfigValidationError(saved).isEmpty();
    const bool runtimeLocked = m_editorReadOnly || isRunningPersonaEditorLocked();
    const bool running = m_voiceClient && m_voiceClient->isRunning();
    const bool busy = running || m_voiceStopPending;

    setDynamicProperty(m_voiceInputButton, "configured", configured);
    setDynamicProperty(m_voiceInputButton, "recording", running && !m_voiceStopPending);
    setDynamicProperty(m_voiceInputButton, "stopping", m_voiceStopPending);
    const QString voiceIcon = running && !m_voiceStopPending
        ? QStringLiteral(":/resources/voice-pause.svg")
        : (configured ? QStringLiteral(":/resources/voice-input.svg")
                      : QStringLiteral(":/resources/voice-input-disabled.svg"));
    m_voiceInputButton->setIcon(QIcon(voiceIcon));
    // When no provider is configured the control is intentionally styled as
    // unavailable but remains clickable so the single click can route the user
    // directly to Voice Configuration, as requested.
    m_voiceInputButton->setEnabled(!runtimeLocked && !m_voiceStopPending);
    if (runtimeLocked) m_voiceInputButton->setToolTip(QStringLiteral("当前人格不可编辑"));
    else if (!configured) m_voiceInputButton->setToolTip(QStringLiteral("请先配置百度实时语音识别"));
    else if (m_voiceStopPending) m_voiceInputButton->setToolTip(QStringLiteral("正在结束本次语音输入"));
    else if (running) m_voiceInputButton->setToolTip(QStringLiteral("暂停语音输入"));
    else m_voiceInputButton->setToolTip(QStringLiteral("开始语音输入"));

    if (m_previousButton) m_previousButton->setEnabled(!busy && m_currentQuestion > 0);
    if (m_nextButton) m_nextButton->setEnabled(!busy);
    if (m_questionPage) {
        const auto indexButtons = m_questionPage->findChildren<QPushButton *>(QStringLiteral("indexButton"));
        for (QPushButton *button : indexButtons) button->setEnabled(!busy);
    }
}

void MainWindow::resetVoiceInterimTracking() {
    m_voiceInterimText.clear();
}

void MainWindow::applyVoiceInterimText(const QString &text) {
    if (!m_answerText || m_editorReadOnly || isRunningPersonaEditorLocked()) return;
    QString current = m_answerText->toPlainText();
    if (!m_voiceInterimText.isEmpty() && current.endsWith(m_voiceInterimText))
        current.chop(m_voiceInterimText.size());
    else if (!m_voiceInterimText.isEmpty())
        m_voiceInterimText.clear(); // user edited the live suffix; never overwrite that edit.
    m_voiceInterimText = text;
    current += text;
    m_updatingVoiceText = true;
    m_answerText->setPlainText(current);
    m_answerText->moveCursor(QTextCursor::End);
    m_updatingVoiceText = false;
}

void MainWindow::applyVoiceFinalText(const QString &text) {
    if (!m_answerText || m_editorReadOnly || isRunningPersonaEditorLocked() || text.isEmpty()) return;
    QString current = m_answerText->toPlainText();
    if (!m_voiceInterimText.isEmpty() && current.endsWith(m_voiceInterimText)) current.chop(m_voiceInterimText.size());
    current += text;
    m_voiceInterimText.clear();
    m_updatingVoiceText = true;
    m_answerText->setPlainText(current);
    m_answerText->moveCursor(QTextCursor::End);
    m_updatingVoiceText = false;
}

void MainWindow::toggleVoiceInput() {
    if (!m_voiceInputButton || !m_voiceClient || !m_views || m_views->currentIndex() != 4) return;
    if (m_editorReadOnly || isRunningPersonaEditorLocked()) return;
    const QJsonObject saved = canonicalVoiceConfig(m_data->voiceConfig());
    if (!voiceConfigValidationError(saved).isEmpty()) {
        switchView(3);
        return;
    }
    if (m_voiceClient->isRunning()) {
        m_voiceStopPending = true;
        m_voiceClient->stop();
        updateVoiceInputState();
        return;
    }
    m_pendingViewAfterVoiceStop = -1;
    m_voiceStopPending = false;
    resetVoiceInterimTracking();
    m_voiceClient->start(saved.value(QStringLiteral("appId")).toString(),
                         saved.value(QStringLiteral("apiKey")).toString());
    updateVoiceInputState();
}

void MainWindow::setSyncStep(int index, const QString &state) {
    if (index < 0 || index >= m_syncSteps.size() || index >= m_syncIndicators.size()) return;
    setDynamicProperty(m_syncSteps.at(index), "state", state);
    if (auto *indicator = dynamic_cast<DetectionIndicator *>(m_syncIndicators.at(index))) {
        indicator->setState(state == QStringLiteral("done") ? QStringLiteral("done") : state);
        if (QWidget *row = indicator->parentWidget()) setDynamicProperty(row, "state", state);
    }
}

void MainWindow::resetSyncSteps() {
    for (int index = 0; index < m_syncSteps.size(); ++index) setSyncStep(index, QStringLiteral("idle"));
}

void MainWindow::resetGenerationSteps() {
    for (int index = 1; index < m_syncSteps.size(); ++index) setSyncStep(index, QStringLiteral("idle"));
}

void MainWindow::generatePet() {
    if (!m_petWindow.isNull()) {
        showModelNotice(QStringLiteral("生成桌宠"), QStringLiteral("请先关闭当前桌宠，再生成新的桌宠。"), true);
        updateGeneratePetAvailability();
        return;
    }
    if (m_generationInProgress || m_modelVerificationRunning) return;

    const QJsonObject config = canonicalModelConfig(collectModelConfig());
    if (config.value(QStringLiteral("baseUrl")).toString().isEmpty() ||
        config.value(QStringLiteral("modelId")).toString().isEmpty()) {
        m_verifiedModelConfig = {};
        resetSyncSteps();
        setSyncStep(0, QStringLiteral("failed"));
        updateGeneratePetAvailability();
        showModelNotice(QStringLiteral("生成桌宠"), QStringLiteral("请先填写 Base URL 和模型 ID。生成桌宠时会自动检测当前模型连接。"), true);
        return;
    }

    m_generationInProgress = true;
    m_modelVerificationRunning = true;
    m_verifiedModelConfig = {};
    resetSyncSteps();
    setSyncStep(0, QStringLiteral("running"));
    updateGeneratePetAvailability();

    QPointer<MainWindow> self(this);
    m_core->verifyText(config, [self, config](bool ok, const QJsonObject &, const QString &error) {
        if (!self) return;
        self->m_modelVerificationRunning = false;

        if (canonicalModelConfig(self->collectModelConfig()) != config) {
            self->m_verifiedModelConfig = {};
            self->m_generationInProgress = false;
            self->setSyncStep(0, QStringLiteral("idle"));
            self->resetGenerationSteps();
            self->updateGeneratePetAvailability();
            self->showModelNotice(QStringLiteral("生成桌宠"), QStringLiteral("检测期间模型配置已发生变化，请使用当前配置重新生成。"), true);
            return;
        }

        if (!ok) {
            self->m_verifiedModelConfig = {};
            self->m_generationInProgress = false;
            self->setSyncStep(0, QStringLiteral("failed"));
            self->resetGenerationSteps();
            self->updateGeneratePetAvailability();
            self->showModelNotice(QStringLiteral("模型连接失败"), error.isEmpty() ? QStringLiteral("当前模型服务无法建立有效连接。") : error, true);
            return;
        }

        self->m_verifiedModelConfig = config;
        self->setSyncStep(0, QStringLiteral("done"));
        self->continueGeneratePetAfterModelVerification(config);
    });
}

void MainWindow::continueGeneratePetAfterModelVerification(const QJsonObject &config) {
    if (!m_generationInProgress || m_exitRequested) return;
    if (!m_petWindow.isNull()) {
        m_generationInProgress = false;
        updateGeneratePetAvailability();
        showModelNotice(QStringLiteral("生成桌宠"), QStringLiteral("当前已经存在桌宠，请先关闭当前桌宠。"), true);
        return;
    }
    if (canonicalModelConfig(collectModelConfig()) != config) {
        m_verifiedModelConfig = {};
        m_generationInProgress = false;
        resetSyncSteps();
        updateGeneratePetAvailability();
        showModelNotice(QStringLiteral("生成桌宠"), QStringLiteral("模型配置已发生变化，请重新生成。"), true);
        return;
    }

    const QJsonObject pack = m_data->packById(m_data->activePersonaId());
    resetGenerationSteps();
    setSyncStep(1, QStringLiteral("running"));
    if (pack.isEmpty()) {
        setSyncStep(1, QStringLiteral("failed"));
        m_generationInProgress = false;
        updateGeneratePetAvailability();
        showModelNotice(QStringLiteral("人格交接"), QStringLiteral("请先在人格管理中选择并使用一个人格。"), true);
        return;
    }
    if (pack.value(QStringLiteral("avatarPresetId")).toString().isEmpty()) {
        setSyncStep(1, QStringLiteral("failed"));
        m_generationInProgress = false;
        updateGeneratePetAvailability();
        showModelNotice(QStringLiteral("人格交接"), QStringLiteral("请先为当前人格选择桌宠形象。"), true);
        return;
    }
    setSyncStep(1, QStringLiteral("done"));
    setSyncStep(2, QStringLiteral("running"));

    QTimer::singleShot(180, this, [this, config, pack] {
        if (m_exitRequested) return;
        if (canonicalModelConfig(collectModelConfig()) != config) {
            m_verifiedModelConfig = {};
            m_generationInProgress = false;
            resetSyncSteps();
            updateGeneratePetAvailability();
            showModelNotice(QStringLiteral("生成桌宠"), QStringLiteral("模型配置已发生变化，请重新生成。"), true);
            return;
        }
        setSyncStep(2, QStringLiteral("done"));
        setSyncStep(3, QStringLiteral("running"));
        QPointer<MainWindow> self(this);
        const QJsonObject continuity = ChatWindow::loadContinuitySnapshot(pack.value(QStringLiteral("id")).toString(),
                                                                          pack.value(QStringLiteral("name")).toString());
        const QString longMemory = continuity.value(QStringLiteral("longMemoryMarkdown")).toString();
        const QJsonArray recentMessages = continuity.value(QStringLiteral("recentMessages")).toArray();
        m_core->createPersonaSession(config, m_data->buildProfileMarkdown(pack),
                                     pack.value(QStringLiteral("strength")).toInt(80),
                                     pack.value(QStringLiteral("id")).toString(),
                                     pack.value(QStringLiteral("name")).toString(),
                                     longMemory, recentMessages,
                                     [self, config, pack](bool ok, const QJsonObject &object, const QString &error) {
            if (!self) return;
            if (canonicalModelConfig(self->collectModelConfig()) != config) {
                self->m_verifiedModelConfig = {};
                self->m_generationInProgress = false;
                self->resetSyncSteps();
                self->updateGeneratePetAvailability();
                self->showModelNotice(QStringLiteral("生成桌宠"), QStringLiteral("模型配置已发生变化，请重新生成。"), true);
                return;
            }
            if (!ok) {
                self->setSyncStep(3, QStringLiteral("failed"));
                self->m_generationInProgress = false;
                self->updateGeneratePetAvailability();
                self->showModelNotice(QStringLiteral("人格交接失败"), error.isEmpty() ? QStringLiteral("无法创建人格会话。") : error, true);
                return;
            }
            self->setSyncStep(3, QStringLiteral("done"));
            self->setSyncStep(4, QStringLiteral("running"));
            self->m_activeSessionId = object.value(QStringLiteral("sessionId")).toString();
            QTimer::singleShot(140, self, [self, pack] {
                if (!self) return;
                self->showPetForPack(pack, self->m_activeSessionId);
                self->setSyncStep(4, QStringLiteral("done"));
                self->m_generationInProgress = false;
                self->updateGeneratePetAvailability();
            });
        });
    });
}

void MainWindow::showPetForPack(const QJsonObject &pack, const QString &sessionId) {
    if (!m_petWindow) m_petWindow = new PetWindow;
    m_runningPersonaId = pack.value(QStringLiteral("id")).toString();
    const QString avatarId = pack.value(QStringLiteral("avatarPresetId")).toString(QStringLiteral("builtin-avatar-01"));
    AvatarWidget *avatar = avatarById(avatarId);
    if (avatar) {
        avatar->ensureLoaded();
        m_petWindow->setAvatarFrames(avatar->frameSvgData(), pack.value(QStringLiteral("name")).toString(),
                                     avatarId, avatar->interactionProfile());
    } else {
        m_petWindow->setAvatarFrames({}, pack.value(QStringLiteral("name")).toString(), avatarId, QString());
    }
    if (!m_chatWindow) m_chatWindow = new ChatWindow(m_core);
    m_chatWindow->setSession(sessionId, pack.value(QStringLiteral("id")).toString(), pack.value(QStringLiteral("name")).toString());
    m_chatWindow->setDockSideRight(m_chatOnRight);
    m_chatWindow->setAlwaysOnTop(m_petWindow->isAlwaysOnTopEnabled());

    QObject::disconnect(m_chatWindow, &ChatWindow::assistantReplied, m_petWindow, &PetWindow::speakText);
    connect(m_chatWindow, &ChatWindow::assistantReplied, m_petWindow, &PetWindow::speakText);
    wirePetChatRelationship();
    m_petWindow->setChatVisible(m_chatWindow->isVisible());

    m_petWindow->show();
    m_petWindow->raise();
    updatePersonaEditorRuntimeLock();
    if (m_chatWindow->isVisible() && !m_petWindow->isCollapsedBubble()) positionChatNextToPet();
    // V101: every successful desktop-pet generation minimizes the Capricorn main
    // window. V95 guarded this with m_hasGeneratedPetThisRun, so only the first
    // generation in a process minimized the shell; later regenerations did not.
    QPointer<PetWindow> generatedPet = m_petWindow;
    QTimer::singleShot(0, this, [this, generatedPet] {
        if (m_exitRequested || !generatedPet) return;
        showMinimized();
        // The pet is an independent top-level window. Reassert its visibility after
        // the main shell changes state so Windows cannot leave it behind the shell.
        QTimer::singleShot(80, this, [generatedPet] {
            if (!generatedPet || generatedPet->isCollapsedBubble()) return;
            generatedPet->show();
            generatedPet->raise();
        });
    });
}

void MainWindow::wirePetChatRelationship() {
    if (!m_petWindow || !m_chatWindow) return;
    QObject::disconnect(m_petWindow, nullptr, this, nullptr);
    QObject::disconnect(m_chatWindow, nullptr, this, nullptr);

    QPointer<MainWindow> self(this);
    connect(m_petWindow, &PetWindow::chatRequested, this, [self] {
        if (!self || !self->m_chatWindow) return;
        if (self->m_petWindow && !self->m_petWindow->isCollapsedBubble()) self->positionChatNextToPet();
        self->m_chatWindow->show();
        if (self->m_petWindow) self->m_petWindow->setChatVisible(true);
        if (self->m_petWindow && !self->m_petWindow->isCollapsedBubble()) self->positionChatNextToPet();
        self->m_chatWindow->raise();
        self->m_chatWindow->activateWindow();
    });
    connect(m_petWindow, &PetWindow::closeChatRequested, this, [self] {
        if (!self || !self->m_chatWindow) return;
        self->m_chatWindow->hide();
        if (self->m_petWindow) self->m_petWindow->setChatVisible(false);
    });
    connect(m_chatWindow, &ChatWindow::visibilityChanged, this, [self](bool visible) {
        if (self && self->m_petWindow) self->m_petWindow->setChatVisible(visible);
    });
    connect(m_chatWindow, &ChatWindow::sessionChanged, this, [self](const QString &sessionId) {
        if (self && !sessionId.isEmpty()) self->m_activeSessionId = sessionId;
    });
    connect(m_chatWindow, &ChatWindow::closePetRequested, this, [self] {
        if (self) self->closePetAndChat();
    });
    connect(m_petWindow, &PetWindow::topmostChanged, this, [self](bool enabled) {
        if (self && self->m_chatWindow) self->m_chatWindow->setAlwaysOnTop(enabled);
    });
    connect(m_petWindow, &PetWindow::collapsedChanged, this, [self](bool collapsed) {
        if (!self || collapsed) return;
        if (self->m_chatWindow && self->m_chatWindow->isVisible()) self->positionChatNextToPet();
    });
    connect(m_petWindow, &PetWindow::closePetRequested, this, [self] {
        if (self) self->closePetAndChat();
    });
    connect(m_petWindow, &PetWindow::geometryChanged, this, [self](const QRect &) {
        if (!self || self->m_syncingPetChatGeometry || !self->m_petWindow || !self->m_chatWindow) return;
        if (self->m_petWindow->isCollapsedBubble() || !self->m_chatWindow->isVisible()) return;
        self->positionChatNextToPet();
    });
    connect(m_chatWindow, &ChatWindow::geometryChanged, this, [self](const QRect &) {
        if (!self || self->m_syncingPetChatGeometry || !self->m_petWindow || !self->m_chatWindow) return;
        if (self->m_petWindow->isCollapsedBubble() || !self->m_petWindow->isVisible()) return;
        self->positionPetNextToChat();
    });
    connect(m_chatWindow, &ChatWindow::dockSideToggleRequested, this, [self] {
        if (self) self->toggleChatDockSide();
    });
}

void MainWindow::positionChatNextToPet() {
    if (m_syncingPetChatGeometry || !m_petWindow || !m_chatWindow || m_petWindow->isCollapsedBubble()) return;
    constexpr int gap = 6; // V90: slightly tighter pet/chat pairing while keeping a clean visual separation.
    const QRect petRect = m_petWindow->geometry();
    const QSize chatSize = m_chatWindow->size().expandedTo(m_chatWindow->minimumSize());
    const QPoint chatTopLeft(m_chatOnRight ? petRect.right() + gap + 1 : petRect.left() - gap - chatSize.width(),
                             petRect.center().y() - chatSize.height() / 2);
    m_syncingPetChatGeometry = true;
    m_chatWindow->move(chatTopLeft);
    m_syncingPetChatGeometry = false;
}

void MainWindow::positionPetNextToChat() {
    if (m_syncingPetChatGeometry || !m_petWindow || !m_chatWindow || m_petWindow->isCollapsedBubble()) return;
    if (m_petWindow->isPositionLocked()) { positionChatNextToPet(); return; }
    constexpr int gap = 6; // V90: slightly tighter pet/chat pairing while keeping a clean visual separation.
    const QRect chatRect = m_chatWindow->geometry();
    const QSize petSize = m_petWindow->size();
    const QPoint petTopLeft(m_chatOnRight ? chatRect.left() - gap - petSize.width() : chatRect.right() + gap + 1,
                            chatRect.center().y() - petSize.height() / 2);
    m_syncingPetChatGeometry = true;
    m_petWindow->move(petTopLeft);
    // V101: dragging the chat window may intentionally pull the pet off-screen.
    // When roughly half of the pet is outside all displays, treat that state the
    // same as the explicit “隐藏桌宠” action and collapse it into the bubble.
    if (m_petWindow->isMostlyOutsideScreens()) m_petWindow->collapseToBubble();
    m_syncingPetChatGeometry = false;
}

void MainWindow::toggleChatDockSide() {
    m_chatOnRight = !m_chatOnRight;
    if (m_chatWindow) m_chatWindow->setDockSideRight(m_chatOnRight);
    if (m_petWindow && m_chatWindow && !m_petWindow->isCollapsedBubble()) positionChatNextToPet();
}

bool MainWindow::isRunningPersonaEditorLocked() const {
    return !m_petWindow.isNull() && !m_runningPersonaId.isEmpty()
        && !m_editPackId.isEmpty() && m_editPackId == m_runningPersonaId;
}

void MainWindow::showRunningPetConfigLockedNotice() {
    // The whole sentence uses one typography level instead of splitting
    // “桌宠使用中” and “请先关闭桌宠” into title/body styles.
    showModelNotice(QStringLiteral("桌宠使用中，请先关闭桌宠。"), QString(), true);
}

void MainWindow::registerRuntimePersonaEditControl(QWidget *widget, const QString &packId) {
    if (!widget) return;
    widget->setProperty("runtimePersonaEditControl", true);
    if (!packId.isEmpty()) widget->setProperty("runtimePersonaPackId", packId);
    widget->installEventFilter(this);
}

void MainWindow::updatePersonaEditorRuntimeLock() {
    const bool runtimeLocked = isRunningPersonaEditorLocked();
    if (m_personaName) {
        m_personaName->setReadOnly(m_editorReadOnly || runtimeLocked);
        m_personaName->setProperty("runtimeLocked", runtimeLocked);
    }
    if (m_answerText) {
        m_answerText->setReadOnly(m_editorReadOnly || runtimeLocked);
        m_answerText->setProperty("runtimeLocked", runtimeLocked);
    }
    if (m_scale) m_scale->setProperty("runtimeLocked", runtimeLocked);
    for (QWidget *widget : std::as_const(m_optionWidgets)) widget->setProperty("runtimeLocked", runtimeLocked);
    for (AvatarWidget *avatar : std::as_const(m_avatarWidgets)) {
        avatar->setEnabled(!m_editorReadOnly || m_builtinAvatarOnly);
        avatar->setProperty("runtimeLocked", runtimeLocked);
        avatar->update();
    }
    if (m_savePersonaButton) m_savePersonaButton->setEnabled(!runtimeLocked);
    if (m_editorMode && !m_editPackId.isEmpty()) {
        if (runtimeLocked) m_editorMode->setText(QStringLiteral("桌宠使用中 · 只读"));
        else if (m_builtinAvatarOnly) m_editorMode->clear();
        else m_editorMode->setText(m_editorReadOnly ? QStringLiteral("查看模式") : QStringLiteral("编辑模式"));
        m_editorMode->setVisible(!m_editorMode->text().isEmpty());
    }
    updateVoiceInputState();
}

void MainWindow::closePetAndChat(bool refreshPacks) {
    m_syncingPetChatGeometry = true;
    if (m_chatWindow) {
        m_chatWindow->hide();
        m_chatWindow->deleteLater();
        m_chatWindow = nullptr;
    }
    if (m_petWindow) {
        m_petWindow->hide();
        m_petWindow->deleteLater();
        m_petWindow = nullptr;
    }
    m_syncingPetChatGeometry = false;
    m_activeSessionId.clear();
    m_runningPersonaId.clear();
    updatePersonaEditorRuntimeLock();
    if (refreshPacks) renderPacks();
    // Closing the pet permits a new generation. V70 performs model connection
    // verification inside every Generate Pet flow, so return all handoff items
    // to idle instead of preserving an old verification check mark.
    m_verifiedModelConfig = {};
    resetSyncSteps();
    updateGeneratePetAvailability();
}


bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (auto *widget = qobject_cast<QWidget *>(watched);
        widget && widget->property("editPersonaResponsiveViewport").toBool()
        && event->type() == QEvent::Resize) {
        // Scrollbar visibility and fractional DPI rounding can change the viewport
        // width without resizing the outer window. Re-run only the lightweight
        // responsive geometry pass; m_applyingResponsiveLayout prevents re-entry.
        applyResponsiveLayout();
    }
#ifndef Q_OS_WIN
    auto *dragWidget = qobject_cast<QWidget *>(watched);
    if (dragWidget && dragWidget->property("windowDragZone").toBool()) {
        if (event->type() == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                toggleWindowMaximize();
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_windowDragging = true;
                m_windowDragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
                if (!m_windowMaximizedState && windowHandle() && windowHandle()->startSystemMove()) {
                    m_windowDragging = false;
                    return true;
                }
                return true;
            }
        }
        if (event->type() == QEvent::MouseMove) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (m_windowDragging && mouse->buttons().testFlag(Qt::LeftButton) && !m_windowMaximizedState) {
                move(mouse->globalPosition().toPoint() - m_windowDragOffset);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_windowDragging = false;
                return true;
            }
        }
    }
#endif
    if (auto *widget = qobject_cast<QWidget *>(watched); widget && widget->property("runtimePersonaEditControl").toBool()) {
        const QString packId = widget->property("runtimePersonaPackId").toString();
        bool blocked = packId.isEmpty() ? isRunningPersonaEditorLocked()
                                        : (!m_petWindow.isNull() && packId == m_runningPersonaId);

        // A built-in persona is intrinsically read-only. While it is running,
        // browsing its modules, selecting/copying text and scrolling must stay
        // silent because those operations cannot change the persona anyway. The
        // only editable field of a built-in persona is its avatar, so only an
        // avatar change attempt receives the running-pet reminder.
        if (blocked && packId.isEmpty() && m_builtinAvatarOnly && !qobject_cast<AvatarWidget *>(widget))
            blocked = false;

        // User-created personas remain fully inspectable while running. Do not
        // block focus, scrolling, text selection or Copy. Prompt only when the
        // input would actually change a value (typing/paste/cut, toggling an
        // option, moving a scale, or selecting/deleting another avatar).
        if (blocked && isControlMutationAttempt(widget, event)) {
            showRunningPetConfigLockedNotice();
            return true;
        }
    }

    if (event->type() == QEvent::MouseButtonRelease) {
        auto *widget = qobject_cast<QWidget *>(watched);
        auto *mouse = static_cast<QMouseEvent *>(event);
        if (widget && mouse->button() == Qt::LeftButton) {
            const QString packId = widget->property("packId").toString();
            if (!packId.isEmpty()) {
                selectPack(packId, true);
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG *msg = static_cast<MSG *>(message);
    if (!msg) return QMainWindow::nativeEvent(eventType, message, result);

    if (msg->message == WM_SYSCOMMAND) {
        const WPARAM command = msg->wParam & 0xFFF0;
        if (command == SC_MAXIMIZE) m_windowMaximizedState = true;
        else if (command == SC_RESTORE) m_windowMaximizedState = false;
    }
    if (msg->message == WM_SIZE) {
        if (msg->wParam == SIZE_MAXIMIZED) m_windowMaximizedState = true;
        else if (msg->wParam == SIZE_RESTORED) m_windowMaximizedState = false;
    }

    // Manual resize has both lower and upper tracking bounds. Maximization still
    // uses the monitor work area because only ptMin/MaxTrackSize are constrained.
    if (msg->message == WM_GETMINMAXINFO) {
        auto *mmi = reinterpret_cast<MINMAXINFO *>(msg->lParam);
        const HWND hwnd = msg->hwnd;
        if (mmi && hwnd) {
            const UINT dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int logical) { return MulDiv(logical, int(dpi), 96); };
            const int logicalMinWidth = qMax(1, m_personaMinimumWindowWidth);
            mmi->ptMinTrackSize.x = scale(logicalMinWidth);
            mmi->ptMinTrackSize.y = scale(qMax(1, m_responsiveMinimumHeight));
            mmi->ptMaxTrackSize.x = scale(qMax(1760, logicalMinWidth));
            mmi->ptMaxTrackSize.y = scale(1120);
            *result = 0;
            return true;
        }
    }

    if (msg->message == WM_SETCURSOR && !actualWindowMaximized()) {
        RECT rect{};
        POINT cursor{};
        const HWND hwnd = msg->hwnd;
        if (hwnd && GetWindowRect(hwnd, &rect) && GetCursorPos(&cursor)) {
            const UINT dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int logical) { return MulDiv(logical, int(dpi), 96); };

            // V90: the visible three-button cluster is always ordinary client UI.
            const LONG controlsLeft = rect.right - scale(138);
            const LONG controlsTop = rect.top;
            const LONG controlsBottom = rect.top + scale(58);
            if (cursor.x >= controlsLeft && cursor.x < rect.right
                && cursor.y >= controlsTop && cursor.y < controlsBottom) {
                SetCursor(LoadCursor(nullptr, IDC_ARROW));
                *result = TRUE;
                return true;
            }

            const int edge = qMax(12, scale(16));
            const int corner = qMax(28, scale(34));
            const bool inLeft = cursor.x >= rect.left && cursor.x < rect.left + edge;
            const bool inRight = cursor.x < rect.right && cursor.x >= rect.right - edge;
            const bool inTop = cursor.y >= rect.top && cursor.y < rect.top + edge;
            const bool inBottom = cursor.y < rect.bottom && cursor.y >= rect.bottom - edge;
            const bool nearLeft = cursor.x >= rect.left && cursor.x < rect.left + corner;
            const bool nearRight = cursor.x < rect.right && cursor.x >= rect.right - corner;
            const bool nearTop = cursor.y >= rect.top && cursor.y < rect.top + corner;
            const bool nearBottom = cursor.y < rect.bottom && cursor.y >= rect.bottom - corner;
            LPCTSTR cursorId = nullptr;
            if ((nearTop && nearLeft) || (nearBottom && nearRight)) cursorId = IDC_SIZENWSE;
            else if ((nearTop && nearRight) || (nearBottom && nearLeft)) cursorId = IDC_SIZENESW;
            else if (inLeft || inRight) cursorId = IDC_SIZEWE;
            else if (inTop || inBottom) cursorId = IDC_SIZENS;
            if (cursorId) {
                SetCursor(LoadCursor(nullptr, cursorId));
                *result = TRUE;
                return true;
            }
        }
    }

    if (msg->message == WM_NCHITTEST) {
        const HWND hwnd = msg->hwnd;
        RECT rect{};
        if (hwnd && GetWindowRect(hwnd, &rect)) {
            const LONG x = GET_X_LPARAM(msg->lParam);
            const LONG y = GET_Y_LPARAM(msg->lParam);
            const UINT dpi = GetDpiForWindow(hwnd);
            auto scale = [dpi](int logical) { return MulDiv(logical, int(dpi), 96); };

            // V90 keeps every resize edge/corner in HTCLIENT so Qt delivers the
            // mouse press to WindowResizeHandle, whose manual geometry path is the
            // authoritative resize implementation.  This avoids the V76 failure
            // mode where Windows showed a resize cursor but no native resize loop
            // actually started.
            const LONG controlsLeft = rect.right - scale(138);
            const LONG controlsRight = rect.right;
            const LONG controlsTop = rect.top;
            const LONG controlsBottom = rect.top + scale(58);
            if (x >= controlsLeft && x < controlsRight && y >= controlsTop && y < controlsBottom) {
                *result = HTCLIENT;
                return true;
            }

            const bool maximized = IsZoomed(hwnd) != FALSE;
            if (!maximized) {
                const int edge = qMax(12, scale(16));
                const bool onLeft = x >= rect.left && x < rect.left + edge;
                const bool onRight = x < rect.right && x >= rect.right - edge;
                const bool onTop = y >= rect.top && y < rect.top + edge;
                const bool onBottom = y < rect.bottom && y >= rect.bottom - edge;
                if (onLeft || onRight || onTop || onBottom) {
                    *result = HTCLIENT;
                    return true;
                }
            }

            // Caption begins below the top resize strip.  The fixed strip left of
            // the controls still supports drag and double-click maximize/restore.
            const LONG dragLeft = rect.left + scale(m_cachedSidebarWidth + 18);
            const LONG dragRight = controlsLeft;
            const LONG dragTop = rect.top + scale(18);
            const LONG dragBottom = rect.top + scale(58);
            if (x >= dragLeft && x < dragRight && y >= dragTop && y < dragBottom) {
                *result = HTCAPTION;
                return true;
            }
        }
    }

#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (m_exitRequested) {
        event->accept();
        return;
    }

    event->ignore();
    if (askTopmostQuestion(QStringLiteral("退出 Capricorn"), QStringLiteral("是否退出软件？"), QStringLiteral("是"), QStringLiteral("否"))) {
        beginFastExit();
        return;
    }
    minimizeToTray();
}
