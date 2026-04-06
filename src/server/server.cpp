#include "emberforge/server/server.hpp"

namespace emberforge::server {

Server::Server(ServerConfig config) : config_(config) {}

std::string Server::describe() const {
    return "Server listening on port " + std::to_string(config_.port);
}

} // namespace emberforge::server
