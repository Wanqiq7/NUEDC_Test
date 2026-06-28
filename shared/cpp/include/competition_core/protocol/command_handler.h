#pragma once

#include "competition_core/task/models.h"

#include "messages.pb.h"

#include <QByteArray>

namespace competition {

class MissionCommandStateMachine {
public:
    explicit MissionCommandStateMachine(CommandState *state);

    AckResult apply(const Envelope &envelope);

private:
    CommandState *state_ = nullptr;
};

AckResult applyCommandEnvelope(const Envelope &envelope, CommandState *state);
AckResult handleEnvelopeCommand(const Envelope &envelope, const QString &output_path, CommandState *state);
AckResult handleCommandBytes(const QByteArray &payload, const QString &output_path, CommandState *state);

} // namespace competition
