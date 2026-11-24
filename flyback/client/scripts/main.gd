extends Node
const S=preload("res://scripts/style.gd")
const Arena=preload("res://scripts/arena.gd")
const Bridge=preload("res://scripts/bridge.gd")
const Setup=preload("res://scripts/setup_view.gd")
const Lobby=preload("res://scripts/lobby.gd")
const StartSignal=preload("res://scripts/start_signal.gd")
const TEMPOS=[["0.5×",0.5],["0.8×",0.8],["1×",1.0],["2×",2.0]]
var arena
var bridge
var ui:Control
var main_layer:Control
var overlays:Control
var popup:PanelContainer
var scrim:ColorRect
var notice:Label
var toast_timer:=0.0
var page:="home"
var me_name:="驿风信使"
var online:=false
var catalog:Dictionary={"maps":[],"bots":[],"servers":[]}
var room:Dictionary={}
var directory:Dictionary={}
var nearby:Dictionary={}
var game:Dictionary={}
var result_data:Dictionary={}
var snapshot:Dictionary={}
var status_text:Label
var clock_text:Label
var mode_text:Label
var phase_text:Label
var cue_text:Label
var dart_arrow:Label
var hud_seats:Array=[]
var hud_vote:Button
var tempo:OptionButton
var start_signal:StartSignal
var field_ready_enabled:=false
var field_ready_queued:=false
var opening_frame:Dictionary={}
var clip_button:Button
var skip_button:Button
var outro_left:=-1.
var outro_round:=-1
var clip_round:=-1
var latest_tick:=-1
var seq:=0
var key_age:=0.0
var aim:=Vector2.RIGHT
var pad_aim:=false
var queued_act:=0
var queued_until:=0
var recall_hold:=false
var recall_at:=-1
var recall_sent_tick:=-1
var scratch_setup:Dictionary={}
var settings_cfg:=ConfigFile.new()
var alive_inputs:=false
var completed_path:=""
var inspect_request:=""
var scene_start_time:=0
var scene_marker:=""
var camera_follow:OptionButton
var last_message:=""
var linked_address:=""
var server_connected:=false
var dev_capture:=""
var clip:Dictionary={}
var clip_time:=0.0
var clip_index:=-1
var clip_live={}
var replay_mode:=false
var replay_list:=[]
var previous_action:={}
var pending_predict_tick:=-1
var pending_events:Dictionary={}

func _ready()->void:
	settings_cfg.load("user://settings.cfg");me_name=settings_cfg.get_value("identity","name",me_name)
	get_window().min_size=Vector2i(880,560)
	arena=Arena.new();add_child(arena)
	arena.combat_audio.set_level(settings_cfg.get_value("audio","level",.85),settings_cfg.get_value("audio","mute",false))
	var layer:=CanvasLayer.new();add_child(layer)
	ui=Control.new();layer.add_child(ui);S.fill(ui);ui.theme=S.theme();ui.mouse_filter=Control.MOUSE_FILTER_IGNORE
	main_layer=Control.new();ui.add_child(main_layer);S.fill(main_layer);main_layer.mouse_filter=Control.MOUSE_FILTER_IGNORE
	overlays=Control.new();ui.add_child(overlays);S.fill(overlays);overlays.mouse_filter=Control.MOUSE_FILTER_IGNORE
	notice=S.text("",16,Color.WHITE,true);notice.visible=false;overlays.add_child(notice);notice.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER
	notice.set_anchors_and_offsets_preset(Control.PRESET_CENTER_BOTTOM);notice.offset_left=-400;notice.offset_right=400;notice.offset_top=-109;notice.offset_bottom=-59
	notice.add_theme_stylebox_override("normal",S.box(Color("#253b34ee"),12,10))
	_home()
	bridge=Bridge.new();add_child(bridge);bridge.packet.connect(_packet);bridge.failure.connect(_bridge_error);bridge.boot()
	for arg in OS.get_cmdline_user_args():
		if arg.begins_with("--capture="):dev_capture=arg.trim_prefix("--capture=")
		if arg.begins_with("--fixture="):call_deferred("_preview_fixture",arg.trim_prefix("--fixture="))
		if arg.begins_with("--open-replay="):replay_mode=true;call_deferred("cmd",{"op":"replay","path":arg.trim_prefix("--open-replay=")})

func _preview_fixture(path:String)->void:
	# Offline presentation QA: load a declared snapshot file, never join or create a match.
	var f=FileAccess.open(path,FileAccess.READ);if not f:return
	var fixture=JSON.parse_string(f.get_as_text());if not fixture is Dictionary:return
	room=fixture.room;game=fixture.manifest;arena.mount(game,int(room.self_seat));_hud()
	_accept_snapshot(fixture.snapshot)

func _clear()->void:
	for n in main_layer.get_children():main_layer.remove_child(n);n.queue_free()
	close_popup();status_text=null;clock_text=null;mode_text=null;phase_text=null;cue_text=null;dart_arrow=null;hud_vote=null;tempo=null;start_signal=null;camera_follow=null;clip_button=null;skip_button=null;hud_seats.clear();recall_at=-1
	alive_inputs=false

func cmd(data:Dictionary)->void:
	if data.op in ["local","host","connect"]:
		settings_cfg.set_value("identity","name",me_name);settings_cfg.save("user://settings.cfg")
	if data.op=="seek":
		latest_tick=-1;arena.ticks=-1;arena.reset_feedback(true);pending_events.clear()
	if data.op in ["start","settings"] and not data.has("room_rev"):data.room_rev=int(room.get("room_rev",0))
	bridge.send(data)

