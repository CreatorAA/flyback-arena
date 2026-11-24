extends Node3D
## The authority supplies a half-metre map. This mesh has no collision of its own.
const SEASONS = {
	"icefloes":["d5f0f5","82c7cd","396d92","dcf4fa","b9e8ee","4a98a8"],
	"yinyang":["d6e3a0","ccad94","846e83","eddae5","f6bbd1","dd86a8"],
	"skybridge":["d8cd73","dfaa73","a06a58","f1d3b3","eebd65","ba8c45"],
	"ruins":["b8ce90","c7bda0","8a6971","d7e7d6","a5bf83","719568"],
	"tidelock":["90b9a5","c4b491","647879","d3e5de","a7bd93","5c977d"],
	"kiln":["cf916e","d79a76","815553","edd2ae","bdac72","966851"],
	"windrose":["cabe77","d1b687","93826d","eae0be","cdd288","9ca364"],
	"moonwell":["a6abc5","bdc0cb","777794","d9dbee","aec0be","828fa3"],
	"stonecircle":["bbcb8c","b5ab91","7b6e83","d7dfed","b0bd88","7c9b78"]
}
const SKIES = {
	"tidelock":["548fa7","a4ccbf"], "kiln":["b98582","ebc5a4"],
	"windrose":["829eb3","e3d6ad"], "moonwell":["737ca6","c3c4df"]
}
const SUMMER = ["b8da83","dbb487","8e7590","c9e5e6","a7d071","66996f"]
var terrain := {}
var course := ""
var w := 0
var h := 0
var cell := 0.5
var width_m := 0.0
var depth_m := 0.0
var colors := SUMMER
var meshroot: Node3D
var ground_mat: StandardMaterial3D
var color_mat: StandardMaterial3D
var fallen := -1
var warn := -1
var warning_mesh: MeshInstance3D
var warning_mat: StandardMaterial3D
var baked:Node3D
var grass_by_wave:Dictionary={}
var painted_grass:=false
var prop_parent: Node3D
var batches: Dictionary = {}
var meshes: Dictionary = {}

func configure(data: Dictionary, id: String) -> void:
	terrain = data
	course = id
	w = data.width
	h = data.height
	cell = float(data.cell_mm) / 1000.0
	width_m = w * cell
	depth_m = h * cell
	colors = SEASONS.get(id,SUMMER)
	var env:=get_world_3d().environment
	if env and env.sky and env.sky.sky_material is ShaderMaterial:
		var sky_material:=env.sky.sky_material as ShaderMaterial
		var sky_colors:Array=SKIES.get(id,[])
		for i in 2:sky_material.set_shader_parameter(["high","low"][i], Color(sky_colors[i]) if not sky_colors.is_empty() else null)
	color_mat = StandardMaterial3D.new()
	color_mat.vertex_color_use_as_albedo = true
	color_mat.vertex_color_is_srgb = true
	color_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	color_mat.roughness = 1
	ground_mat = StandardMaterial3D.new()
	ground_mat.roughness = 1
	ground_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	var path := "res://art/ground/"+id+".webp"
	if ResourceLoader.exists(path):
		ground_mat.albedo_texture = load(path)
	else:
		ground_mat.vertex_color_use_as_albedo = true
		ground_mat.vertex_color_is_srgb = true
	var packed_path:="res://art/ground/"+id+".glb"
	if ResourceLoader.exists(packed_path):
		baked=(load(packed_path) as PackedScene).instantiate();add_child(baked)
		var paint:=ShaderMaterial.new();paint.shader=preload("res://art/land.gdshader")
		paint.set_shader_parameter("ground_tex",ground_mat.albedo_texture)
		paint.set_shader_parameter("field_size",Vector2(width_m,depth_m))
		paint.set_shader_parameter("rock_hi",Color(colors[1]).darkened(0.16));paint.set_shader_parameter("rock_lo",Color(colors[2]).lerp(Color("#4c435b"),0.62));paint.set_shader_parameter("lip",Color(colors[5]).darkened(.13))
		for mesh in baked.find_children("*","MeshInstance3D",true,false):
			mesh.material_override=paint
			# The baked dark strata already describe their own recesses. Shadows from
			# characters/grass still land here; avoid sub-texel self-shadow on the rim.
			mesh.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	var grasses:="res://art/backdrops/"+id+"_grass.glb"
	if ResourceLoader.exists(grasses):
		var tufts:Node3D=(load(grasses) as PackedScene).instantiate();add_child(tufts);painted_grass=true
		for n in tufts.find_children("Grass_*","Node3D",true,false):
			var fields:=n.name.split("_")
			if fields.size()!=3:continue
			if n is MeshInstance3D:
				var leaf=n.get_active_material(0)
				if leaf is StandardMaterial3D:
					if not meshes.has("leaf_mat"):
						var fern:StandardMaterial3D=leaf.duplicate()
						fern.albedo_color=Color.WHITE if SKIES.has(id) else Color("#93b28a")
						meshes["leaf_mat"]=fern
					n.material_override=meshes["leaf_mat"]
			var wave:=_wave(int(fields[1]),int(fields[2]))
			if not grass_by_wave.has(wave):grass_by_wave[wave]=[]
			grass_by_wave[wave].append(n)
	warning_mat = StandardMaterial3D.new()
	warning_mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	warning_mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	warning_mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	_prepare_phases()
	update_phase(0,-1)
	_decor()

