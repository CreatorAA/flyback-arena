#pragma once
#include "net.hpp"
#include <functional>
namespace flyback::net {
class Discovery {
    struct Impl;
    std::unique_ptr<Impl> p;

  public:
    Discovery();
    ~Discovery();
    void advertise(std::string name, std::uint16_t port, const std::string &identity,
                   const std::string &bind_address);
    void browse();
    void pump(const std::function<void(Json)> &receive);
};
} // namespace flyback::net

