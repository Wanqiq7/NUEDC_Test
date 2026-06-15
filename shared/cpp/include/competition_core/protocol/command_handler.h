#pragma once

#include "competition_core/task/models.h"

#include "messages.pb.h"

#include <QByteArray>

namespace competition {

AckResult handleEnvelopeCommand(const Envelope &envelope, const QString &output_path, CommandState *state);
AckResult handleCommandBytes(const QByteArray &payload, const QString &output_path, CommandState *state);

} // namespace competition
