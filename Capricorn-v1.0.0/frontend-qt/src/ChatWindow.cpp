#include "ChatWindow.h"
#include "ChatStore.h"
#include "CoreClient.h"

#include <QAbstractAnimation>
#include <QAbstractButton>
#include <QCheckBox>
#include <QAbstractItemView>
#include <QAbstractTextDocumentLayout>
#include <QDateTime>
#include <QDialog>
#include <QEasingCurve>
#include <QEvent>
#include <QFrame>
#include <QGraphicsOpacityEffect>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QListWidget>
#include <QListView>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QNetworkReply>
#include <QPointer>
#include <QProxyStyle>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShowEvent>
#include <QSizePolicy>
#include <QHideEvent>
#include <QScrollBar>
#include <QStyleOptionSlider>
#include <QScroller>
#include <QStringList>
#include <QTextEdit>
#include <QTimer>
#include <QTextDocument>
#include <QTextLayout>
#include <QTextOption>
#include <QVBoxLayout>
#include <QWindow>
#include <QUuid>
#include <QtMath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#endif

namespace {
void applyChatWindowTopmost(QWidget *window, bool enabled, bool keepAtFrontOfNormalBand = true) {
    if (!window) return;
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd) return;
    constexpr UINT passiveFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER;
    SetWindowPos(hwnd, enabled ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, passiveFlags);
    if (!enabled && keepAtFrontOfNormalBand && GetForegroundWindow() == hwnd) {
        // Only preserve front position if the chat was already the user's active
        // window.  Synchronising the pet's menu toggle must not unexpectedly make
        // the chat steal focus from the pet.
        constexpr UINT activeFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOOWNERZORDER;
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, activeFlags);
        BringWindowToTop(hwnd);
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
QString messageTime(qint64 milliseconds) {
    return QDateTime::fromMSecsSinceEpoch(milliseconds).toString(QStringLiteral("HH:mm"));
}

QByteArray legacyChatValue(const QString &key) {
    for (int version = 60; version >= 48; --version) {
        QSettings legacy(QStringLiteral("Capricorn"), QStringLiteral("CapricornV%1").arg(version));
        const QByteArray value = legacy.value(key).toByteArray();
        if (!value.isEmpty()) return value;
    }
    return {};
}

QString legacyMemoryValue(const QString &key) {
    for (int version = 60; version >= 48; --version) {
        QSettings legacy(QStringLiteral("Capricorn"), QStringLiteral("CapricornV%1").arg(version));
        const QString value = legacy.value(key).toString();
        if (!value.isEmpty()) return value;
    }
    return {};
}

QJsonArray normalizedStoredMessages(const QJsonArray &input) {
    constexpr int maximumMessages = 1000;
    constexpr int maximumCharactersPerMessage = 64000;
    QJsonArray result;
    const int start = qMax(0, input.size() - maximumMessages);
    for (int index = start; index < input.size(); ++index) {
        QJsonObject message = input.at(index).toObject();
        const QString role = message.value(QStringLiteral("role")).toString();
        const QString text = message.value(QStringLiteral("text")).toString().left(maximumCharactersPerMessage);
        if ((role != QStringLiteral("user") && role != QStringLiteral("assistant")) || text.isEmpty()) continue;
        if (message.value(QStringLiteral("id")).toString().isEmpty())
            message.insert(QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces));
        message.insert(QStringLiteral("role"), role);
        message.insert(QStringLiteral("text"), text);
        if (!message.value(QStringLiteral("at")).isDouble())
            message.insert(QStringLiteral("at"), QDateTime::currentMSecsSinceEpoch());
        result.append(message);
    }
    return result;
}

QJsonObject persistentChatSnapshot(const QString &personaId, const QString &personaName) {
    const QString safeId = personaId.trimmed().isEmpty() ? personaName.trimmed() : personaId.trimmed();
    ChatStoreSnapshot stored = ChatStore::instance().load(safeId);
    if (stored.exists) {
        return QJsonObject{
            {QStringLiteral("messages"), stored.messages},
            {QStringLiteral("structuredFacts"), stored.structuredFacts},
            {QStringLiteral("globalMemoryMarkdown"), stored.globalMemoryMarkdown},
            {QStringLiteral("relationshipMemoryMarkdown"), stored.relationshipMemoryMarkdown},
            {QStringLiteral("globalMemoryRevision"), stored.globalMemoryRevision},
            {QStringLiteral("relationshipMemoryRevision"), stored.relationshipMemoryRevision},
            {QStringLiteral("structuredFactsRevision"), stored.structuredFactsRevision}
        };
    }

    // One-time, non-destructive migration from the old QSettings/registry layout.
    // The registry values remain as a recovery copy, while SQLite becomes the
    // authoritative store as soon as this snapshot is committed successfully.
    QSettings settings(QStringLiteral("Capricorn"), QStringLiteral("Capricorn"));
    const QString chatKey = QStringLiteral("chat/%1").arg(safeId);
    QByteArray raw = settings.value(chatKey).toByteArray();
    if (raw.isEmpty() && !personaName.trimmed().isEmpty())
        raw = settings.value(QStringLiteral("chat/%1").arg(personaName.trimmed())).toByteArray();
    if (raw.isEmpty()) raw = legacyChatValue(chatKey);
    if (raw.isEmpty() && !personaName.trimmed().isEmpty())
        raw = legacyChatValue(QStringLiteral("chat/%1").arg(personaName.trimmed()));
    const QJsonDocument document = QJsonDocument::fromJson(raw);
    const QJsonArray messages = normalizedStoredMessages(document.isArray() ? document.array() : QJsonArray{});

    const QString memoryKey = QStringLiteral("chat-memory/%1").arg(safeId);
    QString memory = settings.value(memoryKey).toString();
    if (memory.isEmpty()) memory = legacyMemoryValue(memoryKey);
    memory = memory.trimmed().left(24000);
    const int revision = qMax(0, settings.value(
        QStringLiteral("chat-memory-revision/%1").arg(safeId), 0).toInt());
    if (stored.storeAvailable)
        ChatStore::instance().replaceSnapshot(safeId, personaName, messages, memory, revision);
    return QJsonObject{
        {QStringLiteral("messages"), messages},
        {QStringLiteral("structuredFacts"), stored.structuredFacts},
        {QStringLiteral("globalMemoryMarkdown"), stored.globalMemoryMarkdown},
        {QStringLiteral("relationshipMemoryMarkdown"), memory},
        {QStringLiteral("globalMemoryRevision"), stored.globalMemoryRevision},
        {QStringLiteral("relationshipMemoryRevision"), revision},
        {QStringLiteral("structuredFactsRevision"), stored.structuredFactsRevision}
    };
}

QJsonArray recentContextMessages(const QJsonArray &messages) {
    constexpr int maximumMessages = 40;
    constexpr int maximumCharacters = 32000;
    QJsonArray reversed;
    int remaining = maximumCharacters;
    for (int index = messages.size() - 1; index >= 0 && reversed.size() < maximumMessages && remaining > 0; --index) {
        const QJsonObject item = messages.at(index).toObject();
        const QString role = item.value(QStringLiteral("role")).toString();
        QString text = item.value(QStringLiteral("text")).toString().trimmed();
        if ((role != QStringLiteral("user") && role != QStringLiteral("assistant")) || text.isEmpty()) continue;
        if (text.size() > remaining) {
            if (!reversed.isEmpty()) break;
            text = text.left(remaining);
        }
        reversed.append(QJsonObject{{QStringLiteral("role"), role}, {QStringLiteral("text"), text}});
        remaining -= text.size();
    }
    QJsonArray result;
    for (int index = reversed.size() - 1; index >= 0; --index) result.append(reversed.at(index));
    return result;
}


