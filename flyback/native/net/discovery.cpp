#include "discovery.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#endif
#include <mdns.h>
#include <array>
#include <chrono>
#include <set>
#include <stdexcept>
#include <vector>
namespace flyback::net {
namespace {
using Clock = std::chrono::steady_clock;
const std::string service = "_fbar._udp.local.";
mdns_string_t ms(const std::string &s) {
    return {s.data(), s.size()};
}
std::vector<sockaddr_storage> addrs() {
    std::vector<sockaddr_storage> out;
    auto add = [&](const sockaddr *a) {
        if (!a || (a->sa_family != AF_INET && a->sa_family != AF_INET6))
            return;
        sockaddr_storage s{};
        memcpy(&s, a, a->sa_family == AF_INET ? sizeof(sockaddr_in) : sizeof(sockaddr_in6));
        out.push_back(s);
    };
#ifdef _WIN32
    ULONG size = 16384;
    std::vector<unsigned char> b(size);
    ULONG rc;
    const ULONG flags = GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    for (;;) {
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, (PIP_ADAPTER_ADDRESSES)b.data(), &size);
        if (rc != ERROR_BUFFER_OVERFLOW)
            break;
        b.resize(size);
    }
    if (rc == NO_ERROR)
        for (auto *a = (PIP_ADAPTER_ADDRESSES)b.data(); a; a = a->Next)
            if (a->OperStatus == IfOperStatusUp && a->IfType != IF_TYPE_SOFTWARE_LOOPBACK)
                for (auto *u = a->FirstUnicastAddress; u; u = u->Next)
                    add(u->Address.lpSockaddr);
#else
    ifaddrs *l = nullptr;
    if (getifaddrs(&l) == 0) {
        for (auto *a = l; a; a = a->ifa_next)
            if ((a->ifa_flags & IFF_UP) && (a->ifa_flags & IFF_MULTICAST) &&
                !(a->ifa_flags & IFF_LOOPBACK))
                add(a->ifa_addr);
        freeifaddrs(l);
    }
#endif
    return out;
}
} // namespace
struct Discovery::Impl {
    std::string label, inst, host;
    std::uint16_t port = 0;
    std::vector<int> listeners, browsers;
    std::set<std::string> seen;
    const std::function<void(Json)> *recv = nullptr;
    Clock::time_point last{}, expiry{};
    alignas(8) std::array<char, 2048> packet{};
    static void close(std::vector<int> &s) {
        for (auto fd : s)
            mdns_socket_close(fd);
        s.clear();
    }
    ~Impl() {
        close(browsers);
        close(listeners);
    }
    static int cb(int fd, const sockaddr *from, size_t fromlen, mdns_entry_type_t e, uint16_t id,
                  uint16_t typ, uint16_t cls, uint32_t ttl, const void *data, size_t size, size_t off,
                  size_t, size_t r, size_t rlen, void *ctx) {
        auto &o = *static_cast<Impl *>(ctx);
        char namebuf[256], target[256], addr[NI_MAXHOST];
        auto m = mdns_string_extract(data, size, &off, namebuf, sizeof(namebuf));
        std::string n(m.str, m.length);
        if (e == MDNS_ENTRYTYPE_QUESTION && o.port && n == service &&
            (typ == MDNS_RECORDTYPE_PTR || typ == MDNS_RECORDTYPE_ANY)) {
            mdns_record_t ptr{};
            ptr.name = ms(service);
            ptr.type = MDNS_RECORDTYPE_PTR;
            ptr.data.ptr.name = ms(o.inst);
            ptr.ttl = 30;
            mdns_record_t extra[2]{};
            extra[0].name = ms(o.inst);
            extra[0].type = MDNS_RECORDTYPE_SRV;
            extra[0].data.srv.name = ms(o.host);
            extra[0].data.srv.port = o.port;
            extra[0].ttl = 30;
            extra[1].name = ms(o.inst);
            extra[1].type = MDNS_RECORDTYPE_TXT;
            extra[1].data.txt.key = {"v", 1};
            extra[1].data.txt.value = {"1", 1};
            extra[1].ttl = 30;
            alignas(8) char out[1024];
            if (cls & MDNS_UNICAST_RESPONSE)
                mdns_query_answer_unicast(fd, from, fromlen, out, sizeof(out), id,
                                          mdns_record_type_t(typ), m.str, m.length, ptr, nullptr, 0,
                                          extra, 2);
            else
                mdns_query_answer_multicast(fd, out, sizeof(out), ptr, nullptr, 0, extra, 2);
        } else if (e != MDNS_ENTRYTYPE_QUESTION && ttl && o.recv && typ == MDNS_RECORDTYPE_SRV &&
                   n.size() > service.size() + 1 &&
                   n.compare(n.size() - service.size() - 1, service.size() + 1, "." + service) == 0) {
            auto srv = mdns_record_parse_srv(data, size, r, rlen, target, sizeof(target));
            if (!srv.port || getnameinfo(from, socklen_t(fromlen), addr, sizeof(addr), nullptr, 0,
                                         NI_NUMERICHOST) != 0)
                return 0;
            auto display = n.substr(0, n.size() - service.size() - 1);
            auto ep = std::string(addr) + ":" + std::to_string(srv.port);
            if (o.seen.size() < 1024 && o.seen.insert(ep).second) {
                try {
                    Json j = {
                        {"name", display}, {"host", addr}, {"port", srv.port}, {"source", "mdns"}};
                    (void)j.dump();
                    (*o.recv)(std::move(j));
                } catch (const Json::exception &) {
                }
            }
        }
        return 0;
    }
};
Discovery::Discovery() : p(std::make_unique<Impl>()) {}
Discovery::~Discovery() = default;
void Discovery::advertise(std::string name, std::uint16_t n, const std::string &fp,
                          const std::string &bind_address) {
    auto &o = *p;
    if (name.empty())
        name = "Flyback";
    for (auto &c : name)
        if (c == '.' || static_cast<unsigned char>(c) < 32)
            c = '-';
    if (name.size() > 44) {
        size_t k = 44;
        while (k > 0 && (static_cast<unsigned char>(name[k]) & 192) == 128)
            --k;
        name.resize(k);
    }
    o.label = name;
    o.inst = name + "-" + fp.substr(0, 6) + "." + service;
    o.host = "flyback-" + fp.substr(0, 12) + ".local.";
    o.port = n;
    if (o.listeners.empty()) {
        const bool wildcard = bind_address.empty() || bind_address == "*";
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(MDNS_PORT);
        sockaddr_in6 b{};
        b.sin6_family = AF_INET6;
        b.sin6_port = htons(MDNS_PORT);
        bool v4 = wildcard, v6 = wildcard;
        if (!wildcard) {
            addrinfo hints{}, *ai = nullptr;
            hints.ai_family = AF_UNSPEC;
            hints.ai_flags = AI_NUMERICHOST;
            hints.ai_socktype = SOCK_DGRAM;
            if (getaddrinfo(bind_address.c_str(), nullptr, &hints, &ai) != 0)
                throw std::runtime_error("invalid mDNS interface");
            if (ai->ai_family == AF_INET) {
                a.sin_addr = ((sockaddr_in *)ai->ai_addr)->sin_addr;
                v4 = true;
            } else if (ai->ai_family == AF_INET6) {
                auto x = (sockaddr_in6 *)ai->ai_addr;
                b.sin6_addr = x->sin6_addr;
                b.sin6_scope_id = x->sin6_scope_id;
                v6 = true;
            }
            freeaddrinfo(ai);
            // A listener reachable only from this machine should never appear
            // as an unreachable LAN room on another network interface.
            if ((v4 && ((ntohl(a.sin_addr.s_addr) >> 24) == 127)) ||
                (v6 && IN6_IS_ADDR_LOOPBACK(&b.sin6_addr)))
                return;
        }
        if (v4) {
            auto fd = mdns_socket_open_ipv4(&a);
            if (fd >= 0)
                o.listeners.push_back(fd);
        }
        if (v6) {
            auto fd = mdns_socket_open_ipv6(&b);
            if (fd >= 0)
                o.listeners.push_back(fd);
        }
    }
    if (o.listeners.empty())
        throw std::runtime_error("mDNS UDP 5353 unavailable");
}
void Discovery::browse() {
    auto &o = *p;
    Impl::close(o.browsers);
    o.seen.clear();
    o.expiry = Clock::now() + std::chrono::seconds(3);
    for (auto &s : addrs()) {
        int fd = s.ss_family == AF_INET ? mdns_socket_open_ipv4((sockaddr_in *)&s)
                                        : mdns_socket_open_ipv6((sockaddr_in6 *)&s);
        if (fd >= 0) {
            if (mdns_query_send(fd, MDNS_RECORDTYPE_PTR, service.c_str(), service.size(),
                                o.packet.data(), o.packet.size(), 0) < 0)
                mdns_socket_close(fd);
            else
                o.browsers.push_back(fd);
        }
    }
}
void Discovery::pump(const std::function<void(Json)> &cb) {
    auto &o = *p;
    if (o.browsers.empty() && o.listeners.empty())
        return;
    auto now = Clock::now();
    if (now - o.last < std::chrono::milliseconds(10))
        return;
    o.last = now;
    for (auto fd : o.listeners)
        for (int n = 0; n < 4; ++n)
            if (!mdns_socket_listen(fd, o.packet.data(), o.packet.size(), Impl::cb, &o))
                break;
    if (now > o.expiry) {
        Impl::close(o.browsers);
        return;
    }
    o.recv = &cb;
    for (auto fd : o.browsers)
        for (int n = 0; n < 4; ++n)
            if (!mdns_query_recv(fd, o.packet.data(), o.packet.size(), Impl::cb, &o, 0))
                break;
    o.recv = nullptr;
}
} // namespace flyback::net