func map_name(id:String)->String:
	for m in catalog.get("maps",[]):
		if str(m.id)==id:return str(m.name)
	return id

func series_line(s)->String:
	if not s is Dictionary:return ""
	var maps:=int(s.get("map_count",1));var rounds:=int(s.get("map_rounds",1))
	if maps<=1 and rounds<=1:return ""
	return "%s %d/%d · 地图 %d/%d"%[map_name(str(s.get("map_id",""))),int(s.get("map_round",1)),rounds,int(s.get("map_index",1)),maps]

func entry_name(entry:Dictionary)->String:
	var id:=str(entry.get("bot",""))
	for bot in catalog.get("bots",[]):
		if str(bot.id)==id:return str(bot.name)
	return str(entry.get("name","选手"))

func toast(s:String)->void:
	last_message=s;notice.text=s;notice.show();toast_timer=4.0

func _bridge_error(s:String)->void:
	online=false;last_message=s
	if is_instance_valid(status_text):status_text.text="服务未连接"
	toast(s)

func _card(pos:Vector2,end:Vector2,align:String="left",padding:int=26)->VBoxContainer:
	var p:=S.panel(Color("#fffcf6f7"),3,padding);main_layer.add_child(p)
	if align=="left":p.set_anchors_and_offsets_preset(Control.PRESET_LEFT_WIDE)
	else:p.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	p.offset_left=pos.x;p.offset_top=pos.y;p.offset_right=end.x;p.offset_bottom=end.y;p.grow_vertical=Control.GROW_DIRECTION_END
	var scroll:=ScrollContainer.new();p.add_child(scroll);scroll.horizontal_scroll_mode=ScrollContainer.SCROLL_MODE_DISABLED
	var col:=S.stack(12);scroll.add_child(col);col.size_flags_horizontal=Control.SIZE_EXPAND_FILL
	return col

func _brand(parent:Control)->void:
	var row:=S.row(10);parent.add_child(row)
	var icon:=TextureRect.new();icon.texture=preload("res://art/mark.svg");icon.expand_mode=TextureRect.EXPAND_IGNORE_SIZE;icon.custom_minimum_size=Vector2(36,36);row.add_child(icon)
	row.add_child(S.text("FLYBACK    /    ARENA",15,S.DEEP,true))

func _header(col:Control,title:String,kicker:String="FLYBACK  /  PLAY")->void:
	col.add_child(S.text(kicker,13,S.DEEP,true));col.add_child(S.text(title,34,S.INK,true))

func _home()->void:
	page="home";_clear();room={};snapshot={};game={};result_data={};clip={};clip_live={};replay_mode=false;directory={};server_connected=false;outro_left=-1;clip_round=-1
	var file:=FileAccess.open("res://art/attract.json",FileAccess.READ)
	if file:arena.mount(JSON.parse_string(file.get_as_text()),-1,true)
	var col:=_card(Vector2(28,25),Vector2(425,-25),"left",29)
	_brand(col)
	col.add_child(S.text("浮岛竞赛  /  CLASSIC 20",13,S.SUB,true))
	col.add_child(S.text("飞返竞技场",52,S.INK,true))
	col.add_child(S.text("飞镖掠过浮岛，还会回到你手中。",18,S.SUB))
	var gap:=Control.new();gap.custom_minimum_size.y=8;col.add_child(gap)
	col.add_child(S.btn("快速挑战     →",func():Setup.show_setup(self,"local",false),true))
	col.add_child(S.btn("AI 表演赛",func():Setup.show_setup(self,"local",true)))
	col.add_child(S.btn("对战大厅",func():Lobby.show_hub(self)))
	var sep:=HSeparator.new();col.add_child(sep)
	col.add_child(S.btn("查看回放",func():cmd({"op":"replays"}) if online else _replay_picker()))
	col.add_child(S.btn("操作与设置",_help))
	var filler:=Control.new();filler.custom_minimum_size.y=8;col.add_child(filler)
	status_text=S.text("比赛服务已连接" if online else "正在连接比赛服务…",13,S.SUB);col.add_child(status_text)
	var label:=S.panel(Color("#214538ed"),3,11);main_layer.add_child(label);label.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
	label.offset_left=-250;label.offset_right=-30;label.offset_top=-77;label.offset_bottom=-29
	label.add_child(S.text("01  /  双湖     TWIN LAKES",15,Color("#fff4cf"),true))

func close_popup()->void:
	if is_instance_valid(popup):overlays.remove_child(popup);popup.queue_free()
	if is_instance_valid(scrim):overlays.remove_child(scrim);scrim.queue_free()
	popup=null
	scrim=null

func modal(title:String)->VBoxContainer:
	close_popup()
	scrim=ColorRect.new();scrim.color=Color(0.08,.13,.14,.38);overlays.add_child(scrim);S.fill(scrim);scrim.mouse_filter=Control.MOUSE_FILTER_STOP
	popup=S.panel(S.PAPER,4,21);overlays.add_child(popup);popup.set_anchors_and_offsets_preset(Control.PRESET_CENTER)
	popup.custom_minimum_size=Vector2(470,0);popup.grow_horizontal=Control.GROW_DIRECTION_BOTH;popup.grow_vertical=Control.GROW_DIRECTION_BOTH
	var col:=S.stack(11);popup.add_child(col);col.add_child(S.text(title,25,S.INK,true));return col

