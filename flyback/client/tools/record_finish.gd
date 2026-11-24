extends SceneTree
## Captures actual rendered frames from the real bridge, preserving wall-clock cadence.
## Args: archived-replay target-dir start-tick expected-animation.
var app
var source:=""
var folder:=""
var at:=0
var expect:=""
var phase:=0
var began:=0
var last:=0
var first_result:=0
var count:=0
var busy:=false
var times:Array=[]
var kinds:Dictionary={}
var celebrated:=false
var cloud:=false
var skipped:=false
var want_images:=true
func _initialize()->void:
	var args:=OS.get_cmdline_user_args()
	if args.size()<4:quit(2);return
	source=args[0];folder=args[1];at=int(args[2]);expect=args[3];want_images=folder!="-"
	if want_images:DirAccess.make_dir_recursive_absolute(folder)
	app=load("res://main.tscn").instantiate();root.add_child(app);began=Time.get_ticks_msec()
func _process(_dt:float)->bool:
	var now:=Time.get_ticks_msec()
	if now-began>40000:push_error("recording timeout: "+str([phase,app.page,app.latest_tick,app.last_message]));quit(3);return true
	if phase==0 and app.online:app.cmd({"op":"replay","path":source});phase=1
	elif phase==1 and app.latest_tick>=0:
		if at>app.latest_tick:app.cmd({"op":"seek","tick":at})
		phase=2
	elif phase==2 and app.latest_tick>=at:phase=3
	if phase!=3:return false
	for p in app.arena.cast:
		var anim=p.presentation
		if not anim.death_kind.is_empty():kinds[anim.death_kind]=true
		if anim.celebrated and anim.victory_time>.9 and anim.fx.size()>0:celebrated=true
		if anim.puffed:cloud=true
	if app.page=="results":
		if first_result==0:first_result=now
		if now-first_result>950 and not busy:
			print("actual finish: ",source.get_file()," tick=",app.latest_tick," styles=",kinds," poof/cloud=",cloud," victory + confetti=",celebrated," frames=",count)
			if want_images:
				var f:=FileAccess.open(folder.path_join("frames.ffconcat"),FileAccess.WRITE);f.store_line("ffconcat version 1.0")
				for i in count:
					f.store_line("file '%05d.jpg'"%i);f.store_line("duration %.5f"%((times[i+1]-times[i])/1000. if i+1<count else .04))
				f.store_line("file '%05d.jpg'"%(count-1))
			quit(0 if kinds.has(expect) and celebrated and cloud else 4);return true
	if want_images and not busy and now-last>=32:
		last=now;grab()
	return false
func grab()->void:
	busy=true;await RenderingServer.frame_post_draw
	times.append(Time.get_ticks_msec());root.get_texture().get_image().save_jpg(folder.path_join("%05d.jpg"%count),.93);count+=1;busy=false
