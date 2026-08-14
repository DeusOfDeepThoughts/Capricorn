#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>

class AppData final : public QObject {
    Q_OBJECT
public:
    explicit AppData(QObject *parent = nullptr);

    bool load(QString *error = nullptr);
    bool save(QString *error = nullptr) const;

    const QJsonArray &modules() const { return m_modules; }
    const QJsonArray &avatars() const { return m_avatars; }
    QJsonArray packs() const;
    QJsonObject draft() const;
    QJsonObject modelConfig() const;
    QJsonObject voiceConfig() const;
    QJsonObject userProfileSnapshot() const;
    QJsonArray modelConfigs() const;
    QJsonArray userAvatars() const;
    int maxUserAvatarCount() const { return 5; }
    QString activePersonaId() const;

    QJsonObject importUserAvatar(const QString &path, QString *error = nullptr);
    bool renameUserAvatar(const QString &id, const QString &name, QString *error = nullptr);
    bool removeUserAvatar(const QString &id, QString *error = nullptr);

    void setDraft(const QJsonObject &draft);
    void setPacks(const QJsonArray &packs);
    void setModelConfig(const QJsonObject &config);
    void setVoiceConfig(const QJsonObject &config);
    void setUserProfileSnapshot(const QJsonObject &snapshot);
    void setModelConfigs(const QJsonArray &configs);
    void setActivePersonaId(const QString &id);
    bool updatePersonaState(const QJsonArray &packs, const QString &activePersonaId,
                            QString *error = nullptr);

    QJsonObject packById(const QString &id) const;
    int answeredProfessionalCount(const QJsonObject &answers) const;
    int answeredPrivateCount(const QJsonObject &answers) const;
    int answeredCoreCount(const QJsonObject &answers) const;
    int totalCoreCount() const;
    int moduleAnsweredCount(int moduleIndex, const QJsonObject &answers) const;
    QString buildProfileMarkdown(const QJsonObject &pack) const;

    bool exportPack(const QJsonObject &pack, const QString &path, QString *error = nullptr) const;
    QJsonObject importPack(const QString &path, QString *error = nullptr) const;

    QString stateFilePath() const;

private:
    void ensureDefaults();
    static bool hasAnswer(const QJsonValue &value);
    static QString answerKey(int moduleIndex, int questionIndex);

    QJsonArray m_modules;
    QJsonArray m_avatars;
    QJsonObject m_state;
    QString m_patrickProfile;
    QJsonObject m_patrickAnswers;
    QString m_sunshineProfile;
    QJsonObject m_sunshineAnswers;
    QString m_taurusProfile;
    QJsonObject m_taurusAnswers;
};