class CompactScrollStyle final : public QProxyStyle {
public:
    CompactScrollStyle() = default;

    QRect subControlRect(ComplexControl control, const QStyleOptionComplex *option,
                         SubControl subControl, const QWidget *widget = nullptr) const override {
        QRect rect = QProxyStyle::subControlRect(control, option, subControl, widget);
        if (control != CC_ScrollBar || subControl != SC_ScrollBarSlider) return rect;
        const auto *slider = qstyleoption_cast<const QStyleOptionSlider *>(option);
        if (!slider || slider->orientation != Qt::Vertical || rect.height() <= 72) return rect;

        const QRect groove = QProxyStyle::subControlRect(control, option, SC_ScrollBarGroove, widget);
        const int thumbHeight = qMin(72, groove.height());
        const int span = qMax(0, groove.height() - thumbHeight);
        const int offset = QStyle::sliderPositionFromValue(slider->minimum, slider->maximum,
                                                            slider->sliderPosition, span,
                                                            slider->upsideDown);
        return QRect(groove.left(), groove.top() + offset, groove.width(), thumbHeight);
    }
};

class ChatInputEdit final : public QTextEdit {
public:
    explicit ChatInputEdit(QWidget *parent = nullptr) : QTextEdit(parent) {
        // Reserve only the right side for the embedded send button. Keeping the
        // bottom margin at zero guarantees a usable text line at minimum height.
        setViewportMargins(0, 0, 76, 0);
    }
};

QSize measuredBubbleSize(const QString &text, const QFont &font, int maximumBubbleWidth,
                         const QMargins &margins) {
    // V90: measure with QTextDocument, i.e. the same text engine Qt uses for
    // fallback fonts / CJK glyph shaping.  The previous QTextLayout + fixed-size
    // estimate could under-measure the last glyphs on Windows DPI/font fallback
    // combinations and QLabel would then clip the end of the reply.
    constexpr int kHorizontalPaintGuard = 10;
    constexpr int kVerticalPaintGuard = 8;
    const int maximumTextWidth = qMax(120, maximumBubbleWidth - margins.left() - margins.right()
                                             - kHorizontalPaintGuard);
    const QString normalized = QString(text).replace(QStringLiteral("\r\n"), QStringLiteral("\n"));

    QTextDocument naturalDocument;
    naturalDocument.setUndoRedoEnabled(false);
    naturalDocument.setDocumentMargin(0.0);
    naturalDocument.setDefaultFont(font);
    naturalDocument.setPlainText(normalized);
    naturalDocument.setTextWidth(-1.0);
    const int naturalTextWidth = qMax(40, qCeil(naturalDocument.idealWidth()) + 2);

    const int textWidth = qBound(40, naturalTextWidth, maximumTextWidth);
    QTextDocument wrappedDocument;
    wrappedDocument.setUndoRedoEnabled(false);
    wrappedDocument.setDocumentMargin(0.0);
    wrappedDocument.setDefaultFont(font);
    QTextOption option = wrappedDocument.defaultTextOption();
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    wrappedDocument.setDefaultTextOption(option);
    wrappedDocument.setPlainText(normalized);
    wrappedDocument.setTextWidth(textWidth);

    const QSizeF documentSize = wrappedDocument.documentLayout()->documentSize();
    const QFontMetricsF metrics(font);
    const int textHeight = qMax(qCeil(metrics.lineSpacing()), qCeil(documentSize.height()));
    const int bubbleWidth = qMin(maximumBubbleWidth, textWidth + margins.left() + margins.right()
                                                     + kHorizontalPaintGuard);
    const int bubbleHeight = qMax(42, textHeight + margins.top() + margins.bottom()
                                      + kVerticalPaintGuard);
    return QSize(bubbleWidth, bubbleHeight);
}

QString deriveGlobalMemory(const QString &existing, const QJsonArray &facts) {
    const QString title = QStringLiteral("## 结构化事实（派生，勿手工编辑）");
    QString base = existing;
    const qsizetype marker = base.indexOf(title);
    if (marker >= 0) base.truncate(marker);
    QStringList lines;
    lines.append(base.trimmed());
    lines.append(title);
    int count = 0;
    for (const QJsonValue &value : facts) {
        const QJsonObject fact = value.toObject();
        if (fact.value(QStringLiteral("status")).toString() != QStringLiteral("active")) continue;
        lines.append(QStringLiteral("- [%1] %2：%3")
                         .arg(fact.value(QStringLiteral("category")).toString(),
                              fact.value(QStringLiteral("key")).toString(),
                              fact.value(QStringLiteral("value")).toVariant().toString()));
        if (++count >= 40) break;
    }
    return lines.join(QStringLiteral("\n\n")).trimmed().left(24000);
}
} // namespace

