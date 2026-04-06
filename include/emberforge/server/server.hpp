#pragma once

#include <string>

namespace emberforge::server {

struct ServerConfig {
    int port{4545};
};

class Server {
public:
    explicit Server(ServerConfig config);
    [[nodiscard]] std::string describe() const;

private:
    ServerConfig config_;
};

} // namespace emberforge::server
