extends SceneTree
## Live cold start. Optional arguments: capture directory, original replay path.
var app
var stage:=0
var deadline:=0
var capture:=""
var replay_path:=""
var visits:Array[int]=[]
var marks:Array[int]=[]
var first_pos:=Vector3.ZERO
var live_ms:=0
var live_tick:=-1
var live_origin:=0
var frame_ms:Array[int]=[]
var taking:=false
var next_photo:=0
var saved_phases:Dictionary={}
var score_ok:=false
var completed:=false

func _initialize()->void:
	var args:=OS.get_cmdline_user_args()
	if args.size()>0 and args[0]!="-":
		capture=args[0];DirAccess.make_dir_recursive_absolute(capture)
	if args.size()>1:replay_path=args[1]
	app=load("res://main.tscn").instantiate();root.add_child(app);deadline=Time.get_ticks_msec()+35000

func fail_(message:String)->void:
	completed=true;push_error(message);quit(2)

func _packet(p:Dictionary)->void:
	if stage<2 or stage>3 or p.get("type")!="room":return
	var state:String=p.get("state","")
	if state=="countdown" and (visits.is_empty() or visits.back()!=int(p.countdown_seconds)):
		var n:=int(p.countdown_seconds);visits.append(n);marks.append(Time.get_ticks_msec())
		if app.latest_tick!=0 or app.page!="match" or app.alive_inputs or not app.arena.cast[0].shown or not app.start_signal.visible or app.start_signal.phase!=n:
			fail_("countdown before visible stationary field: "+str([n,app.page,app.latest_tick,app.alive_inputs]));return
		if n==3:first_pos=app.arena.cast[0].position
		if app.arena.cast[0].position!=first_pos or app.arena.sim_rate!=0. or not app.hud_vote.disabled:
			fail_("movement or pause available before opening");return
		app._send_input(1);app._recall_button(true)
		if app.seq!=0 or app.queued_act!=0 or app.recall_at>=0 or app.recall_hold:
			fail_("action buffered before GO");return
	elif state=="playing" and live_ms==0:
		marks.append(Time.get_ticks_msec());live_ms=marks.back()
		if visits!=[3,2,1] or not app.alive_inputs or not app.start_signal.visible or app.start_signal.phase!=0:
			fail_("missing authority GO: "+str(visits));return
		var span:=live_ms-marks[0]
		if span<2800 or span>3400:fail_("countdown wall ms: "+str(span));return
		if float(p.speed)!=.8 or app.tempo.get_item_metadata(app.tempo.selected)!=.8 or app.arena.sim_rate!=.8:
			fail_("0.8x not set consistently");return
		print("Visible field at tick 0. Referee signals 3/2/1/GO at wall ms: ",marks.map(func(x):return x-marks[0]),"; early throw/recall neutral.")
		stage=3

func _process(_dt:float)->bool:
	if completed:return true
	var now:=Time.get_ticks_msec()
	if now>deadline:fail_("opening test timeout "+str([stage,app.room.get("state"),app.page,visits,app.last_message]));return true
	if stage==0 and app.online:
		app.bridge.packet.connect(_packet)
		app.cmd({"op":"local","name":"驿风信使","settings":{"title":"首局手感","seat_count":2,"maps":["tidelock"],"map_policy":"fixed","slots":["human","claude-fable-5.2"]}});stage=1
	elif stage==1 and app.page=="ready":
		stage=2;app.cmd({"op":"start"})
	elif stage in [2,3]:
		if app.page=="match" and app.latest_tick==0 and not score_ok:
			if app.clock_text.text!="0:40":fail_("match clock changed on open");return true
			score_ok=true
		if not capture.is_empty() and not visits.is_empty() and not taking and now>=next_photo:
			next_photo=now+64;photo(app.start_signal.phase)
		if stage==3:
			if app.latest_tick>=1 and live_tick<0:live_tick=app.latest_tick;live_origin=now
			if live_tick>=0 and now-live_origin>=1520 and not taking:
				var elapsed:=(now-live_origin)*.001
				var rate:float=(app.latest_tick-live_tick)/elapsed
				print("Live authority: ",snappedf(rate,.1)," ticks/wall-second; selected 0.8×; countdown display dismissed.")
				if absf(rate-16)>1.35 or app.start_signal.visible:fail_("bad live pace or persistent overlay");return true
				if not capture.is_empty():finish_capture()
				app.tempo.select(2);app.tempo.item_selected.emit(2);stage=4
	elif stage==4 and app.room.speed==1. and app.arena.sim_rate==1.:
		app.tempo.select(1);app.tempo.item_selected.emit(1);stage=5
	elif stage==5 and app.room.speed==.8:
		print("Real speed picker: 0.8× → 1× → 0.8×");app.cmd({"op":"leave"});stage=6
	elif stage==6 and app.page=="home":
		if replay_path.is_empty():completed=true;quit();return true
		app.cmd({"op":"replay","path":replay_path});stage=7
	elif stage==7 and app.page=="match" and app.latest_tick>2:
		if not app.replay_mode or app.room.speed!=1 or app.start_signal.visible:fail_("countdown replayed on old tape");return true
		print("Original replay opened at 1× with no new start signal.");app.cmd({"op":"leave"});stage=8
	elif stage==8 and app.page=="home":
		stage=9;finish()
	return false

func finish()->void:
	app.queue_free();await process_frame;await process_frame;quit()

func photo(n:int)->void:
	taking=true;await RenderingServer.frame_post_draw
	var img:=root.get_texture().get_image()
	if not saved_phases.has(n) and app.start_signal.visible:
		img.save_png(capture.path_join("signal-%s.png"%n));saved_phases[n]=true
	img.save_jpg(capture.path_join("%05d.jpg"%frame_ms.size()),.92);frame_ms.append(Time.get_ticks_msec());taking=false

func finish_capture()->void:
	var f:=FileAccess.open(capture.path_join("frames.ffconcat"),FileAccess.WRITE)
	f.store_line("ffconcat version 1.0")
	for i in frame_ms.size():
		f.store_line("file '%05d.jpg'"%i);f.store_line("duration %.5f"%((frame_ms[i+1]-frame_ms[i])*.001 if i+1<frame_ms.size() else .060))
	f.store_line("file '%05d.jpg'"%(frame_ms.size()-1))
