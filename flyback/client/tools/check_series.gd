extends SceneTree
## Drives the settings widgets, starts once, then watches three real rounds.
var app
var stage:=0
var began:=0
var epochs:Array=[]
var round_maps:Array=[]
var totals:Array=[]
var seen_results:=0
var clicked:=false
func _initialize()->void:
	app=load("res://main.tscn").instantiate();root.add_child(app);began=Time.get_ticks_msec()
func _packets(p:Dictionary)->void:
	if p.get("type")=="manifest":
		var s:Dictionary=p.get("series",{})
		if s.is_empty():push_error("missing server series");quit(2);return
		epochs.append([int(s.map_index),int(s.map_round)]);round_maps.append(p.map)
	if p.get("type")=="result":totals.append(p.get("series",{}));seen_results+=1
func btn(t:String):
	for b in app.main_layer.find_children("*","Button",true,false):
		if b.text==t:return b
	return null
func _process(_dt:float)->bool:
	if Time.get_ticks_msec()-began>145000:push_error("series timeout: "+str(stage)+" "+str(app.last_message)+" "+str(epochs));quit(3);return true
	match stage:
		0:
			if app.online:app.bridge.packet.connect(_packets);btn("AI 表演赛").pressed.emit();stage=1
		1:
			var opts:=app.main_layer.find_children("*","OptionButton",true,false) as Array
			var policy:OptionButton=opts[1];policy.select(1);policy.item_selected.emit(1);stage=2
		2:
			var cfg:Dictionary=app.scratch_setup.data
			for check in app.main_layer.find_children("*","CheckBox",true,false):
				if check.text not in ["双湖","阴阳"] and check.button_pressed:check.button_pressed=false
			var spins=app.main_layer.find_children("*","SpinBox",true,false);spins[0].value=2
			btn("单独设置地图回合数").button_pressed=true;stage=3
		3:
			for spin in app.main_layer.find_children("*","SpinBox",true,false):
				if spin.get_parent().get_child(0).text=="阴阳":spin.value=1
			btn("进入准备厅   →").pressed.emit();stage=4
		4:
			if app.page=="ready":
				var c:Dictionary=app.room.settings
				if int(c.rounds_per_map)!=2 or int(c.map_rounds.get("yinyang",0))!=1 or c.maps.size()!=2:push_error("widget round trip failed: "+str(c));quit(4);return true
				btn("开始比赛   →").pressed.emit();stage=5
		5:
			if app.page=="match" and app.latest_tick>=0 and not clicked:
				clicked=true;app.cmd({"op":"speed","value":2})
			elif seen_results==3:
				if epochs!=[[1,1],[1,2],[2,1]] or round_maps!=["twinlakes","twinlakes","yinyang"] or not totals[-1].finished or totals[-1].standings.size()!=2:
					push_error("wrong series: "+str(epochs)+" "+str(round_maps)+" "+str(totals));quit(5);return true
				print("UI→authority: 3 matches after one start; maps=",round_maps," map-round=",epochs," standings=",totals[-1].standings);app.cmd({"op":"leave"});stage=6
		6:
			if app.page=="home":quit()
	return false
