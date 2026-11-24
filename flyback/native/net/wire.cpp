#include "wire.hpp"
#include <array>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <zstd.h>
namespace flyback::net::wire {
using S = std::string_view;
using U = std::uint64_t;
using B = std::uint8_t;
// Stable v1 dictionary: append only. Unknown keys are literal strings, never process-local IDs.
static constexpr S fields[] = {"",
                               "type",
                               "op",
                               "room_id",
                               "round_epoch",
                               "timeline_epoch",
                               "tick",
                               "state",
                               "players",
                               "darts",
                               "events",
                               "t",
                               "p",
                               "d",
                               "s",
                               "seat",
                               "alive",
                               "vis",
                               "x",
                               "y",
                               "vx",
                               "vy",
                               "face",
                               "st",
                               "timer",
                               "ax",
                               "ay",
                               "cd",
                               "expose",
                               "kills",
                               "team",
                               "msg",
                               "left",
                               "age",
                               "id",
                               "actor",
                               "target",
                               "cause",
                               "ordinal",
                               "name",
                               "seq",
                               "target_tick",
                               "mx",
                               "my",
                               "act",
                               "action_id",
                               "last_applied_input_seq",
                               "map",
                               "phase",
                               "wave",
                               "warn",
                               "collapsed",
                               "status",
                               "mode",
                               "role",
                               "round",
                               "snapshot",
                               "data",
                               "state_tick",
                               "owner",
                               "kind",
                               "warning",
                               "active_wave",
                               "map_id",
                               "index",
                               "controller",
                               "seat_id",
                               "visible",
                               "held",
                               "speed",
                               "round_id",
                               "source",
                               "k",
                               "a",
                               "b",
                               "c",
                               "result",
                               "spectator",
                               "pov",
                               "winner",
                               "step",
                               "hp",
                               "reason",
                               "timeline",
                               "collapse",
                               "last_input_seq",
                               "fallen",
                               "finished",
                               "patch",
                               "sample",
                               "base"};
static const auto idx = []() {
    std::unordered_map<S, unsigned> r;
    for (unsigned i = 1; i < std::size(fields); ++i)
        r.emplace(fields[i], i);
    return r;
}();
enum T : B { Nil, No, Yes, UInt, SInt, F64, Str, Arr, Obj, Rows, Blob, Snapshot };
static void require(bool x, const char *m) {
    if (!x)
        throw std::runtime_error(m);
}
static S label(const Json &j) {
    if (!j.is_object())
        return {};
    for (const char *k : {"op", "type"}) {
        auto i = j.find(k);
        if (i != j.end() && i->is_string())
            return i->get_ref<const std::string &>();
    }
    return {};
}
B type(const Json &j) {
    const S n = label(j);
    static const std::unordered_map<S, B> types = {
        {"_hello", 1},        {"_hello_ack", 2},    {"identify", 3},      {"error", 5},
        {"server", 0x20},     {"list", 0x21},       {"directory", 0x22},  {"create", 0x23},
        {"join", 0x24},       {"room", 0x25},       {"seat", 0x26},       {"settings", 0x27},
        {"ready", 0x28},      {"watch", 0x29},      {"leave", 0x2a},      {"left", 0x2b},
        {"catalog", 0x2c},    {"again", 0x2d},      {"start", 0x2e},      {"lobby", 0x2f},
        {"field_ready", 0x30},
        {"_spec_base", 0x64}, {"_base_ack", 0x67},  {"_base_need", 0x63}, {"input", 0x40},
        {"snapshot", 0x41},   {"delta", 0x42},      {"event", 0x43},      {"manifest", 0x60},
        {"pause", 0x61},      {"vote", 0x61},       {"speed", 0x62},      {"baseline", 0x64},
        {"result", 0x65},     {"replay_clip", 0x66}};
    auto it = types.find(n);
    require(it != types.end(), "unknown FBAR message type");
    return it->second;
}
static void var(Bytes &o, U n) {
    while (n >= 128) {
        o.push_back(B(n) | 128);
        n >>= 7;
    }
    o.push_back(B(n));
}
static void str(Bytes &o, S s) {
    var(o, s.size());
    o.insert(o.end(), s.begin(), s.end());
}
static void key(Bytes &o, S s) {
    auto i = idx.find(s);
    if (i != idx.end())
        var(o, i->second);
    else {
        var(o, 0);
        str(o, s);
    }
}
static void enc(Bytes &, const Json &, unsigned);
struct Reader;
static Json read_snapshot(Reader &, unsigned);
static constexpr const char *moves[] = {"x",     "y",  "vx", "vy", "face",  "st",
                                        "timer", "ax", "ay", "cd", "expose"};
static constexpr const char *flight[] = {"x", "y", "vx", "vy", "left", "age"};
static constexpr S effects[] = {"",        "THROW", "LAUNCH", "LAND",  "PICK",       "TURN",
                                "CATCH",   "SLASH", "KICK",   "DASH",  "HIT",        "CUT",
                                "KICKHIT", "FALL",  "DEATH",  "WARN",  "COLLAPSE",   "END",
                                "BLOCK",   "CALL",  "CANCEL", "CLASH", "DASH_SLASH", "RECALL"};
static constexpr const char *numbers[] = {"tick", "ordinal", "a", "b", "c", "d"};
static bool isnum(const Json &j, const char *k) {
    auto t = j.find(k);
    return t != j.end() && t->is_number_integer() &&
           (!t->is_number_unsigned() || t->get<U>() <= U(INT64_MAX));
}
static bool isbool(const Json &j, const char *k) {
    auto v = j.find(k);
    return v != j.end() && v->is_boolean();
}
static void sint(Bytes &o, std::int64_t n) {
    var(o, n >= 0 ? U(n) * 2 : U(-(n + 1)) * 2 + 1);
}
static bool write_snapshot(Bytes &o, const Json &j, unsigned depth) {
    if (j.size() != 12 && j.size() != 13)
        return false;
    for (auto k : {"room_id", "round_epoch", "timeline_epoch", "pov", "tick", "players", "darts",
                   "events", "finished", "fallen", "warn", "type"})
        if (!j.contains(k))
            return false;
    bool ack = j.contains("last_applied_input_seq");
    if (j.size() != 12 + ack || !j["room_id"].is_string() || !isbool(j, "finished"))
        return false;
    for (auto k : {"round_epoch", "timeline_epoch", "tick", "pov", "warn", "fallen"})
        if (!isnum(j, k))
            return false;
    if (ack && !isnum(j, "last_applied_input_seq"))
        return false;
    auto &ps = j["players"];
    auto &ds = j["darts"];
    auto &es = j["events"];
    if (!ps.is_array() || !ds.is_array() || ps.size() > 4 || ps.size() != ds.size() || !es.is_array())
        return false;
    for (const auto &p : ps) {
        if (!p.is_object() || !isnum(p, "seat") || !isnum(p, "kills") || !isbool(p, "vis") ||
            !isbool(p, "alive"))
            return false;
        bool v = p["vis"];
        if (p.size() != (v ? 4 + std::size(moves) : 4))
            return false;
        if (v)
            for (auto k : moves)
                if (!isnum(p, k))
                    return false;
    }
    for (const auto &d : ds) {
        if (!d.is_object() || !isnum(d, "seat") || !isnum(d, "st") || !isbool(d, "vis"))
            return false;
        bool v = d["vis"];
        if (d.size() != (v ? 3 + std::size(flight) : 3))
            return false;
        if (v)
            for (auto k : flight)
                if (!isnum(d, k))
                    return false;
    }
    o.push_back(Snapshot);
    str(o, j["room_id"].get_ref<const std::string &>());
    for (auto k : {"round_epoch", "timeline_epoch", "tick", "pov", "fallen", "warn"})
        sint(o, j[k].get<std::int64_t>());
    o.push_back(B(ack) | (j["finished"].get<bool>() ? 2 : 0));
    if (ack)
        sint(o, j["last_applied_input_seq"].get<std::int64_t>());
    o.push_back(B(ps.size()));
    for (const auto &p : ps) {
        bool v = p["vis"];
        sint(o, p["seat"]);
        o.push_back(B(v) | (p["alive"].get<bool>() ? 2 : 0));
        sint(o, p["kills"]);
        if (v)
            for (auto k : moves)
                sint(o, p[k]);
    }
    for (const auto &d : ds) {
        bool v = d["vis"];
        sint(o, d["seat"]);
        sint(o, d["st"]);
        o.push_back(B(v));
        if (v)
            for (auto k : flight)
                sint(o, d[k]);
    }
    var(o, es.size());
    for (const auto &e : es) {
        bool known = e.is_object() && e.size() == 7 && e.contains("type") && e["type"].is_string();
        if (known)
            for (auto k : numbers)
                if (!isnum(e, k)) {
                    known = false;
                    break;
                }
        auto eff = known ? std::find(std::begin(effects) + 1, std::end(effects),
                                     e["type"].get_ref<const std::string &>())
                         : std::end(effects);
        if (eff != std::end(effects)) {
            o.push_back(B(eff - std::begin(effects)));
            for (auto k : numbers)
                sint(o, e[k]);
        } else {
            o.push_back(0);
            enc(o, e, depth + 1);
        }
    }
    return true;
}
static void enc(Bytes &o, const Json &j, unsigned depth) {
    require(depth < 48 && o.size() < max_payload, "FBAR frame too large/deep");
    if (j.is_null()) {
        o.push_back(Nil);
        return;
    }
    if (j.is_boolean()) {
        o.push_back(j.get<bool>() ? Yes : No);
        return;
    }
    if (j.is_number_unsigned()) {
        o.push_back(UInt);
        var(o, j.get<U>());
        return;
    }
    if (j.is_number_integer()) {
        auto n = j.get<std::int64_t>();
        if (n >= 0) {
            o.push_back(UInt);
            var(o, U(n));
        } else {
            o.push_back(SInt);
            var(o, U(-(n + 1)));
        }
        return;
    }
    if (j.is_number_float()) {
        double d = j.get<double>();
        U b;
        memcpy(&b, &d, 8);
        o.push_back(F64);
        for (int i = 7; i >= 0; --i)
            o.push_back(B(b >> (8 * i)));
        return;
    }
    if (j.is_binary()) {
        o.push_back(Blob);
        const auto &b = j.get_binary();
        var(o, b.size());
        o.insert(o.end(), b.begin(), b.end());
        return;
    }
    if (j.is_string()) {
        o.push_back(Str);
        str(o, j.get_ref<const std::string &>());
        return;
    }
    if (j.is_array()) {
        bool rows = j.size() > 1 && j[0].is_object() && !j[0].empty();
        if (rows)
            for (auto &v : j) {
                if (!v.is_object() || v.size() != j[0].size()) {
                    rows = false;
                    break;
                }
                auto it = v.begin();
                for (auto kh = j[0].begin(); kh != j[0].end(); ++kh, ++it)
                    if (kh.key() != it.key()) {
                        rows = false;
                        break;
                    }
                if (!rows)
                    break;
            }
        o.push_back(rows ? Rows : Arr);
        var(o, j.size());
        if (rows) {
            var(o, j[0].size());
            for (auto i = j[0].begin(); i != j[0].end(); ++i)
                key(o, i.key());
            for (auto &v : j)
                for (auto &f : v)
                    enc(o, f, depth + 1);
        } else
            for (auto &v : j)
                enc(o, v, depth + 1);
        return;
    }
    require(j.is_object(), "unsupported FBAR value");
    if (label(j) == "snapshot" && write_snapshot(o, j, depth))
        return;
    o.push_back(Obj);
    var(o, j.size());
    for (auto i = j.begin(); i != j.end(); ++i) {
        key(o, i.key());
        enc(o, *i, depth + 1);
    }
}
struct Reader {
    const B *p, *end;
    size_t nodes = 0;
    B byte() {
        require(p < end, "truncated FBAR");
        return *p++;
    }
    U vi() {
        U v = 0;
        for (int i = 0; i <= 63; i += 7) {
            B b = byte();
            if (i == 63)
                require(b <= 1, "invalid uint64");
            v |= (U(b & 127) << i);
            if (!(b & 128))
                return v;
        }
        throw std::runtime_error("invalid varint");
    }
    std::string string() {
        U n = vi();
        require(n <= U(end - p), "truncated FBAR string");
        const B *start = p;
        const B *last = p + n;
        while (p < last) {
            auto c = *p++;
            if (c < 128)
                continue;
            unsigned k = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
            require(c >= 0xc2 && c <= 0xf4 && size_t(last - p) >= k, "invalid UTF-8");
            if ((c == 0xe0 && *p < 0xa0) || (c == 0xed && *p >= 0xa0) || (c == 0xf0 && *p < 0x90) ||
                (c == 0xf4 && *p >= 0x90))
                throw std::runtime_error("invalid UTF-8 code point");
            while (k--) {
                require((*p++ & 0xc0) == 0x80, "invalid UTF-8 continuation");
            }
        }
        return {reinterpret_cast<const char *>(start), size_t(n)};
    }
    std::string field() {
        U n = vi();
        require(n < std::size(fields), "unknown field index");
        return n ? std::string(fields[n]) : string();
    }
    Json value(unsigned depth) {
        require(++nodes <= 300000 && depth < 48, "FBAR nesting/element limit");
        switch (byte()) {
        case Nil:
            return nullptr;
        case No:
            return false;
        case Yes:
            return true;
        case UInt:
            return vi();
        case SInt: {
            auto n = vi();
            require(n <= U(INT64_MAX), "negative overflow");
            return -std::int64_t(n) - 1;
        }
        case F64: {
            U v = 0;
            for (int i = 0; i < 8; i++)
                v = v * 256 + byte();
            double d;
            memcpy(&d, &v, 8);
            require(std::isfinite(d), "nonfinite");
            return d;
        }
        case Blob: {
            auto n = vi();
            require(n <= U(end - p), "invalid byte string");
            Bytes b(p, p + n);
            p += n;
            return Json::binary(std::move(b));
        }
        case Snapshot:
            return read_snapshot(*this, depth);
        case Str:
            return string();
        case Arr: {
            U n = vi();
            require(n <= U(end - p) && n <= 300000, "FBAR array bound");
            Json j = Json::array();
            while (n--)
                j.push_back(value(depth + 1));
            return j;
        }
        case Obj: {
            U n = vi();
            require(n <= U(end - p) / 2 && n < 65536, "FBAR object bound");
            Json j = Json::object();
            while (n--) {
                auto k = field();
                require(!j.contains(k), "duplicate field");
                j.emplace(k, value(depth + 1));
            }
            return j;
        }
        case Rows: {
            U n = vi(), m = vi();
            require(n > 0 && m > 0 && m < 256 && n <= U(end - p) / m && n * m <= 300000,
                    "FBAR rows bound");
            std::vector<std::string> keys;
            Json seen = Json::object();
            while (m--) {
                auto k = field();
                require(!seen.contains(k), "duplicate row field");
                seen[k] = true;
                keys.push_back(std::move(k));
            }
            Json a = Json::array();
            while (n--) {
                Json j = Json::object();
                for (auto &k : keys)
                    j.emplace(k, value(depth + 1));
                a.push_back(std::move(j));
            }
            return a;
        }
        default:
            throw std::runtime_error("invalid FBAR value");
        }
    }
};
static Json read_snapshot(Reader &r, unsigned depth) {
    auto sn = [&]() {
        auto z = r.vi();
        return z & 1 ? -std::int64_t(z / 2) - 1 : std::int64_t(z / 2);
    };
    Json j = {{"type", "snapshot"}, {"room_id", r.string()}};
    for (auto k : {"round_epoch", "timeline_epoch", "tick", "pov", "fallen", "warn"})
        j[k] = sn();
    auto f = r.byte();
    require(f < 4, "invalid snapshot flags");
    j["finished"] = bool(f & 2);
    if (f & 1)
        j["last_applied_input_seq"] = sn();
    auto count = r.byte();
    require(count <= 4, "invalid player count");
    auto ps = Json::array(), ds = Json::array(), events = Json::array();
    for (int i = 0; i < count; ++i) {
        Json p = Json::object();
        p["seat"] = sn();
        auto v = r.byte();
        require(v < 4, "invalid player flags");
        p["vis"] = bool(v & 1);
        p["alive"] = bool(v & 2);
        p["kills"] = sn();
        if (v & 1)
            for (auto k : moves)
                p[k] = sn();
        ps.push_back(std::move(p));
    }
    for (int i = 0; i < count; ++i) {
        Json d = Json::object();
        d["seat"] = sn();
        d["st"] = sn();
        auto v = r.byte();
        require(v < 2, "invalid dart flags");
        d["vis"] = bool(v);
        if (v)
            for (auto k : flight)
                d[k] = sn();
        ds.push_back(std::move(d));
    }
    auto n = r.vi();
    require(n <= U(r.end - r.p) && n <= 2048, "invalid event count");
    while (n--) {
        auto e = r.byte();
        if (!e) {
            events.push_back(r.value(depth + 1));
            continue;
        }
        require(e < std::size(effects), "unknown event code");
        Json ev = {{"type", effects[e]}};
        for (auto k : numbers)
            ev[k] = sn();
        events.push_back(std::move(ev));
    }
    j["players"] = std::move(ps);
    j["darts"] = std::move(ds);
    j["events"] = std::move(events);
    return j;
}
static bool pub(B t) {
    return t == 0x60 || t == 0x64 || t == 0x66;
}
Bytes encode(const Json &j, bool stream) {
    B t = type(j);
    Bytes b;
    b.reserve(256);
    enc(b, j, 0);
    require(b.size() <= max_payload, "FBAR limit");
    Bytes out = {'F', 'B', 'A', 'R', 1, 0, 0, t, 0};
    if (stream && !j.contains("op") && pub(t) && b.size() >= 512) {
        Bytes c(ZSTD_compressBound(b.size()));
        size_t s = ZSTD_compress(c.data(), c.size(), b.data(), b.size(), 1);
        if (!ZSTD_isError(s) && s + 20 <= b.size()) {
            out[8] = 1;
            var(out, b.size());
            out.insert(out.end(), c.begin(), c.begin() + s);
            return out;
        }
    }
    out.insert(out.end(), b.begin(), b.end());
    return out;
}
Json decode(const B *p, size_t n, bool stream) {
    require(n >= 10 && n <= max_payload + 16 && memcmp(p, "FBAR", 4) == 0, "invalid FBAR frame");
    require(p[4] == 1, "ERR_PROTO_MAJOR_MISMATCH");
    require(p[6] == 0, "ERR_PROFILE_MISMATCH");
    require(p[5] == 0 || p[7] == 1, "unnegotiated protocol minor");
    auto t = p[7];
    bool zip = p[8] != 0;
    require(p[8] <= 1 && (!zip || (stream && pub(t))), "invalid compression");
    Reader r{p + 9, p + n};
    Bytes temp;
    if (zip) {
        auto want = r.vi();
        require(want >= 512 && want <= max_payload, "decompression bound");
        temp.resize(size_t(want));
        size_t got = ZSTD_decompress(temp.data(), temp.size(), r.p, r.end - r.p);
        require(!ZSTD_isError(got) && got == want, "invalid zstd frame");
        r = {temp.data(), temp.data() + temp.size()};
    }
    auto j = r.value(0);
    require(r.p == r.end && type(j) == t, "FBAR message type mismatch");
    return j;
}
// Delta nodes: replacement, equal-shape object/array field mask, or a signed
// integer difference. A shape/visibility change replaces that node in full.
static bool structure(const Json &a, const Json &b) {
    if (a.is_object() && b.is_object() && a.size() == b.size()) {
        auto i = a.begin();
        for (auto j = b.begin(); j != b.end(); ++i, ++j)
            if (i.key() != j.key())
                return false;
        return true;
    }
    return a.is_array() && b.is_array() && a.size() == b.size();
}
static void differences(Bytes &out, const Json &a, const Json &b, unsigned d) {
    require(d < 48, "delta nesting bound");
    if (structure(a, b) && a.size() > 0 && a.size() <= 512) {
        out.push_back(1);
        auto mask = out.size();
        out.resize(mask + (a.size() + 7) / 8);
        auto i = a.begin();
        size_t k = 0;
        for (auto j = b.begin(); j != b.end(); ++i, ++j, ++k)
            if (*i != *j) {
                out[mask + k / 8] |= B(1U << (k % 8));
                differences(out, *i, *j, d + 1);
            }
        return;
    }
    if (a.is_number_integer() && b.is_number_integer() && a >= INT32_MIN && a <= INT32_MAX &&
        b >= INT32_MIN && b <= INT32_MAX) {
        auto delta = b.get<std::int64_t>() - a.get<std::int64_t>();
        out.push_back(2);
        var(out, delta >= 0 ? U(delta) * 2 : U(-(delta + 1)) * 2 + 1);
        return;
    }
    out.push_back(0);
    enc(out, b, d);
}
Bytes delta(const Json &base, const Json &current) {
    Bytes o;
    differences(o, base, current, 0);
    return o;
}
static void restore(Reader &r, Json &v, unsigned depth) {
    require(depth < 48, "delta depth");
    auto tag = r.byte();
    if (tag == 0) {
        v = r.value(depth);
        return;
    }
    if (tag == 2) {
        require(v.is_number_integer() && v >= INT32_MIN && v <= INT32_MAX, "delta numeric baseline");
        auto z = r.vi();
        require(z <= U(UINT32_MAX) * 2 + 1, "delta integer overrun");
        auto d = z & 1 ? -std::int64_t(z / 2) - 1 : std::int64_t(z / 2);
        auto n = v.get<std::int64_t>() + d;
        require(n >= INT32_MIN && n <= INT32_MAX, "delta reconstruction overflow");
        v = n;
        return;
    }
    require(tag == 1 && (v.is_array() || v.is_object()) && v.size() > 0 && v.size() <= 512,
            "delta structure");
    auto bytes = (v.size() + 7) / 8;
    require(size_t(r.end - r.p) >= bytes, "delta mask length");
    auto mask = r.p;
    r.p += bytes;
    size_t k = 0;
    for (auto &i : v) {
        if ((mask[k / 8] >> (k % 8)) & 1)
            restore(r, i, depth + 1);
        ++k;
    }
}
Json apply(const Json &base, const Bytes &patch) {
    Json out = base;
    Reader r{patch.data(), patch.data() + patch.size()};
    restore(r, out, 0);
    require(r.p == r.end, "delta has trailing bytes");
    return out;
}
} // namespace flyback::net::wire
