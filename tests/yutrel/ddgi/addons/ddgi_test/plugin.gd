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


# Measure the center of a single irradiance sphere against the black backing screen.
func sphere_center(image: Image, region: Rect2i) -> Vector2:
	var total := Vector2.ZERO
	var count := 0
	for y in range(region.position.y, region.end.y):
		for x in range(region.position.x, region.end.x):
			if image.get_pixel(x, y).r > 0.15:
				total += Vector2(x, y)
				count += 1
	check(count > 0, "Relocated debug sphere is visible")
	return total / maxi(count, 1)


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
		push_error("DDGI editor viewport is unavailable")
		get_tree().quit(1)
		return
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	# Exclude editor grid, axes and gizmos from the pixel probes.
	var editor_layers := camera.cull_mask
	camera.cull_mask = (1 << 20) - 1
	var room: Node3D = scene.get_node("Room")
	room.global_transform = Transform3D(camera.global_basis, camera.global_position - camera.global_basis.z * 6)
	var volume: YutrelDDGIVolume = scene.get_node("Volume")
	volume.global_position = room.to_global(Vector3(0, 0, 1))
	check(volume.get_gizmos().size() > 0, "The editor installs a volume gizmo")
	var popup := menu.get_popup()
	var images: Array[Image] = []
	for id in [0, 11, 12]:
		var index := popup.get_item_index(id)
		check(index >= 0, "Missing viewport mode %d" % id)
		if index < 0:
			break
		popup.id_pressed.emit(id)
		for i in 16:
			RenderingServer.force_draw()
			await get_tree().process_frame
		var image := await snapshot(view)
		check(not image.is_empty(), "Viewport readback for mode %d" % id)
		if id != 0:
			check(popup.get_item_text(index) == ("DDGI Indirect" if id == 11 else "DDGI Probes"), "DDGI menu label")
		if id == 0 or id == 11:
			var color := image.get_pixelv(Vector2i(camera.unproject_position(room.to_global(Vector3(1.25, 0, 0)))))
			print("EDITOR RED RECEIVER mode=%d color=%s" % [id, color])
			check(color.r > 0.02 and color.r > 1.5 * maxf(color.g, color.b), "Red indirect reaches white receiver")
		for previous in images:
			check(previous.get_data() != image.get_data(), "Distinct viewport mode %d" % id)
		images.append(image)
	# Place a black screen behind a known white-sky probe, then in front of the entire grid.
	room.hide()
	var sky: YutrelEnvironment = scene.get_node("Sky")
	sky.sky_color = Color.WHITE
	sky.sky_intensity = 1
	volume.size = Vector3(2, 2, 2)
	volume.probe_counts = Vector3i(3, 3, 3)
	volume.max_ray_distance = 0.1
	volume.probe_classification = false
	volume.brightness_threshold = 1.0
	volume.hysteresis = 0.0
	var mesh := QuadMesh.new()
	mesh.size = Vector2(10, 10)
	var black := StandardMaterial3D.new()
	black.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	black.albedo_color = Color.BLACK
	mesh.material = black
	var screen := MeshInstance3D.new()
	screen.mesh = mesh
	scene.add_child(screen)
	screen.global_transform = Transform3D(camera.global_basis, volume.global_position - camera.global_basis.z * 2)
	var center := Vector2i(camera.unproject_position(volume.global_position))
	popup.id_pressed.emit(0)
	check((await snapshot(view)).get_pixelv(center).r < 0.01, "Final sees the black screen")
	popup.id_pressed.emit(12)
	check((await snapshot(view)).get_pixelv(center).r > 0.95, "Probe sphere overlays the black screen with decoded irradiance")
	volume.probe_classification = true
	var inactive := (await snapshot(view)).get_pixelv(center)
	print("EDITOR INACTIVE PROBE ", inactive)
	check(inactive.r > 0.35 and inactive.r < 0.8 and inactive.b > inactive.g and inactive.g < inactive.r * 0.4, "Empty probe draws in muted crimson")
	screen.global_position = volume.global_position + camera.global_basis.z * 2
	check((await snapshot(view)).get_pixelv(center).r < 0.01, "Scene depth occludes probe spheres behind the screen")

	# Track the center sphere as it escapes the box's right face, then returns to its grid point.
	screen.global_position = volume.global_position - camera.global_basis.z * 0.55
	volume.max_ray_distance = 100
	volume.probe_classification = false
	volume.probe_preview_radius = 0.075
	volume.size = Vector3(6, 4, 3)
	var pixel_scale := camera.unproject_position(volume.global_position + camera.global_basis.x).distance_to(center)
	var half_width := ceili(0.4 * pixel_scale)
	var region := Rect2i(center - Vector2i.ONE * half_width, Vector2i.ONE * (2 * half_width + 1))
	var start := sphere_center(await snapshot(view), region)
	var block_mesh := BoxMesh.new()
	block_mesh.size = Vector3(0.74, 1, 1)
	block_mesh.material = black
	var block := MeshInstance3D.new()
	block.mesh = block_mesh
	scene.add_child(block)
	block.global_transform = Transform3D(camera.global_basis, volume.global_position - camera.global_basis.x * 0.3)
	var moved := sphere_center(await snapshot(view), region)
	block.global_position -= camera.global_basis.x * 8
	var restored := sphere_center(await snapshot(view), region)
	print("EDITOR RELOCATION shift=%.6f m return_error=%.6f m" % [(moved.x - start.x) / pixel_scale, restored.distance_to(start) / pixel_scale])
	check(moved.x - start.x > 0.1 * pixel_scale, "Probe sphere moves outside the overlapping box")
	check(restored.distance_to(start) < 1.5, "Probe sphere returns to its original pixel position when the box leaves")

	screen.hide()
	camera.cull_mask = editor_layers
	popup.id_pressed.emit(1)
	var with_wire := await snapshot(view)
	for gizmo: EditorNode3DGizmo in volume.get_gizmos():
		gizmo.set_hidden(true)
	var without_wire := await snapshot(view)
	check(with_wire.get_data() != without_wire.get_data(), "The volume wireframe draws into the editor viewport")
	print("DDGI EDITOR PASS" if failures == 0 else "DDGI EDITOR FAIL")
	get_tree().quit(failures)
