#pragma once

#include "competition_core/task/models.h"

#include <QAtomicInteger>
#include <QThread>

namespace airborne {

class CommandServer final : public QThread {
    Q_OBJECT

public:
    CommandServer(QString endpoint, QString output_path, competition::CommandState *state, QObject *parent = nullptr);
    ~CommandServer() override;

    void requestStop();

protected:
    void run() override;

private:
    QString endpoint_;
    QString output_path_;
    competition::CommandState *state_ = nullptr;
    QAtomicInteger<bool> stop_requested_{false};
};

} // namespace airborne
