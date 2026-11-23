@tool
extends EditorPlugin

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


func run() -> void:
	for i in 35:
		await get_tree().process_frame
	EditorInterface.open_scene_from_path("res://editor_scene.tscn")
	EditorInterface.set_main_screen_editor("3D")
	await get_tree().process_frame
	var scene := EditorInterface.get_edited_scene_root()
	var view := EditorInterface.get_editor_viewport_3d()
	var menu := find_menu(view.get_parent().get_parent())
	if not menu or not scene:
		push_error("RT reflection editor viewport is unavailable")
		get_tree().quit(1)
		return
	var popup := menu.get_popup()
	var index := popup.get_item_index(14)
	check(index >= 0 and popup.get_item_text(index) == "RT Reflection", "Mode 14 selects RT Reflection")
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	camera.cull_mask = (1 << 20) - 1
	var rig: Node3D = scene.get_node("Rig")
	rig.global_transform = Transform3D(camera.global_basis, camera.global_position - camera.global_basis.z * 4)
	var sky: YutrelEnvironment = scene.get_node("Environment")
	var center := Vector2i(camera.unproject_position(rig.global_position))
	popup.id_pressed.emit(14)
	for i in 4:
		await get_tree().process_frame
	RenderingServer.force_draw()
	var mirror := view.get_texture().get_image()
	var hdr_color := mirror.get_pixelv(center)
	# The panel has radiance 3, so mode 14 should display 3 / (1 + 3).
	var expected := 0.75 if view.use_hdr_2d else Color(0.75, 0, 0).linear_to_srgb().r
	check(popup.is_item_checked(index) and absf(hdr_color.r - expected) < 0.01 and hdr_color.g < 0.01, "Debug mode tonemaps the off-screen red emitter")
	sky.rt_reflection_enabled = false
	RenderingServer.force_draw()
	check(view.get_texture().get_image().get_pixelv(center).r < 0.001, "Disabled debug view is black")
	sky.rt_reflection_enabled = true
	rig.get_node("Wall").mesh.material.roughness = 0.85
	RenderingServer.force_draw()
	check(view.get_texture().get_image().get_pixelv(center).r < 0.001, "Untraced rough pixels have zero debug coverage")
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(mirror.get_data())
	print("RT_REFLECTION IMAGE ", digest.finish().hex_encode())
	print("RT_REFLECTION EDITOR PASS" if failures == 0 else "RT_REFLECTION EDITOR FAIL")
	get_tree().quit(failures)
