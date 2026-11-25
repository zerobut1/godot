@tool
extends EditorPlugin

signal memory_ready(bytes: Vector2i)

var failures := 0


func _enter_tree() -> void:
	run.call_deferred()


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func find_menu(node: Node) -> MenuButton:
	if node is MenuButton and node.text == "Yutrel":
		return node
	for child in node.get_children(true):
		var menu := find_menu(child)
		if menu:
			return menu
	return null


func snapshot(view: Viewport) -> Image:
	for i in 4:
		RenderingServer.force_draw()
		await get_tree().process_frame
	return view.get_texture().get_image()


func read_memory() -> void:
	var rd := RenderingServer.get_rendering_device()
	memory_ready.emit.call_deferred(Vector2i(rd.get_memory_usage(RenderingDevice.MEMORY_TEXTURES), rd.get_memory_usage(RenderingDevice.MEMORY_BUFFERS)))


func memory_usage() -> Vector2i:
	RenderingServer.call_on_render_thread(read_memory)
	return await memory_ready


func run() -> void:
	for i in 35:
		await get_tree().process_frame
	EditorInterface.open_scene_from_path("res://editor_scene.tscn")
	EditorInterface.set_main_screen_editor("3D")
	await get_tree().process_frame
	var view := EditorInterface.get_editor_viewport_3d()
	var scene := EditorInterface.get_edited_scene_root()
	var menu := find_menu(view.get_parent().get_parent())
	if not menu or not scene:
		push_error("Postprocess editor viewport unavailable")
		get_tree().quit(1)
		return
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	camera.cull_mask = (1 << 20) - 1
	var box: MeshInstance3D = scene.get_node("Box")
	box.global_transform = Transform3D(camera.global_basis, camera.global_position - camera.global_basis.z * 4)
	var env: YutrelEnvironment = scene.get_node("Environment")
	var popup := menu.get_popup()
	var sample_pos := Vector2i(view.size.x / 12, view.size.y / 12)
	var before := (await snapshot(view)).get_pixelv(sample_pos)
	env.grading_temperature = -1
	env.vignette_intensity = 1
	env.vignette_smoothness = 1
	var after := (await snapshot(view)).get_pixelv(sample_pos)
	check(before.r - after.r > 0.03, "Live grading and vignette darken the editor sky")
	box.material_override = StandardMaterial3D.new()
	for mode in [10, 6]:
		popup.id_pressed.emit(mode)
		check(popup.is_item_checked(popup.get_item_index(mode)), "Selected diagnostic mode %d" % mode)
		var diagnostic_point := view.size / 2 if mode == 6 else sample_pos
		var ref := (await snapshot(view)).get_pixelv(diagnostic_point)
		check(ref.v > 0.01, "Diagnostic sample has measurable radiance")
		env.bloom_enabled = not env.bloom_enabled
		env.auto_exposure_enabled = not env.auto_exposure_enabled
		env.grading_temperature *= -1.0
		env.vignette_intensity = 1.0 - env.vignette_intensity
		var changed := (await snapshot(view)).get_pixelv(diagnostic_point)
		check(ref.is_equal_approx(changed), "Independent diagnostic output in mode %d: %s vs %s" % [mode, ref, changed])
	popup.id_pressed.emit(0)
	env.auto_exposure_enabled = true
	env.bloom_enabled = true
	await snapshot(view)
	var active_memory := await memory_usage()
	env.auto_exposure_enabled = false
	env.bloom_enabled = false
	await snapshot(view)
	await snapshot(view)
	var idle_memory := await memory_usage()
	var half_size := view.size / 2
	check(active_memory.x - idle_memory.x >= half_size.x * half_size.y * 8, "Disabling effects releases the bloom pyramid")
	check(active_memory.y - idle_memory.y >= 1024, "Disabling metering releases the histogram SSBO")
	# Exercise the imported PNG (CompressedTexture2D) through the same output
	# path as the runtime ImageTexture. Intermediate channels expose double gamma.
	env.grading_temperature = 0
	env.vignette_intensity = 0
	box.hide()
	env.sky_color = Color(0.82, 0.44, 0.23)
	env.sky_intensity = 1
	env.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	var original := (await snapshot(view)).get_pixelv(sample_pos)
	env.grading_lut = load("res://swap_rb.png")
	check(env.grading_lut is CompressedTexture2D, "Editor imports strip PNG as a texture")
	var imported := await snapshot(view)
	var swapped := imported.get_pixelv(sample_pos)
	check(absf(swapped.r - original.b) < 0.008 and absf(swapped.g - original.g) < 0.008 and absf(swapped.b - original.r) < 0.008,
			"Imported PNG swaps intermediate channels: %s -> %s" % [original, swapped])
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(imported.get_data())
	print("POSTPROCESS IMAGE ", hash.finish().hex_encode())
	print("POSTPROCESS EDITOR PASS" if failures == 0 else "POSTPROCESS EDITOR FAIL")
	get_tree().quit(failures)
