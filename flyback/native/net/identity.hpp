#pragma once
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
namespace flyback::net {
struct Identity {
    std::string pin;
    std::vector<std::uint8_t> p12;
};
Identity load_identity(const std::filesystem::path &);
struct Cert {
    std::string pin;
    bool current, self_signed, name_ok;
};
Cert inspect_cert(const std::uint8_t *, size_t, const std::string &hostname);
} // namespace flyback::net
