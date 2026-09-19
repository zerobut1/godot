extends SceneTree
## Run with the Yutrel renderer and the sandbox project; requires a real Vulkan device.

const CLEAR_COLOR := Color(0.15, 0.05, 0.25, 1.0)
var failed := false


func _initialize() -> void:
	_run.call_deferred()


func _check_frame(label: String, expected_size := Vector2i.ZERO) -> void:
	for frame in range(3):
		await RenderingServer.frame_post_draw
	var image := root.get_texture().get_image()
	if image == null or image.is_empty():
		push_error("Yutrel verification: no viewport image at " + label)
		failed = true
		return
	if expected_size != Vector2i.ZERO and image.get_size() != expected_size:
		push_error("Yutrel verification: unexpected size at %s: %s" % [label, image.get_size()])
		failed = true
	for point in [Vector2i.ZERO, image.get_size() / 2, image.get_size() - Vector2i.ONE]:
		var actual := image.get_pixelv(point)
		if absf(actual.r - CLEAR_COLOR.r) > 0.01 or absf(actual.g - CLEAR_COLOR.g) > 0.01 or absf(actual.b - CLEAR_COLOR.b) > 0.01 or absf(actual.a - 1.0) > 0.01:
			push_error("Yutrel verification: unexpected color at %s/%s: %s" % [label, point, actual])
			failed = true
	print("Yutrel verification: %s, size=%s, center=%s" % [label, image.get_size(), image.get_pixelv(image.get_size() / 2)])
	if label == "initial" and not OS.get_cmdline_user_args().is_empty():
		var error := image.save_png(OS.get_cmdline_user_args()[0].path_join("clear.png"))
		if error != OK:
			push_error("Yutrel verification: failed to save capture")
			failed = true


func _run() -> void:
	if RenderingServer.get_current_rendering_method() != "yutrel" or RenderingServer.get_current_rendering_driver_name() != "vulkan":
		push_error("Yutrel verification requires yutrel + vulkan")
		quit(1)
		return
	if change_scene_to_file("res://main.tscn") != OK:
		quit(1)
		return
	await scene_changed
	root.content_scale_size = Vector2i.ZERO
	await _check_frame("initial")
	for size in [Vector2i(640, 360), Vector2i(1200, 720), Vector2i(960, 540)]:
		root.size = size
		await _check_frame("resize", size)
	var mesh := MeshInstance3D.new()
	mesh.mesh = BoxMesh.new()
	current_scene.add_child(mesh)
	await _check_frame("unsupported mesh")
	mesh.queue_free()
	await _check_frame("mesh removed")
	for iteration in range(3):
		if reload_current_scene() != OK:
			failed = true
			break
		await scene_changed
		await _check_frame("scene reload %d" % iteration)
	print("Yutrel verification: " + ("FAIL" if failed else "PASS"))
	quit(1 if failed else 0)
