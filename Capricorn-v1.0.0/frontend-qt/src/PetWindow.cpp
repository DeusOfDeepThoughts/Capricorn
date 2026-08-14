#include "PetWindow.h"

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QEnterEvent>
#include <QFont>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QRandomGenerator>
#include <QResizeEvent>
#include <QScreen>
#include <QShowEvent>
#include <QSize>
#include <QStyleHints>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtMath>


#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <objbase.h>
#include <shobjidl.h>
#endif

namespace {
constexpr qreal kPi = 3.14159265358979323846;
constexpr qreal kPromptTailHeight = 9.0;
constexpr int kBubbleSize = 56;
constexpr int kBubbleSnapDistance = 34;
constexpr qreal kExpandedMinScale = 0.68;
constexpr qreal kExpandedMaxScale = 1.62;
constexpr qreal kExpandedTargetImageHeight = 238.0;
constexpr qreal kExpandedMaxImageWidth = 270.0;
constexpr qreal kExpandedMaxImageHeight = 258.0;
constexpr qreal kExpandedMinImageWidth = 104.0;
constexpr qreal kExpandedBottomPadding = 14.0;
constexpr qreal kExpandedHorizontalPadding = 38.0;
constexpr qreal kExpandedTopReserve = 72.0;
constexpr qreal kBoundaryVisibleRatio = 0.50;
constexpr qreal kBubbleAvatarBox = 34.0;

constexpr int kInteractionCount = 10;
constexpr int kInteractionRasterMaxDimension = 768;
constexpr int kMinimumFrameDwellMs = 900;
constexpr int kHoverFrameDelayMs = 680;
constexpr int kWheelInteractionCooldownMs = 900;
constexpr int kSparkleCooldownMs = 650;

struct CharacterMotionTuning {
    qreal idleBob{1.2};
    qreal breathing{0.003};
    qreal hoverScale{0.012};
    qreal clickLift{0.022};
    qreal clickScale{0.010};
    qreal doubleLift{0.026};
    qreal doubleScale{0.026};
    qreal dropBounce{0.012};
    qreal chatLift{0.010};
    qreal chatScale{0.014};
    qreal speakLift{0.008};
    qreal speakScale{0.006};
    qreal longIdleBob{0.008};
    qreal longIdleScale{0.004};
    qreal specialLift{0.030};
    qreal specialScale{0.016};
    qreal pointerAmplitude{0.72};
    qreal idlePeriodScale{1.0};
    int idleMinimumMs{7200};
    int idleRangeMs{4800};
    int interactionCooldownMs{460};
    int transitionMs{420};
};

CharacterMotionTuning motionTuningForProfile(const QString &profile) {
    CharacterMotionTuning tuning;
    if (profile == QStringLiteral("Capricorn")) {
        // Restrained human gestures: elegant, smaller movement and a softer return.
        tuning.idleBob = 0.9; tuning.breathing = 0.0025; tuning.hoverScale = 0.008;
        tuning.clickLift = 0.016; tuning.clickScale = 0.008;
        tuning.doubleLift = 0.021; tuning.doubleScale = 0.018;
        tuning.chatLift = 0.008; tuning.chatScale = 0.010;
        tuning.specialLift = 0.020; tuning.specialScale = 0.011;
        tuning.pointerAmplitude = 0.58; tuning.idlePeriodScale = 1.18;
        tuning.idleMinimumMs = 9000; tuning.idleRangeMs = 5200;
        tuning.interactionCooldownMs = 560; tuning.transitionMs = 480;
    } else if (profile == QStringLiteral("Scorpio")) {
        // Cooler expressions: deliberate transitions with very limited bounce.
        tuning.idleBob = 0.75; tuning.breathing = 0.0022; tuning.hoverScale = 0.007;
        tuning.clickLift = 0.014; tuning.clickScale = 0.007;
        tuning.doubleLift = 0.019; tuning.doubleScale = 0.016;
        tuning.chatLift = 0.007; tuning.chatScale = 0.009;
        tuning.specialLift = 0.018; tuning.specialScale = 0.010;
        tuning.pointerAmplitude = 0.52; tuning.idlePeriodScale = 1.24;
        tuning.idleMinimumMs = 9400; tuning.idleRangeMs = 5400;
        tuning.interactionCooldownMs = 580; tuning.transitionMs = 500;
    } else if (profile == QStringLiteral("小鸡毛")) {
        // Puppy poses are naturally lively: quicker transitions and springier lifts.
        tuning.idleBob = 1.65; tuning.breathing = 0.0038; tuning.hoverScale = 0.018;
        tuning.clickLift = 0.032; tuning.clickScale = 0.016;
        tuning.doubleLift = 0.042; tuning.doubleScale = 0.031;
        tuning.dropBounce = 0.018; tuning.chatLift = 0.016; tuning.chatScale = 0.019;
        tuning.speakLift = 0.012; tuning.speakScale = 0.008;
        tuning.specialLift = 0.048; tuning.specialScale = 0.024;
        tuning.pointerAmplitude = 0.82; tuning.idlePeriodScale = 0.88;
        tuning.idleMinimumMs = 6200; tuning.idleRangeMs = 4200;
        tuning.interactionCooldownMs = 440; tuning.transitionMs = 420;
    } else if (profile == QStringLiteral("胖橘")) {
        // Seated cat poses read best with slow, low-amplitude movement.
        tuning.idleBob = 0.65; tuning.breathing = 0.0028; tuning.hoverScale = 0.009;
        tuning.clickLift = 0.013; tuning.clickScale = 0.008;
        tuning.doubleLift = 0.018; tuning.doubleScale = 0.015;
        tuning.dropBounce = 0.008; tuning.chatLift = 0.006; tuning.chatScale = 0.010;
        tuning.longIdleBob = 0.004; tuning.specialLift = 0.019; tuning.specialScale = 0.011;
        tuning.pointerAmplitude = 0.46; tuning.idlePeriodScale = 1.35;
        tuning.idleMinimumMs = 10800; tuning.idleRangeMs = 5800;
        tuning.interactionCooldownMs = 640; tuning.transitionMs = 540;
    } else if (profile == QStringLiteral("黑曼波")) {
        // Play-bow / paw poses support brisk but still controlled vertical movement.
        tuning.idleBob = 1.35; tuning.breathing = 0.0034; tuning.hoverScale = 0.015;
        tuning.clickLift = 0.027; tuning.clickScale = 0.014;
        tuning.doubleLift = 0.035; tuning.doubleScale = 0.026;
        tuning.dropBounce = 0.016; tuning.chatLift = 0.013; tuning.chatScale = 0.016;
        tuning.speakLift = 0.010; tuning.specialLift = 0.040; tuning.specialScale = 0.021;
        tuning.pointerAmplitude = 0.78; tuning.idlePeriodScale = 0.92;
        tuning.idleMinimumMs = 6500; tuning.idleRangeMs = 4300;
        tuning.interactionCooldownMs = 450; tuning.transitionMs = 430;
    }
    return tuning;
}

#ifdef Q_OS_WIN
void removeDesktopCompanionTaskbarButton(HWND hwnd) {
    if (!hwnd) return;
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ITaskbarList *taskbar = nullptr;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&taskbar)))) {
        if (SUCCEEDED(taskbar->HrInit())) taskbar->DeleteTab(hwnd);
        taskbar->Release();
    }
    if (SUCCEEDED(initialized)) CoUninitialize();
}
#endif

