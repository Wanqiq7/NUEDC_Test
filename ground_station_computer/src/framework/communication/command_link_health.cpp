#include "framework/communication/command_link_health.h"

CommandLinkHealthTracker::CommandLinkHealthTracker(int failure_threshold)
    : failure_threshold_(failure_threshold >= 1 ? failure_threshold : 3) {}

CommandLinkSnapshot CommandLinkHealthTracker::recordSuccess(const QString &detail) {
    snapshot_.health = CommandLinkHealth::Online;
    snapshot_.consecutive_failures = 0;
    snapshot_.detail = detail;
    return snapshot_;
}

CommandLinkSnapshot CommandLinkHealthTracker::recordFailure(const QString &detail) {
    ++snapshot_.consecutive_failures;
    snapshot_.health = snapshot_.consecutive_failures >= failure_threshold_
        ? CommandLinkHealth::Offline
        : CommandLinkHealth::Checking;
    snapshot_.detail = detail;
    return snapshot_;
}

CommandLinkSnapshot CommandLinkHealthTracker::snapshot() const {
    return snapshot_;
}