func _help()->void:
	var col:=modal("操作与设置")
	for s in ["WASD 移动 · 鼠标瞄准，左键出镖 / 右键近战","空格冲刺 · R 召回（引导期间移动会打断）","手柄：左摇杆走位 / 右摇杆瞄准","RT 投掷 · RB 近战 · LB 冲刺 · LT 召回","Esc 对局菜单 · P 暂停 / 公开表决 · F11 全屏"]:
		col.add_child(S.text(s,15,S.SUB))
	var buf:=CheckButton.new();buf.text="动作缓冲（最多 2 步）";buf.button_pressed=settings_cfg.get_value("controls","buffer",true);col.add_child(buf)
	buf.toggled.connect(func(on):settings_cfg.set_value("controls","buffer",on);settings_cfg.save("user://settings.cfg"))
	var dash:=CheckButton.new();dash.text="冲刺始终朝瞄准方向";dash.button_pressed=settings_cfg.get_value("controls","aim_dash",false);col.add_child(dash)
	dash.toggled.connect(func(on):settings_cfg.set_value("controls","aim_dash",on);settings_cfg.save("user://settings.cfg"))
	var recall:=CheckButton.new();recall.text="长按 R / LT 启动召回";recall.button_pressed=settings_cfg.get_value("controls","hold_recall",false);col.add_child(recall)
	recall.toggled.connect(func(on):settings_cfg.set_value("controls","hold_recall",on);settings_cfg.save("user://settings.cfg"))
	var row:=S.row();col.add_child(row);var deadzone:=SpinBox.new();deadzone.min_value=5;deadzone.max_value=35;deadzone.step=1;deadzone.suffix="%";deadzone.value=roundi(float(settings_cfg.get_value("controls","deadzone",.15))*100);row.add_child(S.text("左摇杆死区",15,S.SUB));row.add_child(deadzone)
	deadzone.value_changed.connect(func(v):settings_cfg.set_value("controls","deadzone",v/100.0);settings_cfg.save("user://settings.cfg"))
	var audio_row:=S.row(9);col.add_child(audio_row);audio_row.add_child(S.text("比赛音效",15,S.SUB))
	var volume:=HSlider.new();volume.name="SfxVolume";volume.max_value=100;volume.step=1;volume.value=roundi(arena.combat_audio.level*100);volume.custom_minimum_size.x=125;volume.size_flags_horizontal=Control.SIZE_EXPAND_FILL;audio_row.add_child(volume)
	var amount:=S.text("%d%%"%roundi(volume.value),14,S.SUB);amount.custom_minimum_size.x=42;audio_row.add_child(amount)
	var mute:=CheckButton.new();mute.name="SfxMute";mute.text="静音";mute.button_pressed=arena.combat_audio.muted;audio_row.add_child(mute)
	volume.value_changed.connect(func(v):
		amount.text="%d%%"%roundi(v);arena.combat_audio.set_level(v/100.,mute.button_pressed)
		settings_cfg.set_value("audio","level",v/100.);settings_cfg.save("user://settings.cfg"))
	mute.toggled.connect(func(on):
		arena.combat_audio.set_level(volume.value/100.,on)
		settings_cfg.set_value("audio","mute",on);settings_cfg.save("user://settings.cfg"))
	col.add_child(S.btn("返回",close_popup,true))

func _replay_picker()->void:
	var d:=FileDialog.new();overlays.add_child(d);d.file_mode=FileDialog.FILE_MODE_OPEN_FILE;d.access=FileDialog.ACCESS_FILESYSTEM
	d.title="打开对局回放";d.filters=PackedStringArray(["*.json,*.fbar,*.replay,*.jsonl,*.txt ; 对局回放"])
	d.file_selected.connect(func(path):replay_mode=true;cmd({"op":"replay","path":path});d.queue_free());d.canceled.connect(d.queue_free);d.popup_centered_ratio(0.76)

