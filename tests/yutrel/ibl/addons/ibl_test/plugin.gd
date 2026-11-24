@tool
extends EditorPlugin

var failures := 0


func _enter_tree() -> void:
	run.call_deferred()


func check(condition: bool, description: String) -> void:
	if not condition:
		failures += 1
		push_error(description)


func find_yutrel_menu(node: Node) -> MenuButton:
	if node is MenuButton and node.text == "Yutrel":
		return node
	for child in node.get_children(true):
		var menu := find_yutrel_menu(child)
		if menu:
			return menu
	return null


func snapshot(view: Viewport) -> Image:
	for i in 5:
		RenderingServer.force_draw()
		await get_tree().process_frame
	return view.get_texture().get_image()


func run() -> void:
	for i in 35:
		await get_tree().process_frame
	EditorInterface.open_scene_from_path("res://editor_scene.tscn")
	EditorInterface.set_main_screen_editor("3D")
	await get_tree().process_frame
	var scene := EditorInterface.get_edited_scene_root()
	var view := EditorInterface.get_editor_viewport_3d()
	var menu := find_yutrel_menu(view.get_parent().get_parent())
	if not menu or not scene:
		push_error("Yutrel editor scene or viewport menu is missing")
		get_tree().quit(1)
		return
	var popup := menu.get_popup()
	var sphere: MeshInstance3D = scene.get_node("Sphere")
	var selection := EditorInterface.get_selection()
	selection.clear()
	var labels: PackedStringArray = []
	var sections: PackedStringArray = []
	var images_by_id := {}
	var core_images: Array[Image] = []
	for i in popup.item_count:
		if popup.is_item_separator(i):
			sections.append(popup.get_item_text(i))
			continue
		var id := popup.get_item_id(i)
		labels.append(popup.get_item_text(i))
		popup.id_pressed.emit(id)
		check(popup.is_item_checked(i), "Yutrel debug menu changes its selection")
		var image := await snapshot(view)
		check(not image.is_empty(), "Editor debug view renders")
		# Dedicated fixtures check the views that require RT hardware or a volume.
		if id < 10:
			for previous in core_images:
				check(image.get_data() != previous.get_data(), "Editor debug views produce distinct output")
			core_images.append(image)
		images_by_id[id] = image
	check(sections == PackedStringArray(["Output", "G-Buffer", "Lighting & Shadows", "Indirect & RT", "RT Surface"]),
			"Editor debug menu groups views into expected categories")
	check(labels == PackedStringArray([
				"Final", "Path Tracer",
				"Base Color", "World Normal", "Roughness", "Metallic", "Material AO", "Depth",
				"Lighting", "Shadow Mask", "CSM Cascades",
				"RTAO", "RT Reflection", "DDGI Indirect", "DDGI Probes",
				"RT Base Color", "RT Shading Normal", "RT Geometric Normal", "RT Roughness", "RT Metallic", "RT Emissive", "RT Hit & Facing",
			]),
			"Editor debug menus expose all expected views")
	if labels.size() != 22:
		get_tree().quit(1)
		return

	var camera := view.get_camera_3d()
	var center := Vector2i(camera.unproject_position(sphere.global_position))
	var ibl: Image = images_by_id[6]
	var ibl_pixel := ibl.get_pixelv(center)
	check(ibl_pixel.r > 0.2 and ibl_pixel.r > ibl_pixel.g + 0.1, "Lighting view contains IBL with all direct lights disabled")
	var key: DirectionalLight3D = scene.get_node("Key")
	key.rotation = camera.rotation
	key.show()
	popup.id_pressed.emit(6)
	var combined := (await snapshot(view)).get_pixelv(center)
	check(combined.r > ibl_pixel.r + 0.04, "Lighting view combines direct and IBL lighting")
	key.hide()

	for mode in [0, 1]:
		popup.id_pressed.emit(mode)
		var unselected := await snapshot(view)
		selection.add_node(sphere)
		var selected := await snapshot(view)
		check(selected.get_data() != unselected.get_data(), "Selection gizmos render over %s" % popup.get_item_text(popup.get_item_index(mode)))
		selection.clear()
	print("IBL EDITOR PASS" if failures == 0 else "IBL EDITOR FAIL")
	get_tree().quit(failures)
