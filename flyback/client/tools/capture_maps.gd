extends SceneTree
## Render live opening frames for the release gallery via the actual bridge.
var app
const MAPS=["tidelock","kiln","windrose","moonwell"]
var idx:=0
var phase:=0
var deadline:=0
var dest:=""
func _initialize()->void:
	dest=OS.get_cmdline_user_args()[0]
	app=load("res://main.tscn").instantiate();root.add_child(app)
	deadline=Time.get_ticks_msec()+100000
func _process(_dt:float)->bool:
	if Time.get_ticks_msec()>deadline:
		push_error("map render timeout %s %s %s" % [MAPS[idx],phase,app.last_message]);quit(3);return true
	if phase==0 and app.online and app.page=="home":
		var seats:=["gpt-6.2-astra","claude-fable-5.2","gpt-6.2-sol","claude-opus-5.5"]
		if idx>=2:seats.pop_back()
		phase=1
		app.cmd({"op":"local","name":"地图实景","settings":{"seat_count":seats.size(),"slots":seats,"maps":[MAPS[idx]],"map_policy":"fixed"}})
	elif phase==1 and app.room.get("state","")=="ready" and app.room.get("settings",{}).get("maps",[])==[MAPS[idx]]:
		phase=2;app.cmd({"op":"start"})
	elif phase==2 and app.game.get("map","")==MAPS[idx] and app.latest_tick>=12:
		phase=3;save_frame()
	return false
func save_frame()->void:
	await RenderingServer.frame_post_draw
	var im:=root.get_texture().get_image()
	var err:=im.save_png(dest.path_join(MAPS[idx]+".png"))
	if err!=OK:quit(4);return
	print("GALLERY ",MAPS[idx]," tick ",app.latest_tick," camera ",app.arena.camera.size)
	idx+=1
	if idx==MAPS.size():quit();return
	app.cmd({"op":"leave"});phase=0