ChatWindow::ChatWindow(CoreClient *client, QWidget *parent) : QWidget(parent), m_client(client) {
    setWindowTitle(QStringLiteral("Capricorn 对话"));
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    resize(520, 680);
    setMinimumSize(400, 640);
    setMaximumSize(960, 1080);

    auto *shell = new QFrame(this);
    shell->setObjectName(QStringLiteral("chatApp"));
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(shell);
    auto *layout = new QVBoxLayout(shell);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *titlebar = new QFrame(shell);
    titlebar->setObjectName(QStringLiteral("chatTitlebar"));
    titlebar->setFixedHeight(60);
    titlebar->installEventFilter(this);
    auto *titleLayout = new QHBoxLayout(titlebar);
    titleLayout->setContentsMargins(16, 0, 10, 0);
    titleLayout->setSpacing(12);
    m_identityAvatar = new QLabel(QStringLiteral("♑"), titlebar);
    m_identityAvatar->setObjectName(QStringLiteral("chatIdentityAvatar"));
    m_identityAvatar->setAlignment(Qt::AlignCenter);
    m_identityAvatar->setFixedSize(36, 36);
    auto *identityCopy = new QVBoxLayout;
    identityCopy->setContentsMargins(0, 0, 0, 0);
    identityCopy->setSpacing(0);
    identityCopy->addStretch(1);
    m_petName = new QLabel(QStringLiteral("Capricorn"), titlebar);
    m_petName->setObjectName(QStringLiteral("chatPetName"));
    identityCopy->addWidget(m_petName);
    identityCopy->addStretch(1);
    titleLayout->addWidget(m_identityAvatar);
    titleLayout->addLayout(identityCopy);
    titleLayout->addStretch(1);

    // V90: keep transient model activity in the quiet title-bar area instead of
    // polluting the transcript with synthetic assistant messages.  The opacity
    // pulse is deliberately slow and subtle so it reads like a natural typing
    // indicator rather than an alert/banner.
    m_typingIndicator = new QLabel(QStringLiteral("对方正在输入中..."), titlebar);
    m_typingIndicator->setObjectName(QStringLiteral("chatTypingIndicator"));
    m_typingIndicator->setAlignment(Qt::AlignCenter);
    m_typingIndicator->setMinimumWidth(120);
    m_typingIndicator->setVisible(false);
    m_typingOpacity = new QGraphicsOpacityEffect(m_typingIndicator);
    m_typingOpacity->setOpacity(1.0);
    m_typingIndicator->setGraphicsEffect(m_typingOpacity);
    m_typingPulse = new QPropertyAnimation(m_typingOpacity, "opacity", m_typingIndicator);
    m_typingPulse->setDuration(1900);
    m_typingPulse->setLoopCount(-1);
    m_typingPulse->setKeyValueAt(0.0, 0.56);
    m_typingPulse->setKeyValueAt(0.5, 1.0);
    m_typingPulse->setKeyValueAt(1.0, 0.56);
    m_typingPulse->setEasingCurve(QEasingCurve::InOutSine);
    titleLayout->addWidget(m_typingIndicator, 0, Qt::AlignVCenter);
    titleLayout->addStretch(1);

    m_dockSide = new QPushButton(QStringLiteral("←"), titlebar);
    m_dockSide->setObjectName(QStringLiteral("chatDockSide"));
    m_dockSide->setFixedSize(30, 30);
    m_dockSide->setToolTip(QStringLiteral("切换到桌宠左侧"));
    m_select = new QPushButton(QStringLiteral("选择"), titlebar);
    m_select->setObjectName(QStringLiteral("chatTool"));
    m_selectAll = new QPushButton(QStringLiteral("全选"), titlebar);
    m_selectAll->setObjectName(QStringLiteral("chatTool"));
    m_deleteSelected = new QPushButton(QStringLiteral("删除"), titlebar);
    m_deleteSelected->setObjectName(QStringLiteral("chatDanger"));
    m_cancelSelection = new QPushButton(QStringLiteral("取消"), titlebar);
    m_cancelSelection->setObjectName(QStringLiteral("chatTool"));
    auto *close = new QPushButton(QStringLiteral("×"), titlebar);
    close->setObjectName(QStringLiteral("chatClose"));
    close->setFixedSize(30, 30);
    titleLayout->addWidget(m_dockSide);
    titleLayout->addWidget(m_select);
    titleLayout->addWidget(m_selectAll);
    titleLayout->addWidget(m_deleteSelected);
    titleLayout->addWidget(m_cancelSelection);
    titleLayout->addWidget(close);
    layout->addWidget(titlebar);

    m_history = new QListWidget(shell);
    m_history->setObjectName(QStringLiteral("chatHistory"));
    m_history->setFrameShape(QFrame::NoFrame);
    m_history->setSpacing(14);
    m_history->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_history->setWordWrap(true);
    m_history->setResizeMode(QListView::Adjust);
    m_history->setUniformItemSizes(false);
    m_history->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_history->verticalScrollBar()->setSingleStep(14);
    m_history->verticalScrollBar()->setPageStep(120);
    auto *compactScrollStyle = new CompactScrollStyle;
    compactScrollStyle->setParent(m_history->verticalScrollBar());
    m_history->verticalScrollBar()->setStyle(compactScrollStyle);
    QScroller::grabGesture(m_history->viewport(), QScroller::TouchGesture);
    layout->addWidget(m_history, 1);

    m_compose = new QFrame(shell);
    m_compose->setObjectName(QStringLiteral("chatCompose"));
    auto *composeOuter = new QVBoxLayout(m_compose);
    composeOuter->setContentsMargins(0, 0, 0, 0);
    composeOuter->setSpacing(0);
    m_composeResizeHandle = new QFrame(m_compose);
    m_composeResizeHandle->setObjectName(QStringLiteral("chatComposeResizeHandle"));
    m_composeResizeHandle->setFixedHeight(8);
    m_composeResizeHandle->setCursor(Qt::SizeVerCursor);
    m_composeResizeHandle->installEventFilter(this);
    composeOuter->addWidget(m_composeResizeHandle);
    auto *composeBody = new QWidget(m_compose);
    auto *composeLayout = new QVBoxLayout(composeBody);
    composeLayout->setContentsMargins(18, 6, 18, 16);
    composeLayout->setSpacing(0);
    m_input = new ChatInputEdit(composeBody);
    m_input->setObjectName(QStringLiteral("chatInput"));
    m_input->setPlaceholderText(QStringLiteral("输入消息，Enter 发送，Shift+Enter 换行"));
    m_input->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_input->setMinimumHeight(68);
    m_input->setMaximumHeight(160);
    m_input->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_input->installEventFilter(this);
    composeLayout->addWidget(m_input, 1);
    m_send = new QPushButton(QStringLiteral("发送"), m_input);
    m_send->setObjectName(QStringLiteral("chatSend"));
    m_send->setFixedSize(62, 32);
    m_send->raise();
    composeOuter->addWidget(composeBody, 1);
    m_compose->setMinimumHeight(108);
    m_compose->setMaximumHeight(208);
    m_compose->setFixedHeight(116);
    layout->addWidget(m_compose);

    connect(m_send, &QPushButton::clicked, this, &ChatWindow::sendMessage);
    connect(m_dockSide, &QPushButton::clicked, this, &ChatWindow::dockSideToggleRequested);
    connect(m_select, &QPushButton::clicked, this, &ChatWindow::enterSelectionMode);
    connect(m_selectAll, &QPushButton::clicked, this, &ChatWindow::toggleSelectAll);
    connect(m_deleteSelected, &QPushButton::clicked, this, &ChatWindow::deleteSelected);
    connect(m_cancelSelection, &QPushButton::clicked, this, &ChatWindow::cancelSelectionMode);
    connect(close, &QPushButton::clicked, this, &QWidget::hide);
    connect(m_input, &QTextEdit::textChanged, this, [this] { adjustInputHeight(); updateSendState(); });

    m_replyTimeout = new QTimer(this);
    m_replyTimeout->setSingleShot(true);
    m_replyTimeout->setInterval(150 * 1000);
    connect(m_replyTimeout, &QTimer::timeout, this, [this] {
        // The timeout belongs to the current request token. Clearing the token
        // first makes any late Core reply harmless, even if it arrives while the
        // one-button notice is still visible.
        if (!m_sending || m_activeRequestId.isEmpty()) return;
        QPointer<QNetworkReply> reply = m_activeReply;
        resetPendingReplyState();
        if (reply) reply->abort();
        showFatalModelNotice(QStringLiteral("模型过慢，请更换配置"));
    });

    adjustInputHeight();
    positionSendButton();
    updateSelectionControls();
}

void ChatWindow::setAuditConversation() {
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    m_messages = QJsonArray{
        QJsonObject{{QStringLiteral("id"), QStringLiteral("audit-1")}, {QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("text"), QStringLiteral("哈哈，你好啊！\n\n有什么我能帮到你的吗？不论是技术问题、写代码还是聊聊天、分析点什么，我都可以认真陪你。")}, {QStringLiteral("at"), now - 600000}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("audit-2")}, {QStringLiteral("role"), QStringLiteral("user")}, {QStringLiteral("text"), QStringLiteral("嗯，我很喜欢格温和那里的小蜘蛛。")}, {QStringLiteral("at"), now - 540000}},
        QJsonObject{{QStringLiteral("id"), QStringLiteral("audit-3")}, {QStringLiteral("role"), QStringLiteral("assistant")}, {QStringLiteral("text"), QStringLiteral("这是一条用于验证多行自适应高度的长消息。窗口变窄时，气泡会重新计算宽度和高度，不会遮住最后一行文字。")}, {QStringLiteral("at"), now - 480000}}
    };
    m_selectionMode = false;
    m_selectedIds.clear();
    renderHistory();
}


