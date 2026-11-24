extends RefCounted
const S=preload("res://scripts/style.gd")

static func show_hub(app)->void:
	app.page="hub";app._clear()
	var col:VBoxContainer=app._card(Vector2(25,23),Vector2(575,-23),"left",24);app._header(col,"对战大厅","NETWORK   /   联机")
	col.add_child(S.btn("创建局域网房间",func():app.Setup.show_setup(app,"host",false),true))
	col.add_child(S.btn("在服务器上创建房间",func():
		if app.server_connected and not app.directory.is_empty():show_directory(app)
		else:join_address(app,"connect")))
	var row:=S.row();col.add_child(row);row.add_child(S.btn("添加服务器",func():join_address(app,"save")));row.add_child(S.btn("地址直连",func():join_address(app,"connect")));row.add_child(S.btn("发现局域网",func():app.nearby.clear();discoveries(app,{});app.cmd({"op":"discover"})))
	col.add_child(S.text("收藏的服务器",20,S.INK,true))
	if app.catalog.get("servers",[]).is_empty():col.add_child(S.text("输入地址后，展开这台服务器的房间列表。",15,S.SUB))
	for server in app.catalog.get("servers",[]):
		var rr:=S.row();col.add_child(rr);var name_:=str(server.get("name",server.address));var addr:=str(server.address)
		var b:=S.btn("▸   %s     %s"%[name_,addr],func():app.linked_address=addr;app.cmd({"op":"connect","address":addr,"name":app.me_name}));b.size_flags_horizontal=Control.SIZE_EXPAND_FILL;rr.add_child(b)
		rr.add_child(S.btn("移除",func():app.cmd({"op":"remove_server","address":addr})))
	col.add_child(S.btn("返回主菜单",app._home))

static func join_address(app,action:String)->void:
	var col:VBoxContainer=app.modal("添加服务器" if action=="save" else "连接服务器")
	var addr:=S.line("服务器地址:UDP端口",app.linked_address);col.add_child(addr)
	var name_:=S.line("服务器备注" if action=="save" else "你的昵称","" if action=="save" else app.me_name);col.add_child(name_)
	col.add_child(S.btn("保存" if action=="save" else "连接",func():
		var a:=addr.text.strip_edges();if a.is_empty():return
		app.linked_address=a;var nm:=name_.text.strip_edges()
		if action=="save":app.cmd({"op":"save_server","address":a,"name":nm if nm else a})
		else:app.me_name=nm if nm else app.me_name;app.cmd({"op":"connect","address":a,"name":app.me_name})
		app.close_popup(),true))
	col.add_child(S.btn("取消",app.close_popup))

static func trust(app,v:Dictionary)->void:
	var col:VBoxContainer=app.modal("核对服务器身份")
	col.add_child(S.text(str(v.get("address","")),17,S.INK,true))
	var fp:=S.text(str(v.get("fingerprint","")),14,S.SUB);fp.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;col.add_child(fp)
	col.add_child(S.text("向服主核对指纹，再提交房间密码。",15,S.SUB))
	col.add_child(S.btn("已核对，信任此服务器",func():app.cmd({"op":"trust","accept":true});app.close_popup(),true))
	col.add_child(S.btn("取消连接",func():app.cmd({"op":"trust","accept":false});app.close_popup()))

static func discoveries(app,v:Dictionary)->void:
	if not v.is_empty():
		var addr:=str(v.get("address",v.get("host","")))
		if not addr.is_empty():
			if not v.has("address") and v.has("port"):addr=("["+addr+"]" if addr.contains(":") else addr)+":"+str(int(v.port))
			elif addr.find(":")<0 and v.has("port"):addr+=":"+str(int(v.port))
			var e:=v.duplicate();e.address=addr;app.nearby[addr]=e
	if app.page!="hub":return
	var col:VBoxContainer=app.modal("发现附近的房间")
	var list:Array=app.nearby.values()
	if list.is_empty():col.add_child(S.text("正在查找同一网络的房间，也可以直接填写地址。",16,S.SUB))
	for x in list:
		var addr:=str(x.get("address",""))
		col.add_child(S.btn("%s   %s"%[x.get("name","局域网房间"),addr],func():app.linked_address=addr;app.cmd({"op":"connect","address":addr,"name":app.me_name});app.close_popup()))
	col.add_child(S.btn("返回",app.close_popup))

