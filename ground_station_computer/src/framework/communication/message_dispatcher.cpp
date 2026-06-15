#include "framework/communication/message_dispatcher.h"

QStringList MessageDispatcher::toQStringList(const google::protobuf::RepeatedPtrField<std::string> &values) {
    QStringList result;
    result.reserve(values.size());
    for (const auto &value : values) {
        result.append(QString::fromStdString(value));
    }
    return result;
}
