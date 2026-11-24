extends SceneTree
## Real local bridge + real Bot. Run with --script res://tools/check_client.gd.
var app
var started:int
var step:=0
var poll_at:=0
var out_path:=""
var three:=false
var four:=false
var cpu_only:=false
var vision:=false
var real_hidden:=0
var real_dart_only:=0
var vision_at:=-1
var err_count:=0
var pseq:=-1
func _initialize()->void:
	app=load("res://main.tscn").instantiate();root.add_child(app);started=Time.get_ticks_msec()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture-game="):out_path=arg.trim_prefix("--capture-game=")
		if arg=="--trio":three=true
		if arg=="--ffa":four=true
		if arg=="--ai":cpu_only=true
		if arg=="--vision":vision=true
	app.dev_capture="" # This harness captures the actual 3D match, not the launch scene.
func button(label:String):
	for b in app.main_layer.find_children("*","Button",true,false):
		if b.text==label:return b
	return null
func _process(_dt:float)->bool:
	var now:=Time.get_ticks_msec()
	if vision and app.latest_tick!=vision_at and app.page=="match":
		vision_at=app.latest_tick
		for p in app.snapshot.get("players",[]):
			var i:=int(p.seat)
			if not p.get("vis",true):
				if app.arena.cast[i].visible or p.has("x") or p.has("y"):push_error("hidden player leaked");quit(4);return true
				real_hidden+=1
				var d:Dictionary=app.snapshot.darts[i]
				if d.get("vis",false) and int(d.get("st",0)) not in [0,5]:
					if not app.arena.darts[i].visible:push_error("legal independent dart not shown");quit(5);return true
					real_dart_only+=1
	if now-started>45000:
		printerr("QA timeout step=",step," page=",app.page," status=",app.last_message," snapshot=",app.latest_tick);quit(2);return true
	if now<poll_at:return false
	poll_at=now+45
	match step:
		0:
			if app.online:
				var b=button("AI 表演赛" if cpu_only else "快速挑战     →")
				if b:b.pressed.emit();step=1
		1:
			if app.page=="setup":
				if three or four:
					var op:OptionButton=app.main_layer.find_children("*","OptionButton",true,false)[0]
					var i:=2 if four else 1;op.select(i);op.item_selected.emit(i)
				step=2
		2:
			var b=button("进入准备厅   →")
			if b:b.pressed.emit();step=3
		3:
			var b=button("开始比赛   →")
			if b and app.page=="ready":b.pressed.emit();step=4
		4:
			if app.page=="match" and app.latest_tick>=(75 if vision else 5):
				print("QA actual setup→ready→match; seat=",app.room.self_seat," n=",app.game.seats.size()," map=",app.game.map)
				if not cpu_only:
					var e:=InputEventMouseButton.new();e.button_index=MOUSE_BUTTON_LEFT;e.position=Vector2(750,400);e.pressed=true;Input.parse_input_event(e)
				app._pause();step=5
		5:
			if app.room.get("state")=="paused":
				app.cmd({"op":"speed","value":2});step=9;poll_at=now+150
		9:
			pseq=app.latest_tick;step=6;poll_at=now+275
		6:
			if app.latest_tick!=pseq:printerr("QA pause moved from ",pseq," to ",app.latest_tick);quit(3);return true
			app._pause();step=7
		7:
			if app.latest_tick>pseq+4:
				print("QA pause/resume/speed: tick=",app.latest_tick," applied=",app.snapshot.get("last_applied_input_seq","?"))
				if not out_path.is_empty():
					root.get_texture().get_image().save_png(out_path)
				app.cmd({"op":"leave"});step=8
		8:
			if app.page=="home":
				print("QA left room, returned home; rendered hidden seat-steps=",real_hidden," independent darts=",real_dart_only);quit()
	return false
