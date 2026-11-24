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
	var view := EditorInterface.get_editor_viewport_3d()
	var scene := EditorInterface.get_edited_scene_root()
	var menu := find_menu(view.get_parent().get_parent())
	if not menu or not scene:
		push_error("Upscaler editor viewport is unavailable")
		get_tree().quit(1)
		return
	var popup := menu.get_popup()
	var mv_index := popup.get_item_index(22)
	check(mv_index >= 0 and popup.get_item_text(mv_index) == "Motion Vectors", "Mode 22 exposes Motion Vectors in Yutrel menu")
	var camera := view.get_camera_3d()
	var rig: Node3D = scene.get_node("Rig")
	var opaque: MeshInstance3D = rig.get_node("Opaque")
	var glass: MeshInstance3D = rig.get_node("Glass")
	rig.global_transform = Transform3D(camera.global_basis, camera.global_position - camera.global_basis.z * 4)
	EditorInterface.get_selection().add_node(opaque)
	for i in 12:
		await get_tree().process_frame
		RenderingServer.force_draw()
	var image := view.get_texture().get_image()
	check(not image.is_empty() and image.get_size() == view.size, "Editor Quality FSR 2 produces full display-size output")
	var opaque_px := Vector2i(camera.unproject_position(opaque.global_position))
	var glass_px := Vector2i(camera.unproject_position(glass.global_position))
	if Rect2i(Vector2i.ZERO, image.get_size()).has_point(opaque_px) and Rect2i(Vector2i.ZERO, image.get_size()).has_point(glass_px):
		var rc := image.get_pixelv(opaque_px)
		var gc := image.get_pixelv(glass_px)
		check(is_finite(rc.r) and is_finite(rc.g) and is_finite(rc.b) and rc.r > 0.3, "Editor Quality FSR 2 renders opaque mesh cleanly")
		check(is_finite(gc.r) and is_finite(gc.g) and is_finite(gc.b) and gc.g > gc.r, "Editor Quality FSR 2 renders transparent mesh cleanly")
	else:
		check(false, "Projected test meshes fell outside editor viewport")
	# Exercise live Motion Vectors DebugView inside the Editor viewport.
	EditorInterface.get_selection().clear()
	var saved_cull := camera.cull_mask
	camera.cull_mask = (1 << 20) - 1
	popup.id_pressed.emit(22)
	for i in 3:
		await get_tree().process_frame
		RenderingServer.force_draw()
	var mv_still := view.get_texture().get_image()
	var still_c := mv_still.get_pixelv(opaque_px)
	print("UPSCALER EDITOR MV still=", still_c)
	check(still_c.v < 0.01, "Stationary Motion Vectors view is black in editor (%s)" % still_c)
	opaque.position += Vector3(0.25, 0.0, 0.0)
	opaque.force_update_transform()
	var moved_px := Vector2i(camera.unproject_position(opaque.global_position))
	RenderingServer.force_draw()
	var mv_moved := view.get_texture().get_image()
	var moved_c := mv_moved.get_pixelv(moved_px)
	print("UPSCALER EDITOR MV moved=", moved_c)
	check(moved_c.v > 0.05, "Moving mesh produces visible Motion Vectors in editor (%s)" % moved_c)
	camera.cull_mask = saved_cull
	popup.id_pressed.emit(0)
	await get_tree().process_frame
	RenderingServer.force_draw()
	print("UPSCALER EDITOR PASS" if failures == 0 else "UPSCALER EDITOR FAIL")
	get_tree().quit(failures)