void applyDesktopCompanionTopmost(QWidget *window, bool enabled, bool keepAtFrontOfNormalBand = true) {
    if (!window) return;
#ifdef Q_OS_WIN
    // Win32 is the single source of truth for TOPMOST. Qt's topmost hint stays
    // absent on Windows so it cannot silently reassert itself after this call.
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;

    constexpr UINT passiveFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    SetWindowPos(hwnd, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, passiveFlags);
    if (!enabled && keepAtFrontOfNormalBand) {
        // HWND_NOTOPMOST returns the pet to the normal band. Put it at the front
        // of that band once, without stealing foreground from the context menu.
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, passiveFlags);
    }
#else
    const bool wasVisible = window->isVisible();
    const QRect oldGeometry = window->geometry();
    window->setWindowFlag(Qt::WindowStaysOnTopHint, enabled);
    if (wasVisible) {
        window->show();
        window->setGeometry(oldGeometry);
        if (enabled) window->raise();
    }
    Q_UNUSED(keepAtFrontOfNormalBand)
#endif
}

void bringNormalDesktopCompanionToFront(QWidget *window) {
    if (!window) return;
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;
    constexpr UINT flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER;
    // User clicked the pet, so treating it as the normal foreground window is
    // exactly the same interaction model as a conventional desktop window.
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, flags);
    SetForegroundWindow(hwnd);
    BringWindowToTop(hwnd);
#else
    window->raise();
    window->activateWindow();
#endif
}

int stableBehaviorSeed(const QString &avatarId, const QString &fallbackName) {
    const QString key = avatarId.trimmed().isEmpty() ? fallbackName : avatarId;
    if (key.startsWith(QStringLiteral("builtin-avatar-"))) {
        bool ok = false;
        const int ordinal = key.mid(QStringLiteral("builtin-avatar-").size()).toInt(&ok);
        if (ok && ordinal >= 1 && ordinal <= 5) return ordinal - 1;
    }
    quint32 hash = 2166136261u;
    for (const QChar ch : key) {
        hash ^= ch.unicode();
        hash *= 16777619u;
    }
    return 5 + int(hash % 97u);
}

class PromptFrame final : public QFrame {
public:
    explicit PromptFrame(QWidget *parent = nullptr) : QFrame(parent) {
        setAttribute(Qt::WA_TranslucentBackground);
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QRectF body(0.5, 0.5, width() - 1.0, height() - kPromptTailHeight - 1.0);
        QPainterPath path;
        path.addRoundedRect(body, 16.0, 16.0);
        const qreal cx = body.center().x();
        QPainterPath tail;
        tail.moveTo(cx - 9.0, body.bottom() - 0.5);
        tail.lineTo(cx, body.bottom() + kPromptTailHeight);
        tail.lineTo(cx + 9.0, body.bottom() - 0.5);
        tail.closeSubpath();
        path = path.united(tail);
        painter.fillPath(path, QColor(255, 255, 255, 248));
        painter.setPen(QPen(QColor(102, 119, 110, 72), 1.0));
        painter.drawPath(path);
    }
};
}

PetWindow::PetWindow(QWidget *parent) : QWidget(parent) {
    setWindowTitle(QStringLiteral("Capricorn 桌宠"));
#ifdef Q_OS_WIN
    // Match Capricorn's main-window type so Windows gives the expanded pet the
    // same native normal-band activation behavior. Qt::Tool was the root cause:
    // Windows/Qt could demote the entire tool surface when the app deactivated.
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
#else
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
#endif
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    resize(m_baseExpandedSize);
    setMinimumSize(qRound(m_baseExpandedSize.width() * kExpandedMinScale),
                   qRound(m_baseExpandedSize.height() * kExpandedMinScale));
    setMaximumSize(qRound(m_baseExpandedSize.width() * kExpandedMaxScale),
                   qRound(m_baseExpandedSize.height() * kExpandedMaxScale));

    m_prompt = new PromptFrame(this);
    m_prompt->setObjectName(QStringLiteral("petPrompt"));
    auto *promptShadow = new QGraphicsDropShadowEffect(m_prompt);
    promptShadow->setBlurRadius(28.0);
    promptShadow->setOffset(0.0, 10.0);
    promptShadow->setColor(QColor(27, 39, 33, 46));
    m_prompt->setGraphicsEffect(promptShadow);
    auto *promptLayout = new QVBoxLayout(m_prompt);
    promptLayout->setContentsMargins(14, 12, 14, 17);
    promptLayout->setSpacing(6);
    m_promptText = new QLabel(m_prompt);
    m_promptText->setObjectName(QStringLiteral("petPromptText"));
    m_promptText->setWordWrap(true);
    auto *actions = new QHBoxLayout;
    actions->setSpacing(5);
    actions->addStretch();
    m_promptYes = new QPushButton(QStringLiteral("√"), m_prompt);
    m_promptYes->setObjectName(QStringLiteral("petPromptYes"));
    m_promptNo = new QPushButton(QStringLiteral("×"), m_prompt);
    m_promptNo->setObjectName(QStringLiteral("petPromptNo"));
    m_promptYes->setFixedSize(26, 24);
    m_promptNo->setFixedSize(26, 24);
    actions->addWidget(m_promptYes);
    actions->addWidget(m_promptNo);
    promptLayout->addWidget(m_promptText);
    promptLayout->addLayout(actions);
    m_prompt->hide();

    m_animationTimer = new QTimer(this);
    m_animationTimer->setInterval(16);
    connect(m_animationTimer, &QTimer::timeout, this, [this] {
        if (!isVisible() || m_collapsedBubble) return;
        const qreal blend = 0.16;
        m_pointerResponse += (m_pointerTarget - m_pointerResponse) * blend;
        if (m_sparkleProgress < 1.0) {
            m_sparkleProgress = qMin<qreal>(1.0, m_sparkleProgress + 0.018);
            if (m_sparkleProgress >= 1.0) m_sparkles.clear();
        }
        update();
    });
    m_animationTimer->start();
    m_promptTimer = new QTimer(this);
    m_promptTimer->setSingleShot(true);
    connect(m_promptTimer, &QTimer::timeout, this, &PetWindow::hidePrompt);
    m_speakingTimer = new QTimer(this);
    m_speakingTimer->setSingleShot(true);
    connect(m_speakingTimer, &QTimer::timeout, this, [this] {
        m_speaking = false;
        if (m_interactionIndex == SpeakInteraction) { m_interactionStarted = -1; returnToRestFrame(); }
        update();
    });
    m_idleInteractionTimer = new QTimer(this);
    m_idleInteractionTimer->setSingleShot(true);
    connect(m_idleInteractionTimer, &QTimer::timeout, this, [this] {
        if (!m_collapsedBubble && !m_dragging && !m_speaking && !m_prompt->isVisible()) playInteraction();
        scheduleIdleInteraction();
    });
    m_hoverFrameTimer = new QTimer(this);
    m_hoverFrameTimer->setSingleShot(true);
    m_hoverFrameTimer->setInterval(kHoverFrameDelayMs);
    connect(m_hoverFrameTimer, &QTimer::timeout, this, [this] {
        if (m_hovered && !m_collapsedBubble && !m_dragging && !m_speaking && m_interactionStarted < 0)
            playInteraction(HoverInteraction);
    });
    connect(m_promptYes, &QPushButton::clicked, this, [this] { hidePrompt(); emit chatRequested(); });
    connect(m_promptNo, &QPushButton::clicked, this, &PetWindow::hidePrompt);
    m_clock.start();
    scheduleIdleInteraction();
}

PetWindow::~PetWindow() = default;

void PetWindow::setAvatar(const QByteArray &svg, const QString &name, const QString &avatarId) {
    setAvatarFrames(svg.isEmpty() ? QList<QByteArray>{} : QList<QByteArray>{svg}, name, avatarId, QString());
}

