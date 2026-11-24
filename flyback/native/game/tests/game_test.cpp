#include "game.hpp"
#include "dart.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <stdexcept>
namespace fs=std::filesystem;using flyback::json;
void check(bool ok,const std::string& s){if(!ok)throw std::runtime_error(s);}
json seat(std::string id=""){return {{"name",id.empty()?"Human":id},{"bot",id}};}
json human(int n){json a=json::array();while(n--)a.push_back(seat());return a;}
std::string bytes(const fs::path& path){std::ifstream f(path,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void legacy_replay(const fs::path& path,const fs::path& root){
 std::ifstream f(path);std::string line,map;dart::u64 seed=0;dart::Config cfg;std::shared_ptr<dart::Game> g;int states=0,steps=0;size_t events=0;
 bool compare_views=path.filename()=="g428.txt" || path.filename()=="g27297.txt";std::unique_ptr<flyback::Match> wrapper;
 while(std::getline(f,line)){
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    std::istringstream in(line);std::string type;in>>type;
    if(type=="CFG"){std::string kv;while(in>>kv){auto pos=kv.find('=');if(pos!=std::string::npos)cfg.set(kv.substr(0,pos),kv.substr(pos+1));}}
    else if(type=="SEED"){in>>seed;}
    else if(type=="MAPNAME"){in>>map;}
    else if(type=="S"){
       if(!g){auto m=std::make_shared<dart::Map>();check(dart::parse_map(bytes(root/"maps"/(map+".txt")),*m).empty(),"parse "+map);g=std::make_shared<dart::Game>(cfg,m,true,seed);
           if(compare_views)wrapper=std::make_unique<flyback::Match>(json{{"map",map},{"seed",seed},{"seats",human(g->n)},{"data_root",root.string()}});
       }
       if(wrapper)for(int s=0;s<g->n;++s){auto view=wrapper->snapshot(s);for(int k=0;k<g->n;++k){
           const auto& p=g->p[k];const auto& d=g->d[k];const bool vis=g->can_see(s,k);auto& p1=view["players"][k];auto& d1=view["darts"][k];
           check(p1["vis"]==vis && p1["alive"]==bool(p.alive) && p1["kills"]==p.kills,"public character state");
           check(vis?(p1["x"]==p.x && p1["y"]==p.y && p1["st"]==p.st && p1["timer"]==p.timer && p1["cd"]==p.cd):!p1.contains("x"),"human seat current visibility/position");
           bool has=d.st!=dart::D_HAND || vis;
           check(d1["vis"]==has && d1["st"]==d.st && (has?(d1["x"]==d.x && d1["y"]==d.y && d1["age"]==d.age && d1["left"]==d.left):!d1.contains("x")),"seat dart view");
       }}
       check(dart::replay_state(*g)==line+"\n",path.filename().string()+" state "+std::to_string(g->t));++states;
    }else if(type=="A"){int t;in>>t;dart::Action a[dart::MAXP]{};json inputs=json::array();for(int i=0;i<g->n;++i){std::string sep;in>>sep>>a[i].mx>>a[i].my>>a[i].act>>a[i].ax>>a[i].ay>>a[i].msg;if(wrapper)inputs.push_back({{"mx",a[i].mx},{"my",a[i].my},{"act",a[i].act},{"ax",a[i].ax},{"ay",a[i].ay},{"msg",a[i].msg}});}g->step(a);if(wrapper)wrapper->tick(inputs);++steps;}
    else if(type=="E"){check(events<g->events.size() && dart::replay_event(g->events[events++])==line+"\n","E "+path.filename().string());}
    else if(type=="END"){check(events==g->events.size(),"missing E");auto end=dart::replay_end(*g);check(end.substr(0,end.find('\n'))==line,"END "+path.filename().string());}
 }
 check(states==steps+1&&steps>0,"missing steps");
}
static bool event(const json& frame,const char* type,int a,int b=-999,int c=-999) {
 for(const auto& e:frame.at("events"))if(e.at("type")==type && e.at("a")==a
     && (b==-999 || e.at("b")==b) && (c==-999 || e.at("c")==c))return true;
 return false;
}
void observation_fixture(){
 const auto path=fs::path(__FILE__).parent_path()/"observation";auto terrain=std::make_shared<dart::Map>();
 check(dart::parse_map(bytes(path/"maps/twinlakes.txt"),*terrain).empty(),"fixture map");
 dart::Config cfg;cfg.mode=dart::M_FFA;dart::Game reference(cfg,terrain,true,0);
 const json spec={{"data_root",path.string()},{"map","twinlakes"},{"seats",human(4)},{"seed",0}};
 flyback::Match m(spec);
 check(m.manifest().at("human_observation_delay_ticks")==0 && m.manifest().at("ai_observation_delay_ticks")==4,"manifest delay contract");
 auto run=[&](const dart::Action& action){
   dart::Action a[4]{};a[0]=action;reference.step(a);
   m.tick(json::array({json{{"mx",action.mx},{"my",action.my},{"act",action.act},{"ax",action.ax},{"ay",action.ay}},nullptr,nullptr,nullptr}));
 };
 dart::Action move{};move.mx=1000;run(move);
 check(m.snapshot(1)["players"][0]["x"]==2350 && reference.observe(1).p[0].x==2100,"visible move: human now / program four ticks back");
 run(move);run(move);
 auto hidden=m.snapshot(1);
 check(!hidden["players"][0].contains("x") && !hidden["darts"][0].contains("x") && hidden["darts"][0]["st"]==dart::D_HAND
       && m.snapshot()["players"][0]["x"]==2850 && reference.observe(1).p[0].vis,"grass entry: no live player or held dart leak");
 dart::Action shoot{};shoot.act=dart::A_THROW;shoot.ax=1000;run(shoot);
 check(m.snapshot(1)["players"][0]["vis"] && event(m.snapshot(1),"THROW",0) && reference.observe(1).p[0].st==dart::S_FREE,"exposure and opposing throw immediately visible");
 run({});auto released=m.snapshot(1);
 check(event(released,"RELEASE",0) && released["darts"][0]["st"]==dart::D_OUT && reference.observe(1).d[0].st==dart::D_HAND,"release now / AI historical held dart");
 run({});check(m.snapshot(1)["darts"][0]["x"]==m.snapshot()["darts"][0]["x"] && m.snapshot(1)["darts"][0]["x"]!=released["darts"][0]["x"],"flying dart does not lag");
 bool hit=false;
 for(int i=0;i<18 && !hit;++i){
   run({});auto full=m.snapshot(),victim=m.snapshot(1),third=m.snapshot(2);
   if(!event(full,"DEATH",1))continue;hit=true;
   check(event(victim,"HIT",0,1) && event(victim,"DEATH",1,dart::K_DART,0),"visible killer and cause on own death tick");
   check(third["players"][1]["alive"]==false && third["players"][1]["vis"]==true &&
       !event(third,"HIT",0,1) && !event(third,"DEATH",1),"public corpse cannot retroactively reveal grass hit");
 }
 check(hit,"fixture must really hit an opponent");
 flyback::Match ground(spec);
 for(int i=0;i<3;++i)ground.tick(json::array({json{{"mx",1000}}}));
 ground.tick(json::array({json{{"act",1},{"ay",1000}}}));
 for(int i=0;i<25;++i)ground.tick(nullptr);
 const auto seen=ground.snapshot(1),all=ground.snapshot();
 check(!seen["players"][0].contains("x") && seen["darts"][0]["st"]==dart::D_GROUND && seen["darts"][0]==all["darts"][0],"hidden owner / public grounded dart");
 std::cout<<"human current vs AI t-4: move, grass, exposure, flying/grounded dart, hit/death, event-time concealment passed\n";
}
void bot_pov(const std::string& root){
 auto ss=human(2);ss[1]=seat("claude-fable-5.2");
 flyback::Match m({{"map","twinlakes"},{"seats",ss},{"data_root",root},{"seed",0}});
 auto start=m.snapshot(1);m.tick(json::array({json{{"mx",1000}}}));const auto moved=m.snapshot();
 check(moved["players"][0]["x"]!=start["players"][0]["x"] && m.snapshot(1)["players"][0]["x"]==start["players"][0]["x"],"AI POV delayed displacement");
 m.tick(json::array({json{{"act",1},{"ax",1000}}}));check(event(m.snapshot(0),"THROW",0) && !event(m.snapshot(1),"THROW",0),"AI POV must not see enemy throw early");
 for(int i=0;i<3;++i)m.tick(nullptr);
 check(m.snapshot(1)["players"][0]["x"]==moved["players"][0]["x"] && !event(m.snapshot(1),"THROW",0),"AI POV delayed state boundary");
 m.tick(nullptr);check(event(m.snapshot(1),"THROW",0),"AI POV enemy event released after four steps");
 std::cout<<"mixed room: human current and Fable POV state/events four ticks behind passed\n";
}
void check_new_spawns(const std::string& root,const json& cat){
 const char* ids[]={"tidelock","kiln","windrose","moonwell"};
 for(const char* id:ids){
   auto entry=std::find_if(cat.at("maps").begin(),cat.at("maps").end(),[&](auto& x){return x.at("id")==id;});
   check(entry!=cat.at("maps").end(),std::string("missing map ")+id);
   dart::Map terrain;check(dart::parse_map(bytes(fs::u8path(root)/"maps"/(std::string(id)+".txt")),terrain).empty(),id);
   for(int n:entry->at("seats")){
     dart::Config cf;cf.mode=n==2?dart::M_DUEL:dart::M_FFA;cf.seats_override=n;
     for(unsigned seed=0;seed<=128;++seed){
       dart::i64 x[dart::MAXP]{},y[dart::MAXP]{};dart::Game::jitter_spawns(cf,terrain,seed,x,y);
       for(int i=0;i<n;++i){
         const auto c=dart::cell_of_point(cf,terrain,x[i],y[i],0);
         check((c==dart::C_FLOOR || c==dart::C_GRASS) && !dart::circle_hits_wall(terrain,cf.body_r,x[i],y[i]),std::string("spawn intersects wall/pit: ")+id);
         for(int j=0;j<i;++j)check(dart::ilen(x[i]-x[j],y[i]-y[j])>=2*cf.body_r,"spawn overlap");
       }
     }
   }
 }
 std::cout<<"new arenas: all 6 modes, seeds 0..128, safe referee spawns\n";
}
int main(int argc,char**argv) try{
 if(argc<2)return 2;std::string root=argv[1];auto c=flyback::catalog(root);check(c["bots"].size()==13&&c["maps"].size()==12,"catalog incomplete");
 observation_fixture();
 check_new_spawns(root,c);
 for(auto& map:c["maps"])for(int n:map["seats"]){flyback::Match m({{"map",map["id"]},{"seats",human(n)},{"data_root",root},{"seed",7}});
   check(int(m.snapshot()["players"].size())==n,"phantom seat");json a=json::array({json{{"mx",1000}},nullptr});
   auto prediction=flyback::predict_self(m.manifest(),m.snapshot(0),0,json::array({a[0]}));
   for(int i=0;i<4;++i){m.tick(a);if(i==0){auto own=m.snapshot(0)["players"][0];check(own["x"]==prediction["player"]["x"]&&own["y"]==prediction["player"]["y"],"self prediction diverged without contact");}auto other=m.snapshot(1)["players"][0];check(!other.at("vis").get<bool>() || other==m.snapshot()["players"][0],"human shown stale enemy");}
   check(m.snapshot()["tick"]==4,"clock");int count=0;while(!m.finished() && ++count<=900)m.tick(nullptr);check(m.finished()&&m.result()["standings"].size()==size_t(n),"result");
   check(m.replay().rfind(n==3?"FLYBACK-REPLAY 1":"DART-REPLAY 2",0)==0,"replay version");
 }
 if(argc>2 && std::string(argv[2]).rfind("--",0)!=0){int count=0;for(auto& e:fs::directory_iterator(argv[2])){auto s=e.path().filename().string();if(e.path().extension()!=".txt" || s[0]!='g')continue;
   std::ifstream in(e.path());std::string line;bool team=false;while(std::getline(in,line)){if(line.rfind("MODE ",0)==0){team=line.rfind("MODE 2",0)==0;break;}}if(team)continue;
   legacy_replay(e.path(),fs::u8path(root));++count;}check(count==117,"incomplete original fixtures");std::cout<<"legacy: "<<count<<" complete action tapes\n";}
 if(argc>2 && (std::string(argv[argc-1])=="--bot-startup" || std::string(argv[argc-1])=="--bot-pov"))bot_pov(root);
 if(argc>2 && std::string(argv[argc-1])=="--bot-startup"){
   for(auto& b:c["bots"]){auto id=b["id"].get<std::string>();auto ss=human(2);ss[0]=seat(id);
     flyback::Match m({{"map","twinlakes"},{"seats",ss},{"data_root",root},{"seed",42}});
     for(int i=0;i<3;++i)m.tick(nullptr);auto stat=m.bot_stats().at(0);
     check(!stat["retired"].get<bool>() && stat["first_cpu_ms"].get<double>()>0 && stat["samples"].get<int>()>0,"startup/decisions "+id+": "+stat.dump());
     std::cout<<"startup "<<id<<" "<<stat.dump()<<std::endl;
   }
 }
 if(argc>2 && (std::string(argv[argc-1])=="--bots" || std::string(argv[argc-1])=="--bot-outcomes")){
   const bool strict=std::string(argv[argc-1])=="--bots";
   for(int n:{3,4}){int idx=0;for(auto& b:c["bots"]){check(b["available"].get<bool>(),"missing bot");json ss=human(n);
    for(int j=0;j<n;++j)ss[j]=seat(c["bots"][(idx+j)%13]["id"]);flyback::Match m({{"map",n==4?"skybridge":idx%2?"trident":"beacon"},{"seats",ss},{"data_root",root},{"seed",idx+1}});
    while(!m.finished())m.tick(nullptr);if(strict)for(auto& s:m.bot_stats())check(!s["retired"].get<bool>(),s.dump());
    check(m.result()["standings"].size()==size_t(n),"match did not resolve all seats");
    for(auto& s:m.bot_stats())if(s["faults"].get<int>()>0){
       check(!m.result()["bot_faults"].empty()&&m.replay().find("BOT_FAULT ")!=std::string::npos,"missing replay fault record");
    }
    std::cout<<"ffa"<<n<<" #"<<++idx<<": tick "<<m.result()["tick"]<<" "<<m.bot_stats().dump()<<std::endl;
   }}
 }
 if(argc==3 && std::string(argv[2])=="--new-map-bots"){
   for(auto& map:c["maps"]){
     const std::string id=map.at("id");
     if(id!="tidelock" && id!="kiln" && id!="windrose" && id!="moonwell")continue;
     for(int n:map["seats"]){
       const char* ids[]={"claude-fable-5.2","gpt-6.2-astra","gpt-6.2-sol","qwen4-max"};
       json ss=json::array();for(int i=0;i<n;++i)ss.push_back(seat(ids[i]));
       flyback::Match m({{"map",id},{"data_root",root},{"seats",ss},{"seed",7}});
       while(!m.finished())m.tick(nullptr);
       check(m.result().at("standings").size()==size_t(n),"new map did not score each seat");
       for(const auto& st:m.bot_stats()) check(!st.at("retired").get<bool>() && st.at("samples")>0,"new map bot could not compete: "+st.dump());
       std::cout<<id<<" "<<n<<" seats, tick "<<m.result().at("tick")<<" winner "<<m.result().at("winner")
                <<" bot faults "<<m.result().at("bot_faults").size()<<std::endl;
     }
   }
 }
 if(argc==4 && std::string(argv[2])=="--balance"){
   const auto samples=std::stoi(argv[3]);for(const auto& id:{"trident","beacon"}){
     double wins[3]{},physical[3]{};const auto same=seat("claude-fable-5.2");
     for(int k=1;k<=samples;++k){flyback::Match m({{"map",id},{"seats",json::array({same,same,same})},{"data_root",root},{"seed",k}});
        while(!m.finished())m.tick(nullptr);auto r=m.result()["standings"];int tied=0;for(auto& v:r)if(v["rank"]==1)++tied;
        for(auto& v:r)if(v["rank"]==1){auto i=v["seat"].get<int>();wins[i]+=1./tied;physical[m.manifest()["seats"][i]["spawn_slot"].get<int>()]+=1./tied;}
     }
     std::cout<<id<<" Fable mirror seeds=1.."<<samples<<" first-place shares "<<wins[0]/samples<<", "<<wins[1]/samples<<", "<<wins[2]/samples
              <<"; physical spawns "<<physical[0]/samples<<", "<<physical[1]/samples<<", "<<physical[2]/samples<<std::endl;
   }
 }
 std::cout<<"game contract passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}
