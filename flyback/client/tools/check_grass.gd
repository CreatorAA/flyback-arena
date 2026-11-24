extends SceneTree
## Takes a restricted view emitted by record-grass-view.cpp.
func _initialize()->void:call_deferred("check")
func check()->void:
	var args:=OS.get_cmdline_user_args()
	if args.size()<1:quit(2);return
	var f:=FileAccess.open(args[0],FileAccess.READ)
	var data:Dictionary=JSON.parse_string(f.get_as_text())
	var arena=load("res://scripts/arena.gd").new();root.add_child(arena)
	arena.mount(data.manifest,int(data.view));var last={};var hidden:=0;var independent:=0;var reappear:=0;var cells:=0;var phases_seen={}
	for frame in data.frames:
		arena.update(frame)
		for p in frame.players:
			var i:=int(p.seat);var dart:Dictionary=frame.darts[i];var a=arena.cast[i]
			if not p.vis:
				if p.has("x") or p.has("y") or a.is_visible_in_tree():push_error("unobserved enemy or its shadow exposed");quit(3);return
				hidden+=1
				if dart.vis and int(dart.st)==3:
					if not arena.darts[i].is_visible_in_tree() or not arena.dart_markers[i].is_visible_in_tree():push_error("visible ground dart lost with hidden owner");quit(4);return
					independent+=1
			elif not last.get(i,true):
				var legal:=Vector3(float(p.x)*.001,0,float(p.y)*.001)
				if a.position.distance_to(legal)>.001:push_error("reappearing enemy flew across the brush");quit(5);return
				reappear+=1
			last[i]=p.vis
		var fallen:=int(frame.fallen)
		if not phases_seen.has(fallen):
			phases_seen[fallen]=true
			for wave in arena.island.grass_by_wave:
				for cell in arena.island.grass_by_wave[wave]:
					if cell.visible!=(int(wave)<0 or int(wave)>=fallen):push_error("grass survived collapsed tile");quit(6);return
					cells+=1
	print(data.match,": restricted seat 0; hidden=",hidden," independently visible ground darts=",independent," reappear resets=",reappear," keyed grass cells=",cells," collapse phases=",phases_seen.keys())
	arena.queue_free()
	await process_frame
	await process_frame
	quit(0 if hidden>0 and independent>0 and reappear>0 and cells>0 and phases_seen.size()>1 else 7)