func _packet(v:Dictionary)->void:
	match str(v.get("type","")):
		"catalog":
			catalog=v;online=true;if is_instance_valid(status_text):status_text.text="比赛服务已连接   ·   %s 位 AI"%v.get("bots",[]).size()
			if not field_ready_enabled and "field_ready" in v.get("local_features",[]):
				field_ready_enabled=true;cmd({"op":"enable_field_ready"})
			if page=="hub":Lobby.show_hub(self)
		"directory":
			directory=v;if page in ["hub","directory"]:Lobby.show_directory(self)
		"connected":
			linked_address=str(v.get("address",linked_address));server_connected=true
		"trust":Lobby.trust(self,v)
		"room":
			var oldstate:=str(room.get("state",""))
			var old_round:=int(room.get("round_epoch",-1))
			room=v;replay_mode=v.get("mode")=="replay"
			if v.get("state")=="paused" and oldstate!="paused":arena.combat_audio.stop_voices()
			if old_round!=int(room.get("round_epoch",0)):
				latest_tick=-1;seq=0;outro_left=-1;clip_round=-1;clip={};clip_live={};pending_events.clear()
				game={};snapshot={};opening_frame={};field_ready_queued=false;_clear_actions()
			if str(v.state)!=oldstate:_clear_actions()
			if clip.is_empty():arena.sim_rate=_field_rate()
			if v.state=="ready":
				if page!="setup" or scratch_setup.get("kind","")!="edit":Lobby.ready_room(self)
			elif v.state in ["loading","countdown"] and game.is_empty():
				if page!="ready":Lobby.ready_room(self)
			elif page=="match" or (not game.is_empty() and v.state in ["playing","paused"]):
				if page!="match":_hud()
				alive_inputs=int(room.get("self_seat",-1))>=0 and v.state=="playing" and clip.is_empty() and outro_left<0
				_refresh_hud()
		"manifest":
			if not replay_mode and int(v.get("round_epoch",room.get("round_epoch",0)))!=int(room.get("round_epoch",0)):return
			game=v;snapshot={};latest_tick=-1;scene_start_time=Time.get_ticks_msec();result_data={};completed_path="";clip={};clip_live={};outro_left=-1;clip_round=-1;_clear_actions()
			arena.mount(game,int(room.get("self_seat",-1)));arena.sim_rate=_field_rate();_hud()
			if not opening_frame.is_empty():
				_accept_snapshot(opening_frame);opening_frame={}
		"prediction":
			if page=="match" and clip.is_empty() and room.get("state")=="playing":arena.predict(v)
		"snapshot":
			if clip.is_empty():_accept_snapshot(v)
			else:clip_live=v
		"event","events":
			var epoch:=int(v.get("round_epoch",-1))
			if epoch!=int(room.get("round_epoch",0)):return
			var events:Array=[v.event] if v.type=="event" else v.get("events",[])
			for e in events:pending_events["%d:%d:%d"%[epoch,int(e.tick),int(e.ordinal)]]=e
			_flush_events()
		"result":
			result_data=v;completed_path=str(v.get("path",v.get("replay_path",completed_path)))
			if clip.is_empty():_begin_outro()
		"replays":
			replay_list=v.get("items",[]);var col:=modal("本机回放")
			for entry in replay_list.slice(0,8):
				var p:=str(entry.get("path",""));col.add_child(S.btn(str(entry.get("name",p.get_file())),func():replay_mode=true;cmd({"op":"replay","path":p});close_popup()))
			if replay_list.is_empty():col.add_child(S.text("还没有已保存的比赛。",15,S.SUB))
			col.add_child(S.btn("从文件打开…",func():close_popup();_replay_picker()));col.add_child(S.btn("返回",close_popup))
		"replay_clip":
			if clip_round<0 or clip_round!=int(room.get("round_epoch",0)) or int(v.get("round_epoch",clip_round))!=clip_round:return
			clip=v;clip_time=0;clip_index=-1;clip_live=snapshot;close_popup()
			arena.mount(v.manifest,-1);arena.close_subject=int(v.get("victim_seat",room.get("self_seat",-1)));_hud();alive_inputs=false
		"left": _home()
		"error":toast(str(v.get("message",v.get("code","请求失败"))))
		"replay_saved":
			completed_path=str(v.get("path",""))
			if page=="results":_results()
		"discovered","discovery":Lobby.discoveries(self,v)

func _accept_snapshot(v:Dictionary)->void:
	if not replay_mode and (v.get("room_id",room.get("id"))!=room.get("id") or int(v.get("round_epoch",room.get("round_epoch",0)))!=int(room.get("round_epoch",0))):return
	if game.is_empty():
		# An unusually large manifest can cross on the bulk stream after tick zero.
		if room.get("state") in ["loading","countdown"] and int(v.get("tick",-1))==0:opening_frame=v
		return
	var t:=int(v.get("tick",0));if latest_tick>=0 and t<latest_tick:return
	latest_tick=t;snapshot=v;arena.update(v);_refresh_hud()
	if t==0 and field_ready_enabled and not field_ready_queued and room.get("field_ready_required",false):
		field_ready_queued=true;_report_field_shown(str(room.id),int(room.round_epoch))
	_flush_events()
	var i:=int(room.get("self_seat",-1))
	if recall_hold and t>recall_sent_tick+2 and i>=0 and i<v.get("players",[]).size() and int(v.players[i].get("st",0))!=9:recall_hold=false
	# Prime the next seat-local endpoint on the arrival of the authoritative
	# sample. The input heartbeat can be almost one rule step out of phase.
	if alive_inputs and i>=0 and i<v.get("players",[]).size() and v.players[i].get("alive",false) and not previous_action.is_empty() and "predict_self" in catalog.get("local_features",[]):
		if (int(previous_action.mx)!=0 or int(previous_action.my)!=0) and pending_predict_tick!=t:
			var held:=previous_action.duplicate();held.act=0;pending_predict_tick=t;previous_action=held
			cmd({"op":"predict","inputs":[held]})

func _report_field_shown(room_id:String,epoch:int)->void:
	# The bridge can deliver the manifest and frame in the same read; neither proves
	# that the player has seen the map. Headless QA has no rendering signal.
	if DisplayServer.get_name()=="headless":await get_tree().process_frame
	else:await RenderingServer.frame_post_draw
	if page=="match" and room.get("id")==room_id and int(room.get("round_epoch",-1))==epoch and room.get("state")=="loading":
		cmd({"op":"field_ready","room_id":room_id,"round_epoch":epoch})

func _field_rate()->float:
	return 0. if room.get("state") in ["paused","loading","countdown"] else float(room.get("speed",1.))

func _clear_actions()->void:
	queued_act=0;recall_at=-1;recall_hold=false;recall_sent_tick=-1;previous_action={};pending_predict_tick=-1;key_age=0.

func _flush_events()->void:
	if not clip.is_empty() or game.is_empty():return
	for key in pending_events.keys():
		var e:Dictionary=pending_events[key]
		if latest_tick<=int(e.tick):continue
		arena.play_events([e],int(room.get("round_epoch",0)))
		if arena.event_ids.has(key):pending_events.erase(key)