static func show_directory(app)->void:
	app.page="directory";app._clear()
	var col:VBoxContainer=app._card(Vector2(25,24),Vector2(611,-24),"left",24)
	app._header(col,str(app.directory.get("name","服务器房间")),"SERVER DIRECTORY   /   "+app.linked_address)
	col.add_child(S.btn("在此服务器创建房间",func():app.Setup.show_setup(app,"create",false),true))
	var list:Array=app.directory.get("rooms",[])
	if list.is_empty():col.add_child(S.text("目前没有房间。创建一间，邀请朋友加入。",16,S.SUB))
	for r in list:
		var title:=str(r.get("title",r.get("name",r.get("id","房间"))))
		var card:=S.panel(S.WARM,11,12);col.add_child(card);var row:=S.row(8);card.add_child(row)
		var info:=S.stack(3);info.size_flags_horizontal=Control.SIZE_EXPAND_FILL;row.add_child(info);info.add_child(S.text(title,18,S.INK,true))
		var compatible:=str(r.get("compatibility","原版 v1" if int(r.get("profile",0))==0 else "扩展房间"))
		info.add_child(S.text("%s   ·   %s   ·   观众 %d%s"%[{"ready":"准备中","playing":"进行中","paused":"暂停","finished":"已结算"}.get(r.get("state",""),"载入中"),compatible,int(r.get("spectator_count",0)),"   ·  有密码" if r.get("has_password",false) else ""],13,S.SUB))
		var series:String=app.series_line(r.get("series",{}));if not series.is_empty():info.add_child(S.text(series,13,S.DEEP))
		row.add_child(S.btn("加入" if r.get("state","ready")=="ready" else "观战",func():_join_room(app,r)))
	col.add_child(S.btn("返回大厅",func():show_hub(app)))

static func _join_room(app,r:Dictionary)->void:
	var col:VBoxContainer=app.modal(str(r.get("title","加入房间")))
	var pass_:=S.line("房间密码（无密码则留空）");pass_.secret=true;col.add_child(pass_)
	if r.get("state","ready")=="ready":
		col.add_child(S.btn("申请参赛席",func():app.cmd({"op":"join","room_id":r.get("id",r.get("room_id","")),"role":"player","password":pass_.text});app.close_popup(),true))
	col.add_child(S.btn("进入观战席",func():app.cmd({"op":"join","room_id":r.get("id",r.get("room_id","")),"role":"spectator","password":pass_.text});app.close_popup()))
	col.add_child(S.btn("取消",app.close_popup))

