#pragma once

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QPoint>
#include <QPointer>
#include <QRect>
#include <QSet>
#include <QWidget>
#include <functional>

class CoreClient;
class QEvent;
class QFrame;
class QMouseEvent;
class QMoveEvent;
class QNetworkReply;
class QShowEvent;
class QHideEvent;
class QLabel;
class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;
class QListWidget;
class QPushButton;
class QTextEdit;
class QResizeEvent;

class ChatWindow final : public QWidget {
    Q_OBJECT
public:
    explicit ChatWindow(CoreClient *client, QWidget *parent = nullptr);
    void setSession(const QString &sessionId, const QString &personaId, const QString &personaName);
    static QJsonObject loadContinuitySnapshot(const QString &personaId, const QString &personaName);
    void setAuditConversation();
    void setAuditComposeText();
    void setAlwaysOnTop(bool enabled);
    void setModalBlocked(bool blocked);
    void setDockSideRight(bool right);
    void reloadCurrentSession();
    bool isSending() const { return m_sending; }
    bool isMemoryRebuilding() const { return m_memoryRebuilding; }
    void rebuildCurrentRelationshipMemory(std::function<void(bool, const QString &)> completion = {});

signals:
    void assistantReplied(const QString &text, int durationMs);
    void geometryChanged(const QRect &geometry);
    void dockSideToggleRequested();
    void visibilityChanged(bool visible);
    void sessionChanged(const QString &sessionId);
    void chatHistoryChanged(const QString &personaId);
    void chatTurnCommitted(const QString &personaId);
    void closePetRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void sendMessage();
    void addMessage(const QString &role, const QString &text);
    void loadHistory();
    bool saveHistory() const;
    void renderHistory(bool preserveScroll = false);
    void updateMessageGeometry();
    void updateSelectionControls();
    void updateSendState();
    void adjustInputHeight();
    void setComposeHeight(int height, bool manual);
    void positionSendButton();
    void enterSelectionMode();
    void cancelSelectionMode();
    void toggleSelectAll();
    void deleteSelected();
    QJsonArray memorySourceMessages(const QSet<QString> &excludedIds = {}) const;
    void persistGlobalMemory(const QString &memory, int revision);
    void persistRelationshipMemory(const QString &memory, int revision);
    void persistResponseMemory(const QJsonObject &response, const QString &personaId,
                               const QString &personaName, int relationshipRevision, int factsRevision,
                               bool updateLocalState);
    void setTypingVisible(bool visible);
    void resetPendingReplyState();
    void showFatalModelNotice(const QString &message);

    CoreClient *m_client{};
    QLabel *m_identityAvatar{};
    QLabel *m_petName{};
    QLabel *m_typingIndicator{};
    QGraphicsOpacityEffect *m_typingOpacity{};
    QPropertyAnimation *m_typingPulse{};
    QTimer *m_replyTimeout{};
    QPointer<QNetworkReply> m_activeReply;
    QPointer<QNetworkReply> m_rebuildReply;
    QListWidget *m_history{};
    QTextEdit *m_input{};
    QFrame *m_compose{};
    QFrame *m_composeResizeHandle{};
    QPushButton *m_send{};
    QPushButton *m_dockSide{};
    QPushButton *m_select{};
    QPushButton *m_selectAll{};
    QPushButton *m_deleteSelected{};
    QPushButton *m_cancelSelection{};
    QJsonArray m_messages;
    QSet<QString> m_selectedIds;
    QString m_sessionId;
    QString m_personaId;
    QString m_personaName;
    QString m_globalUserMemory;
    QString m_relationshipMemory;
    QJsonArray m_structuredFacts;
    QString m_activeRequestId;
    QString m_rebuildRequestId;
    bool m_selectionMode{false};
    bool m_sending{false};
    bool m_memoryRebuilding{false};
    bool m_fatalModelNoticeOpen{false};
    bool m_dragging{false};
    bool m_dockSideRight{true};
    bool m_alwaysOnTop{false};
    bool m_modalBlocked{false};
    bool m_composeResizing{false};
    bool m_composeManuallySized{false};
    int m_globalMemoryRevision{0};
    int m_relationshipMemoryRevision{0};
    int m_structuredFactsRevision{0};
    int m_composeResizeStartY{};
    int m_composeResizeStartHeight{};
    QPoint m_dragOffset;
};