void PetWindow::setAvatarFrames(const QList<QByteArray> &svgs, const QString &name, const QString &avatarId,
                                const QString &interactionProfile) {
    m_name = name.trimmed().isEmpty() ? QStringLiteral("Capricorn") : name.trimmed();
    m_avatarId = avatarId;
    m_interactionProfile = interactionProfile;
    m_frameTransitionDuration = motionTuningForProfile(m_interactionProfile).transitionMs;
    m_behaviorSeed = stableBehaviorSeed(m_avatarId, m_name);
    m_interactionStarted = -1;
    m_frameTransitionStarted = -1;
    m_lastFrameSwitchAt = -1;
    m_lastInteractionAt = -1;
    m_currentFrameIndex = 0;
    m_previousFrameIndex = -1;
    clearPendingFrameRequest();
    m_avatarFrames.clear();
    m_frameImages.clear();
    for (const QByteArray &svg : svgs) {
        if (!svg.isEmpty()) {
            m_avatarFrames << svg;
            const QImage frame = renderInteractionFrame(svg);
            if (!frame.isNull()) m_frameImages << frame;
        }
    }
    // The frame cache is pre-rendered once when a pet is generated. Interactions
    // then switch lightweight QImages instead of reparsing multi-megabyte SVG/PNG
    // payloads on the animation thread, keeping transitions smooth and predictable.
    m_previousRenderer.load(QByteArray{});
    m_renderer.load(m_avatarFrames.isEmpty() ? QByteArray{} : m_avatarFrames.constFirst());
    m_renderer.setAspectRatioMode(Qt::KeepAspectRatio);
    m_previousRenderer.setAspectRatioMode(Qt::KeepAspectRatio);
    if (m_frameImages.size() == m_avatarFrames.size()) m_avatarFrames.clear();
    else m_frameImages.clear();
    configureExpandedSizeForAvatar(true);
    setWindowTitle(m_name);
    scheduleIdleInteraction();
    update();
}

void PetWindow::setChatVisible(bool visible) {
    m_chatVisible = visible;
    if (visible) {
        hidePrompt();
        if (!m_collapsedBubble && !m_dragging) playInteraction(ChatInteraction);
    } else if (!m_speaking && !m_dragging) {
        returnToRestFrame();
    }
}

void PetWindow::showAuditPrompt() {
    showPrompt(QStringLiteral("主人，你要和派大星聊天么？"), true, 60000);
}

void PetWindow::speakText(const QString &text, int durationMs) {
    Q_UNUSED(text)
    if (m_collapsedBubble) return;
    hidePrompt();
    m_speaking = true;
    const int speakingDuration = qBound(1200, durationMs, 9000);
    playInteraction(SpeakInteraction);
    m_interactionIndex = SpeakInteraction;
    m_interactionDuration = speakingDuration;
    m_interactionStarted = m_clock.elapsed();
    m_speakingTimer->start(speakingDuration);
    update();
}

void PetWindow::showPrompt(const QString &text, bool actions, int durationMs) {
    if (m_collapsedBubble) return;
    m_promptText->setText(text);
    m_promptYes->setVisible(actions);
    m_promptNo->setVisible(actions);
    updatePromptGeometry();
    m_prompt->show();
    m_prompt->raise();
    m_promptTimer->start(durationMs);
}

void PetWindow::hidePrompt() {
    m_promptTimer->stop();
    m_prompt->hide();
}

void PetWindow::updatePromptGeometry() {
    if (m_collapsedBubble) return;
    const int margin = qMax(8, int(width() * 0.04));
    m_prompt->setGeometry(margin, qMax(4, int(height() * 0.015)), width() - margin * 2,
                          qBound(72, m_prompt->sizeHint().height(), 138));
}

void PetWindow::scheduleIdleInteraction() {
    if (!m_idleInteractionTimer) return;
    const CharacterMotionTuning tuning = motionTuningForProfile(m_interactionProfile);
    const int temperament = m_behaviorSeed % 5;
    const int minimum = tuning.idleMinimumMs + temperament * 320;
    const int maximum = minimum + tuning.idleRangeMs;
    m_idleInteractionTimer->start(QRandomGenerator::global()->bounded(minimum, maximum));
}

void PetWindow::updatePointerResponse(const QPointF &position) {
    if (width() <= 0 || height() <= 0) return;
    m_pointerTarget = QPointF(
        qBound(0.0, position.x() / width(), 1.0),
        qBound(0.0, position.y() / height(), 1.0));
}

void PetWindow::triggerSparkleBurst(bool generous) {
    const qint64 now = m_clock.elapsed();
    if (m_lastSparkleAt >= 0 && now - m_lastSparkleAt < kSparkleCooldownMs) return;
    m_lastSparkleAt = now;
    m_sparkles.clear();
    const int count = generous ? 16 : 10;
    m_sparkles.reserve(count);
    for (int i = 0; i < count; ++i) {
        const qreal angle = (2.0 * kPi * i / count) + QRandomGenerator::global()->bounded(100) / 420.0;
        const qreal distance = 0.52 + QRandomGenerator::global()->bounded(100) / 230.0;
        m_sparkles.append(QPointF(qCos(angle) * distance, qSin(angle) * distance));
    }
    m_sparkleProgress = 0.0;
}

int PetWindow::frameForInteraction(int interaction) const {
    const int frameCount = !m_frameImages.isEmpty() ? m_frameImages.size() : m_avatarFrames.size();
    if (frameCount <= 0) return 0;
    const int kind = qBound(0, interaction, int(InteractionCount) - 1);
    if (frameCount >= 10) {
        // Each built-in profile maps the same ten common interaction intents to
        // poses selected from that character's actual expressions/gestures.
        static constexpr int capricorn[10]{0, 2, 3, 8, 5, 1, 6, 4, 7, 9};
        static constexpr int scorpio[10]{0, 2, 5, 1, 7, 3, 6, 4, 9, 8};
        static constexpr int dog[10]{0, 2, 1, 9, 5, 7, 3, 8, 4, 6};
        static constexpr int fatCat[10]{2, 7, 3, 9, 5, 1, 0, 6, 8, 4};
        static constexpr int blackCat[10]{1, 9, 0, 5, 4, 7, 8, 6, 3, 2};
        const int *mapping = nullptr;
        if (m_interactionProfile == QStringLiteral("Capricorn")) mapping = capricorn;
        else if (m_interactionProfile == QStringLiteral("Scorpio")) mapping = scorpio;
        else if (m_interactionProfile == QStringLiteral("小鸡毛")) mapping = dog;
        else if (m_interactionProfile == QStringLiteral("胖橘")) mapping = fatCat;
        else if (m_interactionProfile == QStringLiteral("黑曼波")) mapping = blackCat;
        if (mapping) return mapping[kind];
    }
    if (kind == IdleInteraction || frameCount == 1) return 0;
    return 1 + ((kind - 1) % (frameCount - 1));
}

int PetWindow::interactionPriority(int interaction) const {
    switch (interaction) {
    case SpeakInteraction:
    case DragInteraction:
    case DropInteraction:
    case DoubleClickInteraction:
    case ChatInteraction:
        return 3;
    case ClickInteraction:
    case SpecialInteraction:
        return 2;
    default:
        return 1;
    }
}

void PetWindow::clearPendingFrameRequest() {
    m_pendingFrameIndex = -1;
    m_pendingFrameGeneration = ++m_interactionGeneration;
}

