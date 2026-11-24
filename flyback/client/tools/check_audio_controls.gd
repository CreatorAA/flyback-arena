extends SceneTree
## Runs against previously recorded, seat-restricted QUIC/native snapshots; kept
## separate from the movie test so changing a setting cannot color its audio.
var heard:Array=[]
func _initialize()->void:call_deferred("check")
func demand(ok:bool,message:String)->bool:
	if not ok:push_error(message);quit(2);return false
	return true
func check()->void:
	var tape:Dictionary=JSON.parse_string(FileAccess.get_file_as_string(OS.get_cmdline_user_args()[0]))
	var app=load("res://main.tscn").instantiate();root.add_child(app)
	var r:={"type":"room","mode":"offline","state":"playing","round_epoch":1,"speed":1,"self_seat":tape.view}
	app._packet(r);var m:Dictionary=tape.manifest.duplicate();m.type="manifest";app._packet(m)
	var mixer=app.arena.combat_audio
	if not demand(AudioServer.get_bus_index("ArenaFX")>=0,"FX bus missing"):return
	mixer.cue_played.connect(func(cue,seat,tick,pan):heard.append([cue,seat,tick,pan]))
	var selected:Dictionary={}
	var hidden:=0
	for f in tape.frames:
		for p in f.players:
			if not p.vis and f.players[int(tape.view)].alive:
				selected=f.duplicate(true);hidden=int(p.seat);break
		if not selected.is_empty():break
	if not demand(not selected.is_empty(),"no hidden opponent in tape"):return
	selected.type="snapshot";selected.round_epoch=1;selected.events=[];app._packet(selected)
	var me:=int(tape.view);var tick:=int(selected.tick);var epoch:=1
	mixer.set_level(.85,false);app._help()
	var mute:CheckButton=app.popup.find_child("SfxMute",true,false)
	var slider:HSlider=app.popup.find_child("SfxVolume",true,false)
	slider.value=65;mute.button_pressed=true
	if not demand(is_equal_approx(mixer.level,.65) and mixer.muted and AudioServer.is_bus_mute(AudioServer.get_bus_index("ArenaFX")),"mute/volume controls"):return
	var saved:=ConfigFile.new();saved.load("user://settings.cfg")
	if not demand(saved.get_value("audio","mute")==true and is_equal_approx(float(saved.get_value("audio","level")),.65),"audio preferences not persisted"):return
	app.arena.play_events([{"tick":tick,"a":me,"type":"RELEASE","ordinal":0}],epoch)
	if not demand(heard.is_empty(),"muted cue"):return
	mute.button_pressed=false;app.close_popup();app.arena.reset_feedback(false)
	app.arena.play_events([{"tick":tick,"a":hidden,"type":"DASH","ordinal":0},{"tick":tick,"a":hidden,"type":"SWING","ordinal":1}],epoch)
	if not demand(heard.is_empty(),"located a hidden opponent"):return
	var hit:={"tick":tick,"a":-1,"b":me,"type":"HIT","ordinal":2}
	app.arena.play_events([hit,hit],epoch)
	var target:Vector3=app.arena.cast[me].target+Vector3.UP*.35
	var pan:=clampf(app.arena.camera.unproject_position(target).x/root.get_visible_rect().size.x*2.-1.,-1.,1.)
	if not demand(heard.size()==1 and heard[0][0]=="hit" and int(heard[0][1])==me and is_equal_approx(pan,float(heard[0][3])),"anonymous hit not located on visible victim"):return
	r=r.duplicate();r.state="paused";app._packet(r);app.arena.play_events([{"tick":tick+1,"a":me,"type":"DASH","ordinal":3}],epoch)
	if not demand(heard.size()==1 and mixer.voices.all(func(v):return not v.playing),"pause left stale audio"):return
	app.arena.sim_rate=1.;app.arena.reset_feedback(true)
	selected.events=[hit];app.arena.update(selected)
	if not demand(heard.size()==1,"seek played an old baseline hit"):return
	app.arena.play_events([{"tick":tick+1,"a":me,"type":"RELEASE","ordinal":0}],epoch)
	if not demand(heard.size()==2 and heard[-1][0]=="release","new event after seek lost"):return
	print("PASS saved volume/mute, paused voices, hidden action silent, anonymous hit on visible victim only, duplicate and seek suppression")
	app.queue_free();await process_frame;await process_frame;quit()
