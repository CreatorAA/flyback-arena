extends Node3D
const SEATS := [Color("#12b886"),Color("#ff8a1f"),Color("#2e8cff"),Color("#e8459b")]
const SIGNS := ["●","◆","▲","✚"]
const SKINS := ["human_courier","human_tide","human_grove","human_spark"]
var figure:Node3D
var ring:MeshInstance3D
var cue:MeshInstance3D
var progress_arc:MeshInstance3D
var arc_key:=-1
var foot_a:Node3D
var foot_b:Node3D
var arm_a:Node3D
var arm_b:Node3D
var scarf:Node3D
var seat:=0
var local:=false
var human:=false
var shown:=false
var latest:Dictionary
var target:=Vector3.ZERO
var target_yaw:=0.0
var motion_ticks:Array[int]=[]
var motion_points:Array[Vector3]=[]
var render_tick:=0.0
var authority_tick:=-1
var sampled_us:=0
var correction:=Vector3.ZERO
var dead_age:float=-1.0
var mat_cache:Dictionary={}
var stealth:=false
var stealth_eye:Label3D
var badge:Label3D
var presentation
var time_rate:=1.0
var original_mats:Dictionary={}
var transparent_mats:Dictionary={}

func _mat(c:Color)->StandardMaterial3D:
	if mat_cache.has(c):return mat_cache[c]
	var m:=StandardMaterial3D.new();m.albedo_color=c;m.roughness=0.88;mat_cache[c]=m;return m

func _box(parent:Node3D,pos:Vector3,size:Vector3,tint:Color)->MeshInstance3D:
	var o:=MeshInstance3D.new();var m:=BoxMesh.new();m.size=size;o.mesh=m
	o.material_override=_mat(tint);o.position=pos;parent.add_child(o);return o

func _ellipsoid(parent:Node3D,pos:Vector3,size:Vector3,tint:Color)->MeshInstance3D:
	var o:=MeshInstance3D.new();var m:=SphereMesh.new();m.radius=1;m.height=2;m.rings=5;m.radial_segments=12;o.mesh=m
	o.material_override=_mat(tint);o.position=pos;o.scale=size;parent.add_child(o);return o

func _loop(parent:Node3D,r:float,thickness:float,color:Color)->MeshInstance3D:
	var o:=MeshInstance3D.new();var t:=TorusMesh.new();t.inner_radius=r-thickness;t.outer_radius=r;t.rings=32;t.ring_segments=5
	o.mesh=t;o.material_override=_mat(color);o.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;parent.add_child(o);return o

