extends SceneTree
## Exercises a real LAN listen room with three separate local bridge processes.
var app
var guest
var late
var active:=0
var start:=0
var addr:=""
var room_id:=""
var expected_pin:=""
var guest_room:Dictionary={}
var late_room:Dictionary={}
var guest_list:=false
var late_list:=false
var late_snap:=false
var ticks_after:=-1
var password:="only-local-test"
var host_view:Dictionary={}
var guest_view:Dictionary={}
var paired:Dictionary={}
var movement_steps:=0
var flight_steps:=0
var simultaneous_events:=0
var move_dir:=Vector2i.ZERO
var first_tick:=-1
var launched:=false
var seq:=0

func _fail(s:String)->void:push_error(s);quit(9)

func _pair(t:int)->void:
	if paired.has(t) or not host_view.has(t) or not guest_view.has(t):return
	paired[t]=true
	var h:Dictionary=host_view[t];var g:Dictionary=guest_view[t]
	var a:Dictionary=h.players[1];var b:Dictionary=g.players[1]
	if a.vis:
		for key in ["x","y","vx","vy"]:
			if a[key]!=b[key]:_fail("real human sees a stale opponent at tick %d (%s: %s vs %s)"%[t,key,a[key],b[key]]);return
		if int(b.vx)!=0 or int(b.vy)!=0:movement_steps+=1
	var d:Dictionary=h.darts[1];var own:Dictionary=g.darts[1]
	if d.vis:
		if int(d.st)!=int(own.st):_fail("stale enemy dart state at tick "+str(t));return
		if int(d.st) in [1,2,6]:
			for key in ["x","y","vx","vy"]:
				if d[key]!=own[key]:_fail("stale enemy dart "+key+" at tick "+str(t));return
			flight_steps+=1
	if a.vis:
		for e in g.events:
			if int(e.a)!=1 or str(e.type) not in ["THROW","RELEASE"]:continue
			var found:=false
			for observed in h.events:
				if observed.type==e.type and int(observed.a)==1 and observed.tick==e.tick and observed.ordinal==e.ordinal:found=true
			if not found:_fail("missing visible "+str(e.type)+" on the same human-seat tick "+str(t));return
			simultaneous_events+=1

func _host_frame(p:Dictionary)->void:
	if p.get("type")!="snapshot" or active!=5:return
	var t:=int(p.tick);var opponent:Dictionary=p.players[1]
	if opponent.vis:
		var expected:=Vector3(float(opponent.x)*.001,0,float(opponent.y)*.001)
		if app.arena.cast[1].authority_tick!=t or app.arena.cast[1].target.distance_to(expected)>.0001:
			_fail("client inserted delay after receiving current visible coordinates");return
	elif app.arena.cast[1].is_visible_in_tree():_fail("hidden opponent was drawn");return
	host_view[t]=p;_pair(t)

func _exercise_guest(p:Dictionary)->void:
	if active!=5 or app.game.is_empty():return
	var t:=int(p.tick)
	if first_tick<0:
		first_tick=t
		var b:Dictionary=p.players[1];var pos:=Vector2(float(b.x),float(b.y))*.001
		var best:=-1
		# Walk down the longest clear cardinal run from this seed's true spawn.
		for direction:Vector2i in [Vector2i.LEFT,Vector2i.UP,Vector2i.DOWN,Vector2i.RIGHT]:
			var steps:=0
			for j in range(1,13):
				var spot:Vector2=pos+Vector2(direction)*float(j)*.25
				var tile:int=app.arena.island._cell(floori(spot.x/.5),floori(spot.y/.5))
				if tile!=0:break
				steps+=1
			if steps>best:best=steps;move_dir=direction
	var dt:=t-first_tick
	var travel:=move_dir if dt>=3 and dt<=8 else Vector2i.ZERO
	var act:=1 if dt>=10 and not launched else 0
	if act==1:launched=true
	seq+=1
	guest.send({"op":"input","seq":seq,"round_epoch":guest_room.round_epoch,"timeline_epoch":guest_room.timeline_epoch,"mx":travel.x*1000,"my":travel.y*1000,"act":act,"ax":move_dir.x*1000,"ay":move_dir.y*1000,"msg":0})
	guest_view[t]=p;_pair(t)