void ChatWindow::setAuditComposeText() {
    m_input->setPlainText(QStringLiteral("这是一段用于验证输入框纵向扩展的测试文字。\n第二行必须完整显示，发送按钮应处于启用状态。"));
    adjustInputHeight();
    updateSendState();
}

QJsonObject ChatWindow::loadContinuitySnapshot(const QString &personaId, const QString &personaName) {
    const QJsonObject persisted = persistentChatSnapshot(personaId, personaName);
    const QJsonArray messages = persisted.value(QStringLiteral("messages")).toArray();
    const QString relationshipMemory = persisted.value(
        QStringLiteral("relationshipMemoryMarkdown")).toString().left(24000);
    return QJsonObject{
        {QStringLiteral("globalMemoryMarkdown"),
         persisted.value(QStringLiteral("globalMemoryMarkdown")).toString().left(24000)},
        {QStringLiteral("relationshipMemoryMarkdown"), relationshipMemory},
        {QStringLiteral("longMemoryMarkdown"), relationshipMemory},
        {QStringLiteral("globalMemoryRevision"),
         persisted.value(QStringLiteral("globalMemoryRevision")).toInt()},
        {QStringLiteral("relationshipMemoryRevision"),
         persisted.value(QStringLiteral("relationshipMemoryRevision")).toInt()},
        {QStringLiteral("structuredFacts"),
         persisted.value(QStringLiteral("structuredFacts")).toArray()},
        {QStringLiteral("structuredFactsRevision"),
         persisted.value(QStringLiteral("structuredFactsRevision")).toInt()},
        {QStringLiteral("recentMessages"), recentContextMessages(messages)},
        {QStringLiteral("memoryVersion"), 5},
        {QStringLiteral("storage"), QStringLiteral("sqlite")}
    };
}

void ChatWindow::setSession(const QString &sessionId, const QString &personaId, const QString &personaName) {
    const QString normalizedName = personaName.trimmed().isEmpty() ? QStringLiteral("Capricorn") : personaName.trimmed();
    const QString normalizedId = personaId.trimmed().isEmpty() ? normalizedName : personaId.trimmed();
    const bool sessionChangedNow = m_sessionId != sessionId || m_personaId != normalizedId;
    if (sessionChangedNow && m_sending) resetPendingReplyState();
    if (sessionChangedNow && m_memoryRebuilding) {
        m_rebuildRequestId.clear();
        if (m_rebuildReply) m_rebuildReply->abort();
        m_rebuildReply.clear();
        m_memoryRebuilding = false;
        updateSelectionControls();
    }
    m_sessionId = sessionId;
    m_personaId = normalizedId;
    m_personaName = normalizedName;
    setWindowTitle(m_personaName + QStringLiteral(" · Capricorn 对话"));
    m_petName->setText(m_personaName);
    m_identityAvatar->setText(m_personaName.left(1));
    if (sessionChangedNow) {
        m_selectionMode = false;
        m_selectedIds.clear();
        loadHistory();
        renderHistory();
    }
}

void ChatWindow::reloadCurrentSession() {
    m_selectionMode = false;
    m_selectedIds.clear();
    loadHistory();
    renderHistory();
}

void ChatWindow::rebuildCurrentRelationshipMemory(std::function<void(bool, const QString &)> completion) {
    if (m_sending) {
        if (completion) completion(false, QStringLiteral("当前对话尚未完成，请稍后再试。"));
        return;
    }
    if (m_memoryRebuilding) {
        if (completion) completion(false, QStringLiteral("关系记忆正在重建，请稍后再试。"));
        return;
    }
    if (m_sessionId.isEmpty()) {
        if (completion) completion(false, QStringLiteral("当前人格会话不可用，请重新生成桌宠后再试。"));
        return;
    }

    const QString safeId = m_personaId.isEmpty() ? m_personaName : m_personaId;
    const QString personaName = m_personaName;
    const QString sessionId = m_sessionId;
    const int expectedRevision = m_relationshipMemoryRevision;
    const QJsonArray source = memorySourceMessages();
    m_memoryRebuilding = true;
    m_rebuildRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString requestId = m_rebuildRequestId;
    updateSelectionControls();

    QPointer<ChatWindow> self(this);
    m_rebuildReply = m_client->rebuildMemory(sessionId, source,
        [self, safeId, personaName, sessionId, requestId, expectedRevision,
         completion = std::move(completion)](bool ok, const QJsonObject &object, const QString &error) mutable {
            if (!self || self->m_rebuildRequestId != requestId
                || self->m_sessionId != sessionId
                || (self->m_personaId.isEmpty() ? self->m_personaName : self->m_personaId) != safeId)
                return;
            self->m_rebuildReply.clear();
            auto finish = [self, requestId, completion = std::move(completion)](
                              bool succeeded, const QString &failure) mutable {
                if (!self || self->m_rebuildRequestId != requestId) return;
                self->m_rebuildRequestId.clear();
                self->m_rebuildReply.clear();
                self->m_memoryRebuilding = false;
                self->updateSelectionControls();
                if (completion) completion(succeeded, failure);
            };
            if (!ok) {
                finish(false, error.trimmed().isEmpty()
                                  ? QStringLiteral("Core 未能完成关系记忆重建。") : error.trimmed());
                return;
            }
            QString memory = object.value(QStringLiteral("relationshipMemoryMarkdown")).toString();
            if (memory.isEmpty()) memory = object.value(QStringLiteral("longMemoryMarkdown")).toString();
            if (memory.isEmpty()) memory = object.value(QStringLiteral("memorySummary")).toString();
            memory = memory.trimmed().left(24000);
            const QString transactionId = object.value(QStringLiteral("transactionId")).toString();
            const int returnedRevision = object.value(QStringLiteral("relationshipMemoryRevision")).toInt(-1);
            if (transactionId.isEmpty() || returnedRevision != expectedRevision + 1) {
                finish(false, QStringLiteral("Core 返回的重建事务或记忆版本无效，未保存也未刷新活动会话。"));
                return;
            }
            if (!ChatStore::instance().compareAndSwapRelationshipMemory(
                    safeId, personaName, memory, expectedRevision, returnedRevision)) {
                finish(false, QStringLiteral("对话期间记忆版本已变化，未覆盖较新的关系记忆，活动会话也未刷新。"));
                return;
            }
            if (!self || self->m_rebuildRequestId != requestId || self->m_sessionId != sessionId
                || (self->m_personaId.isEmpty() ? self->m_personaName : self->m_personaId) != safeId) return;
            self->m_rebuildReply = self->m_client->acknowledgeMemoryRebuild(
                transactionId, [self, safeId, sessionId, requestId, returnedRevision,
                                finish = std::move(finish)](bool acked, const QJsonObject &, const QString &ackError) mutable {
                    if (!self || self->m_rebuildRequestId != requestId || self->m_sessionId != sessionId
                        || (self->m_personaId.isEmpty() ? self->m_personaName : self->m_personaId) != safeId) return;
                    if (!acked) {
                        self->m_sessionId.clear();
                        emit self->sessionChanged(QString());
                        finish(false, QStringLiteral("关系记忆已保存，但 Core 活动会话提交失败：%1。当前会话已失效，请重新生成桌宠。")
                                          .arg(ackError.trimmed().isEmpty() ? QStringLiteral("未知错误") : ackError.trimmed()));
                        return;
                    }
                    const ChatStoreSnapshot stored = ChatStore::instance().load(safeId);
                    if (stored.storeAvailable && stored.exists && stored.relationshipMemoryRevision == returnedRevision) {
                        self->m_relationshipMemory = stored.relationshipMemoryMarkdown;
                        self->m_relationshipMemoryRevision = stored.relationshipMemoryRevision;
                    }
                    finish(true, QString());
                });
        });
}

