#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

namespace flyback::net {
using Peer = std::uint64_t;
using Json = nlohmann::json;
enum class EventKind { connected, disconnected, message, trust_required, discovered };
struct Event {
    EventKind kind{};
    Peer peer{};
    Json data = Json::object();
    bool reliable = true;
};
// All public calls confined to one host thread. MsQuic workers only enqueue events.
class Endpoint {
  public:
    Endpoint();
    ~Endpoint();
    Endpoint(const Endpoint &) = delete;
    Endpoint &operator=(const Endpoint &) = delete;
    // Generates/persists server.crt/server.key inside identity_dir on first use.
    // bind_address is a numeric IPv4/IPv6 interface or "*" for dual-stack.
    void listen(const std::string &bind_address, std::uint16_t port,
                const std::filesystem::path &identity_dir);
    std::uint16_t port() const;
    std::string fingerprint() const; // SHA-256 of DER SubjectPublicKeyInfo, uppercase hex.
    // Empty pin: trust a verified CA or deliver trust_required {host,fingerprint}.
    // Pinned mismatch: disconnect; never prompt or send application secrets.
    Peer connect(const std::string &host, std::uint16_t port,
                 const std::string &pinned_fingerprint = {});
    void trust(Peer peer, bool accept);
    std::optional<Event> poll();
    // False means not negotiated/closed/too large/backpressured. No hidden reliable fallback.
    bool send(Peer peer, const Json &msg, bool reliable = true);
    void disconnect(Peer peer);
    // Connections and their counters stay separate; native wire counters are per peer.
    Json stats(Peer peer) const;
    // Optional link-local DNS-SD; discover sends one query, poll() emits results.
    void advertise(const std::string &display_name);
    void discover();

  private:
    struct Impl;
    std::unique_ptr<Impl> p_;
};
} // namespace flyback::net
