extends Node
## One-shots only: streams contain edited recordings, never procedural oscillators.
## Screen-space stereo matches the overhead camera; the caller supplies only a
## position already disclosed to this POV (never an inferred attacker position).
const FOLDER := "res://art/combat_audio/"
const VARIANTS := {"release":2, "swing":2, "hit":2, "kick":2, "clash":2, "block":2, "catch":2, "dash":2, "warning":1, "fall":1}
const BUS := &"ArenaFX"
const VOICE_COUNT := 6
var bank:Dictionary={}
var voices:Array[AudioStreamPlayer2D]=[]
var priorities:Array[int]=[]
var turns:Dictionary={}
var level:=1.0
var muted:=false
var same_step:Dictionary={}
signal cue_played(cue:String, seat:int, tick:int, relative_x:float)

func _ready()->void:
	for key in VARIANTS:
		var variants:Array[AudioStream]=[]
		for i in int(VARIANTS[key]):
			var path:=FOLDER+str(key)+"_%02d.wav"%(i+1)
			var sample:=load(path) as AudioStream
			if sample!=null:variants.append(sample)
		bank[key]=variants
	for i in VOICE_COUNT:
		var p:=AudioStreamPlayer2D.new()
		p.bus=BUS;p.max_distance=10000.;p.attenuation=0.;p.panning_strength=0.82
		add_child(p);voices.append(p);priorities.append(-1)

func set_level(value:float, silence:bool)->void:
	level=clampf(value,0.,1.);muted=silence
	var bus:=AudioServer.get_bus_index(BUS)
	AudioServer.set_bus_mute(bus,silence or level<=0.)
	AudioServer.set_bus_volume_db(bus,linear_to_db(maxf(.0001,level)))
	if silence or level<=0.:stop_voices()

func stop_voices()->void:
	for v in voices:v.stop()

func reset()->void:
	stop_voices();turns.clear();same_step.clear()

func play_cue(cue:String,seat:int,tick:int,relative_x:float,local:bool,tempo:float)->void:
	if muted or level<=0. or tempo<=0.:return
	var variants:Array=bank[cue]
	if variants.is_empty():return
	# The same target may receive HIT and CUT on a single rule step. Keep one
	# body contact; callers already de-duplicated retransmitted event ordinals.
	var family:="contact" if cue in ["hit","kick"] else cue
	var key:=family+":"+str(seat)
	if same_step.get(key,-1)==tick:return
	same_step[key]=tick
	var priority:=(3 if family in ["contact","block","clash"] else (2 if cue in ["catch","warning","fall"] else 1))+(1 if local else 0)
	var slot:=-1
	for i in voices.size():
		if not voices[i].playing:slot=i;break
		if priorities[i]<priority and (slot<0 or priorities[i]<priorities[slot]):slot=i
	if slot<0:return
	var v:=voices[slot];var count:=int(turns.get(cue,0))
	turns[cue]=count+1;priorities[slot]=priority
	v.stop();v.stream=variants[count%variants.size()]
	# The 2D mix splits a mono transient between the two channels. Preserve its
	# attack at center; one's own actions stay slightly in front of other seats.
	v.volume_db=1.5 if local else -1.5
	v.pitch_scale=clampf(pow(tempo,.22),.86,1.16)
	# AudioStreamPlayer2D's listener is the viewport center (no Camera2D).
	var size:=get_viewport().get_visible_rect().size
	v.position=Vector2(size.x*(.5+.45*clampf(relative_x,-1.,1.)),size.y*.5)
	v.play()
	cue_played.emit(cue,seat,tick,relative_x)