func _cell(x:int,z:int) -> int:
	if x<0 or z<0 or x>=w or z>=h: return 2
	var wave := _wave(x,z)
	if wave >= 0 and wave < fallen: return 2
	return int(terrain.rows[z][x])

func _wave(x:int,z:int) -> int:
	if z>=terrain.waves.size() or x>=terrain.waves[z].length(): return -1
	return "0123456789abcdefghijklmnopqrstuvwxyz".find(terrain.waves[z][x].to_lower())

func _rnd(x:int,z:int,s:int=0) -> float:
	return fposmod(sin(x*127.1+z*311.7+s*79.33)*43758.5453,1)

func corner(x:int,z:int) -> Vector3:
	var sx:=0.0
	var sz:=0.0
	var count:=0
	for dz in [-1,0]:
		for dx in [-1,0]:
			if _cell(x+dx,z+dz)!=2:
				count+=1
				sx += float(dx)+0.5; sz += float(dz)+0.5
	var jitter := cell*0.08
	return Vector3(x*cell + sx*(0.60 if count==1 or count==3 else 0.0)*cell+(_rnd(x,z)-0.5)*jitter,0,
		z*cell + sz*(0.60 if count==1 or count==3 else 0.0)*cell+(_rnd(x,z,5)-0.5)*jitter)

func _begin() -> SurfaceTool:
	var st:=SurfaceTool.new()
	st.begin(Mesh.PRIMITIVE_TRIANGLES)
	return st

func _tri(st:SurfaceTool,a:Vector3,b:Vector3,c:Vector3,col:Color,n:Vector3) -> void:
	for pt in [a,b,c]:
		st.set_normal(n); st.set_color(col)
		st.set_uv(Vector2((pt.x+0.6)/(width_m+1.2),(pt.z+0.6)/(depth_m+1.2)))
		st.add_vertex(pt)

func _quad(st:SurfaceTool,a:Vector3,b:Vector3,c:Vector3,d:Vector3,col:Color,n:Vector3) -> void:
	_tri(st,a,b,c,col,n); _tri(st,a,c,d,col,n)

func _finish(st:SurfaceTool, parent:Node3D, material:Material)->MeshInstance3D:
	var node:=MeshInstance3D.new()
	node.mesh=st.commit()
	node.material_override=material
	parent.add_child(node)
	return node

# Geometry for built-in maps is already baked. Build the per-wave props and warning
# overlays while mounting, not during the render frame where the referee drops a wave.
var phase_surfaces:Dictionary={}
var wave_props:Dictionary={}
var warning_surfaces:Dictionary={}
func _prepare_phases()->void:
	if is_instance_valid(baked):
		for n in baked.find_children("*","MeshInstance3D",true,false):
			if str(n.name).begins_with("Phase"):phase_surfaces[int(str(n.name).trim_prefix("Phase"))]=n
	var by_wave:Dictionary={}
	for z in h:
		for x in w:
			var tile:=int(terrain.rows[z][x])
			if tile==2:continue
			var wave:=_wave(x,z)
			if not by_wave.has(wave):by_wave[wave]=[]
			by_wave[wave].append(Vector2i(x,z))
	for wave in by_wave:
		fallen=int(wave)
		var group:=Node3D.new();add_child(group);wave_props[wave]=group
		var st:=_begin()
		for pos:Vector2i in by_wave[wave]:
			var x:=pos.x;var z:=pos.y
			_add_cell_props(x,z,int(terrain.rows[z][x]))
			if int(wave)>=0:
				var lift:=Vector3.UP*.016
				_quad(st,corner(x,z)+lift,corner(x+1,z)+lift,corner(x+1,z+1)+lift,corner(x,z+1)+lift,Color.WHITE,Vector3.UP)
		_commit_batches(group)
		if int(wave)>=0:
			var surface:=_finish(st,self,warning_mat);surface.visible=false
			surface.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
			warning_surfaces[wave]=surface
	fallen=-1