func _hud()->void:
	page="match";_clear();alive_inputs=int(room.get("self_seat",-1))>=0 and str(room.get("state","playing"))=="playing"
	var top:=S.panel(Color("#fffcf6f4"),4,9);main_layer.add_child(top);top.set_anchors_and_offsets_preset(Control.PRESET_TOP_WIDE);top.offset_left=19;top.offset_right=-19;top.offset_top=13;top.offset_bottom=78
	var row:=S.row(8);top.add_child(row)
	var seats:Array=game.get("seats",[])
	for i in range(seats.size()+1):
		if i==ceili(seats.size()/2.0):
			var mid:=S.panel(S.WARM,2,4);mid.custom_minimum_size.x=116;row.add_child(mid)
			var v:=S.stack(0);mid.add_child(v);clock_text=S.text("0:40",23,S.INK,true);clock_text.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;v.add_child(clock_text)
			mode_text=S.text("",11,S.SUB);mode_text.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;v.add_child(mode_text)
		if i<seats.size():
			var entry:Dictionary=seats[i];var id:=str(entry.get("bot",""))
			var key:=str(entry.get("model","courier"));var pretty:=str(entry.get("name","选手"))
			for bot in catalog.get("bots",[]):
				if str(bot.id)==id:pretty=str(bot.name);key=str(bot.get("model",key));break
			key={"carbon":"gemini","hydrogen":"gemini_s"}.get(key,key)
			if id.is_empty():key="courier"
			var pp:="res://art/portraits/"+key+(".svg" if key in ["courier","doubao"] else ".png")
			if id.is_empty():
				var skin:=str(entry.get("skin_id",arena.Actor.SKINS[i%4]))
				if not skin in arena.Actor.SKINS:skin=arena.Actor.SKINS[i%4]
				var pf:="res://art/humans/"+skin+".png"
				if ResourceLoader.exists(pf):pp=pf
			var member:=S.row(9);member.size_flags_horizontal=Control.SIZE_EXPAND_FILL;member.alignment=BoxContainer.ALIGNMENT_CENTER;row.add_child(member)
			if ResourceLoader.exists(pp):
				var portrait:=TextureRect.new();portrait.texture=load(pp);portrait.stretch_mode=TextureRect.STRETCH_KEEP_ASPECT_CENTERED;portrait.expand_mode=TextureRect.EXPAND_IGNORE_SIZE;portrait.custom_minimum_size=Vector2(43,43);member.add_child(portrait)
			var vv:=S.stack(0);member.add_child(vv)
			var name_:Label=S.text("%s  %s"%[arena.Actor.SIGNS[i%4],pretty],18,arena.Actor.SEATS[i%4].darkened(0.32),true);vv.add_child(name_)
			var st:=S.text("准备就绪",12,S.SUB);vv.add_child(st);hud_seats.append([name_,st])
	phase_text=S.text("",17,S.ALERT,true);main_layer.add_child(phase_text);phase_text.set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP);phase_text.offset_left=-220;phase_text.offset_right=220;phase_text.offset_top=89;phase_text.offset_bottom=129;phase_text.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;phase_text.add_theme_stylebox_override("normal",S.box(Color("#fff4e8ed"),12,7))
	start_signal=StartSignal.new();main_layer.add_child(start_signal)
	dart_arrow=S.text("➤",24,S.DEEP,true);dart_arrow.hide();main_layer.add_child(dart_arrow)
	var bot:=S.panel(Color("#fffcf6ed"),4,8);main_layer.add_child(bot);bot.set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_WIDE);bot.offset_left=22;bot.offset_right=-22;bot.offset_top=-65;bot.offset_bottom=-13
	var controls:=S.row(9);bot.add_child(controls);cue_text=S.text("",14,S.SUB);cue_text.size_flags_horizontal=Control.SIZE_EXPAND_FILL;controls.add_child(cue_text)
	if str(room.get("mode","offline")) in ["offline","replay"] and clip.is_empty():
		tempo=S.pick(TEMPOS);tempo.name="MatchSpeed";tempo.custom_minimum_size.x=95;tempo.size_flags_horizontal=Control.SIZE_SHRINK_BEGIN;controls.add_child(tempo)
		tempo.item_selected.connect(func(_i):cmd({"op":"speed","value":S.selected(tempo)}))
	if int(room.get("self_seat",-1))<0:
		var opts:Array=[["全景",-1]];for i in seats.size():opts.append(["跟随 "+entry_name(seats[i]),i])
		camera_follow=S.pick(opts);camera_follow.custom_minimum_size.x=135;camera_follow.size_flags_horizontal=Control.SIZE_SHRINK_BEGIN;controls.add_child(camera_follow)
		camera_follow.item_selected.connect(func(_i):arena.watch_seat=int(S.selected(camera_follow)))
	hud_vote=S.btn("暂停",_pause);controls.add_child(hud_vote)
	if not clip.is_empty():controls.add_child(S.btn("返回实况",_end_clip))
	elif not replay_mode:
		clip_button=S.btn("死亡回看",request_clip);controls.add_child(clip_button)
	else:
		controls.add_child(S.btn("退 5 秒",func():cmd({"op":"seek","tick":maxi(0,latest_tick-100)})))
	controls.add_child(S.btn("菜单  Esc",_menu));skip_button=S.btn("跳过演出  →",_finish_outro);skip_button.hide();controls.add_child(skip_button);_refresh_hud()

func request_clip()->void:
	clip_round=int(room.get("round_epoch",0))
	var i:=int(room.get("self_seat",-1));var players:Array=snapshot.get("players",[])
	var self_dead:=i>=0 and i<players.size() and not bool(players[i].alive)
	cmd({"op":"replay_clip","scope":"self" if self_dead else "final","round_epoch":clip_round})

func _begin_outro()->void:
	if page!="match" or not is_instance_valid(clock_text):_results();return
	if outro_left>=0:return
	outro_left=2.8;outro_round=int(room.get("round_epoch",0));alive_inputs=false
	arena.celebration(result_data);_refresh_hud()

