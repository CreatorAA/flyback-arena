extends Node3D
const Island=preload("res://scripts/island.gd")
const Actor=preload("res://scripts/actor.gd")
var camera:Camera3D
var island
var cast:Array=[]
var darts:Array=[]
var dart_targets:Array[Vector3]=[]
var dart_dirs:Array[float]=[]
var dart_markers:Array=[]
var glyphs:Array=[]
var event_ids:Dictionary={}
var manifest:Dictionary
var display_only:=false
var ticks:=-1
var watch_seat:=-1
var self_seat:=-1
var focus:=Vector3.ZERO
var goal:=Vector3.ZERO
var colors:=Actor.SEATS
var pointer:Node3D
var tether:MeshInstance3D
var tether_mat:StandardMaterial3D
var last_warn:=-1
var sim_rate:=1.0
var outro:=-1.0
var podium:Array=[]
var last_fall_elapsed:=-10.
var last_collapse:=-1
var last_fatal:=-1
var close_subject:=-1
var last_known_darts:Dictionary={}
var trail_buffers:Array=[]
var trails:Array=[]
var trail_mats:Array=[]
var safe_bounds:=Rect2()
var vignette_clock:=0.0
var ready_to_draw:=false
var saved_input:Vector2=Vector2.ZERO
var snap:Dictionary
var combat_audio=preload("res://scripts/combat_audio.gd").new()
var flashes:Array=[]
var next_flash:=0
var skip_sounds_once:=false
var impact_ticks:Dictionary={}

func _ready()->void:
	var env:=Environment.new();env.background_mode=Environment.BG_SKY
	var sky:=Sky.new();var gradient:=ShaderMaterial.new();gradient.shader=preload("res://art/sky.gdshader");sky.sky_material=gradient;env.sky=sky
	env.reflected_light_source=Environment.REFLECTION_SOURCE_DISABLED
	env.ambient_light_source=Environment.AMBIENT_SOURCE_COLOR;env.ambient_light_color=Color("#b2c9e1");env.ambient_light_energy=0.31
	env.tonemap_mode=Environment.TONE_MAPPER_FILMIC;env.tonemap_exposure=1.01
	if RenderingServer.get_current_rendering_method()=="forward_plus":
		env.ssao_enabled=true;env.ssao_radius=.54;env.ssao_intensity=1.35;env.ssao_detail=.5
	var world:=WorldEnvironment.new();world.environment=env;add_child(world)
	var sun:=DirectionalLight3D.new();sun.light_color=Color("#fff3de");sun.light_energy=0.94 if RenderingServer.get_current_rendering_method()=="forward_plus" else .49
	sun.rotation_degrees=Vector3(-49,-32,0);sun.shadow_enabled=true;sun.shadow_bias=.075;sun.shadow_normal_bias=1.4;sun.directional_shadow_max_distance=42;add_child(sun)
	if RenderingServer.get_current_rendering_method()=="forward_plus":sun.light_angular_distance=.26
	camera=Camera3D.new();camera.projection=Camera3D.PROJECTION_ORTHOGONAL;camera.current=true;camera.near=0.1;camera.far=100;add_child(camera)
	add_child(combat_audio)
	for i in 6:
		var flash=preload("res://scripts/impact_flash.gd").new();add_child(flash);flashes.append(flash)
	tether=MeshInstance3D.new();tether.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var shaft:=CylinderMesh.new();shaft.height=1;shaft.top_radius=.019;shaft.bottom_radius=.019;shaft.radial_segments=6;tether.mesh=shaft
	tether_mat=StandardMaterial3D.new();tether_mat.shading_mode=BaseMaterial3D.SHADING_MODE_UNSHADED;tether_mat.albedo_color=Color("#92e5d0");tether.material_override=tether_mat;add_child(tether);tether.hide()