void PetWindow::switchAvatarFrame(int frameIndex) {
    const int frameCount = !m_frameImages.isEmpty() ? m_frameImages.size() : m_avatarFrames.size();
    if (frameCount <= 0) return;
    frameIndex = qBound(0, frameIndex, frameCount - 1);
    if (frameIndex == m_currentFrameIndex) {
        m_pendingFrameIndex = -1;
        return;
    }
    const qint64 now = m_clock.elapsed();
    const bool transitionActive = m_frameTransitionStarted >= 0
        && now - m_frameTransitionStarted < m_frameTransitionDuration;
    const bool dwellActive = m_lastFrameSwitchAt >= 0
        && now - m_lastFrameSwitchAt < kMinimumFrameDwellMs;
    if (transitionActive || dwellActive) {
        m_pendingFrameIndex = frameIndex;
        m_pendingFrameGeneration = m_interactionGeneration;
        return;
    }
    m_pendingFrameIndex = -1;
    m_previousFrameIndex = m_currentFrameIndex;
    m_currentFrameIndex = frameIndex;
    // Only fall back to reparsing SVGs when pre-rendering failed. Normal built-in
    // and validated user frames use the image cache and do no decode work here.
    if (m_frameImages.isEmpty() && m_currentFrameIndex < m_avatarFrames.size()) {
        m_previousRenderer.load(m_avatarFrames.at(qBound(0, m_previousFrameIndex, m_avatarFrames.size() - 1)));
        m_renderer.load(m_avatarFrames.at(m_currentFrameIndex));
        m_previousRenderer.setAspectRatioMode(Qt::KeepAspectRatio);
        m_renderer.setAspectRatioMode(Qt::KeepAspectRatio);
    }
    m_lastFrameSwitchAt = now;
    m_frameTransitionStarted = now;
    update();
}

void PetWindow::returnToRestFrame() {
    clearPendingFrameRequest();
    const int target = m_hovered && !m_dragging && !m_speaking ? HoverInteraction : IdleInteraction;
    switchAvatarFrame(frameForInteraction(target));
}

QRectF PetWindow::fittedSvgRect(const QSvgRenderer &renderer, const QRectF &bounds) const {
    QRectF source = renderer.viewBoxF();
    if (!source.isValid() || source.width() <= 0.0 || source.height() <= 0.0) {
        const QSize size = renderer.defaultSize();
        source = QRectF(0.0, 0.0, qMax(1, size.width()), qMax(1, size.height()));
    }
    const qreal scale = qMin(bounds.width() / source.width(), bounds.height() / source.height());
    const QSizeF fitted(source.width() * scale, source.height() * scale);
    return QRectF(bounds.center().x() - fitted.width() / 2.0,
                  bounds.center().y() - fitted.height() / 2.0,
                  fitted.width(), fitted.height());
}

QRectF PetWindow::fittedSvgRect(const QRectF &bounds) const {
    return fittedSvgRect(m_renderer, bounds);
}

QRectF PetWindow::fittedImageRect(const QImage &image, const QRectF &bounds) const {
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) return bounds;
    const qreal scale = qMin(bounds.width() / image.width(), bounds.height() / image.height());
    const QSizeF fitted(image.width() * scale, image.height() * scale);
    return QRectF(bounds.center().x() - fitted.width() / 2.0,
                  bounds.center().y() - fitted.height() / 2.0, fitted.width(), fitted.height());
}

QImage PetWindow::renderInteractionFrame(const QByteArray &svg) const {
    QSvgRenderer renderer(svg);
    if (!renderer.isValid()) return {};
    renderer.setAspectRatioMode(Qt::KeepAspectRatio);
    QRectF source = renderer.viewBoxF();
    if (!source.isValid() || source.width() <= 0.0 || source.height() <= 0.0) {
        const QSize fallback = renderer.defaultSize();
        source = QRectF(0.0, 0.0, qMax(1, fallback.width()), qMax(1, fallback.height()));
    }
    const qreal scale = qMin(kInteractionRasterMaxDimension / source.width(),
                             kInteractionRasterMaxDimension / source.height());
    const int width = qMax(1, qRound(source.width() * scale));
    const int height = qMax(1, qRound(source.height() * scale));
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    renderer.render(&painter, QRectF(0.0, 0.0, width, height));
    return image;
}

QRectF PetWindow::expandedCharacterRect() const {
    if (m_baseExpandedSize.width() <= 0 || m_baseExpandedSize.height() <= 0)
        return QRectF(width() * 0.08, height() * 0.25, width() * 0.84, height() * 0.72);
    const qreal sx = width() / qreal(m_baseExpandedSize.width());
    const qreal sy = height() / qreal(m_baseExpandedSize.height());
    const qreal scale = qMin(sx, sy);
    const QSizeF visual(m_baseCharacterSize.width() * scale, m_baseCharacterSize.height() * scale);
    const qreal bottom = height() - kExpandedBottomPadding * scale;
    return QRectF((width() - visual.width()) / 2.0, bottom - visual.height(),
                  visual.width(), visual.height());
}

void PetWindow::configureExpandedSizeForAvatar(bool resetToDefault) {
    QRectF source = m_renderer.viewBoxF();
    if (!source.isValid() || source.width() <= 0.0 || source.height() <= 0.0) {
        const QSize fallback = m_renderer.defaultSize();
        source = QRectF(0.0, 0.0, qMax(1, fallback.width()), qMax(1, fallback.height()));
    }
    qreal aspect = source.width() / qMax<qreal>(1.0, source.height());
    if (!qIsFinite(aspect) || aspect <= 0.0) aspect = 0.82;

    qreal imageHeight = kExpandedTargetImageHeight;
    qreal imageWidth = imageHeight * aspect;
    if (imageWidth > kExpandedMaxImageWidth) {
        imageWidth = kExpandedMaxImageWidth;
        imageHeight = imageWidth / aspect;
    }
    if (imageHeight > kExpandedMaxImageHeight) {
        imageHeight = kExpandedMaxImageHeight;
        imageWidth = imageHeight * aspect;
    }
    if (imageWidth < kExpandedMinImageWidth) {
        imageWidth = kExpandedMinImageWidth;
        imageHeight = qMin(kExpandedMaxImageHeight, imageWidth / aspect);
    }
    m_baseCharacterSize = QSizeF(qMax<qreal>(1.0, imageWidth), qMax<qreal>(1.0, imageHeight));

    const int baseWidth = qBound(176, qCeil(m_baseCharacterSize.width() + kExpandedHorizontalPadding), 330);
    const int baseHeight = qBound(244, qCeil(m_baseCharacterSize.height() + kExpandedTopReserve + kExpandedBottomPadding), 350);
    m_baseExpandedSize = QSize(baseWidth, baseHeight);
    setMinimumSize(qMax(132, qRound(baseWidth * kExpandedMinScale)),
                   qMax(178, qRound(baseHeight * kExpandedMinScale)));
    setMaximumSize(qMin(520, qRound(baseWidth * kExpandedMaxScale)),
                   qMin(620, qRound(baseHeight * kExpandedMaxScale)));
    if (resetToDefault && !m_collapsedBubble) resize(m_baseExpandedSize);
}

bool PetWindow::shouldCollapseForScreenBoundary() const {
    if (m_collapsedBubble || !isVisible()) return false;
    const QRect petRect = frameGeometry();
    const qint64 totalArea = qint64(petRect.width()) * qint64(petRect.height());
    if (totalArea <= 0) return false;
    qint64 visibleArea = 0;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (!screen) continue;
        const QRect visible = petRect.intersected(screen->geometry());
        if (!visible.isEmpty()) visibleArea += qint64(visible.width()) * qint64(visible.height());
    }
    return visibleArea <= qRound64(totalArea * kBoundaryVisibleRatio);
}

