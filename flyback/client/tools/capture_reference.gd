extends SceneTree
## Captures the same filmed match through the real bridge's legacy replay reader.
var app
var state:=0
var t0:=0
var replay:=""
var save_to:=""
var stop_tick:=20
func _initialize()->void:
	var a:=OS.get_cmdline_user_args()
	if a.size()<2:quit(2);return
	replay=a[0];save_to=a[1]
	if a.size()>2:stop_tick=int(a[2])
	app=load("res://main.tscn").instantiate();root.add_child(app);t0=Time.get_ticks_msec()
func _process(_dt:float)->bool:
	if Time.get_ticks_msec()-t0>35000:push_error("reference frame timed out: "+str(app.last_message));quit(3);return true
	if state==0 and app.online:
		state=1;app.cmd({"op":"replay","path":replay})
	elif state==1 and app.latest_tick>=stop_tick:
		state=2;save_frame()
	return false
func save_frame()->void:
	await RenderingServer.frame_post_draw
	var im:=root.get_texture().get_image();im.save_png(save_to)
	print("real replay ",app.game.get("map")," tick ",app.latest_tick," native ",im.get_size()," ",RenderingServer.get_current_rendering_method());quit()
