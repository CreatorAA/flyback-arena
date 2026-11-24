extends PanelContainer
## The same opening positions and collision cells used by the referee.
const S=preload("res://scripts/style.gd")
const TIPS={
	"twinlakes":"绕湖包抄，或抢住两湖之间的窄地。",
	"yinyang":"草地藏身，石地绕障；冲刺能越过弯沟。",
	"icefloes":"沿冰桥稳走，或冲过冰缝截击。",
	"ruins":"内外墙缺口错开，绕墙才能逼近塔心。",
	"stonecircle":"六块立石能挡镖，盯住石间的缺口。",
	"skybridge":"正桥直通圆台，侧桥断口要靠冲刺跨越。",
	"trident":"三条栈道汇入中心，从侧道绕开迎面飞镖。",
	"beacon":"沿外环游走，或从径向小路切进中心。",
	"tidelock":"横桥适合截击，沿外堤能绕到对手侧面。",
	"kiln":"窑壁能挡镖，绕过弧墙再抢中央空场。",
	"windrose":"从弯曲的岬角切入，沿环道转向另一翼。",
	"moonwell":"避开井心，贴内圈追击或沿外侧草丛绕行。"
}
const PALETTES={
	"icefloes":[Color("#bddcde"),Color("#6ba4b4")],
	"tidelock":[Color("#abcfb4"),Color("#538b79")],
	"kiln":[Color("#dfac89"),Color("#a46c58")],
	"windrose":[Color("#d5cd8e"),Color("#929458")],
	"moonwell":[Color("#c5bfd7"),Color("#8c83ad")]
}
static var atlas:Dictionary={}
var diagram:=Diagram.new()
var heading:=S.text("",23,S.INK,true)
var tip:=S.text("",15,S.SUB)
var dock:Control
var inline:Control
var map_id:=""
var seats:=0

func attach(layer:Control,form:VBoxContainer)->void:
	if atlas.is_empty():
		var file:=FileAccess.open("res://art/map_atlas.json",FileAccess.READ)
		if file!=null:
			var content=JSON.parse_string(file.get_as_text())
			if content is Dictionary:atlas=content
	name="MapPreview";hide()
	mouse_filter=Control.MOUSE_FILTER_IGNORE
	add_theme_stylebox_override("panel",S.box(S.PAPER,3,14,S.LINE))
	var stack:=S.stack(7);add_child(stack)
	var row:=S.row();stack.add_child(row)
	heading.size_flags_horizontal=Control.SIZE_EXPAND_FILL;row.add_child(heading)
	row.add_child(S.text("战术图",13,S.DEEP,true))
	stack.add_child(diagram)
	tip.autowrap_mode=TextServer.AUTOWRAP_WORD_SMART;stack.add_child(tip)
	dock=layer;inline=S.stack(0);form.add_child(inline);dock.add_child(self)
	dock.resized.connect(_layout)
	_layout()

func _layout()->void:
	if not is_inside_tree():return
	var compact:=dock.size.x<835
	inline.visible=compact and not map_id.is_empty()
	var host:=inline if compact else dock
	if get_parent()!=host:reparent(host,false)
	diagram.custom_minimum_size.y=92 if compact else 150
	if not compact:
		set_anchors_and_offsets_preset(Control.PRESET_BOTTOM_RIGHT)
		offset_left=-338;offset_right=-24;offset_top=-258;offset_bottom=-20

func show_map(id:String,title:String,count:int)->void:
	map_id=id if atlas.has(id) else ""
	seats=count
	visible=not map_id.is_empty()
	inline.visible=visible and dock.size.x<835
	if not visible:return
	heading.text=title;tip.text=TIPS.get(id,"")
	diagram.course=atlas[id];diagram.seats=count
	diagram.palette=PALETTES.get(id,[Color("#c2cf9f"),Color("#7c9b69")])
	diagram.queue_redraw()

class Diagram extends Control:
	var course:Dictionary={}
	var seats:=0
	var palette:Array=[]
	const INK=Color("#223b3c")
	const GRID=Color("#2c4746")
	const WALL=Color("#4a5656")
	const MARK=Color("#fff9e9")
	func _init()->void:
		mouse_filter=Control.MOUSE_FILTER_IGNORE
		clip_contents=true
	func _draw()->void:
		draw_rect(Rect2(Vector2.ZERO,size),INK)
		for x in range(0,int(size.x),14):draw_line(Vector2(x,0),Vector2(x,size.y),GRID,1)
		for y in range(0,int(size.y),14):draw_line(Vector2(0,y),Vector2(size.x,y),GRID,1)
		if course.is_empty() or palette.is_empty():return
		var rows:Array=course.grid
		var extent:=Vector2(rows[0].length(),rows.size())
		var k:=minf((size.x-24.)/extent.x,(size.y-10.)/extent.y)
		var origin:=((size-extent*k)*.5).round()
		for y in rows.size():
			var line:String=rows[y]
			var x:=0
			while x<line.length():
				var ch:=line[x];var end:=x+1
				while end<line.length() and line[end]==ch:end+=1
				if ch!="~":
					var color:Color=palette[1] if ch=='"' else (WALL if ch=="#" else palette[0])
					# Snap both sides instead of rounding widths: no subpixel seams.
					var a:=(origin+Vector2(x,y)*k).round()
					var b:=(origin+Vector2(end,y+1)*k).round()
					draw_rect(Rect2(a,b-a),color)
				x=end
		for point in course.spawns.get(str(seats),[]):
			var p:=origin+Vector2(point[0],point[1])*k
			_pin(p)
		_pin(Vector2(13,size.y-13))
		draw_string(get_theme_default_font(),Vector2(22,size.y-8),"出生点",HORIZONTAL_ALIGNMENT_LEFT,-1,13,MARK)
	func _pin(p:Vector2)->void:
		draw_circle(p,4.5,INK,true,-1,true)
		draw_circle(p,3.3,MARK,true,-1,true)
		draw_circle(p,1.3,INK,true,-1,true)
