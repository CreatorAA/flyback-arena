extends RefCounted
const INK=Color("#2e2824")
const SUB=Color("#70695f")
const PAPER=Color("#fffcf6")
const TEAL=Color("#12b886")
const DEEP=Color("#174e42")
const WARM=Color("#f6f0e6")
const LINE=Color("#e4dcce")
const ALERT=Color("#b9562e")

static func fill(c:Control)->void:c.set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
static func box(bg:Color,r:int=12,pad:int=12,border:Color=Color.TRANSPARENT)->StyleBoxFlat:
	var s:=StyleBoxFlat.new();s.bg_color=bg;s.set_corner_radius_all(mini(r,4))
	s.content_margin_left=pad;s.content_margin_right=pad;s.content_margin_top=pad;s.content_margin_bottom=pad
	if border.a>0:s.set_border_width_all(1);s.border_color=border
	return s

static func theme()->Theme:
	var t:=Theme.new()
	var font:=SystemFont.new();font.font_names=PackedStringArray(["Noto Sans SC","Microsoft YaHei UI","Microsoft YaHei","PingFang SC","WenQuanYi Micro Hei","DejaVu Sans"])
	t.default_font=font;t.default_font_size=16
	for k in ["Label","Button","CheckButton","OptionButton","LineEdit","PopupMenu","SpinBox"]:
		t.set_color("font_color",k,INK);t.set_color("font_hover_color",k,DEEP);t.set_color("font_pressed_color",k,DEEP)
		t.set_color("font_focus_color",k,INK);t.set_color("font_disabled_color",k,Color("#9b9387"))
	t.set_stylebox("panel","PopupMenu",box(PAPER,10,8,LINE))
	t.set_stylebox("hover","PopupMenu",box(Color("#e3f7f0"),6,6))
	t.set_constant("v_separation","PopupMenu",12)
	for k in ["Button","OptionButton","LineEdit"]:
		t.set_stylebox("normal",k,box(WARM,3,10,LINE));t.set_stylebox("hover",k,box(Color("#ebf5eb"),3,10,TEAL))
		t.set_stylebox("pressed",k,box(Color("#d2f0dc"),3,10,TEAL));t.set_stylebox("disabled",k,box(Color("#edeae2"),3,10))
		var f:=box(Color.TRANSPARENT,3,0,TEAL);f.draw_center=false;f.set_border_width_all(2);t.set_stylebox("focus",k,f)
	t.set_color("caret_color","LineEdit",DEEP);t.set_color("font_placeholder_color","LineEdit",SUB)
	return t

static func text(s:String,size:int=16,color:Color=INK,bold:bool=false)->Label:
	var l:=Label.new();l.text=s;l.add_theme_font_size_override("font_size",size);l.add_theme_color_override("font_color",color)
	if bold:
		var f:=SystemFont.new();f.font_names=PackedStringArray(["Microsoft YaHei UI","Noto Sans SC","Microsoft YaHei","DejaVu Sans"]);f.font_weight=800;l.add_theme_font_override("font",f)
	l.mouse_filter=Control.MOUSE_FILTER_IGNORE;return l

static func btn(s:String,cb:Callable,green:bool=false)->Button:
	var b:=Button.new();b.text=s;b.custom_minimum_size=Vector2(0,42);b.mouse_default_cursor_shape=Control.CURSOR_POINTING_HAND;b.pressed.connect(cb)
	if green:
		for key in ["normal","hover","pressed"]:
			b.add_theme_stylebox_override(key,box({"normal":DEEP,"hover":Color("#226c57"),"pressed":Color("#143c33")}[key],4,13))
		for key in ["font_color","font_hover_color","font_pressed_color","font_focus_color"]:b.add_theme_color_override(key,Color("#fffdf1"))
	return b

static func panel(bg:Color=PAPER,r:int=18,pad:int=20)->PanelContainer:
	var p:=PanelContainer.new();p.add_theme_stylebox_override("panel",box(bg,r,pad));return p

static func stack(gap:int=10)->VBoxContainer:
	var v:=VBoxContainer.new();v.add_theme_constant_override("separation",gap);return v
static func row(gap:int=8)->HBoxContainer:
	var v:=HBoxContainer.new();v.add_theme_constant_override("separation",gap);return v

static func line(placeholder:String,value:String="")->LineEdit:
	var v:=LineEdit.new();v.placeholder_text=placeholder;v.text=value;v.custom_minimum_size=Vector2(100,39);v.size_flags_horizontal=Control.SIZE_EXPAND_FILL
	return v

static func pick(entries:Array,selected=0)->OptionButton:
	var op:=OptionButton.new();op.custom_minimum_size=Vector2(112,41);op.size_flags_horizontal=Control.SIZE_EXPAND_FILL
	for e in entries:op.add_item(str(e[0]));op.set_item_metadata(op.item_count-1,e[1])
	if int(selected)>=0 and int(selected)<op.item_count:op.select(int(selected))
	return op

static func selected(p:OptionButton):return p.get_item_metadata(p.selected) if p.selected>=0 else null