func mount(data:Dictionary,mine:int,showcase:bool=false)->void:
	ready_to_draw=false;reset_feedback(false)
	for a in cast:a.free()
	for d in darts:d.free()
	for trail in trails:trail.free()
	trails.clear();trail_buffers.clear();trail_mats.clear();last_known_darts.clear()
	for g in glyphs:if is_instance_valid(g):g.free()
	cast.clear();darts.clear();dart_targets.clear();dart_dirs.clear();dart_markers.clear();glyphs.clear();event_ids.clear()
	if is_instance_valid(island):island.free()
	if is_instance_valid(pointer):pointer.free()
	manifest=data;self_seat=mine;display_only=showcase;ticks=-1;watch_seat=-1;snap={};last_warn=-1;tether.hide();outro=-1;podium.clear();last_fatal=-1;close_subject=-1;last_collapse=-1;sim_rate=1;safe_bounds=Rect2()
	island=Island.new();add_child(island);island.configure(data.terrain,str(data.map))
	for i in data.seats.size():
		var a=Actor.new();cast.append(a);add_child(a);a.setup(data.seats[i],i,mine);a.hide()
		var d:=Node3D.new();darts.append(d);add_child(d);d.hide()
		var weapon:=_flight_model(i);d.add_child(weapon)
		var trail:=MeshInstance3D.new();trail.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;add_child(trail);trails.append(trail);trail_buffers.append([])
		var tm:=StandardMaterial3D.new();tm.vertex_color_use_as_albedo=true;tm.shading_mode=BaseMaterial3D.SHADING_MODE_UNSHADED;tm.cull_mode=BaseMaterial3D.CULL_DISABLED;tm.transparency=BaseMaterial3D.TRANSPARENCY_ALPHA;trail_mats.append(tm)
		var marker:=Label3D.new();marker.text="◇";marker.font_size=40;marker.pixel_size=.005;marker.billboard=BaseMaterial3D.BILLBOARD_ENABLED;marker.modulate=Color("#ffd991");marker.outline_size=5;marker.outline_modulate=Color("#44382b");marker.position=Vector3.UP*.47;d.add_child(marker);marker.hide();dart_markers.append(marker)
		dart_targets.append(Vector3.ZERO);dart_dirs.append(0)
	pointer=_boomerang(Color("#fff6d6"));pointer.scale=Vector3.ONE*0.45;add_child(pointer);pointer.hide()
	goal=Vector3(island.width_m*0.5,0,island.depth_m*0.5)
	focus=goal
	_frame(true);ready_to_draw=true

func _flight_model(seat:int)->Node3D:
	var path:="res://art/darts/flight_s%d.glb"%seat
	if ResourceLoader.exists(path):return (load(path) as PackedScene).instantiate()
	return _boomerang(colors[seat])

func _boomerang(c:Color)->Node3D:
	var group:=Node3D.new()
	var material:=StandardMaterial3D.new();material.albedo_color=c.lightened(0.18);material.metallic=0.18
	for side in [-1,1]:
		var blade:=MeshInstance3D.new();var m:=BoxMesh.new();m.size=Vector3(0.116,0.075,0.32)
		blade.mesh=m;blade.material_override=material;blade.rotation.y=side*0.72;blade.position=Vector3(side*0.105,0,0.03);group.add_child(blade)
	return group

func _frame(now:bool=false)->void:
	var rect:=get_viewport().get_visible_rect().size
	var ratio:=maxf(rect.x/maxf(rect.y,1),0.7)
	var size:float=maxf(island.depth_m*0.78+4.25,(island.width_m+3.7)/ratio)*1.10
	goal=Vector3(island.width_m*0.5,-0.18,island.depth_m*0.5-1.25)
	if display_only:
		size*=1.37
		goal.x-=size*ratio*0.15
		goal.z+=0.35
	elif watch_seat>=0 and watch_seat<cast.size() and cast[watch_seat].shown:
		goal=cast[watch_seat].position;size*=0.68
	elif safe_bounds.has_area():
		var bounds:=safe_bounds
		for a in cast:
			if a.shown and a.dead_age<0:bounds=bounds.expand(Vector2(a.target.x,a.target.z))
		var center:=bounds.get_center()
		goal=Vector3(center.x,.15,center.y-.34)
		size=minf(size,maxf(4.8,maxf(bounds.size.y*.84+2.8,(bounds.size.x+3.)/ratio)))
	var yaw:=0.032 if not display_only else 0.12+0.04*sin(vignette_clock*0.08)
	var pitch:=deg_to_rad(54.0)
	if outro>=0 and not podium.is_empty():
		var victim_ok:bool=last_fatal>=0 and last_fatal<cast.size() and cast[last_fatal].shown
		var c:=Vector3.ZERO
		for i in podium:c+=cast[i].target
		c/=podium.size()
		var separation:=0.
		for i in podium:separation=maxf(separation,c.distance_to(cast[i].target))
		var close:=smoothstep(0.,.65,outro)
		var target_pos:=c+Vector3.UP*.70
		var hold:=1.0
		if victim_ok and outro<1.55:
			hold=smoothstep(.72,1.55,outro)
			var f:Vector3=cast[last_fatal].position+cast[last_fatal].figure.position;f.y=maxf(f.y,-2.2)
			target_pos=f.lerp(target_pos,.32+.68*hold)
		goal=goal.lerp(target_pos,close);size=lerpf(size,maxf(4.15+(1.-hold)*2.05,2.8+separation*2.4),close)
		pitch=lerpf(pitch,deg_to_rad(34.),close*hold);yaw=lerpf(yaw,.025,close)
	elif close_subject>=0 and close_subject<cast.size() and cast[close_subject].shown:
		goal=cast[close_subject].target+Vector3.UP*.33;size=minf(size,6.3);pitch=deg_to_rad(43)
	if now:focus=goal;camera.size=size
	else:camera.size=lerpf(camera.size,size,1.-exp(-get_process_delta_time()*8))
	var back:=Vector3(sin(yaw)*cos(pitch),sin(pitch),cos(yaw)*cos(pitch))*24
	camera.position=focus+back
	camera.look_at(focus,Vector3.UP)