func _initialize()->void:
	app=load("res://main.tscn").instantiate();root.add_child(app)
	guest=load("res://scripts/bridge.gd").new();root.add_child(guest);guest.packet.connect(_peer.bind(false));guest.boot()
	late=load("res://scripts/bridge.gd").new();root.add_child(late);late.packet.connect(_peer.bind(true));late.boot()
	start=Time.get_ticks_msec()
func _peer(v:Dictionary,is_late:bool)->void:
	var p=late if is_late else guest
	match v.get("type"):
		"trust":
			if expected_pin.is_empty() or str(v.fingerprint)!=expected_pin:
				push_error("peer identity does not match the privately hosted test room");quit(2);return
			p.send({"op":"trust","accept":true})
		"directory":
			if not v.rooms.is_empty():
				if is_late:late_list=true
				else:guest_list=true
		"room":
			if is_late:late_room=v
			else:guest_room=v
		"snapshot":
			if is_late:late_snap=true
			else:_exercise_guest(v)
		"error":
			push_error("peer error: "+str(v))
			quit(3)
func _process(_dt:float)->bool:
	if Time.get_ticks_msec()-start>50000:
		push_error("LAN timeout at state "+str(active)+"; "+str(app.last_message));quit(4);return true
	match active:
		0:
			if app.online:
				app.bridge.packet.connect(_host_frame)
				app.cmd({"op":"host","port":0,"name":"本机房主","password":password,"settings":{"title":"联机验证","seat_count":2,"maps":["twinlakes"],"map_policy":"fixed","slots":["human","human"]}})
				active=1
		1:
			if app.page=="ready" and int(app.room.port)>0:
				expected_pin=str(app.directory.get("fingerprint",""));addr="127.0.0.1:"+str(int(app.room.port));room_id=str(app.room.id)
				guest.send({"op":"connect","address":addr,"name":"真人二号","fingerprint":expected_pin});active=2
		2:
			if guest_list:
				guest.send({"op":"join","room_id":room_id,"password":password,"role":"player"});active=3
		3:
			if int(guest_room.get("self_seat",-1))==1:
				app.cmd({"op":"ready","ready":true});guest.send({"op":"ready","ready":true});active=4
		4:
			if app.room.get("slots",[]).size()==2 and app.room.slots[0].ready and app.room.slots[1].ready:
				app.cmd({"op":"start","room_rev":app.room.room_rev});active=5
		5:
			if app.page=="match" and first_tick>=0 and app.latest_tick>first_tick+30:
				if int(app.game.get("human_observation_delay_ticks",-1))!=0 or int(app.game.get("ai_observation_delay_ticks",-1))!=4:
					_fail("wrong observation contract in actual room manifest");return true
				if movement_steps<3 or flight_steps<2 or simultaneous_events<1:
					_fail("insufficient live samples: "+str([movement_steps,flight_steps,simultaneous_events]));return true
				print("paired human POVs (not spectator/replay): ",movement_steps," current moving steps, ",flight_steps," flying-dart steps, ",simultaneous_events," simultaneous visible throw events; Godot rendered current endpoints")
				app._pause();active=6
		6:
			if app.room.get("vote") is Dictionary:
				if int(app.room.vote.yes)!=1 or int(app.room.vote.needed)!=2 or app.room.state!="playing":
					push_error("vote changed time without majority");quit(5);return true
				guest.send({"op":"vote","yes":true});active=7
		7:
			if app.room.get("state")=="paused":
				late.send({"op":"connect","address":addr,"fingerprint":expected_pin,"name":"中场观众"});active=8
		8:
			if late_list:late.send({"op":"join","room_id":room_id,"password":password,"role":"player"});active=9
		9:
			if not late_room.is_empty():
				if int(late_room.self_seat)!=-1:push_error("late arrival received player slot");quit(6);return true
				app._pause();active=10
		10:
			if app.room.get("vote") is Dictionary:guest.send({"op":"vote","yes":true});active=11
		11:
			if app.room.get("state")=="playing" and late_snap:
				print("QA authenticated LAN: ",addr,", two real seats, public pause/resume 2/2, mid-match seat=-1, delayed spectator stream")
				app.cmd({"op":"leave"});active=12
		12:
			if app.page=="home":quit()
	return false
