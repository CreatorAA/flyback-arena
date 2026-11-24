#include "room.hpp"
#include <algorithm>
namespace fbar {
static J a() { return J::array(); }
static std::string s(const J &j, const char *k) { return j.value(k, std::string{}); }
static std::vector<std::string> maps_for(const J &cat, int n) {
    std::vector<std::string> out;
    for (auto &m : cat.at("maps"))
        if (m.value("enabled", true) &&
            std::find(m.at("seats").begin(), m.at("seats").end(), J(n)) != m.at("seats").end())
            out.push_back(s(m, "id"));
    return out;
}
Room::Room(std::string i, std::string md, Send fn)
    : send(std::move(fn)), rng(std::random_device{}()), map_rng(std::random_device{}()),bot_rng(std::random_device{}()), id(std::move(i)), mode(std::move(md)) {}
bool Room::active() const {
    return state == "playing" || state == "paused" || state == "loading" || state == "countdown";
}
int Room::humans() const {
    int n = 0;
    for (auto &[_, m] : members)
        n += m.seat >= 0;
    return n;
}
int Room::spectators() const {
    int n = 0;
    for (auto &[_, m] : members)
        n += m.seat < 0 || m.subscribed;
    return n;
}
int Room::alive_members() const { return (int)members.size(); }
bool Room::spec_full() const { return pressure>=2 || (spec_limit >= 0 && spectators() >= spec_limit); }
void Room::configure(const J &j, const J &cat, bool clear) {
    if (active())
        throw Error("room_locked", "比赛阵容已锁定");
    if (!j.is_object())
        throw Error("bad_settings", "房间设置格式不对");
    auto v = j;
    int n = v.value("seat_count", 2);
    if (n < 2 || n > 4)
        throw Error("bad_seats", "请选择 2、3 或 4 席");
    auto pool = maps_for(cat, n);
    if (pool.empty())
        throw Error("no_maps", "该模式没有可用地图");
    auto maps = v.value("maps", std::vector<std::string>{});
    if (maps.empty())
        maps = pool;
    for (auto &k : maps)
        if (std::find(pool.begin(), pool.end(), k) == pool.end())
            throw Error("bad_map", "地图不支持该人数或已停用：" + k);
    auto slots = v.value("slots", std::vector<std::string>{});
    if (slots.empty()) {
        slots.resize(n, "random");
        slots[0] = "human";
    }
    if ((int)slots.size() != n)
        throw Error("bad_seats", "席位配置与人数不符");
    std::set<std::string> picked;
    for (auto &k : slots)
        if (k != "human" && k != "random") {
            if (std::none_of(cat.at("bots").begin(), cat.at("bots").end(),
                             [&](auto &b) { return s(b, "id") == k && b.value("enabled", true); }))
                throw Error("bad_bot", "选手不可选：" + k);
            if (!v.value("allow_mirrors", false) && !picked.insert(k).second)
                throw Error("duplicate_bot", "镜像出战须先启用允许镜像");
        }
    auto strat = v.value("map_policy", std::string("fixed"));
    if (strat != "fixed" && strat != "rotate" && strat != "random")
        throw Error("bad_policy", "地图策略无效");
    for (auto &[_, m] : members)
        if (m.seat >= 0 && (m.seat >= n || slots[m.seat] != "human"))
            throw Error("occupied", "先让已入座的真人退席再改该席");
    auto t = v.value("title", std::string("飞返竞技场"));
    if (t.empty() || t.size() > 120)
        throw Error("bad_title", "房间名应为 1–120 字节");
    int repeats=v.value("rounds_per_map",1);auto overrides=v.value("map_rounds",J::object());
    if(repeats<1||repeats>99||!overrides.is_object()||maps.size()>64||std::set<std::string>(maps.begin(),maps.end()).size()!=maps.size())throw Error("bad_series","每图须为 1–99 回合，且地图不重复");
    for(auto it=overrides.begin();it!=overrides.end();++it)if(std::find(maps.begin(),maps.end(),it.key())==maps.end()||!it->is_number_integer()||it->get<int>()<1||it->get<int>()>99)throw Error("bad_series","指定图回合数无效");
    if(clear)for(auto&[_,m]:members)m.ready=false;
    const bool human_round = std::find(slots.begin(), slots.end(), "human") != slots.end();
    if (settings.is_null() || settings.at("slots") != slots)
        rate = human_round ? .8 : 1.;
    title=t;last_map.clear();queued_map.clear();series.clear();continue_series=false;
    settings = {{"title", title},      {"seat_count", n}, {"maps", maps},
                {"rounds_per_map",repeats},{"map_rounds",overrides},{"map_policy", strat}, {"slots", slots},  {"allow_mirrors", v.value("allow_mirrors", false)}};
    rev++;
}
J Room::summary() const {
    auto n = settings.value("seat_count", 2);
    return {{"id", id},
            {"room_id", id},
            {"title", title},
            {"name", title},
            {"state", state},
            {"speed", rate},
            {"mode", mode},
            {"seat_count", n},
            {"human_count", humans()},{"series",series.status()},
            {"spectator_count", spectators()},
            {"effective_spectator_limit", spec_limit},
            {"spectator_join_block_reason", pressure>=2 ? J("egress_budget") : spec_full() ? J("spectator_limit_reached") : J(nullptr)},
            {"has_password", !password.empty()},
            {"profile", 0},
            {"base_id", "flyback:vanilla"},
            {"required_extensions", a()},
            {"ruleset", "classic20"},
            {"map", last_map.empty() ? settings.at("maps").front() : J(last_map)},
            {"port", port},
            {"room_rev", rev},
            {"round_epoch", round},
            {"timeline_epoch", timeline}};
}
J Room::admin() const {
    J j = summary();
    auto n = settings.value("seat_count", 2);
    j["mode"] = n == 2 ? "duel" : n == 3 ? "ffa3" : "ffa4";
    j["config_revision"] = policy_rev;
    j["map_id"] = j["map"];
    auto &maps = settings.at("maps");
    j["next_map_id"] = series.upcoming(queued_map.empty()?maps.front().get<std::string>():queued_map);
    j["map_strategy"] = settings.at("map_policy");
    j["spectator_snapshot_hz"] = spec_hz;
    j["effective_spectator_hz"]=effective_hz();
    j["tick_tps"]=nullptr;j["tick_late_p99_ms"]=nullptr;
    if(state=="paused")j["tick_tps"]=0;
    if(state=="playing" && pacing.size()>1){
        const double wall=std::chrono::duration<double>(pacing.back().first-pacing.front().first).count();
        if(wall>0)j["tick_tps"]=double(pacing.size()-1)/wall;
        std::vector<double> late;for(auto& sample:pacing)late.push_back(sample.second);
        std::sort(late.begin(),late.end());j["tick_late_p99_ms"]=late[(99*late.size()+99)/100-1];
    }
    j["ai_count"] = 0;
    for (auto &x : settings["slots"])
        if (x != "human")
            j["ai_count"] = j["ai_count"].get<int>() + 1;
    j["members"] = a();
    for (auto &[_, m] : members)
        j["members"].push_back({{"member_id", m.id},
                                {"id", m.id},
                                {"name", m.name},
                                {"kind", "human"},
                                {"seat", m.seat},
                                {"ready", m.ready},
                                {"disconnected", false}});
    j["pause_vote"] =
        ballot.active ? J({{"cast", ballot.cast}, {"needed", electorate().size() / 2 + 1}}) : J(nullptr);
    if(ballot.active){
        auto &v=j["pause_vote"];v["required"]=v["needed"];
        v["yes_count"]=std::count_if(ballot.cast.begin(),ballot.cast.end(),[](auto&x){return x.second;});
        v["proposer"]=members.count(ballot.proposer)?members.at(ballot.proposer).name:"";
        v["seconds_left"]=std::max<int64_t>(0,std::chrono::duration_cast<std::chrono::seconds>(ballot.end-Clock::now()).count());
    }
    return j;
}
void Room::send_room(const std::string &who) {
    auto it = members.find(who);
    if (it == members.end())
        return;
    auto &m = it->second;
    J msg = summary();
    msg["type"] = "room";
    msg["effective_tick"] = hist.empty() ? 0 : hist.back()->views.front().value("tick", 0);
    msg["host_id"] = host;
    msg["self_id"] = who;
    msg["self_seat"] = m.seat;
    msg["view"] = m.view;
    msg["advertise"] = advertise;
    msg["settings"] = settings;
    msg["countdown_seconds"] = countdown_seconds;
    msg["field_ready_required"] = state == "loading" && m.participant && m.field_ready_capable;
    msg["members"] = a();
    msg["slots"] = a();
    for (auto &[_, x] : members)
        msg["members"].push_back({{"id", x.id},
                                  {"name", x.name},
                                  {"seat", x.seat},
                                  {"ready", x.ready},
                                  {"participant", x.participant}});
    int i = 0;
    for (auto &k : settings.at("slots")) {
        J z = {{"seat", i},
               {"kind", k == "human"    ? "human"
                        : k == "random" ? "random"
                                        : "bot"},
               {"name", k == "human" ? J("") : k},
               {"occupied", k != "human"},
               {"ready", k != "human"}};
        for (auto &[_, x] : members)
            if (x.seat == i) {
                z["occupied"] = true;
                z["ready"] = x.ready;
                z["name"] = x.name;
            }
        msg["slots"].push_back(z);
        i++;
    }
    msg["vote"] = nullptr;
    if (ballot.active) {
        int yes = 0;
        for (auto &[_, v] : ballot.cast)
            yes += v;
        msg["vote"] = {
            {"yes", yes},
            {"needed", electorate().size() / 2 + 1},
            {"votes", ballot.cast},
            {"eligible", electorate()},
            {"seconds_left", std::max(0., std::chrono::duration<double>(ballot.end - Clock::now()).count())}};
    }
    send(who, std::move(msg), true);
}
void Room::broadcast() {
    for (auto &[id, _] : members)
        send_room(id);
}
void Room::join(std::string who, std::string name, bool spectate, bool field_ready_capable) {
    if (members.count(who))
        return;
    int pos = -1;
    if ((state == "ready" || state == "finished") && !spectate) {
        int i = 0;
        for (auto &k : settings.at("slots")) {
            if (k == "human" &&
                std::none_of(members.begin(), members.end(), [&](auto &kv) { return kv.second.seat == i; })) {
                pos = i;
                break;
            }
            i++;
        }
        if (pos < 0 && !members.empty())
            throw Error("no_player_seat", "参赛席已满，可选择观战");
    }
    if (pos < 0 && spec_full())
        throw Error(pressure>=2?"egress_budget":"spectator_full", pressure>=2?"出站带宽已达预算，请稍后加入观战":"本房观战已满");
    Member m;
    m.id = who;
    m.name = name;
    m.seat = pos;
    m.field_ready_capable = field_ready_capable;
    m.view = -1;
    members.emplace(who, std::move(m));
    if (host.empty())
        host = who;
    rev++;
    if (active() && !manifest_.empty())
        manifest_to(who);
    broadcast();
}
void Room::leave(const std::string &who) {
    auto it = members.find(who);
    if (it == members.end())
        return;
    if (runner)
        runner->neutral(it->second.seat);
    if (it->second.participant)
        reset_ballot();
    members.erase(it);
    if (who == host) {
        host.clear();
        for (auto &[i, m] : members)
            if (m.seat >= 0) {
                host = i;
                break;
            }
        if (host.empty() && !members.empty())
            host = members.begin()->first;
    }
    rev++;
    if (members.empty())
        empty_since = Clock::now();
    if (state == "paused" && electorate().empty() && had_humans) {
        runner.reset();
        state = "ready";
        room_error("abandoned", "真人均已离开，已返回准备厅");
    }
    broadcast();
}
void Room::room_error(std::string c, std::string text) {
    for (auto &[p, _] : members)
        send(p, {{"type", "error"}, {"code", c}, {"message", text}}, true);
}
void Room::manifest_to(const std::string &p) {
    auto msg = manifest_;
    msg["type"] = "manifest";
    msg["round_epoch"] = round;
    send(p, std::move(msg), true);
}
void Room::reset_ballot() { ballot = {}; }
std::vector<std::string> Room::electorate() const {
    std::vector<std::string> v;
    for (auto &[id, m] : members)
        if (m.participant)
            v.push_back(id);
    return v;
}
void Room::change_clock(bool pause) {
    if (!runner || time_pending)
        return;
    time_pending = true;
    timeline++;
    runner->pause(pause);
    reset_ballot();
}
void Room::check_ballot() {
    auto e = electorate();
    int yes = 0;
    for (auto &[p, v] : ballot.cast)
        if (v && std::find(e.begin(), e.end(), p) != e.end())
            yes++;
    if (!e.empty() && size_t(yes) > e.size() / 2)
        change_clock(state != "paused");
    broadcast();
}
void Room::next_map(const std::string &k, const J &cat) {
    auto v = settings;
    auto maps = maps_for(cat, v.value("seat_count", 2));
    if (std::find(maps.begin(), maps.end(), k) == maps.end())
        throw Error("bad_map", "此图与席数不兼容");
    if(!series.set_next(k,settings))queued_map=k;
    rev++;
    policy_rev++;
    if (!active() && !holds_slot())
        for (auto &[_, m] : members)
            m.ready = false;
    broadcast();
}
void Room::op(const std::string &who, const J &j, const J &cat, const fs::path &assets, bool may_start) {
    auto it = members.find(who);
    if (it == members.end())
        throw Error("not_member", "请先加入房间");
    auto &m = it->second;
    auto op = s(j, "op");
    if (op == "field_ready") {
        if (state == "loading" && !hist.empty() && m.participant && m.field_ready_capable &&
            j.value("round_epoch", -1) == round && s(j, "room_id") == id)
            m.field_ready = true;
        return;
    }
    if (op == "input") {
        if (state == "playing" && m.participant && m.seat >= 0 && j.value("round_epoch", -1) == round &&
            j.value("timeline_epoch", -1) == timeline && runner)
            runner->input(m.seat, j);
        return;
    }
    if (op == "watch") {
        int view = j.value("view", -1);
        if (view < -1 || view >= settings.value("seat_count", 0))
            throw Error("bad_view", "没有这个视角");
        if (m.seat >= 0) {
            bool dead = !hist.empty() &&
                        !hist.back()->views.at(m.seat + 1).at("players").at(m.seat).value("alive", true);
            if (!dead || view == m.seat) {
                m.subscribed = false;
                m.view = m.seat;
                return;
            }
            if (!m.subscribed && spec_full())
                throw Error("spectator_full", "观战席满");
            m.subscribed = true;
        }
        m.view = view;
        m.last_view_tick = -1;
        return;
    }
    if (op == "speed") {
        if (mode != "offline")
            throw Error("fixed_rate", "联机比赛由服务器统一计时，不能调速");
        double v = j.value("value", 1.);
        if (v != .5 && v != .8 && v != 1 && v != 2)
            throw Error("bad_speed", "仅支持 0.5×、0.8×、1×、2×");
        rate = v;
        if (runner)
            runner->speed(v);
        broadcast();
        return;
    }
    if (op == "pause" || op == "vote") {
        if ((state != "paused" && state != "playing") || time_pending)
            throw Error("not_playing", "当前不能更改暂停状态");
        if (mode == "offline") {
            change_clock(state != "paused");
            return;
        }
        if (!m.participant)
            throw Error("not_voter", "只有本局真人参赛者可以投票");
        if (!ballot.active) {
            if (op == "vote")
                throw Error("no_vote", "当前没有票案");
            ballot.active = true;
            ballot.proposer=who;
            ballot.end = Clock::now() + 15s;
        }
        ballot.cast[who] = j.value("yes", true);
        check_ballot();
        return;
    }
    if (op == "replay_clip") {
        if (m.seat < 0 || !m.participant)
            throw Error("clip_not_ready", "仅原参赛者可查看本席死亡回看");
        if(j.contains("round_epoch") && j.at("round_epoch")!=round)
            throw Error("stale_round","该回看属于上一局");
        auto it=clips.find(m.seat);
        bool finale=j.value("scope",std::string("self"))=="final";
        if(finale && state=="finished" && !clips.empty())
            it=std::max_element(clips.begin(),clips.end(),[](const auto&a,const auto&b){return a.second.death_tick<b.second.death_tick;});
        else if(finale) throw Error("clip_not_ready","本局还没有决胜回看");
        if(it==clips.end())throw Error("clip_not_ready","本席没有出局片段");
        const auto& clip=it->second;
        if(Clock::now()<clip.release)
            throw Error("clip_delay","本席出局回看将在三秒观战延迟后开放");
        const auto& frames=clip.frames;
        send(who,
             {{"type", "replay_clip"},
              {"manifest", manifest_},
              {"round_epoch",round},{"scope",finale?"final":"self"},{"victim_seat",it->first},
              {"death_tick",clip.death_tick},{"cause",clip.cause},{"killer",clip.killer},
              {"from_tick", frames.front().value("tick", 0)},
              {"to_tick", frames.back().value("tick", 0)},
              {"frames", frames}},
             true);
        return;
    }
    if (op == "lobby" && state == "finished") {
        if (who != host)
            throw Error("not_host", "待主持人返回准备厅");
        if (had_humans && mode != "offline" && Clock::now() < ended_at + 3s)
            throw Error("spectator_tail", "正在播完观战延迟");
        state = "ready";continue_series=false;
        rev++;
        for (auto &[_, x] : members) {
            x.ready = false;
            x.participant = false;
            x.subscribed = false;
        }
        broadcast();
        return;
    }
    if (active())
        throw Error("room_locked", "比赛进行中，不能更改席位或开局");
    if (op == "ready") {
        if (m.seat < 0)
            throw Error("spectator", "观众无需准备");
        m.ready = j.value("ready", true);
        broadcast();
        return;
    }
    if (op == "seat") {
        int k = j.value("seat", -1);
        if (k < -1 || k >= settings.value("seat_count", 0))
            throw Error("bad_seat", "没有这个席位");
        if (k >= 0 && (settings.at("slots")[k] != "human" ||
                       std::any_of(members.begin(), members.end(),
                                   [&](auto &x) { return x.first != who && x.second.seat == k; })))
            throw Error("occupied", "席位已被占用");
        if (k == -1 && m.seat >= 0 && spec_full())
            throw Error("spectator_full", "观战席满");
        m.seat = k;
        m.ready = false;
        rev++;
        broadcast();
        return;
    }
    if (who != host)
        throw Error("not_host", "此操作需要房间主持权");
    if (j.value("room_rev", -1) != rev)
        throw Error("stale_revision", "房间设置已变化，请以最新阵容重试");
    if (op == "settings") {
        configure(j.at("settings"), cat, true);
        broadcast();
        return;
    }
    if (op != "start" && op != "again")
        throw Error("unknown_op", "未知的房间操作");
    if (!may_start)
        throw Error("match_limit", "专服正在排空或活跃局已满");
    if (state == "finished" && had_humans && Clock::now() - ended_at < 3s && mode != "offline")
        throw Error("spectator_tail", "正在为观众播完本局最后三秒");
    auto conf = settings;
    auto legal = maps_for(cat, conf.value("seat_count", 2));
    std::vector<std::string> pool;
    for (auto &b : cat.at("bots"))
        if (b.value("enabled", true) && b.value("available", false))
            pool.push_back(s(b, "id"));
    J seats = a();
    auto slots = conf.at("slots").get<std::vector<std::string>>(); if((op=="again"||continue_series)&&manifest_.contains("seats")&&manifest_["seats"].size()==slots.size())for(size_t k=0;k<slots.size();k++)if(slots[k]=="random")slots[k]=manifest_["seats"][k].value("bot",std::string("random"));
    bool mirror = conf.value("allow_mirrors", false);
    for (auto &k : slots)
        if (k != "human" && k != "random") {
            auto z = std::find(pool.begin(), pool.end(), k);
            if (z == pool.end())
                throw Error("bot_unavailable", "选手不可用：" + k);
            if (!mirror)
                pool.erase(z);
        }
    pool.erase(std::remove_if(pool.begin(), pool.end(),
                              [&](const std::string &id) {
                                  for (auto &b : cat.at("bots"))
                                      if (s(b, "id") == id)
                                          return !b.value("official", true);
                                  return true;
                              }),
               pool.end());
    for (int i = 0; i < (int)slots.size(); i++) {
        std::string k = slots[i], name = k;
        if (k == "human") {
            auto h =
                std::find_if(members.begin(), members.end(), [&](auto &x) { return x.second.seat == i; });
            if (h == members.end() || (!h->second.ready && mode != "offline"))
                throw Error("not_ready", "仍有真人席空缺或未准备");
            name = h->second.name;
            k.clear();
        } else if (k == "random") {
            if (pool.empty())
                throw Error("bot_pool_empty", "可用随机选手不足");
            size_t n = std::uniform_int_distribution<size_t>(0, pool.size() - 1)(bot_rng);
            name = k = pool[n];
            if (!mirror)
                pool.erase(pool.begin() + n);
        }
        seats.push_back({{"name", name}, {"bot", k}});
    }
    if(!series.pending()){series.begin(settings,map_rng,last_map,queued_map);queued_map.clear();}
    auto map=series.current_map();continue_series=false;
    if(std::find(legal.begin(),legal.end(),map)==legal.end())throw Error("map_disabled","下一张地图已停用："+map);
    contestant_ids.clear();for(size_t i=0;i<seats.size();i++){auto bot=s(seats[i],"bot");if(!bot.empty()){for(auto&b:cat.at("bots"))if(s(b,"id")==bot){seats[i]["name"]=b.at("name");break;}contestant_ids.push_back("bot:"+bot+":"+std::to_string(i));}else{auto a=std::find_if(members.begin(),members.end(),[&](auto&x){return x.second.seat==int(i);});contestant_ids.push_back("human:"+a->first);}}
    manifest_ = J::object();
    final_ = nullptr;
    stats_ = nullptr;
    hist.clear();
    pacing.clear();
    clips.clear();
    round++;
    timeline = 0;
    countdown_seconds = 0;
    field_ready_deadline = {};
    time_pending = false;
    reset_ballot();
    had_humans = humans() > 0;
    for (auto &[_, x] : members) {
        x.participant = x.seat >= 0;
        x.subscribed = false;
        x.view = x.seat;
        x.last_view_tick = -1;
        x.field_ready = false;
    }
    J manifest = {{"map", map},
                  {"seats", seats},
                  {"seed", rng() & 0x7fffffffu},
                  {"data_root", assets.u8string()},
                  {"ruleset", "classic20"}};
    runner = std::make_unique<Runner>(manifest, rate, archive_enabled);
    state = "loading";
    broadcast();
}
void Room::deliver_view(Member &m, const Frame &f, const std::vector<J> &preceding) {
    bool spectator = m.seat < 0 || m.subscribed;
    int seat = spectator ? m.view : m.seat;
    auto &v = f.views.at(seat + 1);
    int tick = v.value("tick", 0);
    if (tick <= m.last_view_tick)
        return;
    J msg = v;
    msg["type"] = "snapshot";
    msg["room_id"] = id;
    msg["pov"] = seat;
    msg["round_epoch"] = round;
    msg["timeline_epoch"] = timeline;
    if (!spectator)
        msg["last_applied_input_seq"] = f.applied.at(seat);
    J events = a();
    for (auto &e : preceding)
        events.push_back(e);
    for (auto &e : v.value("events", a()))
        events.push_back(e);
    msg["events"] = events;
    // Tick zero is the only view while the stage is frozen. Never lose it to
    // a dropped datagram (and keep it ordered after this round's manifest).
    send(m.id, msg, tick == 0);
    m.last_view_tick = tick;
}
void Room::egress(const std::shared_ptr<Frame> &f) {
    for (auto &[_, m] : members)
        if (m.seat >= 0 && !m.subscribed) {
            auto &v = f->views.at(m.seat + 1);
            for (auto &e : v.value("events", a()))
                if (s(e, "type") == "DEATH" || s(e, "type") == "END")
                    send(m.id, {{"type", "event"}, {"round_epoch", round}, {"event", e}}, true);
            deliver_view(m, *f, {});
        } else if (f->views.front().value("tick", -1) == 0 && (!had_humans || mode == "offline")) {
            deliver_view(m, *f, {});
        }
}
void Room::update(Time now, const std::function<void(J&&,std::string&&,const J&)> &archive) {
    if (ballot.active && now >= ballot.end) {
        reset_ballot();
        broadcast();
    }
    if (runner)
        for (auto &e : runner->take()) {
            if (e.kind == "manifest") {
                manifest_ = e.data;manifest_["series"]=series.status();
                for (auto &[p, _] : members)
                    manifest_to(p);
            } else if (e.kind == "playing" || e.kind == "paused") {
                pacing.clear();
                state = e.kind;
                time_pending = false;
                broadcast();
            } else if (e.kind == "frame") {
                hist.push_back(e.frame);
                pacing.emplace_back(e.frame->wall,e.frame->late_ms);if(pacing.size()>100)pacing.pop_front();
                auto &players = e.frame->views.front().at("players");
                for (auto &pl : players) {
                    auto seat = pl.at("seat").get<int>();
                    if (!pl.value("alive", true) && !clips.count(seat)) {
                        J fs = a();
                        auto first = hist.size() > 30 ? hist.size() - 30 : 0;
                        for (size_t i = first; i < hist.size(); i++)
                            fs.push_back(hist[i]->views.front());
                        bool delay=mode!="offline" && settings.value("seat_count",2)>2;
                        auto& all=e.frame->views.front();
                        for(auto& event:all.at("events")) if(event.value("type",std::string{})=="DEATH" && event.value("a",-1)==seat){
                            clips[seat]={now+(delay?3s:0s),std::move(fs),event.at("tick"),event.at("b"),event.at("c")};
                            break;
                        }
                    }
                }
                if (!e.frame->stats.is_null()){
                    stats_=e.frame->stats;
                    for(auto& row:stats_)row["bot"]=manifest_.at("seats").at(row.at("seat").get<size_t>()).at("bot");
                }
                egress(e.frame);
                if (state == "loading")
                    field_ready_deadline = Clock::now() + 10s;
            } else if (e.kind == "ended") {
                ended_at = now;
                state = "finished";
                auto record = std::move(e.data["record"]);
                final_ = record.at("result");series.complete(final_,manifest_.at("seats"),contestant_ids);final_["series"]=series.status(true);record["result"]=final_;continue_series=series.pending();
                auto meta = summary();
                meta["round_epoch"] = round;
                meta["mode"] = settings.value("seat_count", 2) == 2   ? "duel"
                               : settings.value("seat_count", 2) == 3 ? "ffa3"
                                                                      : "ffa4";
                archive(std::move(record),std::move(e.data.at("dart").get_ref<std::string&>()),meta);
                J r = final_;
                r["type"] = "result";
                for (auto &[_, m] : members)
                    if (m.seat >= 0 && !m.subscribed)
                        send(m.id, r, true);
                runner.reset();
                broadcast();
            } else if (e.kind == "failed") {
                runner.reset();
                state = "ready";
                for (auto &[_, m] : members)
                    m.participant = false;
                room_error("match_failed", s(e.data, "message"));
                broadcast();
            }
        }
    if (hist.empty())
        return;
    if (state == "loading") {
        const bool everyone_ready = std::none_of(members.begin(), members.end(), [](const auto &entry) {
            const auto &m = entry.second;
            return m.participant && m.field_ready_capable && !m.field_ready;
        });
        if (everyone_ready || now >= field_ready_deadline) {
            state = "countdown";
            countdown_seconds = 3;
            countdown_end = Clock::now() + 3s;
            broadcast();
        }
    } else if (state == "countdown") {
        const int left = std::max(0, int(std::chrono::ceil<std::chrono::seconds>(countdown_end - now).count()));
        if (left != countdown_seconds) {
            countdown_seconds = left;
            if (left == 0) {
                state = "playing";
                last_map = s(manifest_, "map");
                ++timeline;  // reject inputs sent before GO, even if delivered late
                pacing.clear();
            }
            broadcast();
            if (left == 0)
                runner->start();
        }
    }
    const auto delay = had_humans && mode != "offline" ? 3000ms : 0ms;
    auto cut = now - delay;
    if (now - last_spectate >= std::chrono::milliseconds(1000 / effective_hz())) {
        auto interval=std::chrono::milliseconds(1000/effective_hz()); last_spectate=(last_spectate==Time{}||now-last_spectate>=2*interval)?now:last_spectate+interval;
        auto it = std::find_if(hist.rbegin(), hist.rend(), [&](auto &f) { return f->wall <= cut; });
        if (it != hist.rend())
            for (auto &[_, m] : members)
                if (m.seat < 0 || m.subscribed) {
                    int64_t prev = m.last_view_tick;
                    std::vector<J> events;
                    for (auto &f : hist) {
                        if (f.get() == it->get())
                            break;
                        auto &v = f->views.at(m.view + 1);
                        if (prev >= 0 && v.value("tick", 0) > prev)
                            for (auto &e : v.value("events", a()))
                                events.push_back(e);
                    }
                    deliver_view(m, **it, events);
                }
    }
    if (state == "finished" && now >= ended_at + delay && !final_.is_null()) {
        J r = final_;
        r["type"] = "result";
        for (auto &[_, m] : members)
            if (m.seat < 0 || m.subscribed)
                send(m.id, r, true);
        final_ = nullptr;
    }
    while (hist.size() > 32 && hist[1]->wall < cut - 600ms)
        hist.pop_front();
    if (state == "finished" && now >= ended_at + 30s) {
        state = "ready";
        rev++;
        for (auto &[_, m] : members) {
            m.ready = false;
            m.subscribed = false;
            m.participant = false;
        }
        broadcast();
    }
}
void Room::advance_series(Time now,const J&cat,const fs::path&assets,bool enabled){
 if(!continue_series||state!="finished"||now-ended_at<4s)return;
 if(host.empty()||members.empty()){continue_series=false;return;}
 if(!enabled){continue_series=false;state="ready";rev++;broadcast();return;}
 try{op(host,{{"op","start"},{"room_rev",rev}},cat,assets,true);}
 catch(const std::exception&e){continue_series=false;state="ready";rev++;room_error("series_waiting",std::string("赛程保留，请补齐席位后继续：")+e.what());broadcast();}
}
void Room::close() {
    runner.reset();
    for (auto &[p, _] : members)
        send(p, {{"type", "left"}, {"reason", "room_closed"}}, true);
    members.clear();
}
} // namespace fbar