func update(data:Dictionary)->void:
	if not ready_to_draw:return
	var new_tick:=int(data.get("tick",0))
	if ticks>=0 and new_tick<ticks: return
	if int(data.get("fallen",0))!=int(snap.get("fallen",0)):
		last_collapse=new_tick
		var low:=Vector2(INF,INF);var high:=Vector2(-INF,-INF)
		for z in island.h:
			for x in island.w:
				var wave:int=island._wave(x,z)
				if int(island.terrain.rows[z][x])==2 or (wave>=0 and wave<int(data.fallen)):continue
				low=low.min(Vector2(x,z));high=high.max(Vector2(x+1,z+1))
		safe_bounds=Rect2(low*island.cell,(high-low)*island.cell) if high.x>low.x and high.y>low.y and int(data.fallen)>0 else Rect2()
	ticks=new_tick;snap=data
	island.update_phase(int(data.get("fallen",0)),int(data.get("warn",-1)))
	var warning:=int(data.get("warn",-1))
	if warning>=0 and warning!=last_warn and not skip_sounds_once and not display_only:combat_audio.play_cue("warning",-1,ticks,0.,false,sim_rate)
	last_warn=warning;tether.hide()
	for p in data.get("players",[]):
		var i:=int(p.seat)
		if i>=0 and i<cast.size():
			cast[i].apply(p,new_tick)
			if not p.get("alive",true) and cast[i].shown and cast[i].presentation.death_kind=="" and p.has("cause"):
				_died(i,int(p.get("cause",0)),int(p.get("killer",-1)))
			if i==self_seat and p.has("x"):
				var tile:int=island._cell(floori(float(p.x)/island.cell*.001),floori(float(p.y)/island.cell*.001))
				# A flying own dart exposes the owner under upstream grass rules.
				var ds:int=int(data.darts[i].get("st",0)) if data.get("darts",[]).size()>i else 0
				cast[i].set_stealth(tile==3 and int(p.get("expose",0))==0 and ds not in [1,2,4,6])
	for d in data.get("darts",[]):
		var i:=int(d.seat)
		if i<0 or i>=darts.size():continue
		var node:Node3D=darts[i]
		var visible_now:bool=bool(d.get("vis",false)) and d.has("x") and int(d.get("st",0)) not in [0,5]
		if not d.get("vis",false):last_known_darts.erase(i)
		var st:=int(d.get("st",0));cast[i].presentation.held.visible=cast[i].shown and cast[i].dead_age<0 and d.get("vis",false) and st==0
		if not visible_now or st not in [1,2,6]:trail_buffers[i].clear();trails[i].hide()
		if visible_now:
			var pos:=Vector3(float(d.x)*0.001,(0.12 if int(d.st)==3 else 0.47),float(d.y)*0.001)
			dart_targets[i]=pos
			if not node.visible:node.position=pos;node.reset_physics_interpolation()
			var v:=Vector2(float(d.get("vx",0)),float(d.get("vy",0)))
			if v.length_squared()>0:dart_dirs[i]=atan2(v.x,v.y)
			if st in [1,2,6] and v.length_squared()>0:last_known_darts[i]=Vector3(v.x,0,v.y).normalized()
			node.rotation.y=dart_dirs[i]
		node.visible=visible_now
		dart_markers[i].visible=visible_now and int(d.get("st",0))==3
		if i==self_seat and visible_now and int(d.get("st",0))==4 and cast[i].shown and int(cast[i].latest.get("st",0))==9:
			var a:Vector3=cast[i].target+Vector3.UP*.30;var b:=dart_targets[i];var axis:=b-a
			if axis.length()>.05:
				var orient:=Quaternion(Vector3.UP,axis.normalized());tether.transform=Transform3D(Basis(orient).scaled_local(Vector3(1,axis.length(),1)),(a+b)*.5);tether.show()
	play_events(data.get("events",[]),int(data.get("round_epoch",0)))
	skip_sounds_once=false