func _finish_outro()->void:
	if outro_left<0:return
	outro_left=-1.
	if not result_data.is_empty() and outro_round==int(room.get("round_epoch",0)):_results()

func _refresh_hud()->void:
	if page!="match" or not is_instance_valid(clock_text):return
	var ticks_:=maxi(0,latest_tick)
	var left:=ceili((int(game.get("step_limit",800))-ticks_)/20.0);left=maxi(left,0)
	clock_text.text="%d:%02d"%[left/60,left%60]
	var offline:=str(room.get("mode","offline")) in ["offline","replay"];var spect:=int(room.get("self_seat",-1))<0
	var has_human:=false;for p in game.get("seats",[]):if str(p.get("bot","")).is_empty():has_human=true
	mode_text.text=("观战 · 延迟 3s" if spect and has_human and not offline else ("全景观战" if spect else ("双人决斗" if hud_seats.size()==2 else "%s 人混战"%hud_seats.size())))
	var speed:=float(room.get("speed",1.))
	if is_instance_valid(tempo):
		for i in TEMPOS.size():if is_equal_approx(TEMPOS[i][1],speed):tempo.select(i);break
	elif clip.is_empty() and not is_equal_approx(speed,1.):mode_text.text+=" · %s×"%speed
	var opening:= -1
	if not replay_mode and clip.is_empty() and outro_left<0 and not snapshot.is_empty():
		if room.get("state")=="countdown":opening=int(room.get("countdown_seconds",-1))
		elif room.get("state")=="playing":opening=0
	start_signal.show_phase(opening,map_name(str(game.get("map",""))))
	var list:Array=snapshot.get("players",[])
	for p in list:
		var i:=int(p.seat);if i<0 or i>=hud_seats.size():continue
		hud_seats[i][1].text=("已出局   ·   " if not p.alive else ("自己   ·   " if i==room.get("self_seat",-1) else ""))+"击杀 %d"%int(p.get("kills",0))
		if not p.alive:hud_seats[i][0].modulate=Color("#9b948a")
	var vote=room.get("vote")
	var vtext:=""
	if vote is Dictionary:vtext="公开表决   %s / %s 票"%[vote.get("yes",0),vote.get("needed",0)]
	if vote is Dictionary:
		vtext+="   ·   %d 秒"%ceili(float(vote.get("seconds_left",0)))
	phase_text.text=vtext if not vtext.is_empty() else ("比赛已暂停" if room.get("state")=="paused" else ("地块即将崩落" if int(snapshot.get("warn",-1))>=0 else ""))
	phase_text.visible=not phase_text.text.is_empty()
	hud_vote.visible=offline or not spect
	hud_vote.disabled=room.get("state") in ["loading","countdown"]
	if clip_button:
		var me:=int(room.get("self_seat",-1))
		clip_button.visible=me>=0 and me<list.size() and not bool(list[me].alive) and outro_left<0
	if not clip.is_empty():hud_vote.visible=false;mode_text.text="致命一击 · 回看"
	hud_vote.text=("继续" if room.get("state")=="paused" else "暂停") if offline else ("投票" if vote is Dictionary else ("提议恢复" if room.get("state")=="paused" else "提议暂停"))
	cue_text.text=("观战席  /  "+map_name(str(game.get("map","")))) if spect else "左键 出镖   ·   右键 近战   ·   空格 冲刺   ·   R 召回"
	var pov:=int(snapshot.get("pov",-1))
	if spect and pov>=0 and pov<game.get("seats",[]).size() and clip.is_empty():
		var subject:Dictionary=game.seats[pov]
		cue_text.text=entry_name(subject)+" 席位视角"
		if not str(subject.get("bot","")).is_empty() and int(game.get("ai_observation_delay_ticks",0))>0:
			cue_text.text+="  ·  对手观测延后 %d 步"%int(game.ai_observation_delay_ticks)
		if offline or not has_human:mode_text.text="席位观战"
	var course:=series_line(room.get("series",{}))
	if not course.is_empty():cue_text.text=course+"    /    "+cue_text.text
	if outro_left>=0:
		skip_button.show();hud_vote.hide();phase_text.hide();cue_text.text="终场  /  "+map_name(str(game.get("map","")))
		for control in skip_button.get_parent().get_children():control.visible=(control==skip_button or control==cue_text)
		var winner:=int(result_data.get("winner",-1));mode_text.text=entry_name(game.seats[winner])+" 胜" if winner>=0 and winner<game.seats.size() else "并列终场"

func _pause()->void:
	if room.get("state") in ["loading","countdown"]:return
	if not str(room.get("mode","offline")) in ["offline","replay"] and int(room.get("self_seat",-1))<0:return
	if room.get("vote") is Dictionary:
		var col:=modal("公开表决");col.add_child(S.btn("赞成",func():cmd({"op":"vote","yes":true});close_popup(),true));col.add_child(S.btn("反对",func():cmd({"op":"vote","yes":false});close_popup()))
	else:cmd({"op":"pause"})

func _menu()->void:
	if is_instance_valid(popup):close_popup();return
	var col:=modal("对局菜单")
	if room.get("mode")=="offline" and room.get("state")=="playing":cmd({"op":"pause"})
	col.add_child(S.btn("回到比赛",func():close_popup();if room.get("mode")=="offline" and room.get("state")=="paused":cmd({"op":"pause"}),true))
	col.add_child(S.btn("操作与设置",_help))
	if int(room.get("self_seat",-1))<0 and not replay_mode:
		var viewpoints:Array=[["全图",-1]]
		for i in game.get("seats",[]).size():viewpoints.append(["席位观察："+entry_name(game.seats[i]),i])
		var views:=S.pick(viewpoints);col.add_child(views);views.item_selected.connect(func(_id):cmd({"op":"watch","view":S.selected(views)});close_popup())
	col.add_child(S.btn("退出房间",func():cmd({"op":"leave"});close_popup()))

