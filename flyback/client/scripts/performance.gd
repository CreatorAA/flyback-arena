extends Node3D
## Render-time interpretation of an already-authorized seat. No world or bridge access.
## u is measured in 20-Hz rules steps; pose.py is the reference for the curves.
var actor
var rest:Dictionary={}
var faces:Dictionary={}
var head:Node3D
var hair:Node3D
var root_scale:=Vector3.ONE
var phase:=0.0
var clock:=0.0
var live_state:=0
var state_age:=0.0
var run:=0.0
var last_dir:=Vector3.FORWARD
var last_motion:=0.0
var death_kind:=""
var death_dir:=Vector3.FORWARD
var puffed:=false
var celebrated:=false
var victory_time:=0.0
var victory_yaw:=0.0
var spawn_stamp:=-1.0
var held:Node3D
var fx:Array=[]
var bead:SphereMesh
var chip:BoxMesh
var sleeves:Dictionary={}

func bind(a)->void:
	actor=a;root_scale=actor.figure.scale
	head=a.figure.find_child("head",true,false);hair=a.figure.find_child("hairback",true,false)
	for part in [a.arm_a,a.arm_b,a.foot_a,a.foot_b,head,hair]:
		if is_instance_valid(part):rest[part]=part.transform
	for arm in [a.arm_a,a.arm_b]:
		if not arm:continue
		var sleeve:=MeshInstance3D.new();var segment:=CylinderMesh.new();segment.height=1.;segment.top_radius=.041;segment.bottom_radius=.044;segment.radial_segments=8;sleeve.mesh=segment
		var color:=Color("#daba91")
		if arm is MeshInstance3D and arm.mesh and arm.mesh.get_surface_count()>0:
			var colors=arm.mesh.surface_get_arrays(0)[Mesh.ARRAY_COLOR]
			if colors!=null and colors.size()>0:color=colors[0].linear_to_srgb()
		sleeve.material_override=a._mat(color);arm.get_parent().add_child(sleeve);sleeve.hide();sleeves[arm]=sleeve
	for k in ["open","tight","happy"]:
		var f=a.figure.find_child("face_"+k,true,false)
		if f:faces[k]=f
	bead=SphereMesh.new();bead.radius=1;bead.height=2;bead.radial_segments=7;bead.rings=3
	chip=BoxMesh.new();chip.size=Vector3(.085,.018,.13)
	# Visible in the hand before release. Its parent's joint supplies the wind-up.
	held=Node3D.new()
	if a.arm_a:a.arm_a.add_child(held);held.position=Vector3(.035,-.10,.10)
	else:a.figure.add_child(held);held.position=Vector3(.28,.48,.12)
	var path:="res://art/darts/flight_s%d.glb"%int(a.seat)
	if ResourceLoader.exists(path):
		var prop:Node3D=(load(path) as PackedScene).instantiate();held.add_child(prop);prop.scale=Vector3.ONE*.49;prop.rotation_degrees.x=67;return
	for sign_ in [-1,1]:
		var piece=MeshInstance3D.new();var blade=BoxMesh.new();blade.size=Vector3(.053,.043,.17);piece.mesh=blade
		piece.position=Vector3(sign_*.048,0,0);piece.rotation.y=sign_*.67
		piece.material_override=a._mat(a.SEATS[a.seat%4].lightened(.2));held.add_child(piece)

func change_face(key:String)->void:
	for k in faces:faces[k].visible=k==key

func joint(part:Node3D,offset:Vector3,euler:Vector3,w:float)->void:
	if not part:return
	var base:Transform3D=rest[part]
	part.position=part.position.lerp(base.origin+offset,w)
	var wish:=base.basis*Basis.from_euler(euler)
	part.quaternion=part.quaternion.slerp(wish.get_rotation_quaternion(),w)
	if sleeves.has(part):
		var v:=part.position-base.origin;var sleeve:MeshInstance3D=sleeves[part]
		sleeve.visible=v.length()>.046
		if sleeve.visible:sleeve.transform=Transform3D(Basis(Quaternion(Vector3.UP,v.normalized())).scaled_local(Vector3(1,v.length()+.024,1)),(part.position+base.origin)*.5)

func observed(p:Dictionary,first:bool)->void:
	if not p.get("alive",false):return
	var st:=int(p.get("st",0));var v:=Vector3(float(p.get("vx",0)),0,float(p.get("vy",0)))*.001
	if v.length()>.005:last_dir=v.normalized();last_motion=v.length()
	if live_state!=st:state_age=0
	live_state=st;death_kind="";puffed=false
	if first:phase=0

func hide_evidence()->void:
	for item in fx:if is_instance_valid(item.n):item.n.free()
	fx.clear();death_kind="";puffed=false;celebrated=false

func death(cause:int,direction:Vector3,ground:bool,kicked:bool)->void:
	if not actor.shown or actor.dead_age<0:return
	if death_kind!="" and cause<1:return
	death_dir=direction.normalized() if direction.length_squared()>.0001 else last_dir
	if cause==1:death_kind="dart"
	elif cause==2:death_kind="slash"
	elif cause==3:death_kind="collapse" if ground else ("kickfall" if kicked or live_state==8 else "walkoff")
	else:death_kind="stagger"
	change_face("tight");held.hide()