func setup(entry:Dictionary,index:int,self_seat:int)->void:
	seat=index;local=seat==self_seat
	figure=Node3D.new();add_child(figure)
	human=str(entry.get("bot","")).is_empty()
	var key:=str(entry.get("model",""))
	if key=="carbon":key="gemini"
	if key=="hydrogen":key="gemini_s"
	var skin:=str(entry.get("skin_id",SKINS[index%4]))
	if not skin in SKINS:skin=SKINS[index%4]
	var actor_path:=("res://art/humans/"+skin+".glb") if human else ("res://art/actors/"+key+".glb")
	if ResourceLoader.exists(actor_path):
		var rig:Node3D=(load(actor_path) as PackedScene).instantiate()
		figure.add_child(rig);rig.scale=Vector3.ONE*1.48
		for name_ in ["face_tight","face_happy"]:
			var face:Node3D=rig.find_child(name_,true,false);if face: face.hide()
		foot_a=rig.find_child("footA",true,false);foot_b=rig.find_child("footB",true,false)
		arm_a=rig.find_child("armA",true,false);arm_b=rig.find_child("armB",true,false)
	elif human:
		_courier();figure.scale=Vector3(1.47,1.26,1.47)
	else:
		var plate:=Color("#617381");var pale:=Color("#b9c5bf");var visor:=Color("#1f3640")
		_box(figure,Vector3(0,.48,0),Vector3(.49,.38,.32),plate)
		_box(figure,Vector3(0,.83,0),Vector3(.59,.31,.37),pale)
		_box(figure,Vector3(0,.86,.20),Vector3(.43,.105,.04),visor)
		_box(figure,Vector3(0,.86,.228),Vector3(.24,.029,.016),SEATS[index%4])
		for i in [-1,1]:
			var foot:=_box(figure,Vector3(i*.14,.15,.03),Vector3(.19,.27,.24),visor)
			var arm:=_box(figure,Vector3(i*.32,.48,0),Vector3(.14,.29,.18),plate)
			if i==-1:foot_a=foot;arm_a=arm
			else:foot_b=foot;arm_b=arm
		figure.scale=Vector3.ONE*1.35
	ring=_loop(self,0.43,0.039,SEATS[index%4]);ring.position.y=0.035
	if local:
		var inner:=_loop(self,0.355,0.017,Color.WHITE);inner.position.y=0.036
		cue=_loop(self,0.48,0.015,Color("#4fe3b5"));cue.position.y=0.032
		progress_arc=MeshInstance3D.new();progress_arc.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;progress_arc.position.y=.055;add_child(progress_arc)
	badge=Label3D.new();badge.text=SIGNS[index%4];badge.font_size=32;badge.pixel_size=0.0032;badge.modulate=SEATS[index%4]
	badge.outline_modulate=Color("#303a38");badge.outline_size=5;badge.position=Vector3(0,1.18,0);badge.billboard=BaseMaterial3D.BILLBOARD_ENABLED;add_child(badge)
	if local:
		stealth_eye=Label3D.new();stealth_eye.text="隐";stealth_eye.font_size=28;stealth_eye.pixel_size=.004;stealth_eye.outline_size=5;stealth_eye.position=Vector3(.21,1.19,0);stealth_eye.billboard=BaseMaterial3D.BILLBOARD_ENABLED;add_child(stealth_eye);stealth_eye.hide()
	presentation=preload("res://scripts/performance.gd").new();add_child(presentation);presentation.bind(self)

func _courier()->void:
	var c:Color=SEATS[seat%4]
	var ink:=Color("#283c43");var sand:=Color("#eed3a2");var shoes:=Color("#5d4339")
	# Pilot cap, binocular goggles, angled shoulder strap, split flight cape, messenger bag.
	foot_a=_box(figure,Vector3(-0.115,0.12,0.025),Vector3(0.16,0.19,0.23),shoes)
	foot_b=_box(figure,Vector3(0.115,0.12,0.025),Vector3(0.16,0.19,0.23),shoes)
	_box(figure,Vector3(0,0.28,0),Vector3(0.34,0.15,0.24),ink)
	_box(figure,Vector3(0,0.47,0),Vector3(0.33,0.34,0.28),Color("#ffe2ac"))
	arm_a=_box(figure,Vector3(-0.21,0.48,0),Vector3(0.13,0.26,0.18),Color("#e8bc80"));arm_a.rotation.z=-0.13
	arm_b=_box(figure,Vector3(0.21,0.48,0),Vector3(0.13,0.26,0.18),Color("#e8bc80"));arm_b.rotation.z=0.13
	_ellipsoid(figure,Vector3(0,0.76,0),Vector3(0.215,0.205,0.18),sand)
	_ellipsoid(figure,Vector3(0,0.85,-0.013),Vector3(0.235,0.155,0.205),Color("#754936"))
	_box(figure,Vector3(0,0.83,0.15),Vector3(0.42,0.065,0.14),Color("#986448"))
	_box(figure,Vector3(0,0.775,0.172),Vector3(0.38,0.095,0.055),ink)
	for side in [-1,1]:
		_box(figure,Vector3(side*0.096,0.782,0.205),Vector3(0.104,0.055,0.025),Color("#8ee4d6"))
		_box(figure,Vector3(side*0.21,0.748,-0.002),Vector3(0.045,0.14,0.12),Color("#a87649"))
	_box(figure,Vector3(0,0.625,0),Vector3(0.40,0.085,0.32),c)
	var strap:=_box(figure,Vector3(-0.003,0.47,0.15),Vector3(0.05,0.36,0.032),shoes);strap.rotation.z=-0.55
	var bag:=_box(figure,Vector3(0.21,0.34,-0.04),Vector3(0.15,0.22,0.20),Color("#925f38"));bag.rotation.z=-0.13
	_box(bag,Vector3(0.0,0.044,0.086),Vector3(0.15,0.07,0.042),Color("#c18c57"))
	_box(bag,Vector3(0,0.011,0.109),Vector3(0.038,0.032,0.012),Color("#ffe39b"))
	scarf=Node3D.new();figure.add_child(scarf);scarf.position=Vector3(0,0.615,-0.13)
	for side in [-1,1]:
		var cape:=_box(scarf,Vector3(side*0.093,-0.155,-0.055),Vector3(0.205,0.35,0.06),c.darkened(0.12*int(side==1)));cape.rotation.x=0.30;cape.rotation.z=side*0.25
	var seal:=Label3D.new();seal.text=SIGNS[seat%4];seal.font_size=40;seal.pixel_size=0.0020;seal.outline_size=0;seal.modulate=Color("#fff5cc");seal.position=Vector3(-0.095,0.50,0.163);figure.add_child(seal)