func _died(victim:int,cause:int,killer:int)->void:
	if victim<0 or victim>=cast.size() or not cast[victim].shown:return
	var a=cast[victim];var direction:Vector3=a.presentation.last_dir
	if cause==1 and last_known_darts.has(killer):direction=last_known_darts[killer]
	elif cause==2 and killer>=0 and killer<cast.size() and cast[killer].shown:direction=a.target-cast[killer].target
	var grid:=Vector2i(floori(a.target.x/island.cell),floori(a.target.z/island.cell))
	var wave:int=island._wave(grid.x,grid.y) if grid.x>=0 and grid.y>=0 else -1
	var ground:bool=last_collapse>=0 and ticks-last_collapse<=2 and wave==int(snap.get("fallen",0))-1 and wave>=0
	a.presentation.death(cause,direction,ground,killer>=0);last_fatal=victim

func celebration(result:Dictionary)->void:
	if outro>=0:return
	outro=0.;podium.clear()
	for row in result.get("standings",[]):
		var i:=int(row.get("seat",-1))
		if i<0 or i>=cast.size() or not cast[i].shown:continue
		if not row.get("alive",false):continue
		if int(row.get("rank",99))==1 and cast[i].dead_age<0:
			podium.append(i);cast[i].presentation.cheer(.025)
	if podium.is_empty() and int(result.get("winner",-1))>=0:
		var i:=int(result.winner)
		if i<cast.size() and cast[i].shown and cast[i].dead_age<0:podium.append(i);cast[i].presentation.cheer(.025)
	tether.hide();pointer.hide()

func _trail(i:int)->void:
	var d:Dictionary=snap.darts[i] if i<snap.get("darts",[]).size() else {}
	if not darts[i].visible or not d.get("vis",false) or int(d.get("st",0)) not in [1,2,6]:return
	var pts:Array=trail_buffers[i];var pt:Vector3=darts[i].position
	if pts.is_empty() or pt.distance_to(pts[-1])>.075:pts.append(pt)
	var length:=0.
	for j in range(pts.size()-1,0,-1):
		length+=pts[j].distance_to(pts[j-1])
		if length>.89:
			for k in range(j-1):pts.pop_front()
			break
	if pts.size()<2:return
	var mesh:=ImmediateMesh.new();mesh.surface_begin(Mesh.PRIMITIVE_TRIANGLE_STRIP,trail_mats[i])
	for j in pts.size():
		var v:Vector3=pts[mini(j+1,pts.size()-1)]-pts[maxi(0,j-1)];var offset:=Vector3(-v.z,0,v.x).normalized()*.048*float(j+1)/pts.size()
		var c:Color=colors[i].lightened(.36);c.a=.48*float(j+1)/pts.size()
		for k in [-1,1]:mesh.surface_set_color(c);mesh.surface_add_vertex(pts[j]+offset*k)
	mesh.surface_end();trails[i].mesh=mesh;trails[i].show()

func predict(data:Dictionary)->void:
	if self_seat<0 or self_seat>=cast.size() or int(data.get("tick",-1))<=ticks:return
	cast[self_seat].predict_visual(data.get("player",{}),int(data.tick))
	var d:Dictionary=data.get("dart",{})
	if d.has("x") and int(d.get("st",0)) in [1,2,6] and cast[self_seat].dead_age<0:
		dart_targets[self_seat]=Vector3(float(d.x)*.001,.47,float(d.y)*.001)
		if not darts[self_seat].visible:
			darts[self_seat].position=dart_targets[self_seat];darts[self_seat].show();dart_markers[self_seat].hide()

func reset_feedback(skip_baseline:bool)->void:
	combat_audio.reset();event_ids.clear();impact_ticks.clear();skip_sounds_once=skip_baseline
	for f in flashes:f.advance(1.)

func _sound(cue:String,seat:int,at:Vector3,step:int,personal:bool)->void:
	if skip_sounds_once or display_only:return
	var width:=get_viewport().get_visible_rect().size.x
	var pan:=clampf(camera.unproject_position(at).x/maxf(width,1.)*2.-1.,-1.,1.)
	combat_audio.play_cue(cue,seat,step,pan,personal,sim_rate)

func _contact(cue:String,seat:int,at:Vector3,step:int,personal:bool)->void:
	var key:=("body" if cue in ["hit","kick"] else "metal")+":"+str(seat)
	if impact_ticks.get(key,-1)==step:return
	impact_ticks[key]=step
	if next_flash>=flashes.size():next_flash=0
	flashes[next_flash].fire(at,cue);next_flash+=1
	_sound(cue,seat,at,step,personal)