func cheer(yaw:float)->void:
	if not actor.shown or actor.dead_age>=0:return
	celebrated=true;victory_time=0;victory_yaw=yaw
	change_face("happy")

func particles(where:Vector3,count:int,confetti:bool,color:Color,radius:float)->void:
	for j in count:
		var zz:=fposmod(j*.6180339+.22,1.0);var angle:float=j*2.39996+actor.seat*.9
		var n:=MeshInstance3D.new();n.mesh=chip if confetti else bead;n.cast_shadow=GeometryInstance3D.SHADOW_CASTING_SETTING_OFF;n.position=where
		var col:Color=[Color("#ffcd63"),Color("#45d5be"),Color("#fa835e"),Color("#eff7ec"),Color("#9cc4ff")][j%5] if confetti else color.lightened(zz*.12)
		n.material_override=actor._mat(col);add_child(n)
		var spread:=radius*(.42+.58*zz)
		var height:=fposmod(float(j*j+7)*.3173,1.)
		var velocity:=Vector3(cos(angle)*spread,1.6+height*3.2 if confetti else .4+.75*zz,sin(angle)*spread)
		var life:=1.1+zz*.65 if confetti else .38+zz*.28
		fx.append({"n":n,"origin":where,"velocity":velocity,"t":0.,"life":life,"confetti":confetti,"angle":angle,"z":zz})

func _advance_fx(dt:float)->void:
	for i in range(fx.size()-1,-1,-1):
		var f:Dictionary=fx[i];f.t+=dt
		var t:float=f.t;var u:=float(t/f.life);var n:MeshInstance3D=f.n
		if u>=1:n.free();fx.remove_at(i);continue
		n.position=f.origin+f.velocity*t+Vector3.DOWN*(3.2 if f.confetti else .32)*t*t
		if f.confetti:
			n.position.x+=.12*sin(t*9+f.angle);n.rotation=Vector3(t*7+f.angle,t*6,t*9);n.scale=Vector3.ONE*minf(1,(1-u)*4)
		else:
			var s:float=(.09+.14*f.z)*sin(PI*sqrt(u));n.scale=Vector3(s,s*.84,s)

func render(dt:float)->void:
	if not actor.shown:return
	clock+=dt*20;state_age+=dt*20;_advance_fx(dt)
	if actor.dead_age>=0:_dead(dt);return
	var a=actor
	var speed:=clampf(Vector2(float(a.latest.get("vx",0)),float(a.latest.get("vy",0))).length()/250.,0,1.2)
	run=lerpf(run,speed,1-exp(-dt*18));phase+=run*dt*5*PI/.8
	var s:=sin(phase);var c:=cos(phase);var st:=int(a.latest.get("st",0));var tm:=float(a.latest.get("timer",0))
	var ax:=s*.62*run;var bx:=-ax;var kick_a:=-s*.67*run;var kick_b:=-kick_a
	var arms_z:=.04;var leg_a:=Vector3(0,maxf(0,c)*.07*run,0);var leg_b:=Vector3(0,maxf(0,-c)*.07*run,0)
	var arm_a:=Vector3.ZERO;var arm_b:=Vector3.ZERO;var twist:=0.;var lean:=.13*run;var roll:=s*.045*run
	var lift:=.037*absf(s)*run;var squash:=1.+.012*sin(clock*.42+a.seat)*maxf(0.,1-run)
	if st==1:ax=-1.6;bx=.65;arm_a.y=.14;arm_a.z=-.14;twist=-.6;squash=.92;lean=-.17
	elif st==2:ax=.95;bx=-.7;arm_a.z=.18;twist=.62;lean=.29
	elif st==3:ax=-.95;arm_a.x=.16;twist=-.75;lean=-.11
	elif st==4:ax=-.25;arm_a.x=-.18;arm_a.z=.14;twist=.84*clampf(tm/5,0,1);lean=.25
	elif st==5:kick_a=-.95;leg_a.y=.12;lean=-.22;ax=.84;bx=-.77
	elif st==6:kick_a=-1.65*minf(1,tm*.47);leg_a=Vector3(.01,.17,.22*minf(1,tm*.6));lean=-.31;arms_z=.40
	elif st==7:squash=.80 if tm>6 else 1.07;lean=.52 if tm<=6 and tm>2 else -.15;ax=-1.14;bx=-1.14
	elif st==8:lean=-.60;roll=.12*sin(clock*1.8);kick_a=-.8;ax=-.95;bx=-1.32;arms_z=.37;lift=.06
	elif st==9:arms_z=.83;ax=-.68;bx=-.68;squash=.91;lean=-.13
	if celebrated:
		var prev_time:=victory_time;victory_time+=dt
		var u:=maxf(0.,victory_time*20.-4.);var jp:=fmod(u,9.)/9.;var jump:=sin(PI*jp)
		var land:=exp(-jp*9.)+exp(-(1.-jp)*9.);var blend:=smoothstep(0.,3.,u)
		lift=lerpf(lift,.44*jump,blend);squash=lerpf(squash,1+.12*jump-.18*land,blend);lean=lerpf(lean,-.07,blend)
		ax=lerpf(ax,-.24,blend);bx=lerpf(bx,-.24,blend);arms_z=lerpf(arms_z,2.13+.21*jump,blend)
		arm_a=Vector3(.115,.12,.13)*blend;arm_b=Vector3(-.115,.12,.14)*blend;kick_a=-.34*jump;kick_b=-.29*jump
		a.target_yaw=victory_yaw
		if prev_time<.52 and victory_time>=.52:particles(Vector3(0,1.25,0),34,true,Color.WHITE,2.25)
		if prev_time<1.22 and victory_time>=1.22:particles(Vector3(0,.99,0),16,true,Color.WHITE,1.65)
	var w:=1-exp(-dt*28)
	if dt<=0:return
	a.figure.rotation.y=lerp_angle(a.figure.rotation.y,a.target_yaw+twist,1-exp(-dt*19))
	a.figure.rotation.x=lerpf(a.figure.rotation.x,lean,w);a.figure.rotation.z=lerpf(a.figure.rotation.z,roll,w)
	a.figure.position=Vector3(0,lift if celebrated else lerpf(a.figure.position.y,lift,w),0)
	a.figure.scale=root_scale*Vector3(1+(1-squash)*.44,squash,1+(1-squash)*.44)
	joint(a.arm_a,arm_a,Vector3(ax,0,arms_z),w);joint(a.arm_b,arm_b,Vector3(bx,0,-arms_z),w)
	joint(a.foot_a,leg_a,Vector3(kick_a,0,0),w);joint(a.foot_b,leg_b,Vector3(kick_b,0,0),w)
	joint(head,Vector3.ZERO,Vector3(-.10*run,-twist*.32,s*.042),w);joint(hair,Vector3.ZERO,Vector3(.16*run,0,s*.04),w)
	if not celebrated:change_face("tight" if st in [3,5,7,8] else "open")

