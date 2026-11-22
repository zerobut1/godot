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
	var scene := EditorInterface.get_edited_scene_root() as Node3D
	var view := EditorInterface.get_editor_viewport_3d()
	var menu := find_yutrel_menu(view.get_parent().get_parent())
	if not menu or not scene:
		push_error("Path tracer editor viewport is unavailable")
		get_tree().quit(1)
		return
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	# Exclude editor grid, axes and gizmos from the pixel probes.
	camera.cull_mask = (1 << 20) - 1
	var popup := menu.get_popup()
	var images: Array[Image] = []
	for id in [0, 1, 10]:
		var index := popup.get_item_index(id)
		check(index >= 0, "Missing viewport mode %d" % id)
		if index < 0:
			break
		if id == 10:
			check(popup.get_item_text(index) == "Path Tracer", "Path Tracer menu label")
		popup.id_pressed.emit(id)
		check(popup.is_item_checked(index), "Selected viewport mode %d" % id)
		var image := await snapshot(view)
		for previous in images:
			check(previous.get_data() != image.get_data(), "Distinct viewport mode %d" % id)
		images.append(image)
	print("PATHTRACER EDITOR PASS" if failures == 0 else "PATHTRACER EDITOR FAIL")
	get_tree().quit(failures)