QRect PetWindow::clampExpandedGeometryToScreen(const QRect &input) const {
    if (!input.isValid()) return input;
    QRect bestScreen;
    qint64 bestArea = -1;
    for (QScreen *screen : QGuiApplication::screens()) {
        if (!screen) continue;
        const QRect available = screen->availableGeometry();
        const QRect overlap = input.intersected(screen->geometry());
        const qint64 area = qint64(qMax(0, overlap.width())) * qint64(qMax(0, overlap.height()));
        if (area > bestArea) { bestArea = area; bestScreen = available; }
    }
    if (!bestScreen.isValid()) bestScreen = availableGeometryForPoint(input.center());
    QRect result = input;
    const int x = qBound(bestScreen.left(), result.x(), qMax(bestScreen.left(), bestScreen.right() - result.width() + 1));
    const int y = qBound(bestScreen.top(), result.y(), qMax(bestScreen.top(), bestScreen.bottom() - result.height() + 1));
    result.moveTopLeft(QPoint(x, y));
    return result;
}

void PetWindow::playInteraction(int requested) {
    int interaction = requested;
    if (interaction < 0) {
        int specialChance = 18;
        if (m_interactionProfile == QStringLiteral("Scorpio")) specialChance = 14;
        else if (m_interactionProfile == QStringLiteral("小鸡毛")) specialChance = 38;
        else if (m_interactionProfile == QStringLiteral("胖橘")) specialChance = 10;
        else if (m_interactionProfile == QStringLiteral("黑曼波")) specialChance = 34;
        const bool repeatSpecial = m_interactionIndex == SpecialInteraction;
        interaction = !repeatSpecial && QRandomGenerator::global()->bounded(100) < specialChance
            ? SpecialInteraction
            : LongIdleInteraction;
    }
    interaction = qBound(0, interaction, int(InteractionCount) - 1);
    const qint64 now = m_clock.elapsed();
    const CharacterMotionTuning tuning = motionTuningForProfile(m_interactionProfile);
    const bool activeInteraction = m_dragging || m_speaking || m_interactionStarted >= 0;
    const int activePriority = activeInteraction ? interactionPriority(m_interactionIndex) : 0;
    const int requestedPriority = interactionPriority(interaction);
    if (activeInteraction && requestedPriority < activePriority) return;
    if (m_lastInteractionAt >= 0 && now - m_lastInteractionAt < tuning.interactionCooldownMs
        && requestedPriority <= activePriority
        && interaction != DoubleClickInteraction
        && interaction != SpeakInteraction) {
        return;
    }

    clearPendingFrameRequest();
    m_interactionIndex = interaction;
    m_lastInteractionAt = now;
    switchAvatarFrame(frameForInteraction(interaction));
    if (interaction == ClickInteraction) triggerSparkleBurst(false);
    else if (interaction == DoubleClickInteraction || interaction == SpecialInteraction)
        triggerSparkleBurst(true);
    if (interaction == DragInteraction) {
        m_interactionStarted = -1;
        update();
        return;
    }
    static constexpr int durations[InteractionCount]{
        0, 0, 680, 920, 0, 720, 980, 1200, 1550, 1180
    };
    m_interactionDuration = qMax(1, durations[interaction]);
    m_interactionStarted = now;
    update();
}

void PetWindow::applyExpandedTopmost(bool enabled) {
    m_expandedTopmost = enabled;
    applyDesktopCompanionTopmost(this, enabled, !enabled);
    emit topmostChanged(enabled);
}

void PetWindow::setModalBlocked(bool blocked) {
    if (m_modalBlocked == blocked) return;
    m_modalBlocked = blocked;
    setEnabled(!blocked);
    const bool desiredTopmost = !blocked && (m_collapsedBubble || m_expandedTopmost);
    applyDesktopCompanionTopmost(this, desiredTopmost, !blocked && !desiredTopmost);
}

QRect PetWindow::availableGeometryForPoint(const QPoint &globalPoint) const {
    QScreen *screen = QGuiApplication::screenAt(globalPoint);
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1080);
}

void PetWindow::collapseToBubble() {
    if (m_collapsedBubble) return;
    hidePrompt();
    m_interactionStarted = -1;
    switchAvatarFrame(frameForInteraction(IdleInteraction));
    const QRect rawExpandedGeometry = geometry();
    m_expandedGeometry = clampExpandedGeometryToScreen(rawExpandedGeometry);
    // Preserve the user's expanded-window preference. The compact bubble is
    // temporarily topmost regardless of that preference.
    m_collapsedBubble = true;
    setMinimumSize(kBubbleSize, kBubbleSize);
    setMaximumSize(kBubbleSize, kBubbleSize);
    const QPoint center = rawExpandedGeometry.center();
    const bool wasVisible = isVisible();
    applyDesktopCompanionTopmost(this, true, false);
    resize(kBubbleSize, kBubbleSize);
    const QRect screen = availableGeometryForPoint(center);
    QPoint bubblePos(center.x() - kBubbleSize / 2, center.y() - kBubbleSize / 2);
    bubblePos.setX(qBound(screen.left(), bubblePos.x(), screen.right() - kBubbleSize + 1));
    bubblePos.setY(qBound(screen.top(), bubblePos.y(), screen.bottom() - kBubbleSize + 1));
    move(bubblePos);
    if (wasVisible) show();
    else show();
    raise();
    update();
    emit collapsedChanged(true);
}

void PetWindow::expandFromBubble() {
    if (!m_collapsedBubble) return;
    m_collapsedBubble = false;
    returnToRestFrame();
    setMinimumSize(qMax(132, qRound(m_baseExpandedSize.width() * kExpandedMinScale)),
                   qMax(178, qRound(m_baseExpandedSize.height() * kExpandedMinScale)));
    setMaximumSize(qMin(520, qRound(m_baseExpandedSize.width() * kExpandedMaxScale)),
                   qMin(620, qRound(m_baseExpandedSize.height() * kExpandedMaxScale)));
    const bool wasVisible = isVisible();
    applyDesktopCompanionTopmost(this, m_expandedTopmost, !m_expandedTopmost);
    if (m_expandedGeometry.isValid()) setGeometry(m_expandedGeometry);
    else resize(m_baseExpandedSize);
    if (wasVisible) show();
    else show();
    if (m_expandedTopmost) raise();
    updatePromptGeometry();
    update();
    emit topmostChanged(m_expandedTopmost);
    emit collapsedChanged(false);
    emit geometryChanged(geometry());
}

void PetWindow::snapBubbleToSide() {
    if (!m_collapsedBubble) return;
    QRect screen = availableGeometryForPoint(frameGeometry().center());
    QPoint target = pos();
    const int leftDistance = qAbs(frameGeometry().left() - screen.left());
    const int rightDistance = qAbs(screen.right() - frameGeometry().right());
    if (leftDistance <= kBubbleSnapDistance) target.setX(screen.left() - width() / 2);
    else if (rightDistance <= kBubbleSnapDistance) target.setX(screen.right() - width() / 2 + 1);
    else target.setX(qBound(screen.left(), target.x(), screen.right() - width() + 1));
    target.setY(qBound(screen.top(), target.y(), screen.bottom() - height() + 1));
    move(target);
}