func _results()->void:
	page="results";_clear()
	var col:=_card(Vector2(30,36),Vector2(427,-36),"left",26);_header(col,"对局结束","RESULTS   /   赛后邮报")
	var series:Dictionary=result_data.get("series",{})
	var multi:=int(series.get("total",1))>1
	var standings:Array=result_data.get("standings",[]).duplicate()
	standings.sort_custom(func(a,b):return int(a.get("rank",99))<int(b.get("rank",99)))
	if multi:col.add_child(S.text(series_line(series),15,S.DEEP,true))
	for p in standings:
		var i:=int(p.get("seat",0));var label_:=entry_name(game.seats[i]) if i<game.get("seats",[]).size() else "席位 "+str(i+1)
		col.add_child(S.text("%d   %s   ·   %d 击杀"%[int(p.get("rank",1)),label_,int(p.get("kills",0))],20,S.INK,true))
	if multi:
		col.add_child(S.text("总积分" if series.get("finished",false) else "当前积分",18,S.DEEP,true))
		for person in series.get("standings",[]):
			col.add_child(S.text("%d   %s     %.1f 分 · %d 胜"%[int(person.get("rank",1)),str(person.get("name","")),float(person.get("points",0)),int(person.get("wins",0))],16,S.INK))
		if not series.get("finished",false):col.add_child(S.text("本回合已结算，等待裁判推进下一回合。",14,S.SUB))
	if not completed_path.is_empty():col.add_child(S.btn("播放本局回放",func():cmd({"op":"replay","path":completed_path})))
	if not replay_mode:
		if int(room.get("self_seat",-1))>=0:col.add_child(S.btn("回看致命一击",request_clip))
		if room.get("self_id")==room.get("host_id") and (not multi or series.get("finished",false)):col.add_child(S.btn("返回准备厅",func():cmd({"op":"lobby"}),true))
	col.add_child(S.btn("返回主菜单",func():cmd({"op":"leave"})))

func _end_clip()->void:
	clip={};arena.mount(game,int(room.get("self_seat",-1)));arena.sim_rate=_field_rate();latest_tick=-1
	if not clip_live.is_empty():
		arena.skip_sounds_once=true;_accept_snapshot(clip_live)
	if not result_data.is_empty():_results()
	else:
		_hud()
	clip_live={}

func _movement()->Vector2:
	var m:=Vector2(int(Input.is_physical_key_pressed(KEY_D))-int(Input.is_physical_key_pressed(KEY_A)),int(Input.is_physical_key_pressed(KEY_S))-int(Input.is_physical_key_pressed(KEY_W)))
	if m.length_squared()>0:return m.normalized()
	var js:=Input.get_connected_joypads()
	if js.is_empty():return Vector2.ZERO
	var vec:=Vector2(Input.get_joy_axis(js[0],JOY_AXIS_LEFT_X),Input.get_joy_axis(js[0],JOY_AXIS_LEFT_Y))
	return vec.limit_length(1) if vec.length()>float(settings_cfg.get_value("controls","deadzone",.15)) else Vector2.ZERO

func _recall_button(down:bool)->void:
	if not down:recall_at=-1;return
	if not alive_inputs:return
	if settings_cfg.get_value("controls","hold_recall",false):recall_at=Time.get_ticks_msec()+180;return
	_begin_recall()

func _begin_recall()->void:
	var i:=int(room.get("self_seat",-1))
	recall_hold=i>=0 and i<snapshot.get("darts",[]).size() and int(snapshot.darts[i].get("st",0))==3
	recall_sent_tick=latest_tick;_send_input(4)

func _send_input(act:int=0)->void:
	if not alive_inputs or not get_window().has_focus() or is_instance_valid(popup):return
	var move:=_movement()
	if recall_hold:move=Vector2.ZERO
	if not pad_aim:aim=arena.aim_direction()
	arena.saved_input=aim if pad_aim else Vector2.ZERO
	var dir:=move if act==3 and not settings_cfg.get_value("controls","aim_dash",false) and move.length_squared()>0 else aim
	var i:=int(room.get("self_seat",-1))
	var players:Array=snapshot.get("players",[])
	if i<0 or i>=players.size() or not players[i].get("alive",false):return
	var p:Dictionary=players[i];var st:=int(p.get("st",0))
	var timer:=int(p.get("timer",0));var windup:=st==7 and act==2 and timer>6
	if act!=0 and settings_cfg.get_value("controls","buffer",true) and (windup or (st!=0 and timer<=2 and st!=7)):
		queued_act=act;queued_until=latest_tick+2;act=0
	elif act==0 and queued_act!=0:
		if latest_tick>queued_until:queued_act=0
		elif st==0 or (st==7 and queued_act==2 and timer>2 and timer<=6):act=queued_act;queued_act=0
	if act not in [0,4]:recall_hold=false;recall_at=-1
	seq+=1
	var action:={"mx":roundi(move.x*1000),"my":roundi(move.y*1000),"ax":roundi(dir.x*1000),"ay":roundi(dir.y*1000),"act":act,"msg":0}
	var command:=action.duplicate();command.merge({"op":"input","seq":seq,"round_epoch":room.get("round_epoch",0),"timeline_epoch":room.get("timeline_epoch",0)});cmd(command)
	var release_edge:=int(previous_action.get("mx",0))!=0 or int(previous_action.get("my",0))!=0
	if "predict_self" in catalog.get("local_features",[]) and (act!=0 or move.length_squared()>0 or release_edge) and (pending_predict_tick!=latest_tick or previous_action!=action):
		pending_predict_tick=latest_tick;previous_action=action;cmd({"op":"predict","inputs":[action]})

