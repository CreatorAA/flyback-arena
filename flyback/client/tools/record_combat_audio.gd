extends SceneTree
## Feed actual snapshots captured from a native DART/FBAR replay at 20 Hz. With
## --write-movie and --fixed-fps 30 Godot records its own rendered frames AND mix.
var app
var tape:Dictionary
var frames:Array
var elapsed:=0.
var index:=0
var released:Dictionary={}
var contact_ticks:Dictionary={}
var ended:=false
var silent_check:=false
func _initialize()->void:
	var a:=OS.get_cmdline_user_args()
	if a.size()!=1:quit(2);return
	tape=JSON.parse_string(FileAccess.get_file_as_string(a[0]));frames=tape.frames
	app=load("res://main.tscn").instantiate();root.add_child(app)
	call_deferred("_begin")

func _begin()->void:
	var room:={"type":"room","mode":"replay","state":"playing","round_epoch":1,"speed":1,"self_seat":-1}
	app._packet(room)
	var m:Dictionary=tape.manifest.duplicate();m.type="manifest";app._packet(m)
	app.arena.combat_audio.set_level(.85,false)
	app.arena.combat_audio.cue_played.connect(func(cue,seat,tick,_pan):
		released[cue]=int(released.get(cue,0))+1
		if cue in ["hit","clash","block"]:contact_ticks["%s:%d:%d"%[cue,seat,tick]]=true)
	silent_check=true

func _process(dt:float)->bool:
	if not silent_check:return false
	var target:=int(elapsed*20.+.00001)
	while index<frames.size() and index<=target:
		var f:Dictionary=frames[index].duplicate();f.type="snapshot";f.round_epoch=1;app._packet(f)
		# Redundant legal packets have identical IDs. Never double their audio.
		var n:int=released.values().reduce(func(x,y):return x+y,0)
		app.arena.play_events(f.events,1)
		if released.values().reduce(func(x,y):return x+y,0)!=n:push_error("duplicate combat audio");quit(3);return true
		index+=1
	if index>=frames.size() and not ended:
		var r:Dictionary=tape.result.duplicate();r.type="result";app._packet(r);ended=true
	if elapsed>(frames.size()/20.)+1.75:
		print("real replay audio: ",released," contacts ",contact_ticks)
		if int(tape.manifest.seed)==11012 and not (released.get("release",0)>=4 and released.get("hit",0)==1 and released.get("clash",0)==1 and released.get("block",0)==1 and released.get("catch",0)>=2):
			push_error("unexpected real match audio");quit(4);return true
		quit(0);return true
	elapsed+=dt
	return false
