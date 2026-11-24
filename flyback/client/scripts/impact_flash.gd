extends Node3D
## A short, local contact mark. No camera shake or simulation hit-stop.
var ring:MeshInstance3D
var sparks:Array[MeshInstance3D]=[]
var ink:=StandardMaterial3D.new()
var age:=-1.
var metal:=false
var kick:=false
var duration:=.16

func _ready()->void:
	ink.shading_mode=BaseMaterial3D.SHADING_MODE_UNSHADED
	var rim:=TorusMesh.new();rim.inner_radius=.28;rim.outer_radius=.33;rim.rings=20;rim.ring_segments=4
	ring=MeshInstance3D.new();ring.mesh=rim;ring.material_override=ink
	ring.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;add_child(ring)
	var shard:=PrismMesh.new();shard.size=Vector3(.068,.27,.065)
	for i in 4:
		var ray:=MeshInstance3D.new();ray.mesh=shard;ray.material_override=ink
		ray.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;add_child(ray);sparks.append(ray)
	hide()

func fire(point:Vector3,kind:String)->void:
	age=0.;metal=kind in ["block","clash"];kick=kind=="kick"
	duration=.15 if metal else .19
	ink.albedo_color=Color("#d5fffc") if metal else (Color("#ffbb72") if kick else Color("#fff0aa"))
	position=point
	rotation.y=.42 if metal else -.2
	show();advance(0.)

func advance(dt:float)->void:
	if age<0.:return
	age+=dt
	if age>=duration:hide();age=-1.;return
	var t:=age/duration
	var fade:=1.-t
	var radius:=.62+1.1*(1.-fade*fade)
	ring.position.y=.04
	ring.scale=Vector3(radius,maxf(.12,fade),radius)
	for i in sparks.size():
		var a:=float(i)*TAU/sparks.size()+.23
		var axis:=Vector3(cos(a),.6+float(i%2)*.5,sin(a)).normalized()
		var s:=sparks[i];var size:=maxf(.025,fade)
		s.position=axis*(.12+(.60 if metal else .38)*t)+Vector3.UP*.14
		s.quaternion=Quaternion(Vector3.UP,axis);s.scale=Vector3(size,size*(1.5 if metal else .86),size)