func _unhandled_input(event:InputEvent)->void:
	if event is InputEventKey and event.pressed and not event.echo and event.keycode==KEY_F11:
		get_window().mode=Window.MODE_WINDOWED if get_window().mode==Window.MODE_FULLSCREEN else Window.MODE_FULLSCREEN
		return
	if page!="match":return
	if event is InputEventKey and not event.pressed and event.physical_keycode in [KEY_W,KEY_A,KEY_S,KEY_D]:_send_input();return
	if outro_left>=0 and event is InputEventKey and event.pressed and not event.echo and event.keycode in [KEY_ESCAPE,KEY_ENTER,KEY_SPACE]:_finish_outro();return
	if event is InputEventKey and not event.pressed and event.keycode==KEY_R:_recall_button(false)
	if event is InputEventKey and event.pressed and not event.echo:
		if event.keycode==KEY_ESCAPE:_menu();return
		if is_instance_valid(popup):return
		if event.keycode==KEY_P:_pause();return
		if event.physical_keycode in [KEY_W,KEY_A,KEY_S,KEY_D]:recall_hold=false;_send_input();return
		if event.keycode==KEY_SPACE:recall_hold=false;_send_input(3)
		elif event.keycode==KEY_R:_recall_button(true)
	elif event is InputEventMouseMotion:pad_aim=false
	elif event is InputEventMouseButton and event.pressed and not is_instance_valid(popup):
		pad_aim=false;recall_hold=false
		if event.button_index==MOUSE_BUTTON_LEFT:_send_input(1)
		elif event.button_index==MOUSE_BUTTON_RIGHT:_send_input(2)
	elif event is InputEventJoypadMotion:
		var dev:=event.device
		var rv:=Vector2(Input.get_joy_axis(dev,JOY_AXIS_RIGHT_X),Input.get_joy_axis(dev,JOY_AXIS_RIGHT_Y))
		if rv.length()>0.20:pad_aim=true;aim=rv.normalized()
		if event.axis in [JOY_AXIS_LEFT_X,JOY_AXIS_LEFT_Y] and absf(event.axis_value)>0.35:recall_hold=false
		if event.axis in [JOY_AXIS_TRIGGER_LEFT,JOY_AXIS_TRIGGER_RIGHT]:
			var meta:="trigger_"+str(event.axis);var down:bool=event.axis_value>0.62
			if down and not has_meta(meta):
				set_meta(meta,true)
				if event.axis==JOY_AXIS_TRIGGER_LEFT:_recall_button(true)
				else:recall_hold=false;_send_input(1)
			elif event.axis_value<0.30 and has_meta(meta):
				remove_meta(meta);if event.axis==JOY_AXIS_TRIGGER_LEFT:_recall_button(false)
	elif event is InputEventJoypadButton and event.pressed:
		match event.button_index:
			JOY_BUTTON_LEFT_SHOULDER:_send_input(3)
			JOY_BUTTON_RIGHT_SHOULDER:_send_input(2)
			JOY_BUTTON_START:_menu()

func _notification(what:int)->void:
	if what==NOTIFICATION_APPLICATION_FOCUS_OUT and alive_inputs and bridge:
		queued_act=0;recall_hold=false;recall_at=-1;seq+=1
		cmd({"op":"input","seq":seq,"mx":0,"my":0,"ax":0,"ay":0,"act":0,"msg":0,"round_epoch":room.get("round_epoch",0),"timeline_epoch":room.get("timeline_epoch",0)})

func _process(dt:float)->void:
	if outro_left>=0 and clip.is_empty():
		outro_left=maxf(0.,outro_left-dt)
		if outro_left==0:_finish_outro()
	if not clip.is_empty():
		clip_time+=dt;var idx:=int(clip_time*20.0)
		if idx>=clip.frames.size():_end_clip()
		elif idx!=clip_index:
			clip_index=idx;var frame:Dictionary=clip.frames[idx];arena.update(frame)
			if is_instance_valid(clock_text):clock_text.text="%.1fs"%((clip.frames.size()-idx)/20.0)
	if toast_timer>0:
		toast_timer-=dt
		if toast_timer<=0:notice.hide()
	if page=="match" and alive_inputs:
		if recall_at>=0 and Time.get_ticks_msec()>=recall_at:recall_at=-1;_begin_recall()
		if is_instance_valid(dart_arrow):
			dart_arrow.hide()
			var i:=int(room.get("self_seat",-1))
			if i>=0 and i<arena.darts.size() and arena.dart_markers[i].visible:
				var screen:Vector2=arena.camera.unproject_position(arena.dart_targets[i]);var bounds:=get_viewport().get_visible_rect().size
				var p:=screen.clamp(Vector2(18,94),bounds-Vector2(45,87))
				if screen.distance_to(p)>2:dart_arrow.position=p;dart_arrow.rotation=(screen-p).angle();dart_arrow.show()
		key_age+=dt
		if key_age>=0.05:key_age=fmod(key_age,0.05);_send_input()
	if not dev_capture.is_empty() and Engine.get_process_frames()>36:
		var path:=dev_capture;dev_capture=""
		await RenderingServer.frame_post_draw
		get_viewport().get_texture().get_image().save_png(path)
		get_tree().quit()