static func ready_room(app)->void:
	app.page="ready";app._clear()
	var r:Dictionary=app.room;var s:Dictionary=r.get("settings",{})
	var host:bool=r.get("self_id")==r.get("host_id")
	var col:VBoxContainer=app._card(Vector2(26,25),Vector2(580,-25),"left",25)
	app._header(col,str(r.get("title",s.get("title","准备大厅"))),"READY ROOM   /   准备厅")
	var mode:String={"offline":"本地比赛","lan":"局域网房间","server":"专服房间"}.get(r.get("mode"),"房间")
	var port:=int(r.get("port",0));var state_name:String={"ready":"准备中","loading":"载入地图","countdown":"即将开赛"}.get(r.get("state","ready"),"已入房")
	col.add_child(S.text(mode+("    ·    UDP "+str(port) if port>0 else "")+"    ·    "+state_name,15,S.SUB))
	if port>0:
		var addr:=str(r.get("advertise",""))
		if addr.is_empty() and r.get("mode")=="lan" and host:
			for ip in IP.get_local_addresses():
				if not ip.contains(":") and not ip.begins_with("127.") and not ip.begins_with("169.254.") and not ip.begins_with("0."):
					addr=ip+":"+str(port);break
		if addr.is_empty():addr=app.linked_address
		var link:=str(r.get("invite",""))
		var line_:=S.line("服主公布的地址",addr);line_.editable=false
		if addr:col.add_child(line_)
		if link:col.add_child(S.btn("复制邀请",func():DisplayServer.clipboard_set(link);app.toast("已复制邀请。")))
		elif addr:col.add_child(S.btn("复制连接地址",func():DisplayServer.clipboard_set(addr);app.toast("已复制地址，朋友连接后可在目录中选择本房。")))
	var maps:Array=[]
	for mid in s.get("maps",[]):
		var count:=int(s.get("map_rounds",{}).get(mid,s.get("rounds_per_map",1)))
		maps.append(app.map_name(str(mid))+(" ×%d"%count if count>1 else ""))
	var pool:String={"fixed":"地图","rotate":"轮换","random":"随机地图池"}.get(s.get("map_policy","fixed"),"地图")
	var display:=S.text(pool+"  /  "+("、" if s.get("map_policy")=="random" else " → ").join(maps),16,S.DEEP,true)
	display.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;col.add_child(display)
	var series:String=app.series_line(r.get("series",{}));if not series.is_empty():col.add_child(S.text(series,15,S.SUB))
	for slot in r.get("slots",[]):
		var i:=int(slot.get("seat",0));var card:=S.panel(S.WARM,11,9);col.add_child(card)
		var row:=S.row(9);card.add_child(row);row.add_child(S.text("0%d"%[i+1],19,S.SUB,true))
		var label_:=str(slot.get("name",""))
		if label_.is_empty() or label_=="random":label_={"random":"随机 AI","human":"等待真人","bot":"指定 AI"}.get(slot.get("kind"),"参赛席")
		for named in app.catalog.get("bots",[]):if str(named.id)==label_:label_=str(named.name);break
		var lbl:=S.text(label_,17,S.INK,true);lbl.size_flags_horizontal=Control.SIZE_EXPAND_FILL;row.add_child(lbl)
		row.add_child(S.text("已准备" if slot.get("ready",false) else ("待准备" if slot.get("occupied",false) else "空席"),14,S.SUB))
		if slot.get("kind")=="human" and not slot.get("occupied",false):row.add_child(S.btn("入席",func():app.cmd({"op":"seat","seat":i})))
	col.add_child(S.text("观战席  ·  %s 人"%r.get("spectator_count",0),15,S.SUB))
	var controls:=S.row();col.add_child(controls)
	var myseat:=int(r.get("self_seat",-1));var ready:=false
	for m in r.get("members",[]):if m.get("id")==r.get("self_id"):ready=m.get("ready",false)
	if myseat>=0:
		controls.add_child(S.btn("取消准备" if ready else "准备",func():app.cmd({"op":"ready","ready":not ready})))
		controls.add_child(S.btn("移至观战席",func():app.cmd({"op":"seat","seat":-1})))
	if host:
		controls.add_child(S.btn("房间设置",func():app.Setup.show_setup(app,"edit",false)))
		var start:=S.btn("开始比赛   →",func():app.cmd({"op":"start","room_rev":r.get("room_rev",0)}),true);col.add_child(start)
		for slot in r.get("slots",[]):
			if slot.get("kind")=="human" and (not slot.get("occupied",false) or (r.get("mode")!="offline" and not slot.get("ready",false))):
				start.disabled=true;start.text="等待真人入席并准备";break
	col.add_child(S.btn("离开房间",func():app.cmd({"op":"leave"})))
