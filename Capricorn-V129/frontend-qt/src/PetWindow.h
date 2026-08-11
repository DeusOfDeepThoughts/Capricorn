#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSvgRenderer>
#include <QWidget>

class QContextMenuEvent;
class QEnterEvent;
class QMouseEvent;
class QMoveEvent;
class QPaintEvent;
class QResizeEvent;
class QShowEvent;
class QWheelEvent;
class QFrame;
class QLabel;
class QPushButton;
class QTimer;

class PetWindow final : public QWidget {
    Q_OBJECT
public:
    explicit PetWindow(QWidget *parent = nullptr);
    ~PetWindow() override;
    void setAvatar(const QByteArray &svg, const QString &name, const QString &avatarId = QString());
    void setAvatarFrames(const QList<QByteArray> &svgs, const QString &name, const QString &avatarId = QString(),
                         const QString &interactionProfile = QString());
    void speakText(const QString &text, int durationMs = 1800);
    void showAuditPrompt();
    bool isCollapsedBubble() const { return m_collapsedBubble; }
    bool isAlwaysOnTopEnabled() const { return m_expandedTopmost; }
    bool isPositionLocked() const { return m_positionLocked; }
    bool isMostlyOutsideScreens() const { return shouldCollapseForScreenBoundary(); }
    void collapseToBubble();
    void expandFromBubble();
    void setChatVisible(bool visible);
    void setModalBlocked(bool blocked);

signals:
    void chatRequested();
    void closeChatRequested();
    void closePetRequested();
    void topmostChanged(bool enabled);
    void collapsedChanged(bool collapsed);
    void geometryChanged(const QRect &geometry);

protected:
    void paintEvent(QPaintEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    void showPrompt(const QString &text, bool actions, int durationMs);
    void hidePrompt();
    enum InteractionKind {
        IdleInteraction = 0, HoverInteraction, ClickInteraction, DoubleClickInteraction, DragInteraction,
        DropInteraction, ChatInteraction, SpeakInteraction, LongIdleInteraction, SpecialInteraction,
        InteractionCount
    };
    void playInteraction(int requested = -1);
    int frameForInteraction(int interaction) const;
    void switchAvatarFrame(int frameIndex);
    void returnToRestFrame();
    QRectF fittedSvgRect(const QSvgRenderer &renderer, const QRectF &bounds) const;
    QRectF fittedSvgRect(const QRectF &bounds) const;
    QRectF fittedImageRect(const QImage &image, const QRectF &bounds) const;
    QImage renderInteractionFrame(const QByteArray &svg) const;
    QRectF expandedCharacterRect() const;
    void configureExpandedSizeForAvatar(bool resetToDefault = true);
    bool shouldCollapseForScreenBoundary() const;
    QRect clampExpandedGeometryToScreen(const QRect &geometry) const;
    void scheduleIdleInteraction();
    void updatePromptGeometry();
    void applyExpandedTopmost(bool enabled);
    void snapBubbleToSide();
    QRect availableGeometryForPoint(const QPoint &globalPoint) const;

    QSvgRenderer m_renderer;
    QSvgRenderer m_previousRenderer;
    QList<QByteArray> m_avatarFrames;
    QList<QImage> m_frameImages;
    QString m_name;
    QString m_avatarId;
    QString m_interactionProfile;
    int m_currentFrameIndex{0};
    int m_previousFrameIndex{-1};
    qint64 m_frameTransitionStarted{-1};
    int m_frameTransitionDuration{280};
    QPoint m_dragOffset;
    QPoint m_pressGlobal;
    QPoint m_lastDragGlobal;
    QRect m_expandedGeometry;
    QSize m_baseExpandedSize{250, 320};
    QSizeF m_baseCharacterSize{210.0, 230.0};
    bool m_dragging{false};
    bool m_pressTracking{false};
    bool m_positionLocked{false};
    bool m_collapsedBubble{false};
    bool m_expandedTopmost{true};
    bool m_hovered{false};
    bool m_dragMoved{false};
    QFrame *m_prompt{};
    QLabel *m_promptText{};
    QPushButton *m_promptYes{};
    QPushButton *m_promptNo{};
    QTimer *m_animationTimer{};
    QTimer *m_promptTimer{};
    QTimer *m_speakingTimer{};
    QTimer *m_idleInteractionTimer{};
    QElapsedTimer m_clock;
    QElapsedTimer m_clickCycle;
    qint64 m_interactionStarted{-1};
    int m_interactionIndex{0};
    int m_interactionDuration{760};
    int m_behaviorSeed{0};
    int m_interactionCursor{0};
    bool m_speaking{false};
    bool m_chatVisible{false};
    bool m_modalBlocked{false};
};