func update_phase(new_fallen:int,new_warn:int)->void:
	if new_fallen!=fallen:
		fallen=new_fallen
		if phase_surfaces.has(fallen):
			for k in phase_surfaces:phase_surfaces[k].visible=int(k)==fallen
		else:_raw_phase()
		for k in wave_props:wave_props[k].visible=int(k)<0 or int(k)>=fallen
		for k in grass_by_wave:
			for node in grass_by_wave[k]:node.visible=int(k)<0 or int(k)>=fallen
	if new_warn!=warn:
		warn=new_warn
		if is_instance_valid(warning_mesh):warning_mesh.visible=false
		warning_mesh=warning_surfaces.get(warn)
		if is_instance_valid(warning_mesh):warning_mesh.visible=true

func _add_cell_props(x:int,z:int,tile:int)->void:
	var p:=Vector3((x+0.5)*cell,0,(z+0.5)*cell)
	if tile==1:
		var q:=_rnd(x,z,4)
		if SKIES.has(course):
			# Walls still occupy one real half-metre tile. Only the masonry changes.
			var base_color:Color={"tidelock":Color("#74543f"),"kiln":Color("#75483d"),"windrose":Color("#867a58"),"moonwell":Color("#686781")}[course]
			var cap_color:Color={"tidelock":Color("#ba9566"),"kiln":Color("#c8805a"),"windrose":Color("#c9b989"),"moonwell":Color("#bdb9cd")}[course]
			for j in 3:
				var c:=base_color.lerp(cap_color,float(j)*.19+.09*q)
				_add("masonry",p+Vector3(0,.082+j*.155,0),Vector3(.482,.152,.482),c,0.)
			_add("masonry",p+Vector3.UP*.489,Vector3(.484,.055,.484),cap_color.darkened(.09*q),0.)
			return
		_add("crag",p+Vector3.UP*(0.20+q*0.08),Vector3(0.22+q*0.12,0.30+q*0.26,0.25),Color(colors[2]).lerp(Color("#b1a07c"),q*.55).darkened(.18),q*2.4)
		_add("crag",p+Vector3(0.14,0.11,-0.12),Vector3(0.13,0.22,0.12),Color(colors[1]).darkened(0.23),q)
	elif tile==3 and not painted_grass:
		for j in 6:
			var v:=Vector3((_rnd(x,z,30+j)-0.5)*cell,0,(_rnd(z,x,63+j)-0.5)*cell)
			var q:=_rnd(x,z,108+j)
			_add("blades",p+v,Vector3(0.12+q*0.06,0.20+q*0.27,0.13),Color(colors[5]).lerp(Color(colors[4]),q*0.40).darkened(0.19),q*TAU)
	elif tile!=3 and _rnd(x,z,87)>0.78:
		var q:=_rnd(x,z,49)
		_add("rush",p+Vector3.UP*0.045,Vector3(0.035,0.065,0.04),Color(colors[4]).darkened(0.10),q*TAU)
		if q>0.61: _add("petal",p+Vector3(0.08,0.057,0.03),Vector3(0.04,0.04,0.04),Color("fff0bc"),q)

