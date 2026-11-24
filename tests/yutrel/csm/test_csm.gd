extends SceneTree

var view: SubViewport
var camera: Camera3D
var failures := 0


func _initialize() -> void:
	run.call_deferred()


func check(condition: bool, description: String) -> void:
	if not condition:
		failures += 1
		push_error(description)


func snapshot() -> Image:
	for i in 3:
		await process_frame
		await RenderingServer.frame_post_draw
	return view.get_texture().get_image()


func quad(at: Vector3, extent: Vector2) -> MeshInstance3D:
	var item := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = extent
	item.mesh = mesh
	item.material_override = StandardMaterial3D.new()
	item.position = at
	view.add_child(item)
	return item


func sun(color: Color, dx: float, mode: int) -> DirectionalLight3D:
	var light := DirectionalLight3D.new()
	light.light_color = color
	light.shadow_enabled = true
	light.shadow_blur = 0.75
	light.directional_shadow_mode = mode
	light.directional_shadow_max_distance = 35
	light.directional_shadow_pancake_size = 0
	light.directional_shadow_blend_splits = true
	view.add_child(light)
	light.look_at(Vector3(dx, 0, -1), Vector3.UP)
	return light


func sample_receiver(image: Image, x: float, y: float) -> Color:
	var pixel := Vector2i(camera.unproject_position(Vector3(x, y, -10)))
	return image.get_pixelv(pixel)


func run() -> void:
	root.disable_3d = true
	view = SubViewport.new()
	view.size = Vector2i(640, 400)
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)
	camera = Camera3D.new()
	camera.fov = 60
	camera.far = 50
	view.add_child(camera)
	camera.make_current()
	var environment := YutrelEnvironment.new()
	environment.shadow_mode = YutrelEnvironment.SHADOW_MODE_CSM
	environment.shadow_resolution = 1024
	environment.sky_intensity = 0
	environment.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	view.add_child(environment)

	quad(Vector3(0, 0, -10), Vector2(30, 20))
	var opaque := quad(Vector3(-2, 1, -5), Vector2(1.2, 1.2))
	opaque.scale.x = -1
	var behind := quad(Vector3(-15.1, -1.8, 25), Vector2(1.2, 1.2))
	check(camera.is_position_behind(behind.global_position), "Off-camera caster setup")
	var cutout := quad(Vector3(1.7, -1.8, -6), Vector2(2, 2))
	cutout.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
	var alpha := Image.create_empty(32, 32, false, Image.FORMAT_RGBA8)
	alpha.fill(Color.WHITE)
	alpha.fill_rect(Rect2i(10, 10, 12, 12), Color.TRANSPARENT)
	var mat := cutout.material_override as StandardMaterial3D
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	mat.albedo_texture = ImageTexture.create_from_image(alpha)

	var red := sun(Color.RED, 0.36, DirectionalLight3D.SHADOW_PARALLEL_4_SPLITS)
	var red_only := await snapshot()
	var lit := sample_receiver(red_only, 0, -3).r
	check(lit > 0.12, "Receiver is directly lit")
	for x in range(-6, 7):
		for y in [-3.5, -3.0, 3.0, 3.5]:
			check(sample_receiver(red_only, x, y).r > lit * 0.95, "Lit plane remains free of shadow acne")
	check(sample_receiver(red_only, -0.2, 1).r < lit * 0.05, "Mirrored opaque caster shadow")
	check(sample_receiver(red_only, -2.5, -1.8).r < lit * 0.05, "Caster behind camera casts onto visible receiver")
	check(sample_receiver(red_only, -1.8, -1.8).r > lit * 0.95, "Off-camera shadow has a crisp edge")
	check(sample_receiver(red_only, 3.14, -1.8).r > lit * 0.95, "Scissor hole transmits light")
	check(sample_receiver(red_only, 3.85, -1.8).r < lit * 0.05, "Scissor rim blocks light")

	var green := sun(Color.GREEN, -0.36, DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS)
	var combined := await snapshot()
	# Keep both native culling frusta stable when measuring the single-light references.
	green.light_energy = 0
	red_only = await snapshot()
	green.light_energy = 1
	red.light_energy = 0
	var green_only := await snapshot()
	var left := sample_receiver(combined, -3.8, 0.65)
	var right := sample_receiver(combined, -0.2, 1)
	print("CSM samples lit=%s left=%s right=%s red-only=%s green-only=%s" % [lit, left, right, sample_receiver(red_only, -3.8, 0.65), sample_receiver(green_only, -0.2, 1)])
	check(left.r > lit * 0.95 and left.g < lit * 0.05, "Second light casts an independent shadow")
	check(right.g > lit * 0.95 and right.r < lit * 0.05, "First light retains its shadow after array growth")
	var max_error := 0.0
	for y in range(0, view.size.y, 3):
		for x in range(0, view.size.x, 3):
			var both := combined.get_pixel(x, y)
			max_error = maxf(max_error, absf(both.r - red_only.get_pixel(x, y).r))
			max_error = maxf(max_error, absf(both.g - green_only.get_pixel(x, y).g))
	check(max_error < 0.003, "Combined lights match individual renders, max error %s" % max_error)

	red.light_energy = 1
	green.hide()
	environment.shadow_resolution = 2048
	red.directional_shadow_mode = DirectionalLight3D.SHADOW_ORTHOGONAL
	red.shadow_opacity = 0.5
	check(absf(sample_receiver(await snapshot(), -0.2, 1).r / lit - 0.5) < 0.04, "One cascade, resized array and shadow opacity")
	opaque.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	check(sample_receiver(await snapshot(), -0.2, 1).r > lit * 0.95, "Cast shadow OFF removes the opaque silhouette")
	opaque.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_DOUBLE_SIDED
	opaque.rotate_y(PI)
	red.shadow_opacity = 1
	var before_reload := await snapshot()
	check(sample_receiver(before_reload, -0.2, 1).r < lit * 0.05, "Double-sided back face casts a shadow")
	# Five equal unshadowed lights exercise the per-light loop past the old four-light limit.
	red.hide()
	var extra: Array[DirectionalLight3D] = []
	for i in 5:
		var fill := sun(Color.WHITE, 0, DirectionalLight3D.SHADOW_ORTHOGONAL)
		fill.shadow_enabled = false
		extra.push_back(fill)
	var five := sample_receiver(await snapshot(), 0, -3).r
	extra.back().hide()
	var four := sample_receiver(await snapshot(), 0, -3).r
	check(absf(five / four - 1.25) < 0.005, "All five directional lights contribute")
	for fill in extra:
		fill.free()
	red.show()
	before_reload = await snapshot()
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(before_reload.get_data())
	print("CSM IMAGE ", digest.finish().hex_encode())
	print("CSM READY FOR RELOAD")
	var marker := OS.get_environment("YUTREL_CSM_RELOAD_MARKER")
	while not marker.is_empty() and not FileAccess.file_exists(marker):
		await process_frame
	var after_reload := await snapshot()
	check(before_reload.get_data() == after_reload.get_data(), "Shadow hot reload preserves image")
	print("CSM RUNTIME PASS" if failures == 0 else "CSM RUNTIME FAIL")
	quit(failures)
