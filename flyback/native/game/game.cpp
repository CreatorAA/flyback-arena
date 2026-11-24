#include "game.hpp"
#include "bot.hpp"
#include <array>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace flyback {
namespace {
namespace fs=std::filesystem;
using dart::i64;
struct Entry { const char* id;const char* model;const char* name; };
constexpr Entry roster[]={
 {"gpt-6.2-astra","astra","Astra"},{"gpt-6.2-sol","sol","Sol"},{"claude-fable-5.2","fable","Fable"},
 {"claude-opus-5.5","opus","Opus"},{"gemini-4-carbon","carbon","Carbon"},{"gemini-4-hydrogen","hydrogen","Hydrogen"},
 {"grok-4.7","grok","Grok"},{"glm-5.3","glm","GLM"},{"deepseek-v4.1-flash","deepseek","DeepSeek"},
 {"kimi-k3.1-pro","kimi","Kimi"},{"qwen4-max","qwen","千问"},{"mimo-v2.6-pro","mimo","MiMo"},
 {"doubao-seed-evolving","doubao","豆包"}};
struct MapEntry {const char* id;const char* name;bool trio;};
constexpr MapEntry maps[]={{"twinlakes","双湖",false},{"yinyang","阴阳",false},{"icefloes","裂冰",false},{"ruins","古塔",false},
 {"skybridge","天桥",false},{"stonecircle","石阵",false},{"trident","三岔湾",true},{"beacon","灯塔环礁",true},
 {"tidelock","潮汐船闸",false},{"kiln","赤陶窑场",false},{"windrose","风车岬",true},{"moonwell","月井庭",true}};
fs::path binary(const fs::path& root,const std::string& bot) {
#ifdef _WIN32
 return root/"bots"/(bot+".exe");
#else
 return root/"bots"/bot;
#endif
}
const Entry* find_bot(const std::string& id){for(const auto& b:roster)if(id==b.id)return &b;return nullptr;}
json local_bots(const fs::path& root){
 // Operator-owned registry; the UTF-8 ID selects a known basename, never a client-supplied path.
 const auto file=root/"bots"/"community.json";if(!fs::exists(file))return json::array();
 if(fs::file_size(file)>256*1024)throw std::runtime_error("community.json 超过 256 KiB");
 std::ifstream stream(file);json list=json::parse(stream);if(!list.is_array()||list.size()>512)throw std::runtime_error("community.json 必须是至多 512 项的选手清单");
 json out=json::array();std::vector<std::string> ids;
 for(const auto& b:list){
  const auto id=b.at("id").get<std::string>(),name=b.at("name").get<std::string>();
  if(id.rfind("community-",0)!=0 || id.size()<11 || id.size()>64 || id.find_last_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")!=std::string::npos ||
     std::find(ids.begin(),ids.end(),id)!=ids.end() || name.empty() || name.size()>120 ||
     std::any_of(name.begin(),name.end(),[](unsigned char c){return c<32||c==127;}))throw std::runtime_error("community.json 中的选手 ID 或名称不合法");
  ids.push_back(id);
  const auto build=b.value("build_id",std::string{});if(build.size()>128)throw std::runtime_error("社区 build_id 超过 128 字节");
  out.push_back({{"id",id},{"name",name},{"model","community"},{"build_id",build},{"available",fs::is_regular_file(binary(root,id))},{"official",false}});
 }
 return out;
}
const MapEntry* find_map(const std::string& id){for(const auto& m:maps)if(id==m.id)return &m;return nullptr;}
json terrain(const dart::Map& m){
 json rows=json::array(),waves=json::array();
 for(int y=0;y<m.ny;++y){std::string a,b;for(int x=0;x<m.nx;++x){a+=char('0'+m.at(x,y));b+=dart::wave_char(m.wave(x,y));}rows.push_back(a);waves.push_back(b);}
 return {{"width",m.nx},{"height",m.ny},{"cell_mm",dart::CELL},{"rows",rows},{"waves",waves}};
}
dart::Action decode(const json& j,const dart::Config& cf){
 dart::Action a;if(!j.is_object())return a;
 auto integer=[&](const char* k)->i64{auto it=j.find(k);if(it==j.end())return 0;if(!it->is_number_integer() || (it->is_number_unsigned() && it->get<unsigned long long>()>static_cast<unsigned long long>(std::numeric_limits<i64>::max())))throw std::invalid_argument("input not a signed integer");return it->get<i64>();};
 try{a.mx=integer("mx");a.my=integer("my");a.act=integer("act");a.ax=integer("ax");a.ay=integer("ay");a.msg=integer("msg");}catch(const std::exception&){return {};}
 return dart::action_in_range(a,cf)?a:dart::Action{};
}
json encode_event(const dart::Event& e,int ord){return {{"tick",e.t},{"ordinal",ord},{"type",dart::EV_NAME[e.type]},{"a",e.a},{"b",e.b},{"c",e.c},{"d",e.d}};}
struct Frame {i64 tick;std::array<json,4> later;}; // Events grouped by when recipients may learn them.
}
json catalog(const std::string& data_root){
 auto root=fs::u8path(data_root);json m=json::array(),b=json::array();
 for(auto& x:maps)if(fs::is_regular_file(root/"maps"/(std::string(x.id)+".txt")))m.push_back({{"id",x.id},{"name",x.name},{"seats",x.trio?json::array({3}):json::array({2,4})}});
 for(auto& x:roster)b.push_back({{"id",x.id},{"model",x.model},{"name",x.name},{"available",fs::is_regular_file(binary(root,x.id))},{"official",true}});
 for(auto& local:local_bots(root))b.push_back(local);
 return {{"maps",m},{"bots",b}};
}
struct Match::Impl {
 json meta;dart::Game g;std::array<std::unique_ptr<ai::Bot>,4> bots;
 dart::Action warm[4]{};std::array<bool,4> was_retired{};std::deque<Frame> history;
 json full_events=json::array(),fault_log=json::array();std::array<int,4> recorded_faults{};std::array<json,4> player_events;std::string tape;
 Impl(const json& spec){
    const fs::path root=fs::absolute(fs::u8path(spec.at("data_root").get<std::string>()));
    const auto& seats=spec.at("seats");if(!seats.is_array()||seats.size()<2||seats.size()>4)throw std::invalid_argument("比赛需要 2、3 或 4 席");
    const int n=int(seats.size());const std::string map_id=spec.at("map").get<std::string>();const auto* entry=find_map(map_id);
    if(!entry || entry->trio!=(n==3))throw std::invalid_argument("地图不支持当前席数："+map_id);
    std::ifstream f(root/"maps"/(map_id+".txt"),std::ios::binary);if(!f)throw std::runtime_error("地图文件不存在："+map_id);
    std::string text((std::istreambuf_iterator<char>(f)),{});auto m=std::make_shared<dart::Map>();auto issue=dart::parse_map(text,*m);
    if(!issue.empty())throw std::runtime_error(map_id+"："+issue);
    if(m->name!=map_id || (n==3?!m->trio:!m->modes[n==2?0:1]))throw std::runtime_error("地图清单与数据不一致");
    dart::Config cfg;cfg.mode=n==2?dart::M_DUEL:dart::M_FFA;cfg.seats_override=n;
    const auto seed=spec.value("seed",dart::u64(0));
    // A square grid approximates, but cannot exactly reproduce, C3 rotational geometry.
    // Rotate seat -> physical spawn across seeds independently of the radial/tangent jitter.
    const int shift=n==3?int(seed%3):0;
    if(shift){std::rotate(m->trio_x,m->trio_x+shift,m->trio_x+3);std::rotate(m->trio_y,m->trio_y+shift,m->trio_y+3);}
    g=dart::Game(cfg,m,true,seed);
    meta={{"ruleset","classic20"},{"human_observation_delay_ticks",0},{"ai_observation_delay_ticks",cfg.delay},{"mode",n==2?"duel":"ffa"},{"seat_count",n},{"map",map_id},{"map_name",entry->name},{"seed",seed},{"terrain",terrain(*m)},{"seats",json::array()}};
    std::string names[4];std::array<std::future<std::unique_ptr<ai::Bot>>,4> loads;
    const auto community=local_bots(root);
    if(std::ifstream build(root/"bots"/"build-info.json");build)meta["official_bot_build"]=json::parse(build);
    for(int i=0;i<n;++i){
        const auto& seat=seats.at(i);auto id=seat.value("bot",std::string{});const auto* b=find_bot(id);
        auto custom=std::find_if(community.begin(),community.end(),[&](const json& row){return row["id"]==id;});
        if(!id.empty()&&!b&&custom==community.end())throw std::invalid_argument("未知选手："+id);
        names[i]=seat.value("name",b?std::string(b->name):custom!=community.end()?custom->at("name").get<std::string>():"Player "+std::to_string(i+1));
        if(names[i].empty() || names[i].size()>120 || std::any_of(names[i].begin(),names[i].end(),[](unsigned char c){return c<32 || c==127;}))throw std::invalid_argument("选手名不合法");
        meta["seats"].push_back({{"name",names[i]},{"bot",id},{"model",b?b->model:id.empty()?"courier":"community"},{"spawn_slot",(i+shift)%n},{"x",g.spx[i]},{"y",g.spy[i]}});
        if(custom!=community.end())meta["seats"].back()["build_id"]=(*custom)["build_id"];
        player_events[i]=json::array();if(id.empty())continue;
        auto exe=binary(root,id);if(!fs::is_regular_file(exe))throw std::runtime_error("尚未安装选手："+id);
        auto gi=g.info_for(i,10,1000);loads[i]=std::async(std::launch::async,[exe,gi]{return std::make_unique<ai::Bot>(exe,gi);});
    }
    for(int i=0;i<n;++i)if(loads[i].valid())bots[i]=loads[i].get();
    collect_bots(warm);
    for(int i=0;i<n;++i)if(bots[i]&&bots[i]->retired)throw std::runtime_error("选手预热失败 "+names[i]+"："+bots[i]->last_error);
    tape=dart::replay_header(g,names);
    if(n==3)tape.replace(0,std::string("DART-REPLAY 2").size(),"FLYBACK-REPLAY 1");
    tape+="FBAR 1 0 vanilla classic20 "+std::to_string(n)+"\n";
    tape+="MANIFEST "+meta.dump()+"\n"+dart::replay_state(g);
 }
 void collect_bots(dart::Action* a) {
    bool pending[4]={};int count=0;
    for(int i=0;i<g.n;++i)if(bots[i]&&g.p[i].alive&&!bots[i]->retired){bots[i]->ask(g.observe(i));pending[i]=true;++count;}
    while(count){
        for(int i=0;i<g.n;++i)if(pending[i]&&bots[i]->collect(g.t,a[i])){pending[i]=false;--count;}
        if(count)std::this_thread::sleep_for(std::chrono::microseconds(100));
    }
 }
 // DEATH is emitted after all lethal contacts have been resolved, when a dead body is
 // public under the classic rules. Credit the selected cause using visibility at impact,
 // not the later corpse, a subsequently grounded dart, or the end-of-tick grass state.
 const dart::Event& impact_of(size_t index) const {
    const auto& d=g.events[index];
    if(d.type==dart::EV_DEATH)for(size_t j=index;j-->0;){
        const auto& e=g.events[j];
        if(e.t!=d.t)break;
        if(d.b==dart::K_FALL && e.type==dart::EV_FALL && e.a==d.a && e.b==d.c)return e;
        if(e.a==d.c && e.b==d.a && ((d.b==dart::K_DART && e.type==dart::EV_HIT) || (d.b==dart::K_SLASH && e.type==dart::EV_CUT)))return e;
    }
    return d;
 }
 void update_events(){
    auto tick=g.t-1;Frame frame{};frame.tick=tick;
    full_events=json::array();
    for(int s=0;s<g.n;++s){player_events[s]=json::array();frame.later[s]=json::array();}
    for(size_t i=0;i<g.events.size();++i){
        const auto& e=g.events[i];const auto& impact=impact_of(i);auto raw=encode_event(e,int(i));full_events.push_back(raw);
        for(int s=0;s<g.n;++s){
            auto& now=player_events[s];const bool ai=bool(bots[s]);auto& late=ai?frame.later[s]:now;auto event=raw;
            if(e.type==dart::EV_WARN || e.type==dart::EV_COLLAPSE){now.push_back(event);continue;}
            if(e.type==dart::EV_END)continue; // reliable result/observer watermark belongs to the room
            const auto visible=[&](i64 k){return k>=0 && k<g.n && (impact.visible[k] & (1u<<s));};
            if(e.type==dart::EV_CLASH || e.type==dart::EV_BLOCK) {
                if(!ai && visible(e.a) && visible(e.b))now.push_back(event);
                else if(e.a==s || e.b==s){
                    // An AI POV cannot learn a current opponent via its own immediate event.
                    // Neither POV identifies a hidden opponent or gives away its position.
                    event["a"]=s; event["b"]=-1;event["c"]=0;event["d"]=0;now.push_back(event);
                } else if(visible(e.b) && visible(e.a))late.push_back(event);
                continue;
            }
            if(e.type==dart::EV_HIT || e.type==dart::EV_CUT || e.type==dart::EV_KICKHIT) {
                if(!ai && visible(e.b)){
                    if(!visible(e.a))event["a"]=-1;
                    event["d"]=0; // cumulative hits can include another concealed target
                    now.push_back(event);
                } else if(ai && visible(e.a) && visible(e.b))late.push_back(event);
                continue;
            }
            if(e.type==dart::EV_DEATH || e.type==dart::EV_FALL) {
                if(ai && e.a==s){event["b"]=-1;event["c"]=-1;event["d"]=0;now.push_back(event);}
                else if(visible(e.a)){
                    if(e.type==dart::EV_DEATH && !visible(e.c))event["c"]=-1;
                    if(e.type==dart::EV_FALL && !visible(e.b))event["b"]=-1;
                    late.push_back(event);
                }
                continue;
            }
            if(e.a<0 || e.a>=g.n)continue;
            if(e.type==dart::EV_SWING)event["c"]=0; // obscured targets must not be counted via sound
            if(e.type==dart::EV_CATCH){event["b"]=0;event["c"]=0;} // internal hit count / cumulative bend are not in the view
            if(e.a==s)now.push_back(event);else if(visible(e.a))late.push_back(event);
        }
    }
    history.push_back(std::move(frame));
    if(history.size()>size_t(g.cf.delay)){
       for(int s=0;s<g.n;++s)for(auto& e:history.front().later[s])player_events[s].push_back(e);
        history.pop_front();
    }
 }
 void step(const json& inputs){
    if(g.over)return;
    dart::Action a[4]{};if(g.t==0){std::copy_n(warm,g.n,a);}else collect_bots(a);
    g.events.clear();
    for(int i=0;i<g.n;++i){
        if(!bots[i] && inputs.is_array() && i<int(inputs.size()))a[i]=decode(inputs[i],g.cf);
        if(auto* b=bots[i].get();b && (b->faults!=recorded_faults[i] || (b->retired&&!was_retired[i]))){
            recorded_faults[i]=b->faults;
            json fault={{"tick",g.t},{"seat",i},{"code",b->last_code},{"count",b->faults},{"retired",b->retired}};
            tape+="BOT_FAULT "+fault.dump()+"\n";fault_log.push_back(std::move(fault));
            if(b->retired){g.retire(i);was_retired[i]=true;tape+="BOT_RETIRED "+std::to_string(g.t)+" "+std::to_string(i)+"\n";}
        }
    }
    tape+=dart::replay_actions(g.t,a,g.n,g.cf);g.step(a);for(auto&e:g.events)tape+=dart::replay_event(e);tape+=dart::replay_state(g);update_events();
    if(g.over)tape+=dart::replay_end(g);
 }
};
Match::Match(const json& manifest):p(std::make_unique<Impl>(manifest)){}
Match::~Match()=default;Match::Match(Match&&) noexcept=default;Match& Match::operator=(Match&&) noexcept=default;
void Match::tick(const json& a){p->step(a);}
bool Match::finished() const{return p->g.over;}
const json& Match::manifest() const{return p->meta;}
std::string Match::replay() const{return p->tape;}
json Match::bot_stats()const{
 json r=json::array();for(int i=0;i<p->g.n;++i)if(auto* b=p->bots[i].get())r.push_back({{"seat",i},{"first_cpu_ms",b->first_ms},{"samples",b->samples},{"mean_cpu_ms",b->samples?b->total_ms/b->samples:0.0},{"max_cpu_ms",b->max_ms},{"decision_cpu_ms",b->first_ms+b->total_ms},{"rss_bytes",b->proc?b->proc->rss_bytes():0},{"faults",b->faults},{"retired",b->retired},{"last_error",b->last_error}});return r;
}
json Match::result() const {
 const auto& g=p->g;if(!g.over)return nullptr;int rank[4]{};g.ranks(rank);
 json standings=json::array();
 for(int i=0;i<g.n;++i){
  int tied=std::count(rank,rank+g.n,rank[i]);double placement=g.n-rank[i]-(tied-1)*.5;
  auto& s=g.p[i];standings.push_back({{"seat",i},{"rank",rank[i]},{"alive",bool(s.alive)},{"kills",s.kills},{"points",g.n==2?json(nullptr):json(placement+s.kills)},{"placement_points",g.n==2?json(nullptr):json(placement)},{"death_tick",s.dead_t},{"killer",s.killer},{"cause",dart::CAUSE_NAME[s.cause]}});
 }
 return {{"winner",g.winner},{"tick",g.t},{"standings",standings},{"bot_faults",p->fault_log}};
}
json Match::snapshot(int seat) const{
 const auto& g=p->g;if(seat < -1 || seat>=g.n)throw std::invalid_argument("视角席号无效");
 const bool ai=seat>=0 && bool(p->bots[seat]);auto ob=ai?g.observe(seat):dart::Obs{};json players=json::array(),darts=json::array();
 for(int i=0;i<g.n;++i){
  auto& a=ai?ob.p[i]:g.p[i];auto& d=ai?ob.d[i]:g.d[i];
  bool vis=seat<0 || (ai?bool(a.vis):g.can_see(seat,i));
  json chara={{"seat",i},{"vis",vis},{"alive",bool(a.alive)},{"kills",a.kills}};
  if(vis)chara.update(json{{"x",a.x},{"y",a.y},{"vx",a.vx},{"vy",a.vy},{"face",a.face},{"st",a.st},{"timer",a.timer},{"ax",a.ax},{"ay",a.ay},{"cd",a.cd},{"expose",a.expose}});
  // vis means the seat may receive these coordinates, not "draw this model".
  // D_GONE retains its last position in DART 2; the renderer hides it by state.
  players.push_back(chara);bool dv=d.st!=dart::D_HAND || vis;
  json disk={{"seat",i},{"st",d.st},{"vis",dv}};
  if(dv)disk.update(json{{"x",d.x},{"y",d.y},{"vx",d.vx},{"vy",d.vy},{"left",d.left},{"age",d.age}});darts.push_back(disk);
 }
 i64 fallen,warn;dart::collapse_state(g.cf,g.M(),g.t,fallen,warn);
 return {{"tick",g.t},{"players",players},{"darts",darts},{"events",seat<0?p->full_events:p->player_events[seat]},{"fallen",fallen},{"warn",warn},{"finished",g.over}};
}
json predict_self(const json& meta,const json& snap,int seat,const json& inputs){
 const int n=int(meta.at("seats").size());
 if(n<2||n>4||seat<0||seat>=n||!inputs.is_array()||inputs.size()>2)throw std::invalid_argument("预测仅支持当前席及随后至多两步");
 const auto& a=snap.at("players").at(seat);const auto& disk=snap.at("darts").at(seat);
 if(a.at("seat")!=seat||disk.at("seat")!=seat||!a.at("vis").get<bool>())throw std::invalid_argument("本席快照不匹配");
 if(!a.at("alive").get<bool>() || inputs.empty())return {{"tick",snap.at("tick")},{"player",a},{"dart",disk}};
 const auto& terrain=meta.at("terrain");
 auto m=std::make_shared<dart::Map>();m->nx=terrain.at("width");m->ny=terrain.at("height");
 if(m->nx<6||m->nx>60||m->ny<6||m->ny>60||terrain.at("cell_mm")!=dart::CELL)throw std::invalid_argument("预测用地图尺寸无效");
 for(int y=0;y<m->ny;++y){
   const auto& row=terrain.at("rows").at(y).get_ref<const std::string&>();const auto& wave=terrain.at("waves").at(y).get_ref<const std::string&>();
   if(int(row.size())!=m->nx||wave.size()!=row.size())throw std::invalid_argument("预测用地图数据不完整");
   for(int x=0;x<m->nx;++x){if(row[x]<'0'||row[x]>'3')throw std::invalid_argument("地形编码无效");m->c.push_back(row[x]-'0');int w=dart::wave_char_value(wave[x]);m->w.push_back(static_cast<signed char>(w));m->nwave=std::max(m->nwave,w+1);}
 }
 dart::GameInfo info{};info.n=n;info.seat=seat;info.map=m;info.cfg.mode=n==2?dart::M_DUEL:dart::M_FFA;info.cfg.seats_override=n;
 dart::Obs ob{};ob.t=snap.at("tick");ob.n=n;ob.seat=seat;
 for(int k=0;k<n;++k){info.team[k]=k;ob.p[k].alive=1;ob.p[k].vis=0;ob.d[k].st=dart::D_GONE;}
 auto& own=ob.p[seat];own.vis=1;own.x=a.at("x");own.y=a.at("y");own.vx=a.at("vx");own.vy=a.at("vy");
 own.face=a.at("face");own.st=a.at("st");own.timer=a.at("timer");own.ax=a.at("ax");own.ay=a.at("ay");own.cd=a.at("cd");own.expose=a.at("expose");own.kills=a.at("kills");
 auto& d=ob.d[seat];d.st=disk.at("st");d.x=disk.value("x",0LL);d.y=disk.value("y",0LL);d.vx=disk.value("vx",0LL);d.vy=disk.value("vy",0LL);d.age=disk.value("age",0LL);d.left=disk.value("left",0LL);
 dart::Game g(info,ob);for(const auto& input:inputs){dart::Action acts[4]{};acts[seat]=decode(input,g.cf);g.step(acts);}
 auto& pp=g.p[seat];auto& dd=g.d[seat];auto player=a,projectile=disk;
 if(!pp.alive)return {{"tick",g.t},{"player",a},{"dart",disk}}; // only the authority commits a fall
 player.update(json{{"x",pp.x},{"y",pp.y},{"vx",pp.vx},{"vy",pp.vy},{"face",pp.face},{"st",pp.st},{"timer",pp.timer},{"ax",pp.ax},{"ay",pp.ay},{"cd",pp.cd},{"expose",pp.expose}});
 projectile.update(json{{"st",dd.st},{"x",dd.x},{"y",dd.y},{"vx",dd.vx},{"vy",dd.vy},{"left",dd.left},{"age",dd.age},{"vis",true}});
 return {{"tick",g.t},{"player",player},{"dart",projectile}};
}
}
