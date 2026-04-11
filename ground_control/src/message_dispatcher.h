#pragma once

#include <QMap>
#include <QStringList>

#include "messages.pb.h"

class MessageDispatcher {
public:
    static QStringList toQStringList(const google::protobuf::RepeatedPtrField<std::string> &values);
    static QMap<QString, int> toSummaryMap(const MissionSummary &summary);
};
