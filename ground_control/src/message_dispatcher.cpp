#include "message_dispatcher.h"

QStringList MessageDispatcher::toQStringList(const google::protobuf::RepeatedPtrField<std::string> &values) {
    QStringList result;
    result.reserve(values.size());
    for (const auto &value : values) {
        result.append(QString::fromStdString(value));
    }
    return result;
}

QMap<QString, int> MessageDispatcher::toSummaryMap(const MissionSummary &summary) {
    QMap<QString, int> totals;
    for (const auto &item : summary.totals()) {
        totals.insert(QString::fromStdString(item.animal_name()), static_cast<int>(item.total_count()));
    }
    return totals;
}