func _sample(t:float)->Vector3:
	if motion_ticks.size()==1 or t<=motion_ticks[0]:return motion_points[0]
	for i in range(1,motion_ticks.size()):
		if t<=motion_ticks[i]:
			var f:float=(t-motion_ticks[i-1])/(motion_ticks[i]-motion_ticks[i-1])
			return motion_points[i-1].lerp(motion_points[i],f)
	return motion_points.back()

func _endpoint(tick:int,pt:Vector3)->void:
	var i:=motion_ticks.bsearch(tick)
	if i<motion_ticks.size() and motion_ticks[i]==tick:motion_points[i]=pt
	else:motion_ticks.insert(i,tick);motion_points.insert(i,pt)
	while motion_ticks.size()>6:motion_ticks.pop_front();motion_points.pop_front()

func apply(p:Dictionary,tick:int)->void:
	var was:=shown
	shown=bool(p.get("vis",false)) and p.has("x")
	if not shown:
		visible=false;dead_age=-1
		if was:presentation.hide_evidence()
		motion_ticks.clear();motion_points.clear();correction=Vector3.ZERO
		return
	var point:=Vector3(float(p.x)*0.001,0,float(p.y)*0.001)
	var alive:=bool(p.get("alive",false))
	if alive:dead_age=-1;visible=true;figure.show()
	elif dead_age<0:dead_age=0.0;visible=true
	var discontinuity:bool=not was or motion_ticks.is_empty() or (int(p.get("st",0)) in [8,10] and int(latest.get("st",0))!=int(p.st)) or target.distance_squared_to(point)>3.0 or tick<motion_ticks.front()
	if discontinuity:
		motion_ticks.clear();motion_points.clear();_endpoint(tick-1,point);_endpoint(tick,point)
		render_tick=float(tick)-(.72 if local else 1.15)
		position=point;correction=Vector3.ZERO;reset_physics_interpolation()
	else:
		var old:=_sample(render_tick);var replacing:=tick in motion_ticks
		if local and latest.get("vx",0)==0 and latest.get("vy",0)==0 and old.distance_squared_to(point)<.000001:
			# While stationary the input clock can reacquire phase without moving
			# a pixel. A fresh key edge then starts at the current authority step.
			render_tick=maxf(render_tick,float(tick)-.72)
			old=_sample(render_tick)
		_endpoint(tick,point)
		# Replace a predicted endpoint on the same simulation tick, NOT a new
		# easing interval. Preserve the previously displayed sample on correction.
		if replacing:correction+=old-_sample(render_tick)
		if render_tick<float(tick)-2.25:render_tick=float(tick)-1.15
	presentation.observed(p,not was)
	ring.visible=alive;badge.visible=alive
	if cue:cue.visible=alive
	if progress_arc and not alive:progress_arc.hide()
	if not alive:
		for item in get_children():if item is MeshInstance3D:item.hide()
	authority_tick=tick;target=point;latest=p
	target_yaw=PI/2-deg_to_rad(float(p.get("face",0))*0.01)
	if not was:figure.rotation.y=target_yaw
	if cue and alive:
		var st:=int(p.get("st",0));var cd:=int(p.get("cd",0));var time:=int(p.get("timer",0))
		cue.material_override=_mat(Color.WHITE if st==0 and time==1 else (Color("#ff7a3c") if int(p.get("expose",0))>0 else (Color("#ffb454") if cd>0 else Color("#4fe3b5"))))
		var key:=100+time if st==9 else cd
		if key!=arc_key:
			arc_key=key;_arc(1.0-clampf(float(time)/12 if st==9 else float(cd)/20,0,1),Color("#8df5ff") if st==9 else Color("#ffe3a1"),st==9 or cd>0)

