extends SceneTree
## In a real 3-human listen room, seat 0 walks into the water; seats 1 and 2
## stay in play. Checks delayed self replay, then survivor's final replay.
var app
var survivor
var peers:Array=[]
var rooms:Array=[{},{}]
var listed:Array=[false,false]
var s:=0
var index:=0
var until:=0
var addr:=""
var pin:=""
var id:=""
var epoch:=0
var died_at:=0
var last_send:=0
var first_live:=-1
var final_clip:Dictionary={}
var got_result:=false
var latest:Array=[{},{}]
func _initialize()->void:call_deferred("setup")
func setup()->void:
	app=load("res://main.tscn").instantiate();root.add_child(app)
	for i in 2:
		var b
		if i==1:
			survivor=load("res://main.tscn").instantiate();root.add_child(survivor);b=survivor.bridge
		else:
			b=load("res://scripts/bridge.gd").new();root.add_child(b);b.boot()
		b.packet.connect(peer.bind(i));peers.append(b)
	until=Time.get_ticks_msec()+52000
func peer(p:Dictionary,i:int)->void:
	match p.get("type"):
		"trust":
			if str(p.fingerprint)==pin:peers[i].send({"op":"trust","accept":true})
			else:quit(7)
		"directory":listed[i]=not p.rooms.is_empty()
		"room":rooms[i]=p
		"snapshot":latest[i]=p
		"result":if i==1:got_result=true
		"replay_clip":if i==1:final_clip=p
		"error":push_error("peer "+str(i)+": "+str(p));quit(8)
func move(peer_index:int,mx:int,my:int)->void:
	var p:Dictionary=app.room if peer_index<0 else rooms[peer_index]
	var cmd:={"op":"input","round_epoch":p.round_epoch,"timeline_epoch":p.timeline_epoch,"seq":Time.get_ticks_msec(),"mx":mx,"my":my,"act":0,"ax":mx,"ay":my,"msg":0}
	if peer_index<0:app.cmd(cmd)
	else:peers[peer_index].send(cmd)
func _process(_dt:float)->bool:
	var now:=Time.get_ticks_msec()
	if now>until:push_error("clip E2E timeout: "+str([s,app.page,app.latest_tick,app.last_message,got_result]));quit(2);return true
	match s:
		0:
			if app.online:app.cmd({"op":"host","name":"检验席 A","port":0,"settings":{"seat_count":3,"slots":["human","human","human"],"maps":["trident"],"map_policy":"fixed","title":"回看实测"}});s=1
		1:
			if app.page=="ready":
				addr="127.0.0.1:"+str(int(app.room.port));id=app.room.id;pin=app.directory.fingerprint;s=2
		2:
			peers[index].send({"op":"connect","address":addr,"fingerprint":pin,"name":"检验席 "+str(index+2)});s=3
		3:
			if listed[index]:peers[index].send({"op":"join","room_id":id,"password":"","role":"player"});s=4
		4:
			if int(rooms[index].get("self_seat",-1))==index+1:
				peers[index].send({"op":"ready","ready":true});index+=1
				if index<2:s=2
				else:app.cmd({"op":"ready","ready":true});s=5
		5:
			var ready:=true
			for r in app.room.slots:ready=ready and bool(r.ready)
			if ready:app.cmd({"op":"start","room_rev":app.room.room_rev});s=6
		6:
			if app.page=="match" and app.latest_tick>=1:
				if bool(app.snapshot.players[0].alive):
					if now-last_send>32:move(-1,0,-1000);last_send=now
				else:epoch=int(app.room.round_epoch);died_at=now;s=7
		7:
			if now-died_at>3330 and app.clip_button and app.clip_button.visible:app.clip_button.pressed.emit();s=8
		8:
			if not app.clip.is_empty():
				var c:Dictionary=app.clip
				if int(c.victim_seat)!=0 or int(c.round_epoch)!=epoch or app.arena.close_subject!=0 or c.frames.size()>30:push_error("incorrect death/seat clip");quit(3);return true
				first_live=int(app.clip_live.tick);s=9
		9:
			if app.clip.is_empty() and app.page=="match":
				if app.latest_tick<=first_live:push_error("live state did not advance during replay");quit(4);return true
				print("mid-match UI self clip victim=0 epoch=",epoch," kept live frames ",first_live,"→",app.latest_tick);s=10
		10:
			if not latest[0].is_empty() and not got_result and now-last_send>32:move(0,1000,1000);last_send=now
			if got_result:died_at=now;s=11
		11:
			if now-died_at>3100 and survivor.page=="results":
				for button in survivor.main_layer.find_children("*","Button",true,false):
					if button.text=="回看致命一击":button.pressed.emit();s=12;break
		12:
			if not final_clip.is_empty():
				if int(final_clip.victim_seat)!=1 or int(final_clip.death_tick)<first_live or final_clip.scope!="final" or survivor.arena.close_subject!=1 or survivor.clip.is_empty():push_error("final clip incorrectly chose early seat");quit(5);return true
				print("winner's actual result button → scoped finale clip victim=1, camera target 1, death=",final_clip.death_tick," tick range=",final_clip.from_tick,"…",final_clip.to_tick)
				app.cmd({"op":"leave"});s=13
		13:
			if app.page=="home":quit()
	return false
