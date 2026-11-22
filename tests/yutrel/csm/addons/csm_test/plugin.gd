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
		push_error("CSM editor viewport is unavailable")
		get_tree().quit(1)
		return
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	# Exclude editor grid, axes and gizmos from the pixel probes.
	camera.cull_mask = (1 << 20) - 1
	var light: DirectionalLight3D = scene.get_node("Key")
	light.global_basis = camera.global_basis
	var popup := menu.get_popup()
	var mask_id := 8
	var cascade_id := 9
	check(popup.get_item_text(popup.get_item_index(mask_id)) == "Shadow Mask", "Shadow Mask menu entry")
	check(popup.get_item_text(popup.get_item_index(cascade_id)) == "CSM Cascades", "CSM Cascades menu entry")
	# Front-facing receivers at known camera depths sample all four splits and the distance fade.
	var size := Vector2(view.get_visible_rect().size)
	var depths := [2.0, 6.0, 12.0, 20.0, 30.0]
	var pixels: Array[Vector2i] = []
	var receivers: Array[MeshInstance3D] = []
	for i in depths.size():
		var pixel := Vector2i(size * Vector2(0.20 + i * 0.14, 0.38))
		pixels.push_back(pixel)
		receivers.push_back(panel(scene, camera, pixel, depths[i], depths[i] * 0.1))
	var transition_pixel := Vector2i(size * Vector2(0.33, 0.62))
	var fade_pixel := Vector2i(size * Vector2(0.68, 0.62))
	var split := lerpf(camera.near, 28, light.directional_shadow_split_1)
	panel(scene, camera, transition_pixel, lerpf(camera.near, split, 0.95), 0.45)
	panel(scene, camera, fade_pixel, 28 * (1 + light.directional_shadow_fade_start) / 2, 2.0)
	var caster := panel(scene, camera, pixels[2], 10, 0.8)
	caster.global_position = receivers[2].global_position + camera.global_basis.z * 2
	caster.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY

	# A fully transparent single-cascade light makes selection of either light observable.
	var secondary := DirectionalLight3D.new()
	secondary.shadow_enabled = true
	secondary.shadow_opacity = 0
	secondary.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	scene.add_child(secondary)
	secondary.global_basis = camera.global_basis
	popup.id_pressed.emit(cascade_id)
	var cascades := await snapshot(view)
	var a := cascades.get_pixelv(pixels[0])
	var b := cascades.get_pixelv(pixels[1])
	var c := cascades.get_pixelv(pixels[2])
	var d := cascades.get_pixelv(pixels[3])
	var far_color := cascades.get_pixelv(pixels[4])
	check(a.r > a.g + 0.1 and a.r > a.b + 0.1, "First cascade is red")
	check(b.g > b.r + 0.1 and b.g > b.b + 0.1, "Second cascade is green")
	check(c.b > c.r + 0.1 and c.b > c.g + 0.1, "Third cascade is blue")
	check(minf(d.r, d.g) > d.b + 0.1, "Fourth cascade is yellow")
	check(absf(far_color.r - far_color.b) < 0.02, "Cascade tint fades to neutral beyond max distance")
	var transition := cascades.get_pixelv(transition_pixel)
	check(minf(transition.r, transition.g) > transition.b + 0.06, "Split transition interpolates red and green")
	var fade := cascades.get_pixelv(fade_pixel)
	check(fade.r - fade.b < d.r - d.b and fade.r > fade.b + 0.02, "Far cascade gradually loses its tint")
	popup.id_pressed.emit(mask_id)
	var mask := await snapshot(view)
	check(mask.get_pixelv(pixels[2]).r < 0.05 and mask.get_pixelv(pixels[1]).r > 0.95, "Primary shadow mask has lit and shadowed pixels")
	light.shadow_opacity = 0.5
	var half_mask := (await snapshot(view)).get_pixelv(pixels[2])
	var half_value := Color(0.5, 0.5, 0.5).srgb_to_linear().r if view.use_hdr_2d else 0.5
	check(absf(half_mask.r - half_value) < 0.02, "Shadow mask displays per-light opacity")
	light.shadow_enabled = false
	popup.id_pressed.emit(cascade_id)
	var promoted := (await snapshot(view)).get_pixelv(pixels[2])
	check(promoted.r > maxf(promoted.g, promoted.b) + 0.1, "The next shadow-casting light becomes primary")
	secondary.shadow_enabled = false
	popup.id_pressed.emit(mask_id)
	check((await snapshot(view)).get_pixelv(pixels[2]).r > 0.95, "Missing shadow light produces white mask")
	popup.id_pressed.emit(cascade_id)
	check((await snapshot(view)).get_pixelv(pixels[2]).r < 0.05, "Missing shadow light produces neutral cascade view")
	print("CSM EDITOR PASS" if failures == 0 else "CSM EDITOR FAIL")
	get_tree().quit(failures)
