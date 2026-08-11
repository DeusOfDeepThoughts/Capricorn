#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QPoint>

class AppData;
class AvatarWidget;
class ChatWindow;
class CircularProgressWidget;
class CoreClient;
class PetWindow;
class ProcessSupervisor;
class VoiceRecognitionClient;
class QBoxLayout;
class QButtonGroup;
class QCheckBox;
class QDialog;
class QEvent;
class QLayout;
class QResizeEvent;
class QFrame;
class QGridLayout;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScrollArea;
class QSlider;
class QStackedWidget;
class QSystemTrayIcon;
class QMenu;
class QTextEdit;
class QToolButton;
class QVBoxLayout;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(AppData *data, CoreClient *core, ProcessSupervisor *processes, QWidget *parent = nullptr);
    ~MainWindow() override;
    void startUiAuditCapture(const QString &directory);
    void restoreFromTray();

protected:
    void closeEvent(QCloseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private:
    QWidget *buildWindowControls(QWidget *parent);
    QWidget *buildSidebar();
    QWidget *buildPersonaPage();
    QWidget *buildQuestionPage();
    QWidget *buildPacksPage();
    QWidget *buildModelPage();
    QWidget *buildVoicePage();
    QWidget *makeHeader(const QString &eyebrow, const QString &title, const QString &subtitle);
    QWidget *makeProgressCard(const QString &title, QLabel **detail,
                              CircularProgressWidget **ring, const QString &objectName,
                              const QString &centerSuffix);
    QWidget *buildAvatarStudio(QGridLayout **gridOut);

    void switchView(int index);
    void updateWindowControlIcons();
    bool actualWindowMaximized() const;
    void synchronizeWindowState();
    void setWindowMaximized(bool maximized);
    void toggleWindowMaximize();
    void updateWindowShape();
    void updateResizeHandles();
    void updatePersonaLayoutMetrics();
    void applyResponsiveLayout();
    void setupTrayIcon();
    void minimizeToTray();
    void beginFastExit();
    void relayoutGrid(QGridLayout *layout, const QList<QWidget *> &widgets, int columns, bool spanLast = false);
    void scrollCurrentPageToTop();
    void renderProgress();
    bool hasQuestionResponse(int moduleIndex, int questionIndex) const;
    void renderModules();
    void openModule(int moduleIndex);
    void renderQuestion();
    void captureCurrentAnswer();
    void saveDraft();
    QJsonObject currentAnswer() const;
    QString currentAnswerKey() const;
    void savePersona();
    void setPersonaStatusMessage(const QString &message);
    void resetDraft();
    void restoreDraft();
    void selectAvatar(AvatarWidget *avatar);
    AvatarWidget *avatarById(const QString &id) const;
    void refreshUserAvatarSlots();
    void uploadUserAvatar();
    void renameUserAvatar(AvatarWidget *avatar);
    void deleteUserAvatar(AvatarWidget *avatar);


    void renderPacks();
    void selectPack(const QString &id, bool announce = false);
    void invalidateActivePersona(const QString &id, const QString &reason);
    void usePack(const QString &id);
    void viewPack(const QString &id);
    void deletePack(const QString &id);
    void exportPack(const QString &id);
    void refreshExportPanel();
    void performExport();
    void importPack();

    QJsonObject collectModelConfig() const;
    void loadModelConfig();
    void saveModelConfig();
    void refreshSavedModelConfigs();
    void applySavedModelConfig(const QJsonObject &config, bool persistSelection = true);
    void deleteSavedModelConfig(const QString &id);
    void updateModelSaveState();
    void invalidateModelVerification();
    void updateGeneratePetAvailability();
    void loadVoiceConfig();
    void saveVoiceConfig();
    void updateVoiceSaveState();
    void updateVoiceInputState();
    void toggleVoiceInput();
    void applyVoiceInterimText(const QString &text);
    void applyVoiceFinalText(const QString &text);
    void resetVoiceInterimTracking();
    void resetGenerationSteps();
    void showModelNotice(const QString &title, const QString &message, bool error = false);
    int execTopmostDialog(QDialog &dialog);
    bool askTopmostQuestion(const QString &title, const QString &message, const QString &acceptText = QStringLiteral("是"), const QString &rejectText = QStringLiteral("否"));
    void setSyncStep(int index, const QString &state);
    void resetSyncSteps();
    void generatePet();
    void continueGeneratePetAfterModelVerification(const QJsonObject &config);
    void showPetForPack(const QJsonObject &pack, const QString &sessionId);
    void wirePetChatRelationship();
    void positionChatNextToPet();
    void positionPetNextToChat();
    void closePetAndChat(bool refreshPacks = true);
    void toggleChatDockSide();
    bool isRunningPersonaEditorLocked() const;
    void updatePersonaEditorRuntimeLock();
    void showRunningPetConfigLockedNotice();
    void registerRuntimePersonaEditControl(QWidget *widget, const QString &packId = QString());

    AppData *m_data{};
    CoreClient *m_core{};
    ProcessSupervisor *m_processes{};

    QStackedWidget *m_views{};
    QScrollArea *m_desktopScroll{};
    QFrame *m_appShell{};
    QWidget *m_contentHost{};
    QWidget *m_sidebar{};
    QWidget *m_titleBar{};
    QWidget *m_windowDragArea{};
    QWidget *m_windowControlsCluster{};
    QWidget *m_windowOutline{};
    QToolButton *m_minimizeControl{};
    QToolButton *m_maximizeControl{};
    QToolButton *m_closeControl{};
    QSystemTrayIcon *m_trayIcon{};
    QMenu *m_trayMenu{};
    QObject *m_motionFilter{};
    QObject *m_resizeFilter{};
    QList<QWidget *> m_resizeHandles;
    QPoint m_windowDragOffset;
    bool m_windowDragging{false};
    bool m_windowMaximizedState{false};
    int m_cachedSidebarWidth{236};
    bool m_exitRequested{false};
    bool m_modalActive{false};
    QList<QPushButton *> m_navButtons;
    QWidget *m_personaPage{};
    QWidget *m_questionPage{};

    QLabel *m_professionalDetail{};
    QLabel *m_privateDetail{};
    QLabel *m_coreDetail{};
    CircularProgressWidget *m_professionalRing{};
    CircularProgressWidget *m_privateRing{};
    CircularProgressWidget *m_coreRing{};
    QLabel *m_questionProfessionalDetail{};
    QLabel *m_questionPrivateDetail{};
    QLabel *m_questionCoreDetail{};
    CircularProgressWidget *m_questionProfessionalRing{};
    CircularProgressWidget *m_questionPrivateRing{};
    CircularProgressWidget *m_questionCoreRing{};
    QGridLayout *m_moduleGrid{};
    QGridLayout *m_personaProgressGrid{};
    QGridLayout *m_questionProgressGrid{};
    QList<QWidget *> m_personaProgressCards;
    QList<QWidget *> m_questionProgressCards;
    QList<QWidget *> m_moduleCards;
    QList<QWidget *> m_optionWidgets;
    QList<QWidget *> m_packCards;
    QBoxLayout *m_questionBodyLayout{};
    QBoxLayout *m_personaHeaderLayout{};
    QBoxLayout *m_personaIdentityLayout{};
    QBoxLayout *m_packsHeaderLayout{};
    QBoxLayout *m_answerHeaderLayout{};
    QBoxLayout *m_exportHeaderLayout{};
    QBoxLayout *m_modelColumnsLayout{};
    QBoxLayout *m_exportColumnsLayout{};
    QGridLayout *m_providerLayout{};
    QGridLayout *m_packGrid{};
    QWidget *m_packGridHost{};
    QVBoxLayout *m_packsPageLayout{};
    int m_packGridColumns{0};
    int m_personaCardWidth{335};
    int m_personaCardGap{18};
    int m_personaMinimumWindowWidth{960};
    int m_responsiveMinimumHeight{640};
    int m_personaMaximumColumns{3};
    int m_personaMetricsScreenWidth{0};
    int m_personaMetricsScreenHeight{0};
    QList<QLayout *> m_pageLayouts;
    bool m_applyingResponsiveLayout{false};
    QString m_currentQuestionType;
    QGridLayout *m_avatarGrid{};
    QList<QGridLayout *> m_avatarGrids;
    QLineEdit *m_personaName{};
    QLabel *m_personaStatus{};
    QLabel *m_editorMode{};
    QPushButton *m_savePersonaButton{};
    QList<AvatarWidget *> m_avatarWidgets;
    QList<AvatarWidget *> m_userAvatarWidgets;
    QList<QWidget *> m_userAvatarSlotWidgets;

    QLabel *m_questionModuleTitle{};
    QLabel *m_questionModuleDescription{};
    QLabel *m_questionPosition{};
    QLabel *m_questionType{};
    QLabel *m_questionText{};
    QLabel *m_questionHint{};
    QToolButton *m_evidenceToggle{};
    QLabel *m_questionEvidence{};
    QGridLayout *m_questionIndexLayout{};
    QLabel *m_coreLegend{};
    QLabel *m_extensionLegend{};
    QWidget *m_optionHead{};
    QLabel *m_optionMode{};
    QWidget *m_optionsHost{};
    QGridLayout *m_optionsLayout{};
    QButtonGroup *m_optionGroup{};
    QSlider *m_scale{};
    QLabel *m_scaleLow{};
    QLabel *m_scaleHigh{};
    QTextEdit *m_answerText{};
    QLabel *m_questionStatus{};
    QPushButton *m_previousButton{};
    QPushButton *m_nextButton{};

    QVBoxLayout *m_packsLayout{};
    QLabel *m_packStatus{};
    QPushButton *m_exportSelectedButton{};
    QFrame *m_exportPanel{};
    QLabel *m_exportTitle{};
    QLineEdit *m_exportPackName{};
    QPushButton *m_exportPackButton{};
    QGridLayout *m_exportModulesLayout{};
    QList<QCheckBox *> m_exportModuleChecks;
    QString m_selectedPackId;
    QString m_exportTargetPackId;

    QLineEdit *m_voiceAppId{};
    QLineEdit *m_voiceApiKey{};
    QPushButton *m_saveVoiceButton{};
    QToolButton *m_voiceInputButton{};
    VoiceRecognitionClient *m_voiceClient{};
    QJsonObject m_lastSavedVoiceConfig;
    QString m_voiceInterimText;
    bool m_loadingVoiceConfig{false};
    bool m_updatingVoiceText{false};
    bool m_voiceStopPending{false};
    int m_pendingViewAfterVoiceStop{-1};

    QLineEdit *m_baseUrl{};
    QLineEdit *m_modelId{};
    QLineEdit *m_apiKey{};
    QPushButton *m_saveModelButton{};
    QScrollArea *m_savedModelConfigsScroll{};
    QWidget *m_savedModelConfigsHost{};
    QVBoxLayout *m_savedModelConfigsLayout{};
    QLabel *m_providerName{};
    QLabel *m_activePersonaLabel{};
    QList<QLabel *> m_syncSteps;
    QList<QWidget *> m_syncIndicators;
    QPushButton *m_generatePetButton{};
    QList<QPushButton *> m_providerButtons;
    QString m_provider{QStringLiteral("custom")};
    QString m_providerDisplay{QStringLiteral("自定义服务")};
    QString m_loadedModelConfigId;
    QJsonObject m_lastSavedModelConfig;
    QJsonObject m_verifiedModelConfig;
    bool m_loadingModelConfig{false};
    bool m_modelVerificationRunning{false};
    bool m_generationInProgress{false};

    QJsonObject m_draft;
    QJsonObject m_answers;
    int m_currentModule{0};
    int m_currentQuestion{0};
    QString m_selectedAvatarId;
    QString m_editPackId;
    bool m_editorReadOnly{false};
    bool m_builtinAvatarOnly{false};

    QPointer<PetWindow> m_petWindow;
    QPointer<ChatWindow> m_chatWindow;
    QString m_activeSessionId;
    QString m_runningPersonaId;
    bool m_chatOnRight{true};
    bool m_syncingPetChatGeometry{false};
};
