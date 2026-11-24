extends SceneTree
## Compare both the real replay tick and the presentation's continuous pose clock.
var app
var phase:=0
var idx:=0
var since:=0
var base_tick:=0
var base_pose:=0.
var rates:=[.5,.8,1.,2.,0.]
var deadline:=0
func _initialize()->void:
	app=load("res://main.tscn").instantiate();root.add_child(app);deadline=Time.get_ticks_msec()+25000
func _process(_dt:float)->bool:
	var now:=Time.get_ticks_msec()
	if now>deadline:push_error("tempo test timed out");quit(2);return true
	if phase==0 and app.online:
		app.cmd({"op":"replay","path":OS.get_cmdline_user_args()[0]});phase=1
	elif phase==1 and app.latest_tick>=3:
		var rate:float=rates[idx]
		if rate==0:app.cmd({"op":"pause"})
		else:app.cmd({"op":"speed","value":rate})
		phase=2
	elif phase==2 and is_equal_approx(app.arena.sim_rate,rates[idx]):
		since=now;base_tick=app.latest_tick;base_pose=app.arena.cast[0].presentation.clock;phase=3
	elif phase==3 and now-since>=1100:
		var dt:=float(now-since)*.001
		var server:=float(app.latest_tick-base_tick)/dt
		var visual:=float(app.arena.cast[0].presentation.clock-base_pose)/dt;var target:float=rates[idx]*20.
		print("rate ",rates[idx]," authority ",snappedf(server,.1)," ticks/s; continuous pose ",snappedf(visual,.1)," steps/s")
		if absf(server-target)>2.5 or absf(visual-target)>1.8:push_error("speed mismatch");quit(3);return true
		idx+=1
		if idx==rates.size():quit()
		else:phase=1
	return false
