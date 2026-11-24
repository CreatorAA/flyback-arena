extends SceneTree
## Run with a real graphical Godot renderer and the native bridge. Records presentation intervals,
## not simulated tick deltas. Arguments: original-replay.txt output.json [pve] [late-frame.png]
var app
var state:=0
var last_usec:=0
var started:=0
var data:Array=[]
var args:PackedStringArray
var snap_tick:=-1
var photo_taken:=false
var start_tick:=6
var sample_limit:=374
var sent_round:=false
var walk_start:=0
var last_input:=-1
func _initialize()->void:
	args=OS.get_cmdline_user_args();started=Time.get_ticks_msec()
	app=load("res://main.tscn").instantiate();root.add_child(app)
	RenderingServer.viewport_set_measure_render_time(root.get_viewport_rid(),true)
func _process(_dt:float)->bool:
	var now:=Time.get_ticks_usec()
	var tick:int=app.latest_tick
	if Time.get_ticks_msec()-started>65000:
		printerr("frame-pacing timed out at ",state," ",tick);quit(2);return true
	if state==0 and app.online:
		state=1
		if args.size()>2 and args[2]=="pve":
			sample_limit=160
			app.cmd({"op":"local","name":"FrameQA","settings":{"seat_count":2,"map_policy":"fixed","maps":["twinlakes"],"slots":["human","gpt-6.2-astra"]}})
		else:app.cmd({"op":"replay","path":args[0]})
	if state==1 and args.size()>2 and args[2]=="pve" and not sent_round and app.room.get("state")=="ready":
		sent_round=true;app.cmd({"op":"start","room_rev":app.room.room_rev})
	if state==1 and tick>=start_tick:state=2;walk_start=Time.get_ticks_msec()
	if state==2:
		var f:int=app.snapshot.get("fallen",0)
		if last_usec>0:
			data.append([tick, snappedf((now-last_usec)/1000.,.01),
				snappedf(RenderingServer.viewport_get_measured_render_time_gpu(root.get_viewport_rid()),.01),
				Performance.get_monitor(Performance.RENDER_TOTAL_DRAW_CALLS_IN_FRAME),
				Performance.get_monitor(Performance.RENDER_TOTAL_PRIMITIVES_IN_FRAME),f])
		last_usec=now
		if args.size()>2 and args[2]=="pve" and tick!=last_input:
			var dir:=1 if (tick/12 as int)%2==0 else -1
			app.cmd({"op":"input","mx":dir*1000,"my":300,"act":0,"ax":1000,"ay":0,"seq":10000+tick});last_input=tick
		if args.size()>3 and not photo_taken and tick>=366:
			photo_taken=true;call_deferred("photo")
		if tick>=sample_limit or (app.result_data.size()>0 and data.size()>30):
			var f2:=FileAccess.open(args[1],FileAccess.WRITE)
			f2.store_string(JSON.stringify({"renderer":RenderingServer.get_current_rendering_method(),"size":str(root.size),"cols":["tick","frame_ms","gpu_ms","draws","primitives","fallen"],"frames":data}));f2.close()
			print("Frame pacing sampled ",data.size()," frames through tick ",tick);quit(0);return true
	return false
func photo()->void:
	await RenderingServer.frame_post_draw
	root.get_texture().get_image().save_png(args[3])
