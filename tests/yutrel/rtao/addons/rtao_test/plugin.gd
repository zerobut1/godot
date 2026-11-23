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
	if not menu or not scene or menu.get_popup().get_item_index(13) < 0:
		push_error("The editor viewport is missing the RTAO debug mode")
		get_tree().quit(1)
		return
	var popup := menu.get_popup()
	var index := popup.get_item_index(13)
	check(popup.get_item_text(index) == "RTAO" and popup.is_item_radio_checkable(index), "RTAO is a radio menu item")
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	camera.cull_mask = (1 << 20) - 1
	var corner: Node3D = scene.get_node("Corner")
	corner.global_transform = Transform3D(camera.global_basis, camera.global_position - camera.global_basis.z * 6)
	var black := StandardMaterial3D.new()
	black.albedo_color = Color.BLACK
	black.metallic_specular = 0
	(corner.get_node("Receiver") as MeshInstance3D).material_override = black
	var sky: YutrelEnvironment = scene.get_node("Sky")
	sky.sky_intensity = 0
	sky.rtao_enabled = false
	var crease := Vector2i(camera.unproject_position(corner.to_global(Vector3(-0.09, 0, 0))))
	var open := Vector2i(camera.unproject_position(corner.to_global(Vector3(-1.5, 0, 0))))
	popup.id_pressed.emit(0)
	for i in 4:
		await get_tree().process_frame
	RenderingServer.force_draw()
	var unlit := view.get_texture().get_image()
	popup.id_pressed.emit(13)
	for i in 3:
		await get_tree().process_frame
	RenderingServer.force_draw()
	var fallback := view.get_texture().get_image()
	print("RTAO EDITOR BASELINE final=%.7f disabled=%.7f" % [unlit.get_pixelv(open).r, fallback.get_pixelv(crease).r])
	check(popup.is_item_checked(index) and unlit.get_pixelv(open).r < 0.02 and fallback.get_pixelv(crease).r > 0.99,
			"The debug view shows the white fallback independently of lighting")
	# One forced draw from a cleared history gives a reproducible full viewport hash
	# even when the editor's idle frame rate differs between launches.
	sky.rtao_enabled = true
	RenderingServer.force_draw()
	var first := view.get_texture().get_image()
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(first.get_data())
	print("RTAO IMAGE ", digest.finish().hex_encode())
	check(first.get_data() != fallback.get_data(), "The hashed debug image includes occlusion")
	for i in 18:
		await get_tree().process_frame
	var image := view.get_texture().get_image()
	var near := image.get_pixelv(crease)
	var far := image.get_pixelv(open)
	print("RTAO EDITOR crease=%.7f open=%.7f" % [near.r, far.r])
	check(near.r < 0.85 and near.r == near.g and near.g == near.b and far.r > 0.98, "A grayscale crease is visible against the open surface")
	print("RTAO EDITOR PASS" if failures == 0 else "RTAO EDITOR FAIL")
	get_tree().quit(failures)
