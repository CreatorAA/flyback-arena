extends SceneTree
## Four independent bridge processes join one actual listen host. All four seats
## must resolve to separate, animated GLBs rather than a primitive fallback.
var app
var peers:Array=[]
var rooms:Array=[{},{},{}]
var listed:Array=[false,false,false]
var active:=0
var index:=0
var start:=0
var addr:=""
var pin:=""
var room_id:=""
var shot:=""
func _initialize()->void:
	var a:=OS.get_cmdline_user_args()
	if not a.is_empty():shot=a[0]
	app=load("res://main.tscn").instantiate();root.add_child(app)
	for i in 3:
		var p=load("res://scripts/bridge.gd").new();root.add_child(p);peers.append(p)
		p.packet.connect(_peer.bind(i));p.boot()
	start=Time.get_ticks_msec()
func _peer(v:Dictionary,i:int)->void:
	match v.get("type"):
		"trust":
			if str(v.fingerprint)!=pin:push_error("unexpected host");quit(2);return
			peers[i].send({"op":"trust","accept":true})
		"directory":listed[i]=not v.rooms.is_empty()
		"room":rooms[i]=v
		"error":push_error("peer: "+str(v));quit(3)
func _process(_dt:float)->bool:
	if Time.get_ticks_msec()-start>55000:push_error("four-seat timeout "+str(active));quit(4);return true
	match active:
		0:
			if app.online:
				app.cmd({"op":"host","port":0,"name":"风邮","settings":{"title":"四位真人","seat_count":4,"maps":["skybridge"],"map_policy":"fixed","slots":["human","human","human","human"]}});active=1
		1:
			if app.page=="ready" and int(app.room.get("port",0))>0:
				pin=str(app.directory.get("fingerprint",""));addr="127.0.0.1:"+str(int(app.room.port));room_id=str(app.room.id);active=2
		2:
			peers[index].send({"op":"connect","address":addr,"fingerprint":pin,"name":["潮汐","树守","焰火"][index]});active=3
		3:
			if listed[index]:peers[index].send({"op":"join","room_id":room_id,"role":"player","password":""});active=4
		4:
			if int(rooms[index].get("self_seat",-1))==index+1:
				peers[index].send({"op":"ready","ready":true});index+=1
				if index<3:active=2
				else:app.cmd({"op":"ready","ready":true});active=5
		5:
			var all_ready:bool=app.room.get("slots",[]).size()==4
			for seat in app.room.get("slots",[]):all_ready=all_ready and bool(seat.ready)
			if all_ready:app.cmd({"op":"start","room_rev":app.room.room_rev});active=6
		6:
			if app.page=="match" and app.latest_tick>=8:
				var paths:Array=[]
				for i in 4:
					var p=app.arena.cast[i]
					if not p.human or not is_instance_valid(p.arm_a) or p.figure.get_child_count()!=1:push_error("missing human rig");quit(5);return true
					var path:String=p.figure.get_child(0).scene_file_path
					if path.get_file().get_basename()!=p.SKINS[i]:push_error("wrong variant: "+path);quit(6);return true
					paths.append(path.get_file())
				print("4 real human seats in one authenticated LAN room: ",paths,"; tick=",app.latest_tick)
				if not shot.is_empty():root.get_texture().get_image().save_png(shot)
				app.cmd({"op":"leave"});active=7
		7:
			if app.page=="home":quit()
	return false