void PetWindow::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_collapsedBubble) {
        const QRectF circle(3.0, 3.0, width() - 6.0, height() - 6.0);
        painter.setPen(Qt::NoPen);
        painter.setBrush(QColor(40, 60, 50, 30));
        painter.drawEllipse(circle.translated(0, 2));
        painter.setBrush(QColor(247, 250, 248, 250));
        painter.setPen(QPen(QColor(139, 163, 150, 190), 1.2));
        painter.drawEllipse(circle);
        const QRectF inner(circle.center().x() - kBubbleAvatarBox / 2.0,
                           circle.center().y() - kBubbleAvatarBox / 2.0,
                           kBubbleAvatarBox, kBubbleAvatarBox);
        painter.save();
        QPainterPath clip;
        clip.addEllipse(inner);
        painter.setClipPath(clip);
        painter.fillRect(inner, QColor(QStringLiteral("#EEEAFE")));
        if (!m_frameImages.isEmpty() && m_currentFrameIndex >= 0 && m_currentFrameIndex < m_frameImages.size()) {
            const QImage &frame = m_frameImages.at(m_currentFrameIndex);
            painter.drawImage(fittedImageRect(frame, inner), frame);
        } else if (m_renderer.isValid()) m_renderer.render(&painter, fittedSvgRect(inner));
        else {
            painter.setPen(QColor(QStringLiteral("#6757E8")));
            painter.setFont(QFont(QStringLiteral("Arial"), 18, QFont::Bold));
            painter.drawText(inner, Qt::AlignCenter, QStringLiteral("♑"));
        }
        painter.restore();
        return;
    }

    const CharacterMotionTuning tuning = motionTuningForProfile(m_interactionProfile);
    const int frameCount = !m_frameImages.isEmpty() ? m_frameImages.size() : m_avatarFrames.size();
    const bool singleFrameAvatar = frameCount == 1;
    const qreal now = m_clock.elapsed() / 1000.0;
    const qreal idlePeriod = (3.2 + (m_behaviorSeed % 5) * 0.28) * tuning.idlePeriodScale;
    // Single-frame avatars cannot express themselves through pose changes, so give
    // them a restrained center-of-mass drift and breathing rhythm. Movement is
    // translation + UNIFORM scale only: no rotation, mirroring or aspect distortion.
    const qreal pointerX = m_hovered ? (m_pointerResponse.x() - 0.5) : 0.0;
    const qreal pointerY = m_hovered ? (m_pointerResponse.y() - 0.42) : 0.0;
    qreal offsetX = (singleFrameAvatar
        ? qSin(now * (2.0 * kPi / (idlePeriod * 1.9))) * qMin<qreal>(1.25, width() * 0.0038)
        : 0.0) + pointerX * qMin<qreal>(3.6, width() * 0.013) * tuning.pointerAmplitude;
    qreal offsetY = qSin(now * (2.0 * kPi / idlePeriod)) * (tuning.idleBob + (singleFrameAvatar ? 0.36 : 0.0))
        + pointerY * qMin<qreal>(1.8, height() * 0.006) * tuning.pointerAmplitude;
    qreal uniformScale = 1.0 + qSin(now * (2.0 * kPi / (idlePeriod + 0.7)))
        * (tuning.breathing + (singleFrameAvatar ? 0.0016 : 0.0));
    if (m_hovered && m_interactionStarted < 0 && !m_dragging) {
        uniformScale += tuning.hoverScale + (singleFrameAvatar ? 0.0025 : 0.0);
        if (singleFrameAvatar) offsetY -= qMin<qreal>(1.1, height() * 0.0038);
    }

    if (m_interactionStarted >= 0) {
        const qreal p = qBound(0.0, (m_clock.elapsed() - m_interactionStarted) / qreal(qMax(1, m_interactionDuration)), 1.0);
        if (p >= 1.0) {
            m_interactionStarted = -1;
            returnToRestFrame();
        } else {
            const qreal wave = qSin(p * kPi);
            switch (m_interactionIndex) {
            case ClickInteraction:
                offsetY -= wave * height() * tuning.clickLift;
                uniformScale += wave * tuning.clickScale;
                if (singleFrameAvatar) offsetX += qSin(p * kPi * 2.0) * width() * 0.0045;
                break;
            case DoubleClickInteraction:
                offsetY -= qAbs(qSin(p * kPi * 2.0)) * height() * tuning.doubleLift;
                uniformScale += wave * tuning.doubleScale;
                if (singleFrameAvatar) offsetX += qSin(p * kPi * 3.0) * width() * 0.0060;
                break;
            case DropInteraction:
                offsetY += qSin(p * kPi * 2.0) * height() * tuning.dropBounce * (1.0 - p);
                if (singleFrameAvatar) uniformScale += wave * 0.004;
                break;
            case ChatInteraction:
                offsetY -= wave * height() * tuning.chatLift;
                uniformScale += wave * tuning.chatScale;
                if (singleFrameAvatar) offsetX += qSin(p * kPi) * width() * 0.0040;
                break;
            case SpeakInteraction:
                offsetY -= qAbs(qSin(p * kPi * 7.0)) * height() * tuning.speakLift;
                uniformScale += qAbs(qSin(p * kPi * 5.0)) * tuning.speakScale;
                if (singleFrameAvatar) offsetX += qSin(p * kPi * 6.0) * width() * 0.0025;
                break;
            case LongIdleInteraction:
                offsetY += wave * height() * tuning.longIdleBob;
                uniformScale -= wave * tuning.longIdleScale;
                if (singleFrameAvatar) offsetX += qSin(p * kPi * 2.0) * width() * 0.0055;
                break;
            case SpecialInteraction:
                offsetY -= qAbs(qSin(p * kPi * 2.0)) * height() * tuning.specialLift;
                uniformScale += wave * tuning.specialScale;
                if (singleFrameAvatar) offsetX += qSin(p * kPi * 4.0) * width() * 0.0070;
                break;
            default:
                break;
            }
        }
    }

    const QRectF character = expandedCharacterRect();
    painter.save();
    const QPointF pivot(character.center().x(), character.bottom() - character.height() * 0.10);
    painter.translate(pivot.x() + offsetX, pivot.y() + offsetY);
    painter.scale(uniformScale, uniformScale);
    painter.translate(-pivot.x(), -pivot.y());

    qreal currentOpacity = 1.0;
    qreal previousOpacity = 0.0;
    const bool cachedFrames = !m_frameImages.isEmpty()
        && m_currentFrameIndex >= 0 && m_currentFrameIndex < m_frameImages.size();
    const bool validPreviousCache = cachedFrames && m_previousFrameIndex >= 0
        && m_previousFrameIndex < m_frameImages.size();
    const bool validPreviousRenderer = !cachedFrames && m_previousRenderer.isValid();
    if (m_frameTransitionStarted >= 0 && (validPreviousCache || validPreviousRenderer)) {
        const qreal rawTransition = qBound(0.0, (m_clock.elapsed() - m_frameTransitionStarted) / qreal(m_frameTransitionDuration), 1.0);
        // Smoothstep removes the visible hard start/end of a linear dissolve. The
        // two frames remain in exactly the same proportional fitted rectangle.
        const qreal transition = rawTransition * rawTransition * (3.0 - 2.0 * rawTransition);
        currentOpacity = transition;
        previousOpacity = 1.0 - transition;
        if (rawTransition >= 1.0) {
            m_frameTransitionStarted = -1;
            m_previousFrameIndex = -1;
            if (m_pendingFrameIndex >= 0 && m_pendingFrameIndex != m_currentFrameIndex) {
                const int pending = m_pendingFrameIndex;
                const quint64 generation = m_pendingFrameGeneration;
                m_pendingFrameIndex = -1;
                const int dwellDelay = qMax(0, kMinimumFrameDwellMs
                    - int(m_clock.elapsed() - m_lastFrameSwitchAt));
                QTimer::singleShot(dwellDelay, this, [this, pending, generation] {
                    if (generation != m_interactionGeneration) return;
                    switchAvatarFrame(pending);
                });
            }
        }
    }
    if (previousOpacity > 0.001 && validPreviousCache) {
        const QImage &previous = m_frameImages.at(m_previousFrameIndex);
        painter.save();
        painter.setOpacity(previousOpacity);
        painter.drawImage(fittedImageRect(previous, character), previous);
        painter.restore();
    } else if (previousOpacity > 0.001 && validPreviousRenderer) {
        painter.save();
        painter.setOpacity(previousOpacity);
        m_previousRenderer.render(&painter, fittedSvgRect(m_previousRenderer, character));
        painter.restore();
    }
    if (cachedFrames) {
        const QImage &current = m_frameImages.at(m_currentFrameIndex);
        painter.save();
        painter.setOpacity(currentOpacity);
        painter.drawImage(fittedImageRect(current, character), current);
        painter.restore();
    } else if (m_renderer.isValid()) {
        painter.save();
        painter.setOpacity(currentOpacity);
        m_renderer.render(&painter, fittedSvgRect(m_renderer, character));
        painter.restore();
    } else {
        painter.setPen(QColor(QStringLiteral("#6F7583")));
        painter.setFont(QFont(QStringLiteral("Arial"), qBound(40, width() / 3, 100), QFont::Bold));
        painter.drawText(character, Qt::AlignCenter, QStringLiteral("♑"));
    }
    painter.restore();

    if (!m_sparkles.isEmpty() && m_sparkleProgress < 1.0) {
        const qreal eased = 1.0 - qPow(1.0 - m_sparkleProgress, 3.0);
        const qreal fade = qBound(0.0, 1.0 - m_sparkleProgress, 1.0);
        const QPointF origin(character.center().x(), character.top() + character.height() * 0.44);
        const qreal radius = qMin(character.width(), character.height()) * (0.26 + eased * 0.46);
        static const QColor colors[]{QColor(245, 139, 169), QColor(255, 205, 92),
                                     QColor(119, 196, 164), QColor(143, 124, 224)};
        painter.save();
        painter.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < m_sparkles.size(); ++i) {
            const QPointF direction = m_sparkles.at(i);
            const QPointF point = origin + QPointF(direction.x() * radius, direction.y() * radius);
            QColor color = colors[i % 4];
            color.setAlphaF(fade * 0.88);
            painter.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap));
            if (i % 3 == 0) {
                const qreal size = 2.8 + fade * 2.5;
                QPainterPath heart;
                heart.moveTo(point.x(), point.y() + size);
                heart.cubicTo(point.x() - size * 1.7, point.y(), point.x() - size, point.y() - size,
                              point.x(), point.y() - size * 0.25);
                heart.cubicTo(point.x() + size, point.y() - size, point.x() + size * 1.7, point.y(),
                              point.x(), point.y() + size);
                painter.setBrush(color);
                painter.setPen(Qt::NoPen);
                painter.drawPath(heart);
            } else {
                const qreal arm = 2.0 + fade * 3.2;
                painter.drawLine(point + QPointF(-arm, 0.0), point + QPointF(arm, 0.0));
                painter.drawLine(point + QPointF(0.0, -arm), point + QPointF(0.0, arm));
            }
        }
        painter.restore();
    }
}

void PetWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    // Reassert the logical preference after any shell-driven hide/show without
    // recreating the native window. Collapsed bubbles intentionally remain topmost.
    applyDesktopCompanionTopmost(this, !m_modalBlocked && (m_collapsedBubble || m_expandedTopmost), false);
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    // DeleteTab keeps the normal Qt::Window semantics (and therefore the same
    // Z-order behavior as Capricorn) without adding a second taskbar button.
    QTimer::singleShot(0, this, [hwnd] { removeDesktopCompanionTaskbarButton(hwnd); });
#endif
}

void PetWindow::resizeEvent(QResizeEvent *event) {
    updatePromptGeometry();
    QWidget::resizeEvent(event);
    if (!m_collapsedBubble) emit geometryChanged(geometry());
}

void PetWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    emit geometryChanged(geometry());
}

void PetWindow::enterEvent(QEnterEvent *event) {
    m_hovered = true;
    updatePointerResponse(event->position());
    if (!m_collapsedBubble && !m_dragging && m_interactionStarted < 0 && !m_speaking)
        m_hoverFrameTimer->start();
    update();
    QWidget::enterEvent(event);
}

void PetWindow::leaveEvent(QEvent *event) {
    m_hovered = false;
    m_hoverFrameTimer->stop();
    m_pointerTarget = QPointF(0.5, 0.38);
    clearPendingFrameRequest();
    if (!m_dragging && m_interactionStarted < 0 && !m_speaking) returnToRestFrame();
    update();
    QWidget::leaveEvent(event);
}

void PetWindow::mouseDoubleClickEvent(QMouseEvent *event) {
    if (!m_collapsedBubble && event->button() == Qt::LeftButton && !m_prompt->geometry().contains(event->position().toPoint())) {
        ++m_clickGeneration;
        m_lastClickAt = m_clock.elapsed();
        clearPendingFrameRequest();
        playInteraction(DoubleClickInteraction);
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PetWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !m_collapsedBubble && !m_expandedTopmost && !m_modalBlocked) {
        // In normal (non-topmost) mode a direct click must behave like any other
        // ordinary app window: bring this window to the front of the normal band.
        bringNormalDesktopCompanionToFront(this);
    }
    if (event->button() == Qt::LeftButton && (m_collapsedBubble || !m_prompt->geometry().contains(event->position().toPoint()))) {
        m_pressTracking = true;
        m_dragMoved = false;
        m_pressGlobal = event->globalPosition().toPoint();
        m_lastDragGlobal = m_pressGlobal;
        m_dragOffset = m_pressGlobal - frameGeometry().topLeft();
        m_dragging = m_collapsedBubble || !m_positionLocked;
    }
    QWidget::mousePressEvent(event);
}

void PetWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!m_collapsedBubble) updatePointerResponse(event->position());
    if (m_dragging && event->buttons().testFlag(Qt::LeftButton)) {
        const QPoint global = event->globalPosition().toPoint();
        m_lastDragGlobal = global;
        if (!m_dragMoved && (global - m_pressGlobal).manhattanLength() >= 7) {
            m_dragMoved = true;
            playInteraction(DragInteraction);
        }
        move(global - m_dragOffset);
        if (!m_collapsedBubble && m_dragMoved && shouldCollapseForScreenBoundary()) {
            m_dragging = false;
            m_pressTracking = false;
            collapseToBubble();
            event->accept();
            return;
        }
        update();
    }
    QWidget::mouseMoveEvent(event);
}

void PetWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && m_pressTracking) {
        const bool click = !m_dragMoved && (event->globalPosition().toPoint() - m_pressGlobal).manhattanLength() < 10;
        m_pressTracking = false;
        m_dragging = false;
        if (m_collapsedBubble) {
            if (click) expandFromBubble();
            else snapBubbleToSide();
            event->accept();
            return;
        }
        if (m_dragMoved && shouldCollapseForScreenBoundary()) {
            collapseToBubble();
            event->accept();
            return;
        }
        if (m_dragMoved) playInteraction(DropInteraction);
        if (click) {
            const qint64 now = m_clock.elapsed();
            const int doubleClickInterval = QGuiApplication::styleHints()->mouseDoubleClickInterval();
            const bool followsDoubleClick = m_lastClickAt >= 0
                && now - m_lastClickAt <= doubleClickInterval;
            const quint64 clickGeneration = ++m_clickGeneration;
            if (!followsDoubleClick) {
                QTimer::singleShot(doubleClickInterval, this, [this, clickGeneration] {
                    if (clickGeneration != m_clickGeneration || m_dragging || m_speaking || m_collapsedBubble) return;
                    playInteraction(ClickInteraction);
                });
            }
            if (m_chatVisible) {
                hidePrompt();
                event->accept();
                return;
            }
            // V62: the chat invitation is rate-limited to exactly one display per
            // ten-second cycle. Other clicks in the same cycle still play the pet
            // interaction above, but never reopen or extend the invitation bubble.
            if (!m_clickCycle.isValid() || m_clickCycle.elapsed() >= 10000) {
                if (m_clickCycle.isValid()) m_clickCycle.restart(); else m_clickCycle.start();
                showPrompt(QStringLiteral("主人，你要和%1聊天么？").arg(m_name), true, 10000);
            }
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void PetWindow::wheelEvent(QWheelEvent *event) {
    if (m_collapsedBubble) { event->ignore(); return; }
    const int delta = event->angleDelta().y();
    if (delta == 0) { event->ignore(); return; }
    const qreal currentScale = qMin(width() / qreal(qMax(1, m_baseExpandedSize.width())),
                                    height() / qreal(qMax(1, m_baseExpandedSize.height())));
    const qreal targetScale = qBound(kExpandedMinScale, currentScale + (delta > 0 ? 0.08 : -0.08), kExpandedMaxScale);
    const int newWidth = qBound(minimumWidth(), qRound(m_baseExpandedSize.width() * targetScale), maximumWidth());
    const int newHeight = qBound(minimumHeight(), qRound(m_baseExpandedSize.height() * targetScale), maximumHeight());
    const QPoint bottomCenter = QPoint(frameGeometry().center().x(), frameGeometry().bottom());
    resize(newWidth, newHeight);
    move(bottomCenter.x() - newWidth / 2, bottomCenter.y() - newHeight + 1);
    const qint64 now = m_clock.elapsed();
    if (m_lastWheelSpecialAt < 0 || now - m_lastWheelSpecialAt >= kWheelInteractionCooldownMs) {
        m_lastWheelSpecialAt = now;
        playInteraction(SpecialInteraction);
    }
    event->accept();
}

void PetWindow::contextMenuEvent(QContextMenuEvent *event) {
    if (m_collapsedBubble) { event->accept(); return; }
    QMenu menu(this);
    menu.setObjectName(QStringLiteral("petContextMenu"));
    QAction *chat = menu.addAction(
        QIcon(m_chatVisible ? QStringLiteral(":/resources/menu-chat-close.svg")
                            : QStringLiteral(":/resources/menu-chat.svg")),
        m_chatVisible ? QStringLiteral("关闭对话") : QStringLiteral("打开对话"));
    // V90: bind the icon to the QAction state instead of choosing it only once
    // when the menu is created.  The explicit sync lambda also makes the state
    // visible with styles/platforms that do not propagate QIcon::On automatically.
    QIcon topmostStateIcon;
    topmostStateIcon.addFile(QStringLiteral(":/resources/menu-top.svg"), QSize(), QIcon::Normal, QIcon::Off);
    topmostStateIcon.addFile(QStringLiteral(":/resources/menu-top-filled.svg"), QSize(), QIcon::Normal, QIcon::On);
    QAction *topmost = menu.addAction(topmostStateIcon, QStringLiteral("始终显示在上层"));
    topmost->setCheckable(true);
    const auto syncTopmostIcon = [topmost](bool enabled) {
        topmost->setIcon(QIcon(enabled
                                  ? QStringLiteral(":/resources/menu-top-filled.svg")
                                  : QStringLiteral(":/resources/menu-top.svg")));
    };
    topmost->setChecked(m_expandedTopmost);
    syncTopmostIcon(m_expandedTopmost);
    QObject::connect(topmost, &QAction::toggled, &menu, [syncTopmostIcon, &menu](bool enabled) {
        syncTopmostIcon(enabled);
        menu.update();
    });
    QAction *locked = menu.addAction(QIcon(m_positionLocked
                                               ? QStringLiteral(":/resources/menu-pin-filled.svg")
                                               : QStringLiteral(":/resources/menu-pin.svg")),
                                     QStringLiteral("固定到当前位置"));
    locked->setCheckable(true);
    locked->setChecked(m_positionLocked);
    QAction *hidePet = menu.addAction(QIcon(QStringLiteral(":/resources/menu-bubble.svg")), QStringLiteral("隐藏桌宠"));
    QAction *exportChat = menu.addAction(QIcon(QStringLiteral(":/resources/menu-export-chat.svg")), QStringLiteral("导出聊天记录"));
    QAction *importChat = menu.addAction(QIcon(QStringLiteral(":/resources/menu-import-chat.svg")), QStringLiteral("导入聊天记录"));
    menu.addSeparator();
    QAction *closePet = menu.addAction(QIcon(QStringLiteral(":/resources/menu-close-pet.svg")), QStringLiteral("关闭桌宠"));

    QAction *chosen = menu.exec(event->globalPos());
    if (chosen == chat) {
        if (m_chatVisible) emit closeChatRequested();
        else emit chatRequested();
    } else if (chosen == topmost) {
        applyExpandedTopmost(topmost->isChecked());
    } else if (chosen == locked) {
        m_positionLocked = locked->isChecked();
        m_dragging = false;
    } else if (chosen == hidePet) {
        collapseToBubble();
    } else if (chosen == exportChat) {
        emit exportChatRequested();
    } else if (chosen == importChat) {
        emit importChatRequested();
    } else if (chosen == closePet) {
        hidePrompt();
        emit closePetRequested();
    }
}

bool PetWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_NCHITTEST) {
        if (m_collapsedBubble) { *result = HTCLIENT; return true; }
        RECT rect{};
        if (GetWindowRect(msg->hwnd, &rect)) {
            const LONG x = GET_X_LPARAM(msg->lParam);
            const LONG y = GET_Y_LPARAM(msg->lParam);
            const UINT dpi = GetDpiForWindow(msg->hwnd);
            const int edge = qMax(7, MulDiv(8, int(dpi), 96));
            const int corner = qMax(18, MulDiv(20, int(dpi), 96));
            const bool left = x >= rect.left && x < rect.left + edge;
            const bool right = x < rect.right && x >= rect.right - edge;
            const bool top = y >= rect.top && y < rect.top + edge;
            const bool bottom = y < rect.bottom && y >= rect.bottom - edge;
            const bool nearLeft = x >= rect.left && x < rect.left + corner;
            const bool nearRight = x < rect.right && x >= rect.right - corner;
            const bool nearTop = y >= rect.top && y < rect.top + corner;
            const bool nearBottom = y < rect.bottom && y >= rect.bottom - corner;
            if (nearTop && nearLeft) { *result = HTTOPLEFT; return true; }
            if (nearTop && nearRight) { *result = HTTOPRIGHT; return true; }
            if (nearBottom && nearLeft) { *result = HTBOTTOMLEFT; return true; }
            if (nearBottom && nearRight) { *result = HTBOTTOMRIGHT; return true; }
            if (left) { *result = HTLEFT; return true; }
            if (right) { *result = HTRIGHT; return true; }
            if (top) { *result = HTTOP; return true; }
            if (bottom) { *result = HTBOTTOM; return true; }
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QWidget::nativeEvent(eventType, message, result);
}
