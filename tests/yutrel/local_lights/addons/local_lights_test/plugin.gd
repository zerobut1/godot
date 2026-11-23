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


func capture(view: Viewport) -> Image:
	for i in 3:
		await get_tree().process_frame
		RenderingServer.force_draw()
	return view.get_texture().get_image()


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
		push_error("Local light editor viewport unavailable")
		get_tree().quit(1)
		return

	# Adding a hidden sun switches off the editor's preview sunlight.
	var hidden_sun := DirectionalLight3D.new()
	hidden_sun.hide()
	scene.add_child(hidden_sun)
	EditorInterface.get_selection().clear()
	var camera := view.get_camera_3d()
	camera.cull_mask = (1 << 20) - 1
	var rig: Node3D = scene.get_node("Rig")
	rig.global_transform = Transform3D(camera.global_basis, camera.global_position - camera.global_basis.z * 4)
	var center := Vector2i(camera.unproject_position(rig.global_position))
	var wall: MeshInstance3D = rig.get_node("Receiver")
	var omni: OmniLight3D = rig.get_node("Omni")
	var popup := menu.get_popup()
	var direct := await capture(view)
	check(direct.get_pixelv(center).r > 0.2 and direct.get_pixelv(center).b < 0.01, "Editor renders the colored local light")
	popup.id_pressed.emit(10)
	var reference := (await capture(view)).get_pixelv(center)
	check(absf(reference.r - direct.get_pixelv(center).r) < 0.025, "Path tracer and deferred diffuse agree at the receiver center")

	var emission := StandardMaterial3D.new()
	emission.emission_enabled = true
	emission.emission = Color.BLUE
	wall.material_override = emission
	popup.id_pressed.emit(6)
	var lighting := await capture(view)
	check(lighting.get_pixelv(center).r > 0.2 and lighting.get_pixelv(center).b < 0.01, "Lighting-only clears base emission before the first local draw")
	var mirror := StandardMaterial3D.new()
	mirror.metallic = 1
	mirror.roughness = 0.02
	wall.material_override = mirror
	var panel := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(5, 5)
	panel.mesh = quad
	panel.rotation.y = PI
	panel.position.z = 9
	rig.add_child(panel)
	omni.position.z = 8
	omni.omni_range = 1.7
	omni.light_energy = 1.5
	omni.light_color = Color.RED
	var sky: YutrelEnvironment = scene.get_node("Environment")
	sky.rt_reflection_enabled = true
	sky.rt_reflection_half_resolution = false
	popup.id_pressed.emit(0)
	var reflected := await capture(view)
	popup.id_pressed.emit(10)
	var traced := (await capture(view)).get_pixelv(center)
	check(camera.is_position_behind(omni.global_position) and traced.r > 0.2 and traced.g < 0.01
			and absf(traced.r - reflected.get_pixelv(center).r) < 0.07, "Reference path tracer and RT mirror agree on off-screen local lighting")
	omni.light_color = Color.GREEN
	var changed := (await capture(view)).get_pixelv(center)
	check(changed.g > 0.2 and changed.r < 0.01, "Changing only the local light resets path accumulation")

	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(direct.get_data())
	digest.update(lighting.get_data())
	digest.update(reflected.get_data())
	print("LOCAL_LIGHTS IMAGE ", digest.finish().hex_encode())
	print("LOCAL_LIGHTS EDITOR PASS" if failures == 0 else "LOCAL_LIGHTS EDITOR FAIL")
	get_tree().quit(failures)
