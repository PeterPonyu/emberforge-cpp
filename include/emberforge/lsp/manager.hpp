#pragma once

#include <string>

namespace emberforge::lsp {

class LspManager {
public:
    [[nodiscard]] std::string summary() const;
};

} // namespace emberforge::lsp
