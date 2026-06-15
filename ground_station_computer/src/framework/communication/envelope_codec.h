#pragma once

#include <QByteArray>
#include <QString>

struct CommandSendResult {
    bool ok = false;
    QString message;
};

class EnvelopeCodec {
public:
    static CommandSendResult parseAck(const QByteArray &payload);
};
