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


func panel(scene: Node3D, camera: Camera3D, pixel: Vector2, depth: float, width: float) -> MeshInstance3D:
	var item := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = Vector2.ONE * width
	item.mesh = mesh
	scene.add_child(item)
	item.global_transform = Transform3D(camera.global_basis, camera.project_position(pixel, depth))
	return item


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
		push_error("Transparent editor viewport is unavailable")
		get_tree().quit(1)
		return
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	# Exclude editor grid, axes and gizmos from the pixel probes.
	camera.cull_mask = (1 << 20) - 1
	var pixel := Vector2i(view.get_visible_rect().size * 0.5)
	var wall := panel(scene, camera, pixel, 6.0, 2.0)
	var mat := StandardMaterial3D.new()
	mat.albedo_color = Color.BLUE
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	wall.material_override = mat
	var decal := panel(scene, camera, pixel, 4.0, 1.0)
	var overlay := mat.duplicate() as StandardMaterial3D
	overlay.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	overlay.albedo_color = Color(1, 0, 0, 0.35)
	decal.material_override = overlay

	# Probe the opaque-only GBuffer and depth debug views, then the composited views.
	for mode in [1, 2, 3, 4, 5, 7, 6, 0]:
		menu.get_popup().id_pressed.emit(mode)
		decal.hide()
		var reference := (await snapshot(view)).get_pixelv(pixel)
		decal.show()
		var result := (await snapshot(view)).get_pixelv(pixel)
		if mode in [0, 6]:
			check(result.r > reference.r + 0.05, "Transparent draw contributes to view %d" % mode)
		else:
			check(result == reference, "Transparent draw preserves opaque debug view %d" % mode)
	print("TRANSPARENT EDITOR PASS" if failures == 0 else "TRANSPARENT EDITOR FAIL")
	get_tree().quit(failures)
