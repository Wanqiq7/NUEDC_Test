#include "command_server.h"

#include "competition_core/protocol/command_handler.h"
#include "competition_core/protocol/envelope_codec.h"

#include <zmq.hpp>

namespace airborne {

CommandServer::CommandServer(QString endpoint, QString output_path, competition::CommandState *state, QObject *parent)
    : QThread(parent),
      endpoint_(std::move(endpoint)),
      output_path_(std::move(output_path)),
      state_(state) {}

CommandServer::~CommandServer() {
    requestStop();
    wait(1000);
}

void CommandServer::requestStop() {
    stop_requested_.storeRelease(true);
}

void CommandServer::run() {
    zmq::context_t context(1);
    zmq::socket_t socket(context, zmq::socket_type::rep);
    socket.set(zmq::sockopt::rcvtimeo, 100);
    socket.bind(endpoint_.toStdString());

    while (!stop_requested_.loadAcquire()) {
        zmq::message_t request;
        const auto received = socket.recv(request, zmq::recv_flags::none);
        if (!received.has_value()) {
            continue;
        }

        const QByteArray payload(static_cast<const char *>(request.data()), static_cast<qsizetype>(request.size()));
        const competition::AckResult ack = competition::handleCommandBytes(payload, output_path_, state_);
        const QByteArray reply = competition::buildAckBytes(ack.success, ack.message);
        socket.send(zmq::buffer(reply.constData(), static_cast<size_t>(reply.size())), zmq::send_flags::none);
    }

    socket.close();
    context.close();
}

} // namespace airborne
