extends Control
## The referee chooses the digit. Only the sweep and the dismissal are animated here.
const S=preload("res://scripts/style.gd")
const CREAM=Color("#fff8da")
const MINT=Color("#9af4ca")
var phase:=-1
var age:=0.
var digit:Label
var location:Label
var caption:Label

func _ready()->void:
	name="StartSignal"
	mouse_filter=Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_CENTER_TOP)
	offset_left=-111;offset_right=111;offset_top=96;offset_bottom=174
	pivot_offset=Vector2(111,39)
	digit=S.text("",44,CREAM,true);add_child(digit);digit.position=Vector2(8,-1);digit.size=Vector2(69,78)
	digit.horizontal_alignment=HORIZONTAL_ALIGNMENT_CENTER;digit.vertical_alignment=VERTICAL_ALIGNMENT_CENTER
	location=S.text("",14,MINT);add_child(location);location.position=Vector2(88,13)
	caption=S.text("",24,CREAM,true);add_child(caption);caption.position=Vector2(87,31)
	hide();set_process(false)

func show_phase(value:int,course:String)->void:
	if value==phase:return
	var from:=phase
	phase=value;age=0.
	if phase<0 or (phase==0 and from<1):
		hide();set_process(false);return
	location.text=course;caption.text="开战" if phase==0 else "准备开赛"
	digit.text="" if phase==0 else str(phase)
	show();modulate.a=1.;set_process(true);queue_redraw()

func _process(dt:float)->void:
	age+=dt
	if phase==0:
		modulate.a=1.-smoothstep(.32,.64,age)
		if age>=.64:hide();set_process(false);return
	queue_redraw()

func _draw()->void:
	var plate:=Rect2(Vector2.ZERO,size)
	draw_rect(Rect2(Vector2(0,3),size),Color("#122a2559"))
	draw_rect(plate,Color("#193d35f7"))
	draw_rect(Rect2(0,0,4,size.y),MINT if phase==0 else Color("#f5bd67"))
	var center:=Vector2(42,39)
	draw_arc(center,27,-PI*.5,TAU-PI*.5,56,Color("#426454"),2.5,true)
	var fraction:=1.-clampf(age,0.,1.)
	if fraction>.001 or phase==0:
		draw_arc(center,27,-PI*.5,TAU*(1. if phase==0 else fraction)-PI*.5,56,MINT if phase==0 else CREAM,2.5,true)
	if phase==0:
		# A swept dart replaces the digit at the exact instant that controls unlock.
		draw_colored_polygon(PackedVector2Array([center+Vector2(-9,-11),center+Vector2(13,0),center+Vector2(-9,11),center+Vector2(-2,0)]),MINT)
	for j in 3:
		draw_rect(Rect2(size.x-27,17+j*8,9,3),MINT if phase==0 or j<phase else Color("#4e6b5f"))
