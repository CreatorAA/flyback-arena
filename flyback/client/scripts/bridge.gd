extends Node

signal packet(data: Dictionary)
signal failure(message: String)
var pipe: FileAccess
var errors: FileAccess
var pid := -1
var buf := PackedByteArray()
var outgoing: Array[PackedByteArray] = []

func boot() -> void:
	var file := OS.get_environment("FLYBACK_BRIDGE")
	if file.is_empty():
		var base := OS.get_environment("FLYBACK_HOME")
		if base.is_empty(): base = OS.get_executable_path().get_base_dir()
		file = base.path_join("flyback-bridge" + (".exe" if OS.get_name() == "Windows" else ""))
	if not FileAccess.file_exists(file):
		failure.emit("找不到比赛服务。请从飞返竞技场启动器打开，或设置 FLYBACK_BRIDGE。")
		return
	var io := OS.execute_with_pipe(file, PackedStringArray(["client"]), false)
	if io.is_empty():
		failure.emit("无法启动比赛服务：" + file)
		return
	pipe = io.stdio
	errors = io.stderr
	pid = int(io.pid)

func send(data: Dictionary) -> void:
	if pipe == null:
		failure.emit("比赛服务未连接。")
		return
	outgoing.append((JSON.stringify(data) + "\n").to_utf8_buffer())
	_flush()

func _flush() -> void:
	while not outgoing.is_empty() and pipe != null:
		pipe.store_buffer(outgoing[0])
		if pipe.get_error() != OK: break
		outgoing.pop_front()

func _process(_dt: float) -> void:
	if pipe == null: return
	_flush()
	# Windows named pipes and POSIX pipes both expose available bytes via get_length().
	for i in 8:
		var n := mini(pipe.get_length(), 262144)
		if n <= 0: break
		buf.append_array(pipe.get_buffer(n))
	while true:
		var end := buf.find(10)
		if end < 0: break
		var s := buf.slice(0,end).get_string_from_utf8()
		buf = buf.slice(end+1)
		var v = JSON.parse_string(s)
		if typeof(v) == TYPE_DICTIONARY: packet.emit(v)
	if errors != null and errors.get_length() > 0:
		push_warning(errors.get_buffer(mini(errors.get_length(),65536)).get_string_from_utf8())
	if not OS.is_process_running(pid):
		pipe = null
		errors = null
		failure.emit("比赛服务已退出，请重新启动游戏。")

func _exit_tree() -> void:
	if pipe != null:
		pipe.close()
		pipe = null
	if errors != null: errors.close()