func _arc(portion:float,color:Color,enabled:bool)->void:
	progress_arc.visible=enabled
	if not enabled:return
	var geo:=ImmediateMesh.new();geo.surface_begin(Mesh.PRIMITIVE_TRIANGLE_STRIP)
	for i in 33:
		var a:=TAU*(float(i)/32)*portion-PI/2.0
		for r in [.475,.52]:geo.surface_add_vertex(Vector3(cos(a)*r,0,sin(a)*r))
	geo.surface_end();var m:=_mat(color);m.cull_mode=BaseMaterial3D.CULL_DISABLED;progress_arc.mesh=geo;progress_arc.material_override=m

func set_stealth(on:bool)->void:
	if not local or stealth==on:return
	stealth=on
	if stealth_eye:stealth_eye.visible=on
	for mesh in figure.find_children("*","MeshInstance3D",true,false):
		if not original_mats.has(mesh):
			var original:Material=mesh.material_override if mesh.material_override else mesh.get_active_material(0)
			if not original is StandardMaterial3D:continue
			var transparent:StandardMaterial3D=original.duplicate();transparent.transparency=BaseMaterial3D.TRANSPARENCY_ALPHA
			var tint:=transparent.albedo_color;tint.a=.42;transparent.albedo_color=tint
			original_mats[mesh]=original;transparent_mats[mesh]=transparent
		mesh.material_override=transparent_mats[mesh] if on else original_mats[mesh]

func predict_visual(p:Dictionary,tick:int)->void:
	if not local or not shown or dead_age>=0 or not p.has("x") or int(latest.get("st",0)) in [8,10]:return
	var point:=Vector3(float(p.x)*.001,0,float(p.y)*.001)
	if point.distance_to(position)>1.60:return
	if motion_ticks.is_empty():return
	var old:=_sample(render_tick);var replacing:=tick in motion_ticks
	_endpoint(tick,point)
	if replacing:correction+=old-_sample(render_tick)

func _process(dt:float)->void:
	if not shown:sampled_us=0;return
	var now:=Time.get_ticks_usec()
	# Godot can quantize process delta to its physics scheduler even with evenly
	# spaced draws. The authority clock is wall-clock paced, so sample it in kind.
	var wall_dt:=minf(float(now-sampled_us)*.000001,.12) if sampled_us else dt
	sampled_us=now
	dt*=time_rate
	if not motion_ticks.is_empty():
		var ceiling:=float(authority_tick)+(.4 if local else 0.)
		render_tick=minf(render_tick+wall_dt*time_rate*20.,ceiling)
		correction*=exp(-wall_dt*time_rate*45.)
		if correction.length_squared()<.00000001:correction=Vector3.ZERO
		position=_sample(render_tick)+correction
	presentation.render(dt)
