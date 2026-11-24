#pragma once
#include "net.hpp"
#include <vector>
namespace flyback::net::wire {
using Bytes = std::vector<std::uint8_t>;
constexpr size_t max_payload = 4 * 1024 * 1024;
Bytes delta(const Json &base, const Json &current);
Json apply(const Json &base, const Bytes &patch);
std::uint8_t type(const Json &);
Bytes encode(const Json &, bool stream);
Json decode(const std::uint8_t *, std::size_t, bool stream);
} // namespace flyback::net::wire