func play_events(events:Array,epoch:int)->void:
	# SWING is emitted both on contact and in clear air. Don't put an extra
	# whoosh on top of a confirmed impact from the same swing.
	var connected:Dictionary={}
	for e in events:
		var tag:=str(e.get("type","")).trim_prefix("EV_")
		if tag in ["CUT","KICKHIT","BLOCK"]:connected[Vector2i(int(e.get("tick",ticks)),int(e.get("a",-1)))]=true
	for e in events:
		var step:=int(e.get("tick",ticks))
		var id:="%d:%d:%d"%[epoch,step,int(e.get("ordinal",0))]
		if event_ids.has(id):continue
		var i:=int(e.get("a",-1));var tag:=str(e.get("type","")).trim_prefix("EV_")
		if tag=="DEATH" and (i<0 or i>=cast.size() or not cast[i].shown or cast[i].dead_age<0):continue
		event_ids[id]=true
		if tag in ["HIT","CUT","KICKHIT"]:
			var victim:=int(e.get("b",-1))
			if victim>=0 and victim<cast.size() and cast[victim].shown:
				# A hidden attacker may be -1: neither pan nor FX locates them.
				_contact("kick" if tag=="KICKHIT" else "hit",victim,cast[victim].target+Vector3.UP*.35,step,self_seat>=0 and self_seat in [i,victim])
			continue
		if i<0 or i>=cast.size():continue
		var personal:bool=i==self_seat
		if tag in ["CLASH","BLOCK"]:
			var other:=int(e.get("b",-1));var loc:Vector3
			if tag=="CLASH" and other>=0:
				# A real two-dart collision emits one CLASH per blade. It's one sound.
				var pair:="pair:%d:%d"%[mini(i,other),maxi(i,other)]
				if impact_ticks.get(pair,-1)==step:continue
				impact_ticks[pair]=step
			if other>=0:loc=Vector3(float(e.c)*.001,.36,float(e.d)*.001)
			elif darts[i].visible:loc=dart_targets[i]
			elif cast[i].shown:loc=cast[i].target+Vector3.UP*.3
			else:continue
			_contact("clash" if tag=="CLASH" else "block",i,loc,step,personal or (self_seat>=0 and self_seat==other))
			continue
		if not cast[i].shown:continue
		var loc:Vector3=cast[i].target
		match tag:
			"RELEASE":_sound("release",i,loc,step,personal)
			"SWING":
				if not connected.has(Vector2i(step,i)):_sound("swing",i,loc,step,personal)
			"DASH":_sound("dash",i,loc,step,personal)
			"CATCH","PICK":_sound("catch",i,loc,step,personal)
			"DEATH":
				var cause:=int(e.get("b",-1))
				_died(i,cause,int(e.get("c",-1)))
				if cause==3:_sound("fall",i,loc,step,personal)

func aim_direction()->Vector2:
	if self_seat<0 or self_seat>=cast.size() or not cast[self_seat].shown:return Vector2.RIGHT
	var xy:=get_viewport().get_mouse_position()
	var point=Plane(Vector3.UP,0).intersects_ray(camera.project_ray_origin(xy),camera.project_ray_normal(xy))
	if point==null:return Vector2.RIGHT
	var v:Vector3=point-cast[self_seat].position
	return Vector2(v.x,v.z).normalized()

func _process(dt:float)->void:
	if not ready_to_draw:return
	if outro>=0:outro+=dt
	for a in cast:a.time_rate=1.0 if outro>=0 else sim_rate
	var game_dt:=dt*(1. if outro>=0 else sim_rate)
	for f in flashes:
		if f.visible:f.advance(game_dt)
	vignette_clock+=dt;_frame();focus=focus.lerp(goal,1-exp(-6.0*dt))
	for i in darts.size():
		if darts[i].visible:
			darts[i].position=darts[i].position.lerp(dart_targets[i],1-exp(-32*game_dt))
			if absf(float(darts[i].position.y)-0.12)>0.015:darts[i].get_child(0).rotation.y+=game_dt*18.5
			_trail(i)
	if outro<0 and self_seat>=0 and self_seat<cast.size() and cast[self_seat].shown and cast[self_seat].dead_age<0 and not display_only:
		pointer.visible=true
		var dir:=saved_input if saved_input.length_squared()>.01 else aim_direction()
		pointer.position=cast[self_seat].position+Vector3(dir.x*0.66,0.065,dir.y*0.66);pointer.rotation.y=atan2(dir.x,dir.y)
	else:pointer.hide()
