#pragma once

#include <QMetaType>
#include <QString>

enum class CommandLinkHealth {
    Checking,
    Online,
    Offline,
};

struct CommandLinkSnapshot {
    CommandLinkHealth health = CommandLinkHealth::Checking;
    int consecutive_failures = 0;
    QString detail;
    quint64 generation = 0;
};

Q_DECLARE_METATYPE(CommandLinkSnapshot)

class CommandLinkHealthTracker {
public:
    explicit CommandLinkHealthTracker(int failure_threshold = 3);

    CommandLinkSnapshot recordSuccess(const QString &detail);
    CommandLinkSnapshot recordFailure(const QString &detail);
    CommandLinkSnapshot snapshot() const;

private:
    int failure_threshold_ = 3;
    CommandLinkSnapshot snapshot_;
};
