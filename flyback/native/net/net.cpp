#include "net.hpp"
#include "wire.hpp"
#include "identity.hpp"
#include "discovery.hpp"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#ifdef __MINGW32__
inline LONG RtlIpv4StringToAddressA(PCSTR s, BOOLEAN strict, PCSTR *end, IN_ADDR *addr) {
    char *t = nullptr;
    auto status = ::RtlIpv4StringToAddressA(s, strict, &t, addr);
    *end = t;
    return status;
}
extern "C" LONG NTAPI RtlIpv6StringToAddressA(PCSTR, PCSTR *, IN6_ADDR *);
#endif
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif
#include <msquic.h>
#include <openssl/crypto.h>
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <map>
#include <tuple>
#include <deque>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_map>
namespace flyback::net {
namespace {
using Bytes = wire::Bytes;
using B = std::uint8_t;
void check(QUIC_STATUS s, const char *where) {
    if (QUIC_FAILED(s))
        throw std::runtime_error(std::string(where) + ": MsQuic status " +
                                 std::to_string(std::uint32_t(s)));
}
QUIC_BUFFER alpn{6, (B *)"fbar-1"};
std::string address(const QUIC_ADDR &a) {
    char b[NI_MAXHOST]{};
    socklen_t n = QuicAddrGetFamily(&a) == AF_INET6 ? sizeof(sockaddr_in6) : sizeof(sockaddr_in);
    if (getnameinfo((const sockaddr *)&a, n, b, sizeof(b), nullptr, 0, NI_NUMERICHOST) != 0)
        return {};
    return b;
}
std::string normalized(std::string pin) {
    std::string p;
    for (unsigned char c : pin) {
        if (c == ':')
            continue;
        if (!std::isxdigit(c))
            throw std::invalid_argument("invalid host fingerprint");
        p += char(std::toupper(c));
    }
    if (!p.empty() && p.size() != 64)
        throw std::invalid_argument("invalid host fingerprint length");
    return p;
}
Json hello(bool ack) {
    return {{"type", ack ? "_hello_ack" : "_hello"},
            {"base_id", "flyback:vanilla"},
            {"proto_major", 1},
            {"proto_minor", 0},
            {"profile", 0},
            {"implementation", "flyback-native/1"},
            {"required_extensions", Json::array()},
            {"optional_extensions", Json::array()},
            {"ruleset", "classic20"},
            {"datagram", true}};
}
} // namespace
struct Endpoint::Impl {
    struct C;
    struct S {
        C *c;
        HQUIC h = nullptr;
        bool bulk = false;
        Bytes rx;
    };
    struct Send {
        std::shared_ptr<const Bytes> data;
        QUIC_BUFFER buf;
        C *c;
        bool reliable;
    };
    struct C {
        Impl *own;
        Peer id;
        bool server;
        HQUIC h = nullptr;
        S control;
        std::atomic<HQUIC> stream{nullptr};
        std::atomic_bool ready{false}, cert_pending{false}, stopped{false};
        std::atomic_uint mtu{0};
        std::atomic_size_t control_bytes{0}, dgram_bytes{0};
        std::atomic_uint64_t delta_sent{0}, base_sent{0};
        std::string host, pinned, actual, why;
        unsigned msg_count = 0;
        size_t received_charge = 0;
        std::atomic_uint64_t sent_base{0}, acked_base{0};
        std::uint64_t recv_id = 0;
        Json recv_base;
        C(Impl *o, Peer id_, bool s) : own(o), id(id_), server(s), control{this} {}
    };
    const QUIC_API_TABLE *q = nullptr;
    HQUIC reg = nullptr, clientcfg = nullptr, servercfg = nullptr, listener = nullptr;
    std::uint16_t port = 0;
    std::string fp, bind_address;
    std::mutex mx;
    std::condition_variable ended;
    std::unordered_map<Peer, std::shared_ptr<C>> peers;
    using ViewKey = std::tuple<std::string, std::uint64_t, std::uint64_t, int>;
    struct View {
        std::uint64_t id = 0;
        std::int64_t window = -1;
        Json base, last;
        std::shared_ptr<const Bytes> initial, full, delta;
        std::chrono::steady_clock::time_point used{};
    };
    std::map<ViewKey, View> views;
    std::uint64_t serial = 0;
    std::chrono::steady_clock::time_point purge{};
    std::deque<std::pair<Event, size_t>> inbox;
    size_t inbox_bytes = 0;
    Peer next = 1;
    Discovery mdns;
    bool watch(C &, const Json &);
    bool post(C &, std::shared_ptr<const Bytes>, std::uint8_t, bool);
    Impl();
    ~Impl();
    HQUIC config(bool server);
    std::shared_ptr<C> find(Peer id) {
        std::lock_guard l(mx);
        auto it = peers.find(id);
        return it == peers.end() ? nullptr : it->second;
    }
    std::shared_ptr<C> add(bool server) {
        std::lock_guard l(mx);
        auto c = std::make_shared<C>(this, next++, server);
        peers.emplace(c->id, c);
        return c;
    }
    bool push(Event ev, size_t charge = 0) {
        std::lock_guard l(mx);
        auto it = peers.find(ev.peer);
        if (ev.kind == EventKind::message) {
            if (it == peers.end() || it->second->msg_count >= 128 || inbox.size() >= 8192)
                return false;
            const size_t cap = it->second->server ? 256 * 1024 : 8 * 1024 * 1024;
            if (it->second->received_charge + charge > cap || inbox_bytes + charge > 16 * 1024 * 1024)
                return false;
            ++it->second->msg_count;
            it->second->received_charge += charge;
            inbox_bytes += charge;
        }
        inbox.emplace_back(std::move(ev), charge);
        return true;
    }
    static QUIC_STATUS QUIC_API listen_cb(HQUIC, void *, QUIC_LISTENER_EVENT *);
    static QUIC_STATUS QUIC_API conn_cb(HQUIC, void *, QUIC_CONNECTION_EVENT *);
    static QUIC_STATUS QUIC_API stream_cb(HQUIC, void *, QUIC_STREAM_EVENT *);
    void fail(C &c, std::string why) {
        QUIC_UINT62 code = why == "ERR_PROTO_MAJOR_MISMATCH" ? 0x101
                           : why == "ERR_MISSING_EXTENSION"  ? 0x102
                           : why == "ERR_PROFILE_MISMATCH"   ? 0x103
                                                             : 0x100;
        if (c.why.empty())
            c.why = std::move(why);
        c.ready = false;
        q->ConnectionShutdown(c.h, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, code);
    }
    void frame(C &c, const B *p, size_t n, bool reliable);
    bool submit(C &, const Json &, bool reliable, bool handshake = false);
};
Endpoint::Impl::Impl() {
    check(MsQuicOpen2(&q), "open MsQuic");
    try {
        QUIC_REGISTRATION_CONFIG rc{"flyback", QUIC_EXECUTION_PROFILE_LOW_LATENCY};
        check(q->RegistrationOpen(&rc, &reg), "register");
        clientcfg = config(false);
    } catch (...) {
        if (reg)
            q->RegistrationClose(reg);
        MsQuicClose(q);
        throw;
    }
}
HQUIC Endpoint::Impl::config(bool server) {
    QUIC_SETTINGS s{};
#define FB_SET(n, v)                                                                                   \
    s.n = v;                                                                                           \
    s.IsSet.n = 1
    FB_SET(PeerBidiStreamCount, 1);
    FB_SET(PeerUnidiStreamCount, 8);
    FB_SET(DatagramReceiveEnabled, TRUE);
    FB_SET(HandshakeIdleTimeoutMs, 90000);
    FB_SET(IdleTimeoutMs, 45000);
    FB_SET(KeepAliveIntervalMs, 10000);
#undef FB_SET
    HQUIC h{};
    check(q->ConfigurationOpen(reg, &alpn, 1, &s, sizeof(s), nullptr, &h), "QUIC config");
    if (!server) {
        QUIC_CREDENTIAL_CONFIG c{};
        c.Type = QUIC_CREDENTIAL_TYPE_NONE;
        c.Flags = QUIC_CREDENTIAL_FLAGS(QUIC_CREDENTIAL_FLAG_CLIENT |
                                        QUIC_CREDENTIAL_FLAG_INDICATE_CERTIFICATE_RECEIVED |
                                        QUIC_CREDENTIAL_FLAG_DEFER_CERTIFICATE_VALIDATION |
                                        QUIC_CREDENTIAL_FLAG_USE_PORTABLE_CERTIFICATES);
        auto st = q->ConfigurationLoadCredential(h, &c);
        if (QUIC_FAILED(st)) {
            q->ConfigurationClose(h);
            check(st, "TLS client credentials");
        }
    }
    return h;
}
Endpoint::Impl::~Impl() {
    if (listener)
        q->ListenerClose(listener); // no new peers after this synchronous call
    q->RegistrationShutdown(reg, QUIC_CONNECTION_SHUTDOWN_FLAG_SILENT, 0);
    std::unordered_map<Peer, std::shared_ptr<C>> ending;
    {
        std::lock_guard l(mx);
        ending.swap(peers);
    }
    for (auto &it : ending) {
        auto &c = *it.second;
        if (c.cert_pending.exchange(false))
            q->ConnectionCertificateValidationComplete(c.h, FALSE, QUIC_TLS_ALERT_CODE_BAD_CERTIFICATE);
    }
    for (auto &it : ending) {
        auto &c = *it.second;
        // Keep both handles valid while the worker completes outstanding sends, then release
        // the child stream before its parent. ConnectionClose alone leaves stream refs.
        {
            std::unique_lock l(mx);
            ended.wait(l, [&] { return c.stopped.load(); });
        }
        if (c.control.h)
            q->StreamClose(c.control.h);
        if (c.h)
            q->ConnectionClose(c.h);
    }
    if (servercfg)
        q->ConfigurationClose(servercfg);
    if (clientcfg)
        q->ConfigurationClose(clientcfg);
    q->RegistrationClose(reg);
    MsQuicClose(q);
}
QUIC_STATUS QUIC_API Endpoint::Impl::listen_cb(HQUIC, void *ctx, QUIC_LISTENER_EVENT *e) {
    auto &o = *static_cast<Impl *>(ctx);
    if (e->Type != QUIC_LISTENER_EVENT_NEW_CONNECTION)
        return QUIC_STATUS_SUCCESS;
    try {
        auto c = o.add(true);
        c->h = e->NEW_CONNECTION.Connection;
        c->host = address(*e->NEW_CONNECTION.Info->RemoteAddress);
        o.q->SetCallbackHandler(c->h, (void *)conn_cb, c.get());
        auto st = o.q->ConnectionSetConfiguration(c->h, o.servercfg);
        if (QUIC_FAILED(st)) {
            std::lock_guard l(o.mx);
            o.peers.erase(c->id);
        }
        return st;
    } catch (...) {
        return QUIC_STATUS_OUT_OF_MEMORY;
    }
}
bool Endpoint::Impl::submit(C &c, const Json &j, bool rel, bool handshake) {
    if (!handshake && !c.ready.load())
        return false;
    const auto t = wire::type(j);
    if (!rel && t != 0x40 && t != 0x41 && t != 0x42 && t != 0x43)
        return false;
    return post(c, std::make_shared<Bytes>(wire::encode(j, rel)), t, rel);
}
bool Endpoint::Impl::post(C &c, std::shared_ptr<const Bytes> frame, std::uint8_t t, bool rel) {
    unsigned cap = c.mtu.load();
    if (!rel && (frame->size() > cap || !cap))
        return false;
    auto bytes = frame->size() + (rel ? 4 : 0);
    size_t lim = rel ? 8 * 1024 * 1024 : 96 * 1024;
    auto &queued = rel ? c.control_bytes : c.dgram_bytes;
    if (queued.fetch_add(bytes) + bytes > lim) {
        queued -= bytes;
        return false;
    }
    auto snd = std::make_unique<Send>();
    snd->c = &c;
    snd->reliable = rel;
    if (rel) {
        auto n = frame->size();
        auto prefixed = std::make_shared<Bytes>();
        prefixed->reserve(n + 4);
        for (int i = 3; i >= 0; --i)
            prefixed->push_back(B(n >> (8 * i)));
        prefixed->insert(prefixed->end(), frame->begin(), frame->end());
        snd->data = std::move(prefixed);
    } else
        snd->data = std::move(frame);
    snd->buf = {std::uint32_t(snd->data->size()), const_cast<B *>(snd->data->data())};
    auto *token = snd.release();
    QUIC_STATUS st = QUIC_STATUS_INVALID_STATE;
    const bool bulk = rel && bytes > 16384 && (t == 0x60 || t == 0x64 || t == 0x66);
    if (bulk) {
        auto bs = new S{&c, nullptr, true, {}};
        st = q->StreamOpen(c.h, QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL, stream_cb, bs, &bs->h);
        if (QUIC_SUCCEEDED(st)) {
            st = q->StreamSend(bs->h, &token->buf, 1,
                               QUIC_SEND_FLAGS(QUIC_SEND_FLAG_START | QUIC_SEND_FLAG_FIN), token);
            if (QUIC_FAILED(st)) {
                q->StreamClose(bs->h);
                delete bs;
            }
        } else
            delete bs;
    } else if (rel) {
        auto h = c.stream.load();
        if (h)
            st = q->StreamSend(h, &token->buf, 1, QUIC_SEND_FLAG_NONE, token);
    } else
        st = q->DatagramSend(c.h, &token->buf, 1, QUIC_SEND_FLAG_NONE, token);
    if (QUIC_FAILED(st)) {
        queued -= bytes;
        delete token;
        return false;
    }
    return true;
}
bool Endpoint::Impl::watch(C &c, const Json &j) {
    if (!c.ready)
        return false;
    auto now = std::chrono::steady_clock::now();
    if (now - purge >= std::chrono::seconds(10)) {
        for (auto i = views.begin(); i != views.end();) {
            if (now - i->second.used > std::chrono::seconds(15))
                i = views.erase(i);
            else
                ++i;
        }
        purge = now;
    }
    ViewKey key{j.at("room_id").get<std::string>(), j.at("round_epoch").get<std::uint64_t>(),
                j.at("timeline_epoch").get<std::uint64_t>(), j.at("pov").get<int>()};
    auto &v = views[key];
    v.used = now;
    auto window = j.at("tick").get<std::int64_t>() / 20;
    if (!v.id || v.window != window) {
        v.id = ++serial;
        v.window = window;
        v.base = j;
        v.base["events"] = Json::array();
        v.last = nullptr;
        v.initial = std::make_shared<Bytes>(
            wire::encode(Json{{"type", "_spec_base"}, {"base", v.id}, {"sample", v.base}}, true));
    }
    if (c.sent_base.load() != v.id) {
        c.sent_base = v.id;
        c.acked_base = 0;
        if (!post(c, v.initial, 0x64, true))
            c.sent_base = 0;
        else
            ++c.base_sent;
    }
    // Within a room the feed is shared, but late joiners may legitimately have
    // different batched events. Compare the actual view before reusing cleartext.
    if (j != v.last) {
        v.last = j;
        v.full = std::make_shared<Bytes>(wire::encode(j, false));
        auto patch = wire::delta(v.base, j);
        v.delta = std::make_shared<Bytes>(wire::encode(
            Json{{"type", "delta"}, {"base", v.id}, {"patch", Json::binary(std::move(patch))}}, false));
    }
    bool delta = c.acked_base == v.id && v.delta->size() < v.full->size();
    bool ok = post(c, delta ? v.delta : v.full, delta ? 0x42 : 0x41, false);
    if (ok && delta)
        ++c.delta_sent;
    return ok;
}
void Endpoint::Impl::frame(C &c, const B *p, size_t n, bool rel) {
    if (c.server && n >= 9 && p[8])
        throw std::runtime_error("client control must not use compression");
    auto j = wire::decode(p, n, rel);
    auto t = wire::type(j);
    if (!c.ready) {
        if (!rel)
            return; // A data packet can overtake the reliable HelloAck.
        if (t != (c.server ? 1 : 2) || j.at("base_id") != "flyback:vanilla" || j.at("proto_major") != 1)
            throw std::runtime_error("ERR_PROTO_MAJOR_MISMATCH");
        if (j.at("profile") != 0)
            throw std::runtime_error("ERR_PROFILE_MISMATCH");
        if (!j.at("required_extensions").is_array() || !j.at("required_extensions").empty())
            throw std::runtime_error("ERR_MISSING_EXTENSION");
        if (j.at("datagram") != true)
            throw std::runtime_error("DATAGRAM required");
        if (c.server && !submit(c, hello(true), true, true))
            throw std::runtime_error("HelloAck send failed");
        c.ready = true;
        push({EventKind::connected,
              c.id,
              {{"host", c.host},
               {"fingerprint", c.server ? fp : c.actual},
               {"base_id", "flyback:vanilla"},
               {"profile", 0},
               {"proto_major", 1},
               {"proto_minor", 0}}});
        return;
    }
    if (t <= 2)
        throw std::runtime_error("unexpected repeated handshake");
    const auto label = j.value("type", std::string{});
    if (label == "_spec_base") {
        if (c.server || !rel || !j.at("sample").is_object() ||
            j.at("sample").value("type", "") != "snapshot")
            throw std::runtime_error("invalid observation baseline");
        c.recv_id = j.at("base").get<std::uint64_t>();
        c.recv_base = std::move(j["sample"]);
        if (!submit(c, {{"type", "_base_ack"}, {"base", c.recv_id}}, true))
            throw std::runtime_error("baseline acknowledgement failed");
        return;
    }
    if (label == "_base_ack" || label == "_base_need") {
        if (!c.server || !rel)
            throw std::runtime_error("unexpected baseline response");
        auto id = j.at("base").get<std::uint64_t>();
        if (id == c.sent_base) {
            if (label == "_base_ack")
                c.acked_base = id;
            else
                c.sent_base = 0;
        }
        return;
    }
    if (label == "delta") {
        if (c.server || rel || !j.at("patch").is_binary())
            throw std::runtime_error("unexpected spectator delta");
        auto id = j.at("base").get<std::uint64_t>();
        if (id != c.recv_id) {
            if (id > c.recv_id)
                submit(c, {{"type", "_base_need"}, {"base", id}}, true);
            return;
        }
        auto &patch = j.at("patch").get_binary();
        j = wire::apply(c.recv_base, patch);
        if (j.value("type", "") != "snapshot")
            throw std::runtime_error("invalid reconstructed snapshot");
    }
    if (c.server && !j.contains("op"))
        throw std::runtime_error("expected client command");
    if (!rel && t != 0x40 && t != 0x41 && t != 0x42 && t != 0x43)
        throw std::runtime_error("control message on datagram");
    if (!push({EventKind::message, c.id, std::move(j), rel}, n) && rel)
        throw std::runtime_error("peer control queue limit");
}
QUIC_STATUS QUIC_API Endpoint::Impl::conn_cb(HQUIC h, void *ctx, QUIC_CONNECTION_EVENT *e) {
    auto &c = *static_cast<C *>(ctx);
    auto &o = *c.own;
    try {
        switch (e->Type) {
        case QUIC_CONNECTION_EVENT_PEER_CERTIFICATE_RECEIVED: {
            auto &pc = e->PEER_CERTIFICATE_RECEIVED;
            auto *b = (QUIC_BUFFER *)pc.Certificate;
            if (c.server || !b)
                return QUIC_STATUS_BAD_CERTIFICATE;
            auto cert = inspect_cert(b->Buffer, b->Length, c.host);
            c.actual = cert.pin;
            if (!cert.current) {
                c.why = "certificate_expired_or_not_yet_valid";
                return QUIC_STATUS_BAD_CERTIFICATE;
            }
            if (!c.pinned.empty()) {
                if (c.pinned == cert.pin)
                    return QUIC_STATUS_SUCCESS;
                c.why = "certificate_pin_changed";
                return QUIC_STATUS_BAD_CERTIFICATE;
            }
            if (QUIC_SUCCEEDED(pc.DeferredStatus) && cert.name_ok)
                return QUIC_STATUS_SUCCESS;
            if (!cert.self_signed) {
                c.why = "certificate_chain_or_hostname_invalid";
                return QUIC_STATUS_BAD_CERTIFICATE;
            }
            c.cert_pending = true;
            o.push({EventKind::trust_required, c.id, {{"host", c.host}, {"fingerprint", cert.pin}}});
            return QUIC_STATUS_PENDING;
        }
        case QUIC_CONNECTION_EVENT_CONNECTED:
            if (!c.server) {
                check(
                    o.q->StreamOpen(h, QUIC_STREAM_OPEN_FLAG_NONE, stream_cb, &c.control, &c.control.h),
                    "open control");
                c.stream.store(c.control.h);
                check(o.q->StreamStart(c.control.h, QUIC_STREAM_START_FLAG_IMMEDIATE), "start control");
                if (!o.submit(c, hello(false), true, true))
                    throw std::runtime_error("Hello send failed");
            }
            break;
        case QUIC_CONNECTION_EVENT_PEER_STREAM_STARTED: {
            auto v = e->PEER_STREAM_STARTED;
            bool uni = (v.Flags & QUIC_STREAM_OPEN_FLAG_UNIDIRECTIONAL) != 0;
            S *s = nullptr;
            if (!uni && c.server && !c.stream.load()) {
                s = &c.control;
                s->h = v.Stream;
                c.stream.store(v.Stream);
            } else if (uni && c.ready) {
                s = new S{&c, v.Stream, true, {}};
            } else {
                o.q->StreamClose(v.Stream);
                throw std::runtime_error("unexpected stream");
            }
            o.q->SetCallbackHandler(v.Stream, (void *)stream_cb, s);
            break;
        }
        case QUIC_CONNECTION_EVENT_DATAGRAM_STATE_CHANGED:
            c.mtu = e->DATAGRAM_STATE_CHANGED.SendEnabled ? e->DATAGRAM_STATE_CHANGED.MaxSendLength : 0;
            break;
        case QUIC_CONNECTION_EVENT_DATAGRAM_RECEIVED:
            if (!(e->DATAGRAM_RECEIVED.Flags & QUIC_RECEIVE_FLAG_0_RTT))
                o.frame(c, e->DATAGRAM_RECEIVED.Buffer->Buffer, e->DATAGRAM_RECEIVED.Buffer->Length,
                        false);
            break;
        case QUIC_CONNECTION_EVENT_DATAGRAM_SEND_STATE_CHANGED:
            if (QUIC_DATAGRAM_SEND_STATE_IS_FINAL(e->DATAGRAM_SEND_STATE_CHANGED.State)) {
                auto *t = (Send *)e->DATAGRAM_SEND_STATE_CHANGED.ClientContext;
                if (t) {
                    (t->reliable ? c.control_bytes : c.dgram_bytes) -= t->data->size();
                    delete t;
                }
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_TRANSPORT:
            if (c.why.empty()) {
                auto st = e->SHUTDOWN_INITIATED_BY_TRANSPORT.Status;
                c.why = (st == QUIC_STATUS_CONNECTION_TIMEOUT || st == QUIC_STATUS_UNREACHABLE)
                            ? "udp_unreachable_or_timeout"
                            : ("transport_" + std::to_string(std::uint32_t(st)));
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_INITIATED_BY_PEER:
            if (c.why.empty()) {
                auto ec = e->SHUTDOWN_INITIATED_BY_PEER.ErrorCode;
                c.why = ec == 0x101   ? "ERR_PROTO_MAJOR_MISMATCH"
                        : ec == 0x102 ? "ERR_MISSING_EXTENSION"
                        : ec == 0x103 ? "ERR_PROFILE_MISMATCH"
                        : ec == 0x100 ? "protocol_rejected"
                                      : "peer_closed";
            }
            break;
        case QUIC_CONNECTION_EVENT_SHUTDOWN_COMPLETE:
            c.ready = false;
            c.cert_pending = false;
            {
                std::lock_guard lock(o.mx);
                c.stopped = true;
            }
            o.ended.notify_all();
            o.push({EventKind::disconnected,
                    c.id,
                    {{"reason", c.why.empty() ? "closed" : c.why},
                     {"host", c.host},
                     {"fingerprint", c.actual}}});
            break;
        default:
            break;
        }
    } catch (const std::exception &x) {
        o.fail(c, x.what());
    }
    return QUIC_STATUS_SUCCESS;
}
QUIC_STATUS QUIC_API Endpoint::Impl::stream_cb(HQUIC h, void *ctx, QUIC_STREAM_EVENT *e) {
    auto &s = *static_cast<S *>(ctx);
    auto &c = *s.c;
    auto &o = *c.own;
    try {
        switch (e->Type) {
        case QUIC_STREAM_EVENT_RECEIVE: {
            if (e->RECEIVE.Flags & QUIC_RECEIVE_FLAG_0_RTT)
                throw std::runtime_error("0-RTT disabled");
            for (std::uint32_t i = 0; i < e->RECEIVE.BufferCount; i++) {
                const auto &b = e->RECEIVE.Buffers[i];
                s.rx.insert(s.rx.end(), b.Buffer, b.Buffer + b.Length);
            }
            size_t at = 0;
            while (s.rx.size() - at >= 4) {
                auto *p = s.rx.data() + at;
                size_t n = std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 |
                           std::uint32_t(p[2]) << 8 | p[3];
                if (n < 10 || n > (c.server ? 65536 : wire::max_payload + 16))
                    throw std::runtime_error("invalid stream frame length");
                if (s.rx.size() - at < n + 4)
                    break;
                if (s.bulk && p[11] != 0x60 && p[11] != 0x64 && p[11] != 0x66)
                    throw std::runtime_error("unexpected bulk frame");
                o.frame(c, p + 4, n, true);
                at += n + 4;
            }
            if (at)
                s.rx.erase(s.rx.begin(), s.rx.begin() + at);
            break;
        }
        case QUIC_STREAM_EVENT_PEER_SEND_SHUTDOWN:
            if (!s.rx.empty())
                throw std::runtime_error("truncated control stream");
            if (!s.bulk && !c.stopped)
                o.q->ConnectionShutdown(c.h, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
            break;
        case QUIC_STREAM_EVENT_PEER_SEND_ABORTED:
            if (!s.bulk)
                o.q->ConnectionShutdown(c.h, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 1);
            break;
        case QUIC_STREAM_EVENT_SEND_COMPLETE: {
            auto *t = (Send *)e->SEND_COMPLETE.ClientContext;
            if (t) {
                (t->reliable ? c.control_bytes : c.dgram_bytes) -= t->data->size();
                delete t;
            }
            break;
        }
        case QUIC_STREAM_EVENT_SHUTDOWN_COMPLETE:
            if (s.bulk) {
                o.q->StreamClose(h);
                delete &s;
            } else
                c.stream = nullptr;
            break;
        default:
            break;
        }
    } catch (const std::exception &x) {
        o.fail(c, x.what());
    }
    return QUIC_STATUS_SUCCESS;
}
Endpoint::Endpoint() : p_(std::make_unique<Impl>()) {}
Endpoint::~Endpoint() = default;
void Endpoint::listen(const std::string &ip, std::uint16_t port, const std::filesystem::path &dir) {
    auto &o = *p_;
    if (o.listener)
        throw std::logic_error("already listening");
    QUIC_ADDR addr{};
    if (ip == "*" || ip.empty()) {
        QuicAddrSetFamily(&addr, QUIC_ADDRESS_FAMILY_UNSPEC);
    } else {
        addrinfo hints{}, *ai = nullptr;
        hints.ai_family = AF_UNSPEC;
        hints.ai_flags = AI_NUMERICHOST;
        hints.ai_socktype = SOCK_DGRAM;
        if (getaddrinfo(ip.c_str(), nullptr, &hints, &ai) != 0)
            throw std::invalid_argument("invalid listen interface");
        memcpy(&addr, ai->ai_addr, std::min<size_t>(sizeof(addr), ai->ai_addrlen));
        freeaddrinfo(ai);
    }
    QuicAddrSetPort(&addr, port);
    std::uint16_t retry_threshold = 0;
    check(o.q->SetParam(nullptr, QUIC_PARAM_GLOBAL_RETRY_MEMORY_PERCENT, sizeof(retry_threshold),
                        &retry_threshold),
          "enable address validation");
    auto id = load_identity(dir);
    HQUIC conf = o.config(true);
    HQUIC listener = nullptr;
    try {
        QUIC_CERTIFICATE_PKCS12 bundle{id.p12.data(), static_cast<std::uint32_t>(id.p12.size()), ""};
        QUIC_CREDENTIAL_CONFIG cr{};
        cr.Type = QUIC_CREDENTIAL_TYPE_CERTIFICATE_PKCS12;
        cr.CertificatePkcs12 = &bundle;
        auto st = o.q->ConfigurationLoadCredential(conf, &cr);
        OPENSSL_cleanse(id.p12.data(), id.p12.size());
        check(st, "load server identity");
        check(o.q->ListenerOpen(o.reg, Impl::listen_cb, &o, &listener), "open listener");
        o.servercfg = conf;
        o.fp = id.pin;
        check(o.q->ListenerStart(listener, &alpn, 1, &addr), "bind UDP");
        QUIC_ADDR actual{};
        std::uint32_t len = sizeof(actual);
        check(o.q->GetParam(listener, QUIC_PARAM_LISTENER_LOCAL_ADDRESS, &len, &actual), "bound port");
        o.port = QuicAddrGetPort(&actual);
        o.bind_address = ip;
        o.listener = listener;
    } catch (...) {
        if (listener)
            o.q->ListenerClose(listener);
        o.servercfg = nullptr;
        o.q->ConfigurationClose(conf);
        throw;
    }
}
std::uint16_t Endpoint::port() const {
    return p_->port;
}
std::string Endpoint::fingerprint() const {
    return p_->fp;
}
Peer Endpoint::connect(const std::string &host, std::uint16_t port, const std::string &pin) {
    if (host.empty() || !port)
        throw std::invalid_argument("missing destination");
    auto key = normalized(pin);
    auto &o = *p_;
    auto c = o.add(false);
    c->host = host;
    c->pinned = key;
    try {
        check(o.q->ConnectionOpen(o.reg, Impl::conn_cb, c.get(), &c->h), "open connection");
        check(o.q->ConnectionStart(c->h, o.clientcfg, QUIC_ADDRESS_FAMILY_UNSPEC, host.c_str(), port),
              "start connection");
    } catch (...) {
        if (c->h)
            o.q->ConnectionClose(c->h);
        std::lock_guard l(o.mx);
        o.peers.erase(c->id);
        throw;
    }
    return c->id;
}
void Endpoint::trust(Peer peer, bool accept) {
    auto c = p_->find(peer);
    if (c && c->cert_pending.exchange(false)) {
        check(p_->q->ConnectionCertificateValidationComplete(
                  c->h, accept ? TRUE : FALSE,
                  accept ? QUIC_TLS_ALERT_CODE_SUCCESS : QUIC_TLS_ALERT_CODE_BAD_CERTIFICATE),
              "finish certificate check");
    }
}
std::optional<Event> Endpoint::poll() {
    auto &o = *p_;
    o.mdns.pump([&](Json v) { o.push({EventKind::discovered, 0, std::move(v)}); });
    std::optional<Event> e;
    std::shared_ptr<Impl::C> released;
    {
        std::lock_guard l(o.mx);
        if (o.inbox.empty())
            return {};
        auto charge = o.inbox.front().second;
        e = std::move(o.inbox.front().first);
        o.inbox.pop_front();
        o.inbox_bytes -= charge;
        auto it = o.peers.find(e->peer);
        if (it != o.peers.end()) {
            if (e->kind == EventKind::message) {
                it->second->msg_count--;
                it->second->received_charge -= charge;
            }
            if (e->kind == EventKind::disconnected) {
                released = it->second;
                o.peers.erase(it);
            }
        }
    }
    if (released) {
        if (released->control.h)
            o.q->StreamClose(released->control.h);
        o.q->ConnectionClose(released->h);
    }
    return e;
}
bool Endpoint::send(Peer p, const Json &j, bool rel) {
    auto c = p_->find(p);
    if (!c)
        return false;
    if (!rel && c->server && j.value("type", "") == "snapshot" &&
        !j.contains("last_applied_input_seq") && j.contains("room_id") && j.contains("pov"))
        return p_->watch(*c, j);
    return p_->submit(*c, j, rel);
}
void Endpoint::disconnect(Peer p) {
    auto c = p_->find(p);
    if (c)
        p_->q->ConnectionShutdown(c->h, QUIC_CONNECTION_SHUTDOWN_FLAG_NONE, 0);
}
Json Endpoint::stats(Peer p) const {
    auto c = p_->find(p);
    if (!c)
        return Json::object();
    QUIC_STATISTICS_V2 st{};
    std::uint32_t n = sizeof(st);
    if (QUIC_FAILED(p_->q->GetParam(c->h, QUIC_PARAM_CONN_STATISTICS_V2, &n, &st)))
        return Json::object();
    return {{"rtt_us", st.Rtt},
            {"bytes_sent", st.SendTotalBytes},
            {"stateless_retry", bool(st.StatelessRetry)},
            {"bytes_received", st.RecvTotalBytes},
            {"max_datagram", c->mtu.load()},
            {"queued_bytes", c->control_bytes.load() + c->dgram_bytes.load()},
            {"spectator_bases", c->base_sent.load()},
            {"spectator_deltas", c->delta_sent.load()},
            {"host", c->host}};
}
void Endpoint::advertise(const std::string &s) {
    if (!p_->port)
        throw std::logic_error("listen before advertise");
    p_->mdns.advertise(s, p_->port, p_->fp, p_->bind_address);
}
void Endpoint::discover() {
    p_->mdns.browse();
}
} // namespace flyback::net