func _dead(dt:float)->void:
	if death_kind=="":death(-1,last_dir,false,false)
	var a=actor;a.dead_age+=dt
	var u:float=a.dead_age*20;var fall:=death_kind in ["walkoff","kickfall","collapse"]
	var ofs:=Vector3.ZERO;var angle:=0.;var sc:=1.;var y:=0.
	if fall:
		var hang:=6. if death_kind=="walkoff" else (.4 if death_kind=="kickfall" else 0.)
		var f:=maxf(0.,u-hang);y=-.025*f*f-(.05*f if death_kind=="collapse" else 0.)
		var drift:=3.2 if death_kind=="kickfall" else 2.2
		ofs=death_dir*maxf(.12,last_motion)*drift*(1-exp(-u/3.))
		sc=1-.55*clampf(-y/12.,0.,1.)
		angle=sin(u*3.4)*.32 if u<hang else (-.72-.15*u-.004*u*u if death_kind=="kickfall" else (.34+.19*f if death_kind=="walkoff" else .04*sin(u)))
		var squash:=1.-.28*exp(-u/1.6) if death_kind=="collapse" else 1.
		a.figure.scale=root_scale*Vector3(sc,sc*squash,sc)
		if not puffed and y< -3.1:puffed=true;particles(ofs+Vector3.DOWN*2.55,19,false,Color("#cedfeb"),1.75)
		a.figure.visible=y> -9.
	else:
		var dist:=2.4*(1-exp(-u/4.5));ofs=death_dir*dist;y=maxf(0.,.40*u-.026*u*u);angle=-.59*u if death_kind!="dart" else -.26*u
		if death_kind=="dart":ofs+=Vector3(-death_dir.z,0,death_dir.x)*.23*sin(u*.4);angle+=.6
		sc=1.-smoothstep(8.3,12.,u);a.figure.scale=root_scale*maxf(sc,.001);a.figure.visible=u<12.
		if not puffed and u>=8.3:puffed=true;particles(ofs+Vector3.UP*y,16,false,Color("#e4c29c"),1.6)
	a.figure.position=ofs+Vector3.UP*y;a.figure.rotation=Vector3(angle,atan2(death_dir.x,death_dir.z),.19*sin(u*.7))
	var wind:=sin(u*1.08);var w:=1-exp(-dt*29)
	joint(a.arm_a,Vector3(0,.18,0),Vector3(-.74+wind*.66,0,-.95),w);joint(a.arm_b,Vector3(0,.18,0),Vector3(-.72-wind*.66,0,.96),w)
	joint(a.foot_a,Vector3(0,.06,0),Vector3(wind*.79,0,0),w);joint(a.foot_b,Vector3(0,.10,0),Vector3(-wind*.8,0,0),w)
	if not a.figure.visible and fx.is_empty():a.hide()