func _raw_phase()->void:
	if is_instance_valid(meshroot):meshroot.free()
	meshroot=Node3D.new();add_child(meshroot)
	var cap:=_begin(); var cliff:=_begin()
	var surface_count:=0
	for z in h:
		for x in w:
			var tile:=_cell(x,z)
			if tile==2: continue
			surface_count+=1
			var a:=corner(x,z);var b:=corner(x+1,z);var c:=corner(x+1,z+1);var d:=corner(x,z+1)
			var c0:=Color(colors[0]).lightened(_rnd(x,z)*0.08)
			_quad(cap,a,b,c,d,c0,Vector3.UP)
			var edges := [[a,b,Vector2i(0,-1)],[b,c,Vector2i(1,0)],[c,d,Vector2i(0,1)],[d,a,Vector2i(-1,0)]]
			for e in edges:
				var off:Vector2i=e[2]
				if _cell(x+off.x,z+off.y)!=2: continue
				var l:Vector3=e[0];var r:Vector3=e[1]
				var n:=Vector3(off.x,0,off.y)
				var lip_l:=l-Vector3.UP*0.09+n*0.035
				var lip_r:=r-Vector3.UP*0.09+n*0.035
				var brow_l:=lip_l-Vector3.UP*0.16;var brow_r:=lip_r-Vector3.UP*0.16
				var low_l:=l-Vector3.UP*(1.3+_rnd(roundi(l.x*10),roundi(l.z*10),2)*0.55)-n*0.10
				var low_r:=r-Vector3.UP*(1.3+_rnd(roundi(r.x*10),roundi(r.z*10),2)*0.55)-n*0.10
				var shade:=_rnd(x,z,17)*0.10
				_quad(cliff,l,r,lip_r,lip_l,Color(colors[0]).darkened(0.08),n)
				_quad(cliff,lip_l,lip_r,brow_r,brow_l,Color(colors[1]).lightened(0.1),n)
				_quad(cliff,brow_l,brow_r,low_r,low_l,Color(colors[1]).darkened(shade),n)
				_quad(cliff,low_l,low_r,low_r-Vector3.UP*0.52-n*0.23,low_l-Vector3.UP*0.52-n*0.23,Color(colors[2]).lightened(shade),n)
	if surface_count>0:
		_finish(cap,meshroot,ground_mat);_finish(cliff,meshroot,color_mat)

func _primitive(kind:String)->Mesh:
	if meshes.has(kind): return meshes[kind]
	var m:Mesh
	if kind=="masonry":
		var brick:=BoxMesh.new();brick.size=Vector3.ONE;m=brick
	elif kind=="crag":
		var st:=_begin();var lows:Array[Vector3]=[];var shoulders:Array[Vector3]=[];var rim:Array[Vector3]=[]
		for j in 6:
			var k:=Vector2.from_angle(j*TAU/6.0+.08)
			lows.append(Vector3(k.x,-1,k.y))
			shoulders.append(Vector3(k.x*.81+.05,.50+k.y*.21,k.y*.79))
			rim.append(Vector3(k.x*.49+.13,.88+k.y*.15,k.y*.39))
		for j in 6:
			var ne:=(j+1)%6;var f:Vector3=(lows[j]+lows[ne])*0.5;var n:=Vector3(f.x,.08,f.z).normalized()
			_quad(st,lows[j],lows[ne],shoulders[ne],shoulders[j],Color("#a7a5a5").lightened((j%3)*.077),n)
			_quad(st,shoulders[j],shoulders[ne],rim[ne],rim[j],Color("#e2d6b0"),(n+Vector3.UP*.61).normalized())
			_tri(st,rim[j],rim[ne],Vector3(.13,.97,0),Color("#ebd9a7"),Vector3.UP)
		m=st.commit()
	elif kind=="blades":
		var st:=_begin()
		for j in 6:
			var t:=Vector2.from_angle(j*2.41);var b:=Vector3(t.x*.44,0,t.y*.44)
			var left:=Vector3(-t.y,0,t.x)*.26;var joint:=b+Vector3(t.x*.21,.55,t.y*.21);var tip:=b+Vector3(t.x*.60,1.08-(j%3)*.14,t.y*.60)
			_tri(st,b-left,b+left,joint,Color("#789657"),Vector3(t.x,.48,t.y).normalized())
			_tri(st,joint,b+left,tip,Color("#c7d397"),Vector3(t.x,.65,t.y).normalized())
		m=st.commit()
	elif kind in ["cloud","crown","petal"]:
		var s:=SphereMesh.new();s.radial_segments=16;s.rings=8;s.radius=1;s.height=2;m=s
	else:
		var c:=CylinderMesh.new();c.radial_segments=6 if kind=="crag" else 5;c.rings=1;c.height=2
		c.top_radius={"crag":0.61,"trunk":0.72,"isle":1.0}.get(kind,0.03)
		c.bottom_radius=0.58 if kind=="isle" else 1.0;m=c
	var mat:=StandardMaterial3D.new();mat.vertex_color_use_as_albedo=true;mat.vertex_color_is_srgb=true;mat.roughness=1;mat.cull_mode=BaseMaterial3D.CULL_DISABLED
	if kind=="cloud":
		var soft:=ShaderMaterial.new();soft.shader=preload("res://art/cloud.gdshader")
		m.material=soft;meshes[kind]=m;return m
	if m is ArrayMesh:m.surface_set_material(0,mat)
	else:m.material=mat
	meshes[kind]=m
	return m

