extends SceneTree
## Check map selection against the running bridge catalog. Optional argument: screenshot path.
var app
var running:=false
var deadline:=0
func _initialize()->void:
	app=load("res://main.tscn").instantiate();root.add_child(app)
	deadline=Time.get_ticks_msec()+35000
func _process(_dt:float)->bool:
	if Time.get_ticks_msec()>deadline:push_error("map picker timed out");quit(2);return true
	if not running and app.online and app.catalog.maps.size()>0:
		running=true
		check()
	return false
func pick(node_name:String,meta)->bool:
	var b:=app.main_layer.find_child(node_name,true,false) as OptionButton
	if b==null:return false
	for i in b.item_count:
		if b.get_item_metadata(i)==meta:
			b.select(i);b.item_selected.emit(i);return true
	return false
func check()->void:
	app.Setup.show_setup(app,"local",false)
	var visited:Dictionary={}
	for n in [2,3,4]:
		if not pick("SeatFormat",n):return fail("format "+str(n))
		for course in app.catalog.maps:
			var offered:bool=pick("SelectedMap",course.id)
			var expected:=false
			for count in course.seats:
				if int(count)==n:expected=true
			if offered!=expected:return fail("seat filter "+str(course.id))
			if not offered:continue
			var preview=app.main_layer.find_child("MapPreview",true,false)
			if preview==null or not preview.visible or preview.map_id!=course.id or preview.heading.text!=course.name or preview.tip.text.is_empty():return fail("wrong preview "+course.id)
			var dots:Array=preview.diagram.course.spawns.get(str(n),[])
			if dots.size()!=n or app.scratch_setup.data.maps!=[course.id]:return fail("wrong opening points "+course.id)
			visited[course.id]=true
	if visited.size()!=app.catalog.maps.size():return fail("missing catalog map")
	for policy in ["rotate","random"]:
		if not pick("MapPolicy",policy) or app.main_layer.find_child("MapPreview",true,false)!=null:return fail(policy)
		var pool:Array=app.scratch_setup.data.maps.duplicate()
		var checks:Array=app.main_layer.find_children("*","CheckBox",true,false)
		checks[0].button_pressed=false;checks[0].button_pressed=true
		var first=pool.pop_front();pool.append(first)
		if app.scratch_setup.data.maps!=pool:return fail("pool order: "+policy)
	if not pick("MapPolicy","fixed") or not pick("SelectedMap","kiln"):return fail("restore fixed")
	await process_frame
	await process_frame
	if not OS.get_cmdline_user_args().is_empty():
		await RenderingServer.frame_post_draw
		if root.get_texture().get_image().save_png(OS.get_cmdline_user_args()[0])!=OK:return fail("cannot save screenshot")
	print("MAP PICKER: ",visited.size()," arena diagrams; all seat filters, fixed previews, start points, rotate and random pools passed.")
	quit()
func fail(reason:String)->void:
	push_error(reason);quit(3)
