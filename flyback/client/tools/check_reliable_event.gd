extends SceneTree
## Reorders a genuine, seat-restricted DEATH ahead of its matching snapshot,
## drops its redundant copy, then delivers the reliable packet again.
func _initialize()->void:call_deferred("check")
func check()->void:
	var data:Dictionary=JSON.parse_string(FileAccess.get_file_as_string(OS.get_cmdline_user_args()[0]))
	var app=load("res://main.tscn").instantiate();root.add_child(app)
	var m:Dictionary=data.manifest.duplicate();m.type="manifest";app.room={"self_seat":data.view,"round_epoch":1,"state":"playing","mode":"offline"};app._packet(m)
	for raw in data.frames:
		var f:Dictionary=raw.duplicate(true);f.type="snapshot";f.round_epoch=1
		for e in f.events:
			if e.type!="DEATH":continue
			var packet:={"type":"event","round_epoch":1,"event":e}
			var id:="1:%d:%d"%[int(e.tick),int(e.ordinal)]
			app._packet(packet)
			if app.arena.event_ids.has(id) or not app.pending_events.has(id):push_error("premature reliable dispatch");quit(2);return
			f.events.erase(e);app._packet(f)
			if not app.arena.event_ids.has(id) or app.pending_events.has(id):push_error("missing reliable presentation");quit(3);return
			var count:int=app.arena.event_ids.size();app._packet(packet)
			if app.arena.event_ids.size()!=count or not app.pending_events.is_empty():push_error("not idempotent");quit(4);return
			print("reordered restricted DEATH ",id,": deferred while alive, consumed at snapshot ",f.tick,", duplicate ignored")
			app.queue_free();await process_frame;await process_frame;quit();return
		app._packet(f)
	quit(5)
