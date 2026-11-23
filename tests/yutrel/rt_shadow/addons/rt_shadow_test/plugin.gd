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
		push_error("RT shadow editor viewport is unavailable")
		get_tree().quit(1)
		return
	var popup := menu.get_popup()
	var mask_index := popup.get_item_index(8)
	check(popup.get_item_text(mask_index) == "Shadow Mask", "Shadow Mask is mode 8")
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	camera.cull_mask = (1 << 20) - 1
	var rig: Node3D = scene.get_node("Rig")
	rig.global_transform = Transform3D(camera.global_basis, camera.global_position - camera.global_basis.z * 10)
	var sun: DirectionalLight3D = rig.get_node("Sun")
	var environment: YutrelEnvironment = scene.get_node("Environment")
	var dark := Vector2i(camera.unproject_position(rig.global_position))
	var open := Vector2i(camera.unproject_position(rig.to_global(Vector3(0, 1.5, 0))))
	popup.id_pressed.emit(0)
	RenderingServer.force_draw()
	popup.id_pressed.emit(8)
	sun.light_angular_distance = 0
	for i in 4:
		await get_tree().process_frame
	RenderingServer.force_draw()
	var hard := view.get_texture().get_image()
	check(popup.is_item_checked(mask_index) and hard.get_pixelv(dark).r < 0.01 and hard.get_pixelv(open).r > 0.99,
			"The editor shows the hard RT shadow mask")
	environment.shadow_mode = YutrelEnvironment.SHADOW_MODE_CSM
	RenderingServer.force_draw()
	check(view.get_texture().get_image().get_pixelv(dark).r < 0.05, "CSM supplies the same shadow mask view")
	environment.shadow_mode = YutrelEnvironment.SHADOW_MODE_RAY_TRACED
	popup.id_pressed.emit(9)
	RenderingServer.force_draw()
	var cascade := view.get_texture().get_image().get_pixelv(open)
	check(maxf(cascade.r, maxf(cascade.g, cascade.b)) - minf(cascade.r, minf(cascade.g, cascade.b)) > 0.05,
			"Cascade debug still uses CSM in RT mode")
	popup.id_pressed.emit(8)
	sun.light_angular_distance = 2
	# Exactly one forced soft frame gives the same history age across editor launches.
	RenderingServer.force_draw()
	var soft := view.get_texture().get_image()
	check(soft.get_data() != hard.get_data() and soft.get_pixelv(dark).r < 0.01 and soft.get_pixelv(open).r > 0.99, "Editor soft shadow mask")
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(soft.get_data())
	print("RT_SHADOW IMAGE ", digest.finish().hex_encode())
	print("RT_SHADOW EDITOR PASS" if failures == 0 else "RT_SHADOW EDITOR FAIL")
	get_tree().quit(failures)