void ChatWindow::setAlwaysOnTop(bool enabled) {
    m_alwaysOnTop = enabled;
    if (m_modalBlocked) return;
    applyChatWindowTopmost(this, enabled, !enabled);
}

void ChatWindow::setModalBlocked(bool blocked) {
    if (m_modalBlocked == blocked) return;
    m_modalBlocked = blocked;
    setEnabled(!blocked);
    const bool desiredTopmost = !blocked && m_alwaysOnTop;
    applyChatWindowTopmost(this, desiredTopmost, !blocked && !desiredTopmost);
}

void ChatWindow::setDockSideRight(bool right) {
    m_dockSideRight = right;
    if (!m_dockSide) return;
    m_dockSide->setText(right ? QStringLiteral("←") : QStringLiteral("→"));
    m_dockSide->setToolTip(right ? QStringLiteral("切换到桌宠左侧")
                                  : QStringLiteral("切换到桌宠右侧"));
}

bool ChatWindow::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_composeResizeHandle) {
        auto *mouse = dynamic_cast<QMouseEvent *>(event);
        if (event->type() == QEvent::MouseButtonPress && mouse && mouse->button() == Qt::LeftButton) {
            m_composeResizing = true;
            m_composeResizeStartY = mouse->globalPosition().toPoint().y();
            m_composeResizeStartHeight = m_compose->height();
            return true;
        }
        if (event->type() == QEvent::MouseMove && mouse && m_composeResizing && mouse->buttons().testFlag(Qt::LeftButton)) {
            const int delta = m_composeResizeStartY - mouse->globalPosition().toPoint().y();
            setComposeHeight(m_composeResizeStartHeight + delta, true);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && mouse && mouse->button() == Qt::LeftButton) {
            m_composeResizing = false;
            return true;
        }
    }
    if (watched == m_input && event->type() == QEvent::Resize) {
        positionSendButton();
    }
    if (watched == m_input && event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return && !key->modifiers().testFlag(Qt::ShiftModifier)) {
            sendMessage();
            return true;
        }
    }
    auto *widget = qobject_cast<QWidget *>(watched);
    if (widget && widget->objectName() == QStringLiteral("chatTitlebar")) {
        auto *mouse = dynamic_cast<QMouseEvent *>(event);
        if (event->type() == QEvent::MouseButtonPress && mouse && mouse->button() == Qt::LeftButton) {
            m_dragging = true;
            m_dragOffset = mouse->globalPosition().toPoint() - frameGeometry().topLeft();
            return true;
        }
        if (event->type() == QEvent::MouseMove && mouse && m_dragging && mouse->buttons().testFlag(Qt::LeftButton)) {
            move(mouse->globalPosition().toPoint() - m_dragOffset);
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && mouse && mouse->button() == Qt::LeftButton) {
            m_dragging = false;
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChatWindow::mousePressEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton && !isMaximized()) {
        constexpr int edge = 9;
        const QPoint p = event->position().toPoint();
        Qt::Edges edges;
        if (p.x() <= edge) edges |= Qt::LeftEdge;
        if (p.x() >= width() - edge - 1) edges |= Qt::RightEdge;
        if (p.y() <= edge) edges |= Qt::TopEdge;
        if (p.y() >= height() - edge - 1) edges |= Qt::BottomEdge;
        if (edges != Qt::Edges() && windowHandle() && windowHandle()->startSystemResize(edges)) {
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && event->position().y() <= 60) {
        m_dragging = true;
        m_dragOffset = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void ChatWindow::mouseMoveEvent(QMouseEvent *event) {
    if (!isMaximized() && event->buttons() == Qt::NoButton) {
        constexpr int edge = 9;
        const QPoint p = event->position().toPoint();
        const bool left = p.x() <= edge;
        const bool right = p.x() >= width() - edge - 1;
        const bool top = p.y() <= edge;
        const bool bottom = p.y() >= height() - edge - 1;
        if ((left && top) || (right && bottom)) setCursor(Qt::SizeFDiagCursor);
        else if ((right && top) || (left && bottom)) setCursor(Qt::SizeBDiagCursor);
        else if (left || right) setCursor(Qt::SizeHorCursor);
        else if (top || bottom) setCursor(Qt::SizeVerCursor);
        else unsetCursor();
    }
    if (m_dragging && event->buttons().testFlag(Qt::LeftButton)) move(event->globalPosition().toPoint() - m_dragOffset);
    QWidget::mouseMoveEvent(event);
}

void ChatWindow::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() == Qt::LeftButton) m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

void ChatWindow::resizeEvent(QResizeEvent *event) {
    QWidget::resizeEvent(event);
    emit geometryChanged(geometry());
    if (m_history && m_history->verticalScrollBar())
        m_history->verticalScrollBar()->setPageStep(qMax(90, m_history->viewport()->height() / 3));
    QTimer::singleShot(0, this, &ChatWindow::updateMessageGeometry);
    adjustInputHeight();
    positionSendButton();
}

void ChatWindow::moveEvent(QMoveEvent *event) {
    QWidget::moveEvent(event);
    emit geometryChanged(geometry());
}

void ChatWindow::showEvent(QShowEvent *event) {
    QWidget::showEvent(event);
    if (!m_modalBlocked) applyChatWindowTopmost(this, m_alwaysOnTop, false);
    emit visibilityChanged(true);
}

void ChatWindow::hideEvent(QHideEvent *event) {
    QWidget::hideEvent(event);
    emit visibilityChanged(false);
}

bool ChatWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG *msg = static_cast<MSG *>(message);
    if (msg && msg->message == WM_NCHITTEST && !isMaximized()) {
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

void ChatWindow::sendMessage() {
    const QString text = m_input->toPlainText().trimmed();
    if (text.isEmpty() || m_sessionId.isEmpty() || m_sending || m_selectionMode || m_memoryRebuilding) return;

    m_sending = true;
    m_activeRequestId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString requestId = m_activeRequestId;
    m_input->clear();
    m_input->setEnabled(false);
    updateSendState();
    addMessage(QStringLiteral("user"), text);
    setTypingVisible(true);
    m_replyTimeout->start();

    const int relationshipRevision = m_relationshipMemoryRevision;
    const int factsRevision = m_structuredFactsRevision;
    const QString personaId = m_personaId.isEmpty() ? m_personaName : m_personaId;
    const QString personaName = m_personaName;
    QPointer<ChatWindow> self(this);
    m_activeReply = m_client->chat(m_sessionId, text, m_structuredFacts, factsRevision,
                   [self, requestId, personaId, personaName, relationshipRevision, factsRevision](bool ok, const QJsonObject &object, const QString &) {
        if (!self) return;
        const bool active = self->m_activeRequestId == requestId;
        if (!active) return;
        self->m_activeReply.clear();

        if (!ok) {
            self->resetPendingReplyState();
            self->showFatalModelNotice(QStringLiteral("模型错误，请更换配置"));
            return;
        }

        const QString reply = object.value(QStringLiteral("text")).toString().trimmed();
        if (reply.isEmpty()) {
            self->resetPendingReplyState();
            self->showFatalModelNotice(QStringLiteral("模型错误，请更换配置"));
            return;
        }

        if (self->m_replyTimeout) self->m_replyTimeout->stop();
        self->setTypingVisible(false);

        const QString returnedSession = object.value(QStringLiteral("sessionId")).toString();
        if (!returnedSession.isEmpty() && returnedSession != self->m_sessionId) {
            self->m_sessionId = returnedSession;
            emit self->sessionChanged(returnedSession);
        }
        self->persistResponseMemory(object, personaId, personaName,
                                    relationshipRevision, factsRevision, true);

        self->addMessage(QStringLiteral("assistant"), reply);
        emit self->assistantReplied(reply, qBound(1400, int(reply.size()) * 95, 9000));
        const QString transactionId = object.value(QStringLiteral("transactionId")).toString();
        auto finish = [self, requestId, personaId](bool ok) {
            if (!self || self->m_activeRequestId != requestId) return;
            self->m_activeRequestId.clear();
            self->m_sending = false;
            self->m_input->setEnabled(true);
            self->adjustInputHeight();
            self->updateSendState();
            self->m_input->setFocus();
            if (ok) emit self->chatTurnCommitted(personaId);
            else self->showFatalModelNotice(QStringLiteral("会话提交失败，请重新打开桌宠"));
        };
        if (transactionId.isEmpty()) {
            finish(true);
        } else {
            self->m_client->acknowledgeChat(transactionId,
                [finish = std::move(finish)](bool acked, const QJsonObject &, const QString &) mutable {
                    finish(acked);
                });
        }
    });
}

void ChatWindow::setTypingVisible(bool visible) {
    if (!m_typingIndicator || !m_typingPulse || !m_typingOpacity) return;
    if (visible) {
        m_typingIndicator->setVisible(true);
        m_typingOpacity->setOpacity(0.38);
        if (m_typingPulse->state() != QAbstractAnimation::Running) m_typingPulse->start();
    } else {
        m_typingPulse->stop();
        m_typingOpacity->setOpacity(1.0);
        m_typingIndicator->setVisible(false);
    }
}

void ChatWindow::resetPendingReplyState() {
    if (m_replyTimeout) m_replyTimeout->stop();
    m_activeReply.clear();
    m_activeRequestId.clear();
    m_sending = false;
    setTypingVisible(false);
    if (m_input) {
        m_input->setEnabled(!m_selectionMode && !m_memoryRebuilding);
        adjustInputHeight();
        updateSendState();
    }
}

void ChatWindow::showFatalModelNotice(const QString &message) {
    if (m_fatalModelNoticeOpen) return;
    m_fatalModelNoticeOpen = true;

    auto *dialog = new QDialog(this);
    dialog->setObjectName(QStringLiteral("modelNoticeDialog"));
    dialog->setProperty("capricornUnified", true);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    dialog->setAttribute(Qt::WA_DeleteOnClose, true);
    dialog->setAttribute(Qt::WA_TranslucentBackground, true);
    dialog->setMinimumWidth(360);
    auto *outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(12, 12, 12, 12);
    auto *card = new QFrame(dialog);
    card->setObjectName(QStringLiteral("modelNoticeCard"));
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(20, 18, 20, 16);
    layout->setSpacing(11);
    auto *heading = new QLabel(QStringLiteral("提示"), card);
    heading->setObjectName(QStringLiteral("modelNoticeTitleError"));
    auto *body = new QLabel(message, card);
    body->setObjectName(QStringLiteral("modelNoticeBody"));
    body->setWordWrap(true);
    auto *buttonRow = new QHBoxLayout;
    buttonRow->addStretch();
    auto *ok = new QPushButton(QStringLiteral("知道了"), card);
    ok->setObjectName(QStringLiteral("modelNoticeOk"));
    ok->setFixedSize(82, 34);
    connect(ok, &QPushButton::clicked, dialog, &QDialog::accept);
    buttonRow->addWidget(ok);
    layout->addWidget(heading);
    layout->addWidget(body);
    layout->addLayout(buttonRow);
    outer->addWidget(card);
    connect(dialog, &QDialog::finished, this, [this](int) {
        m_fatalModelNoticeOpen = false;
        emit closePetRequested();
    });
    dialog->open();
}

void ChatWindow::addMessage(const QString &role, const QString &text) {
    const QJsonObject message{{QStringLiteral("id"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
                              {QStringLiteral("role"), role},
                              {QStringLiteral("text"), text.left(64000)},
                              {QStringLiteral("at"), QDateTime::currentMSecsSinceEpoch()}};
    m_messages.append(message);
    while (m_messages.size() > 1000) m_messages.removeAt(0);
    const QString safeId = m_personaId.isEmpty() ? m_personaName : m_personaId;
    if (!ChatStore::instance().appendMessage(safeId, m_personaName, message)) saveHistory();
    if (role == QStringLiteral("user")) emit chatHistoryChanged(safeId);
    renderHistory();
}

void ChatWindow::loadHistory() {
    const QJsonObject snapshot = persistentChatSnapshot(m_personaId, m_personaName);
    m_messages = normalizedStoredMessages(snapshot.value(QStringLiteral("messages")).toArray());
    m_globalUserMemory = snapshot.value(QStringLiteral("globalMemoryMarkdown")).toString().left(24000);
    m_relationshipMemory = snapshot.value(QStringLiteral("relationshipMemoryMarkdown")).toString().left(24000);
    m_structuredFacts = snapshot.value(QStringLiteral("structuredFacts")).toArray();
    m_globalMemoryRevision = qMax(0, snapshot.value(QStringLiteral("globalMemoryRevision")).toInt());
    m_relationshipMemoryRevision = qMax(0, snapshot.value(QStringLiteral("relationshipMemoryRevision")).toInt());
    m_structuredFactsRevision = qMax(0, snapshot.value(QStringLiteral("structuredFactsRevision")).toInt());
}

bool ChatWindow::saveHistory() const {
    const QString safeId = m_personaId.isEmpty() ? m_personaName : m_personaId;
    const bool relationshipSaved = ChatStore::instance().replaceSnapshot(
        safeId, m_personaName, m_messages, m_relationshipMemory, m_relationshipMemoryRevision);
    const bool globalSaved = ChatStore::instance().updateGlobalMemory(
        m_globalUserMemory, m_globalMemoryRevision);
    if (relationshipSaved && globalSaved) return true;
    QSettings settings(QStringLiteral("Capricorn"), QStringLiteral("Capricorn"));
    const QString key = QStringLiteral("chat/%1").arg(safeId);
    const QString memoryKey = QStringLiteral("chat-memory/%1").arg(safeId);
    settings.setValue(key, QJsonDocument(m_messages).toJson(QJsonDocument::Compact));
    if (m_relationshipMemory.isEmpty()) settings.remove(memoryKey);
    else settings.setValue(memoryKey, m_relationshipMemory);
    settings.setValue(QStringLiteral("chat-memory-revision/%1").arg(safeId),
                      m_relationshipMemoryRevision);
    settings.sync();
    return relationshipSaved;
}

void ChatWindow::persistResponseMemory(const QJsonObject &response, const QString &personaId,
                                       const QString &personaName, int relationshipRevision,
                                       int factsRevision,
                                       bool updateLocalState) {
    ChatStore &store = ChatStore::instance();
    const QString responseGlobalMemory = response.value(
        QStringLiteral("globalMemoryMarkdown")).toString().trimmed().left(24000);

    const int responseRelationshipRevision = response.value(
        QStringLiteral("relationshipMemoryRevision")).toInt(relationshipRevision + 1);
    QString relationshipMemory = response.value(
        QStringLiteral("relationshipMemoryMarkdown")).toString();
    if (relationshipMemory.isEmpty())
        relationshipMemory = response.value(QStringLiteral("longMemoryMarkdown")).toString();
    if (relationshipMemory.isEmpty())
        relationshipMemory = response.value(QStringLiteral("memorySummary")).toString();
    relationshipMemory = relationshipMemory.trimmed().left(24000);
    const bool relationshipSaved = !relationshipMemory.isEmpty()
        && store.compareAndSwapRelationshipMemory(personaId, personaName, relationshipMemory,
                                                  relationshipRevision, responseRelationshipRevision);

    QJsonArray factUpdates = response.value(QStringLiteral("factUpdates")).toArray();
    if (factUpdates.isEmpty())
        factUpdates = response.value(QStringLiteral("structuredFactUpdates")).toArray();
    const int responseFactsRevision = response.value(
        QStringLiteral("structuredFactsRevision")).toInt(factsRevision + 1);
    const bool factsSaved = !factUpdates.isEmpty()
        && store.applyStructuredFactUpdates(factUpdates, factsRevision, responseFactsRevision);

    ChatStoreSnapshot finalSnapshot = store.load(personaId);
    bool globalSaved = false;
    for (int attempt = 0; finalSnapshot.storeAvailable && attempt < 3
         && (!responseGlobalMemory.isEmpty() || factsSaved); ++attempt) {
        const QString baseMemory = factsSaved ? finalSnapshot.globalMemoryMarkdown
                                              : responseGlobalMemory;
        const QString globalMemory = deriveGlobalMemory(baseMemory, finalSnapshot.structuredFacts);
        globalSaved = store.compareAndSwapGlobalMemory(
            globalMemory, finalSnapshot.globalMemoryRevision, finalSnapshot.globalMemoryRevision + 1);
        finalSnapshot = store.load(personaId);
        if (globalSaved) break;
    }

    if (!updateLocalState) return;
    Q_UNUSED(globalSaved)
    Q_UNUSED(relationshipSaved)
    Q_UNUSED(factsSaved)
    const QString currentPersonaId = m_personaId.isEmpty() ? m_personaName : m_personaId;
    if (currentPersonaId != personaId) return;
    const ChatStoreSnapshot refreshed = store.load(personaId);
    if (!refreshed.storeAvailable) return;
    m_globalUserMemory = refreshed.globalMemoryMarkdown;
    m_globalMemoryRevision = refreshed.globalMemoryRevision;
    m_relationshipMemory = refreshed.relationshipMemoryMarkdown;
    m_relationshipMemoryRevision = refreshed.relationshipMemoryRevision;
    m_structuredFacts = refreshed.structuredFacts;
    m_structuredFactsRevision = refreshed.structuredFactsRevision;
}

void ChatWindow::persistGlobalMemory(const QString &memory, int revision) {
    const QString normalized = memory.trimmed().left(24000);
    if (normalized.isEmpty()) return;
    m_globalUserMemory = normalized;
    m_globalMemoryRevision = qMax(m_globalMemoryRevision + 1, revision);
    ChatStore::instance().updateGlobalMemory(normalized, m_globalMemoryRevision);
}

void ChatWindow::persistRelationshipMemory(const QString &memory, int revision) {
    const QString normalized = memory.trimmed().left(24000);
    if (normalized.isEmpty()) return;
    m_relationshipMemory = normalized;
    m_relationshipMemoryRevision = qMax(m_relationshipMemoryRevision + 1, revision);
    const QString safeId = m_personaId.isEmpty() ? m_personaName : m_personaId;
    if (ChatStore::instance().updateRelationshipMemory(
            safeId, m_personaName, normalized, m_relationshipMemoryRevision)) return;
    QSettings settings(QStringLiteral("Capricorn"), QStringLiteral("Capricorn"));
    settings.setValue(QStringLiteral("chat-memory/%1").arg(safeId), normalized);
    settings.setValue(QStringLiteral("chat-memory-revision/%1").arg(safeId),
                      m_relationshipMemoryRevision);
}

QJsonArray ChatWindow::memorySourceMessages(const QSet<QString> &excludedIds) const {
    QJsonArray source;
    bool waitingForAssistant = false;
    for (const QJsonValue &value : m_messages) {
        const QJsonObject item = value.toObject();
        const QString role = item.value(QStringLiteral("role")).toString();
        const QString text = item.value(QStringLiteral("text")).toString().trimmed();
        if (text.isEmpty()) continue;
        const QString id = item.value(QStringLiteral("id")).toString();
        if (role == QStringLiteral("user")) {
            const bool excluded = excludedIds.contains(id);
            if (!excluded) {
                source.append(QJsonObject{{QStringLiteral("id"), id},
                                          {QStringLiteral("role"), role},
                                          {QStringLiteral("text"), text.left(12000)},
                                          {QStringLiteral("at"), item.value(QStringLiteral("at"))}});
            }
            // An assistant reply is memory evidence only when its own user turn is
            // still present. This prevents an old assistant echo from reviving a
            // user message that the user explicitly deleted.
            waitingForAssistant = !excluded;
        } else if (role == QStringLiteral("assistant")) {
            if (waitingForAssistant && !excludedIds.contains(id)) {
                source.append(QJsonObject{{QStringLiteral("id"), id},
                                          {QStringLiteral("role"), role},
                                          {QStringLiteral("text"), text.left(12000)},
                                          {QStringLiteral("at"), item.value(QStringLiteral("at"))}});
            }
            waitingForAssistant = false;
        }
    }
    return source;
}

void ChatWindow::renderHistory(bool preserveScroll) {
    QScrollBar *scrollBar = m_history ? m_history->verticalScrollBar() : nullptr;
    const int previousValue = scrollBar ? scrollBar->value() : 0;
    const bool wasNearBottom = !scrollBar || (scrollBar->maximum() - scrollBar->value() <= 24);
    m_history->clear();
    if (m_messages.isEmpty()) {
        auto *item = new QListWidgetItem;
        item->setFlags(Qt::NoItemFlags);
        auto *empty = new QLabel(QStringLiteral("还没有聊天记录"));
        empty->setObjectName(QStringLiteral("chatEmpty"));
        empty->setAlignment(Qt::AlignCenter);
        item->setSizeHint(QSize(100, 360));
        m_history->addItem(item);
        m_history->setItemWidget(item, empty);
        updateSelectionControls();
        return;
    }

    const int viewportWidth = qMax(360, m_history->viewport()->width());
    const int maximumBubbleWidth = qBound(180, viewportWidth - 178, 390);

    for (const QJsonValue &value : m_messages) {
        const QJsonObject message = value.toObject();
        const bool user = message.value(QStringLiteral("role")).toString() == QStringLiteral("user");
        const QString id = message.value(QStringLiteral("id")).toString();
        const QString messageText = message.value(QStringLiteral("text")).toString();

        auto *item = new QListWidgetItem;
        item->setFlags(Qt::NoItemFlags);
        auto *row = new QWidget;
        row->setObjectName(user ? QStringLiteral("chatMessageUser") : QStringLiteral("chatMessageAssistant"));
        row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 0, 12, 0);
        rowLayout->setSpacing(10);
        rowLayout->setAlignment(Qt::AlignTop);

        auto *select = new QCheckBox(row);
        select->setVisible(m_selectionMode);
        select->setChecked(m_selectedIds.contains(id));
        connect(select, &QCheckBox::toggled, this, [this, id](bool checked) {
            if (checked) m_selectedIds.insert(id); else m_selectedIds.remove(id);
            updateSelectionControls();
        });

        auto *avatar = new QLabel(user ? QStringLiteral("我") : m_personaName.left(1), row);
        avatar->setObjectName(user ? QStringLiteral("chatAvatarUser") : QStringLiteral("chatAvatarAssistant"));
        avatar->setAlignment(Qt::AlignCenter);
        avatar->setFixedSize(40, 40);

        auto *contentHost = new QWidget(row);
        auto *content = new QVBoxLayout(contentHost);
        content->setContentsMargins(0, 0, 0, 0);
        content->setSpacing(4);

        auto *bubble = new QLabel(messageText, contentHost);
        bubble->setObjectName(user ? QStringLiteral("chatBubbleUser") : QStringLiteral("chatBubbleAssistant"));
        bubble->setWordWrap(true);
        bubble->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bubble->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        bubble->setTextFormat(Qt::PlainText);
        bubble->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        bubble->setContentsMargins(14, 10, 14, 10);
        bubble->ensurePolished();
        const QSize bubbleSize = measuredBubbleSize(messageText, bubble->font(), maximumBubbleWidth,
                                                    bubble->contentsMargins());
        bubble->setFixedSize(bubbleSize);

        auto *meta = new QLabel(messageTime(qint64(message.value(QStringLiteral("at")).toDouble())), contentHost);
        meta->setObjectName(QStringLiteral("chatMessageMeta"));
        meta->setAlignment(user ? Qt::AlignRight : Qt::AlignLeft);
        content->addWidget(bubble, 0, user ? Qt::AlignRight : Qt::AlignLeft);
        content->addWidget(meta);

        const int contentHeight = bubbleSize.height() + 4 + meta->sizeHint().height();
        contentHost->setFixedSize(bubbleSize.width(), contentHeight);
        const int rowHeight = qMax(40, contentHeight);
        row->setFixedHeight(rowHeight + 4);

        if (user) {
            rowLayout->addStretch(1);
            rowLayout->addWidget(contentHost, 0, Qt::AlignTop);
            rowLayout->addWidget(avatar, 0, Qt::AlignTop);
            if (m_selectionMode) rowLayout->addWidget(select, 0, Qt::AlignTop);
        } else {
            if (m_selectionMode) rowLayout->addWidget(select, 0, Qt::AlignTop);
            rowLayout->addWidget(avatar, 0, Qt::AlignTop);
            rowLayout->addWidget(contentHost, 0, Qt::AlignTop);
            rowLayout->addStretch(1);
        }

        item->setSizeHint(QSize(qMax(1, viewportWidth - 24), rowHeight + 8));
        m_history->addItem(item);
        m_history->setItemWidget(item, row);
    }
    updateSelectionControls();
    QPointer<ChatWindow> self(this);
    QTimer::singleShot(0, this, [self, preserveScroll, previousValue, wasNearBottom] {
        if (!self || !self->m_history) return;
        QScrollBar *bar = self->m_history->verticalScrollBar();
        if (!bar) return;
        if (preserveScroll && !wasNearBottom) bar->setValue(qMin(previousValue, bar->maximum()));
        else bar->setValue(bar->maximum());
    });
}

void ChatWindow::updateMessageGeometry() {
    if (m_messages.isEmpty() || !m_history || m_history->viewport()->width() <= 0) return;
    renderHistory(true);
}

void ChatWindow::updateSelectionControls() {
    m_select->setVisible(!m_selectionMode);
    m_select->setEnabled(!m_messages.isEmpty());
    m_selectAll->setVisible(m_selectionMode);
    m_deleteSelected->setVisible(m_selectionMode);
    m_cancelSelection->setVisible(m_selectionMode);
    m_deleteSelected->setEnabled(!m_sending && !m_memoryRebuilding && !m_selectedIds.isEmpty());
    m_selectAll->setText(!m_messages.isEmpty() && m_selectedIds.size() == m_messages.size() ? QStringLiteral("取消全选") : QStringLiteral("全选"));
    m_input->setEnabled(!m_selectionMode && !m_sending && !m_memoryRebuilding);
    updateSendState();
}

void ChatWindow::updateSendState() {
    const bool hasText = !m_input->toPlainText().trimmed().isEmpty();
    m_send->setEnabled(!m_selectionMode && !m_sending && !m_memoryRebuilding && hasText);
}

void ChatWindow::adjustInputHeight() {
    if (!m_input || !m_compose) return;
    const int documentWidth = qMax(140, m_input->viewport()->width() - 10);
    m_input->document()->setTextWidth(documentWidth);
    const int wantedInput = qBound(68,
                                    qCeil(m_input->document()->documentLayout()->documentSize().height()) + 28,
                                    160);
    const int wantedCompose = qBound(108, wantedInput + 48, 208);
    if (!m_composeManuallySized || wantedCompose > m_compose->height())
        setComposeHeight(wantedCompose, false);
}

void ChatWindow::setComposeHeight(int height, bool manual) {
    if (!m_compose || !m_input) return;
    const int clampedCompose = qBound(108, height, 208);
    if (manual) m_composeManuallySized = true;
    m_compose->setFixedHeight(clampedCompose);
    m_input->setFixedHeight(qBound(68, clampedCompose - 48, 160));
    positionSendButton();
}

void ChatWindow::positionSendButton() {
    if (!m_input || !m_send) return;
    const int x = qMax(8, m_input->width() - m_send->width() - 10);
    const int y = qMax(8, m_input->height() - m_send->height() - 8);
    m_send->move(x, y);
    m_send->raise();
}

void ChatWindow::enterSelectionMode() {
    m_selectionMode = true;
    m_selectedIds.clear();
    renderHistory();
}

void ChatWindow::cancelSelectionMode() {
    m_selectionMode = false;
    m_selectedIds.clear();
    renderHistory();
}

void ChatWindow::toggleSelectAll() {
    if (m_selectedIds.size() == m_messages.size()) m_selectedIds.clear();
    else for (const QJsonValue &value : m_messages) m_selectedIds.insert(value.toObject().value(QStringLiteral("id")).toString());
    renderHistory();
}

void ChatWindow::deleteSelected() {
    if (m_sending || m_memoryRebuilding || m_selectedIds.isEmpty()) return;
    const QJsonArray previousMessages = m_messages;
    QJsonArray kept;
    for (const QJsonValue &value : m_messages) {
        if (!m_selectedIds.contains(value.toObject().value(QStringLiteral("id")).toString())) kept.append(value);
    }
    m_messages = kept;
    if (!saveHistory()) {
        m_messages = previousMessages;
        showFatalModelNotice(QStringLiteral("聊天记录保存失败，未执行删除。"));
        return;
    }
    m_selectedIds.clear();
    m_selectionMode = false;

    emit chatHistoryChanged(m_personaId.isEmpty() ? m_personaName : m_personaId);
    renderHistory();
    updateSelectionControls();
    QPointer<ChatWindow> self(this);
    rebuildCurrentRelationshipMemory([self](bool ok, const QString &error) {
        if (self && !ok) self->showFatalModelNotice(
            error.isEmpty() ? QStringLiteral("记忆重建失败，已保留原有记忆") : error);
    });
}
