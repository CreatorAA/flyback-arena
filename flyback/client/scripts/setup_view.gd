extends RefCounted
const S=preload("res://scripts/style.gd")
const MapPreview=preload("res://scripts/map_preview.gd")
const DEFAULT_MAPS=[["双湖","twinlakes"],["阴阳","yinyang"],["裂冰","icefloes"],["古塔","ruins"],["石阵","stonecircle"],["天桥","skybridge"]]

static func show_setup(app,kind:String,spectator:bool)->void:
	app.page="setup";app._clear()
	var base:Dictionary=app.room.get("settings",{}).duplicate(true) if kind=="edit" else {}
	if base.is_empty():
		base={"title":"浮岛练习赛","seat_count":2,"map_policy":"fixed","maps":["twinlakes"],"slots":["random","random"] if spectator else ["human","random"],"allow_mirrors":false,"rounds_per_map":1,"map_rounds":{}}
	if not base.has("rounds_per_map"):base.rounds_per_map=1
	if not base.has("map_rounds"):base.map_rounds={}
	app.scratch_setup={"kind":kind,"data":base,"spectator":spectator,"port":0,"password":""}
	_build(app)

static func _choices(app,n:int)->Array:
	var out:Array=[]
	for m in app.catalog.get("maps",[]):
		for supported in m.get("seats",[]):
			if int(supported)==n:
				out.append([m.name,m.id]);break
	if out.is_empty() and n!=3:out.assign(DEFAULT_MAPS)
	return out

