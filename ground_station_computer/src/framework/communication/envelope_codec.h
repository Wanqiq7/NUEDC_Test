#pragma once

#include <QByteArray>
#include <QString>

struct CommandSendResult {
    bool ok = false;
    QString message;
    QString task_id;
    bool mission_loaded = false;
    bool mission_running = false;
    quint64 last_accepted_sequence = 0;
    bool vision_armed = false;
};

class EnvelopeCodec {
public:
    static CommandSendResult parseAck(const QByteArray &payload);
};