func _add(kind:String,p:Vector3,s:Vector3,col:Color,yaw:float)->void:
	if not batches.has(kind):batches[kind]=[]
	var t:=Transform3D(Basis(Vector3.UP,yaw).scaled_local(s),p)
	batches[kind].append([t,col])

func _commit_batches(parent:Node3D)->void:
	for key in batches:
		var arr:Array=batches[key]
		var mm:=MultiMesh.new()
		mm.transform_format=MultiMesh.TRANSFORM_3D;mm.use_colors=true;mm.mesh=_primitive(key);mm.instance_count=arr.size()
		for i in arr.size(): mm.set_instance_transform(i,arr[i][0]);mm.set_instance_color(i,arr[i][1])
		var obj:=MultiMeshInstance3D.new();obj.multimesh=mm;parent.add_child(obj)
	batches.clear()

func _decor()->void:
	prop_parent=Node3D.new();add_child(prop_parent)
	var original:="res://art/backdrops/"+course+".glb"
	var has_backdrop:=ResourceLoader.exists(original)
	if has_backdrop:
		var scenery:Node3D=(load(original) as PackedScene).instantiate()
		prop_parent.add_child(scenery)
		var shades:Dictionary={}
		for mesh in scenery.find_children("*","MeshInstance3D",true,false):
			var n:String=mesh.name
			if not n.begins_with("decor_"):continue
			var kind:="leaf" if n.contains("leaves") else "stone"
			if n.contains("flowers"):continue
			if not shades.has(kind):
				var old=mesh.get_active_material(0)
				if not old is StandardMaterial3D:continue
				var m:StandardMaterial3D=old.duplicate()
				m.albedo_color=Color.WHITE if SKIES.has(course) else (Color("#829d7b") if kind=="leaf" else Color("#9ca7a5"))
				shades[kind]=m
			mesh.material_override=shades[kind]
			if kind=="stone" and not n.contains("trunk"):
				mesh.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	# Leave the play surface clear; outlying islands and cloud banks give altitude and depth cues.
	for i in (0 if has_backdrop else 13):
		var angle:=i*TAU/13.0
		var p:=Vector3(width_m*(0.5+0.82*cos(angle)),-2.7-_rnd(i,7)*1.4,depth_m*(0.5+0.91*sin(angle)))
		var size:=1.30+_rnd(i,12)*1.30
		_add("isle",p,Vector3(size,1.5,size*0.74),Color(colors[1]).darkened(0.06),i*0.4)
		_add("crown",p+Vector3.UP*1.45,Vector3(size,0.27,size*0.78),Color(colors[4]),i*0.4)
		for j in 2:
			var t:=p+Vector3((j-0.5)*1.15,1.6,_rnd(i,j)*0.48)
			_add("trunk",t+Vector3.UP*0.32,Vector3(0.12,0.46,0.14),Color("986f55"),i)
			var shade:=Color(colors[5]).darkened(0.18).lerp(Color(colors[4]).darkened(0.08),_rnd(i,j))
			_add("crown",t+Vector3.UP*1.08,Vector3(0.52,0.69+_rnd(i,j)*0.32,0.52),shade,i)
			_add("crown",t+Vector3(-.35,0.92,.04),Vector3(.34,.40,.36),shade.darkened(.09),i)
	var backdrop_clouds:=has_backdrop and prop_parent.find_child("clouds",true,false)!=null
	for i in (12 if backdrop_clouds else 24):
		var p:=Vector3((_rnd(i,29)*2.6-0.8)*width_m,-7.3-_rnd(i,31)*1.8,(_rnd(i,30)*2.3-0.65)*depth_m)
		for j in 3:
			_add("cloud",p+Vector3(j*0.85,_rnd(i,j)*0.3,j*0.12),Vector3(1.35,0.52+0.24*_rnd(j,i),0.9),Color("#d7e7ef"),i+j)
	_commit_batches(prop_parent)

func _process(_dt:float)->void:
	if is_instance_valid(warning_mesh): warning_mat.albedo_color=Color(1.0,0.55,0.16,0.19+0.12*sin(Time.get_ticks_msec()*0.012))