static func _build(app)->void:
	app._clear()
	var draft:Dictionary=app.scratch_setup
	var base:Dictionary=draft.data;var kind:String=draft.kind
	var col:VBoxContainer=app._card(Vector2(24,20),Vector2(512,-20),"left",22)
	var title:String={"local":"AI 表演赛" if draft.spectator else "快速挑战","host":"创建局域网房间","create":"在服务器上创建房间","edit":"房间设置"}.get(kind,"开局设置")
	app._header(col,title,"MATCH DESK   /   比赛设置")
	if kind!="edit":
		var name_line:=S.line("你的昵称",app.me_name);col.add_child(_field("昵称",name_line))
		name_line.text_changed.connect(func(value):app.me_name=value.strip_edges().left(32))
	if kind!="local":
		var room_title:=S.line("输入房间名",base.get("title",""));col.add_child(_field("房间名称",room_title))
		room_title.text_changed.connect(func(v):base.title=v)
		if kind!="edit":
			var pass_line:=S.line("可选",draft.password);pass_line.secret=true;col.add_child(_field("房间密码",pass_line))
			pass_line.text_changed.connect(func(v):draft.password=v)
		if kind=="host":
			var port_line:=S.line("0 = 自动分配",str(draft.port));col.add_child(_field("UDP 端口",port_line));port_line.text_changed.connect(func(v):draft.port=v.to_int())
	var n:=int(base.seat_count)
	var format:=S.pick([["1v1 · 双人",2],["1v1v1 · 三人",3],["1v1v1v1 · 四人",4]],n-2)
	format.name="SeatFormat";col.add_child(_field("赛制",format))
	format.item_selected.connect(func(_i):
		var k:int=S.selected(format);base.seat_count=k
		var arr:Array=base.slots
		while arr.size()<k:arr.append("random")
		arr.resize(k)
		var choices:=_choices(app,k)
		base.maps=[choices[0][1]] if not choices.is_empty() else []
		_build(app))
	var policies=[["指定","fixed"],["轮换","rotate"],["随机","random"]]
	var pp:=S.pick(policies,["fixed","rotate","random"].find(base.map_policy));pp.name="MapPolicy";col.add_child(_field("地图策略",pp))
	pp.item_selected.connect(func(_i):
		base.map_policy=S.selected(pp)
		if base.map_policy!="fixed":
			base.maps=[]
			for m in _choices(app,n):base.maps.append(m[1])
		elif base.maps.size()>1:base.maps.resize(1)
		_build(app))
	var maps:=_choices(app,n)
	if base.map_policy=="fixed":
		var pos:=0
		for i in maps.size():
			if maps[i][1] in base.maps:pos=i
		var m:=S.pick(maps,pos);m.name="SelectedMap";col.add_child(_field("比赛地图",m))
		var preview:=MapPreview.new();preview.attach(app.main_layer,col)
		if not maps.is_empty():preview.show_map(str(maps[pos][1]),str(maps[pos][0]),n)
		m.item_selected.connect(func(_i):
			base.maps=[S.selected(m)]
			preview.show_map(str(base.maps[0]),m.get_item_text(m.selected),n)
			if draft.get("custom_rounds",false):_build(app))
		if not maps.is_empty() and base.maps.is_empty():base.maps=[maps[pos][1]]
	else:
		col.add_child(S.text("地图池  ·  按勾选顺序轮换" if base.map_policy=="rotate" else "地图池  ·  无放回抽选",14,S.SUB))
		var flow:=HFlowContainer.new();col.add_child(flow)
		for m in maps:
			var check:=CheckBox.new();check.text=m[0];check.add_theme_color_override("font_color",S.INK);check.button_pressed=m[1] in base.maps;flow.add_child(check)
			check.toggled.connect(func(on):
				if on and not m[1] in base.maps:base.maps.append(m[1])
				elif not on:base.maps.erase(m[1])
				if draft.get("custom_rounds",false):_build(app))
	var rounds:=SpinBox.new();rounds.min_value=1;rounds.max_value=99;rounds.step=1;rounds.value=int(base.rounds_per_map);rounds.size_flags_horizontal=Control.SIZE_EXPAND_FILL
	col.add_child(_field("每图回合",rounds));rounds.value_changed.connect(func(v):base.rounds_per_map=int(v))
	var advanced:=CheckBox.new();advanced.text="单独设置地图回合数";advanced.button_pressed=draft.get("custom_rounds",not base.map_rounds.is_empty());advanced.add_theme_color_override("font_color",S.SUB);col.add_child(advanced)
	advanced.toggled.connect(func(v):
		draft.custom_rounds=v
		if not v:base.map_rounds={}
		_build(app))
	if advanced.button_pressed:
		for info in maps:
			if not info[1] in base.maps:continue
			var per:=SpinBox.new();per.min_value=0;per.max_value=99;per.step=1;per.value=int(base.map_rounds.get(info[1],0));per.size_flags_horizontal=Control.SIZE_EXPAND_FILL
			col.add_child(_field(str(info[0]),per))
			var map_id:String=info[1];per.value_changed.connect(func(v):
				if int(v)==0:base.map_rounds.erase(map_id)
				else:base.map_rounds[map_id]=int(v))
		col.add_child(S.text("0 跟随每图回合数。",13,S.SUB))
	var opts:Array=[["真人席","human"],["随机 AI","random"]]
	for b in app.catalog.get("bots",[]):
		if b.get("available",true):opts.append([b.name,b.id])
	col.add_child(S.text("参赛名单",18,S.INK,true))
	for i in n:
		var selected:=0
		for j in opts.size():
			if opts[j][1]==base.slots[i]:selected=j
		var seat:=S.pick(opts,selected)
		var ix:=i
		seat.item_selected.connect(func(_j):base.slots[ix]=S.selected(seat))
		col.add_child(_field("席位 0%d"%(i+1),seat))
	var mirror:=CheckBox.new();mirror.text="允许同一 AI 镜像对决";mirror.button_pressed=bool(base.get("allow_mirrors",false));mirror.add_theme_color_override("font_color",S.INK);col.add_child(mirror)
	mirror.toggled.connect(func(on):base.allow_mirrors=on)
	var buttons:=S.row();col.add_child(buttons);var flow_btn:=S.btn("保存设置" if kind=="edit" else "进入准备厅   →",func():
		if base.maps.is_empty():app.toast("请至少选择一张兼容地图。");return
		for mid in base.map_rounds.keys():
			if not mid in base.maps:base.map_rounds.erase(mid)
		if kind=="local" and base.slots.count("human")>1:app.toast("本机只有一位操作者。多人对战请创建局域网房间。");return
		if kind=="edit":app.cmd({"op":"settings","settings":base,"room_rev":app.room.room_rev});app.scratch_setup={};app.page="ready"
		else:app.cmd({"op":kind,"name":app.me_name,"settings":base,"password":draft.password,"port":draft.port}),true)
	flow_btn.size_flags_horizontal=Control.SIZE_EXPAND_FILL;buttons.add_child(flow_btn)
	buttons.add_child(S.btn("返回",func():
		if kind=="edit":app.scratch_setup={};app.Lobby.ready_room(app)
		elif kind=="host":app.Lobby.show_hub(app)
		elif kind=="create":app.Lobby.show_directory(app)
		else:app._home()))

static func _field(caption:String,control:Control)->HBoxContainer:
	var r:=S.row(8);var l:=S.text(caption,15,S.SUB);l.custom_minimum_size.x=91;r.add_child(l);r.add_child(control);return r
