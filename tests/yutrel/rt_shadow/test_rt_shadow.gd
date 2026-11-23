extends SceneTree

signal profile_ready(stages: PackedStringArray)

var view: SubViewport
var camera: Camera3D
var rig: Node3D
var environment: YutrelEnvironment
var sun: DirectionalLight3D
var failures := 0


func _initialize() -> void:
	run.call_deferred()


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func snapshot(frames := 3) -> Image:
	for i in frames:
		await RenderingServer.frame_post_draw
	return view.get_texture().get_image()


func read_profile() -> void:
	var rd := RenderingServer.get_rendering_device()
	var stages := PackedStringArray()
	for i in rd.get_captured_timestamps_count():
		stages.append(rd.get_captured_timestamp_name(i))
	profile_ready.emit.call_deferred(stages)


func snapshot_profile() -> PackedStringArray:
	# Readback may flush an extra transfer-only frame. Wait for a rendered profile.
	for i in 6:
		await RenderingServer.frame_post_draw
	RenderingServer.call_on_render_thread(read_profile)
	return await profile_ready


func check_shadow_stages(soft: bool, half_res: bool) -> void:
	var stages := await snapshot_profile()
	check(stages.count("Yutrel RT Scene Update") == 1 and stages.count("Yutrel RT Shadow Trace") == 1, "A single TLAS update and trace serve the opaque viewport")
	check(not stages.has("Yutrel CSM Shadows"), "Opaque RT lighting skips CSM")
	check(stages.has("Yutrel RT Shadow Temporal") == soft and stages.has("Yutrel RT Shadow Denoise") == soft,
			"Only soft shadows run temporal and denoise")
	check(stages.has("Yutrel RT Shadow Upsample") == (half_res or not soft), "Full-resolution soft shadows resolve in the last denoise pass")


func pixel(x: float, y: float) -> Vector2i:
	return Vector2i(camera.unproject_position(rig.to_global(Vector3(x, y, 0))))


func value(image: Image, x: float, y: float) -> float:
	return image.get_pixelv(pixel(x, y)).r


# Average the 5%-95% transition width across nine parallel receiver profiles.
func penumbra_width(image: Image, x: float, lit: float) -> float:
	var center := pixel(x, 0.43)
	var total := 0
	for dx in range(-4, 5):
		for y in range(pixel(x, 0.85).y, pixel(x, 0.05).y):
			var v := image.get_pixel(center.x + dx, y).r / lit
			if v > 0.05 and v < 0.95:
				total += 1
	return total / 9.0


func test_cutout() -> void:
	var caster := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(1.4, 0.8)
	var alpha := Image.create_empty(32, 8, false, Image.FORMAT_RGBA8)
	alpha.fill(Color.WHITE)
	alpha.fill_rect(Rect2i(10, 0, 12, 8), Color.TRANSPARENT)
	var mat := StandardMaterial3D.new()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	mat.albedo_texture = ImageTexture.create_from_image(alpha)
	quad.material = mat
	caster.mesh = quad
	caster.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
	caster.position = Vector3(-1, 1.22, 1)
	rig.add_child(caster)
	for diameter in [0.0, 2.0]:
		sun.light_angular_distance = diameter
		var image := await snapshot(20)
		var open := value(image, 1, 1.22)
		check(value(image, 0, 1.22) > open * 0.98 and value(image, -0.52, 1.22) < open * 0.02
				and value(image, 0.52, 1.22) < open * 0.02, "The cutout transmits its center stripe and blocks both rims")
	sun.light_angular_distance = 0
	caster.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_OFF
	check(value(await snapshot(), 0.52, 1.22) > 0.12, "Disabling shadow casting removes the cutout silhouette")
	caster.free()


func test_resolution(half_res: bool, digest: HashingContext) -> Image:
	environment.shadow_rt_half_resolution = half_res
	environment.shadow_rt_samples = 1
	sun.light_angular_distance = 0
	var hard := await snapshot(8)
	await check_shadow_stages(false, half_res)
	var lit := value(hard, 0, 1.5)
	check(hard.get_size() == view.size and lit > 0.12 and value(hard, 0, 0) < 0.001, "Hard RT shadow on the odd viewport")
	environment.shadow_rt_samples = 8
	check((await snapshot()).get_data() == hard.get_data(), "Hard shadows are deterministic and independent of the soft sample count")
	sun.shadow_opacity = 0.4
	check(absf(value(await snapshot(), 0, 0) / lit - 0.6) < 0.01, "Hard shadow opacity blends visibility linearly")
	sun.shadow_opacity = 1
	sun.light_angular_distance = 2.0
	environment.shadow_rt_samples = 1
	var first := await snapshot(1)
	var soft := await snapshot(32)
	await check_shadow_stages(true, half_res)
	var near_width := penumbra_width(soft, -1.70, lit)
	var far_width := penumbra_width(soft, 1.32, lit)
	var hard_width := penumbra_width(hard, 1.32, lit)
	print("RT_SHADOW WIDTH half=%s hard=%.2f near=%.2f far=%.2f" % [half_res, hard_width, near_width, far_width])
	check(far_width >= near_width + 1 and far_width >= hard_width + 1, "The far penumbra has a wider intermediate-gray transition than the contact edge")
	var far_edge := pixel(1.32, 0.43)
	var levels := {}
	for y in range(far_edge.y - 12, far_edge.y + 13):
		var gray := roundi(255 * soft.get_pixel(far_edge.x, y).r / lit)
		if gray > 5 and gray < 250:
			levels[gray] = true
	check(levels.size() >= 4, "The far edge resolves a smooth range of intermediate visibility levels")
	check(first.get_data() != soft.get_data(), "Soft shadows accumulate across frames")
	digest.update(hard.get_data())
	digest.update(soft.get_data())
	sun.shadow_opacity = 0.4
	var translucent := await snapshot()
	check(absf(value(translucent, 0, 0) / lit - 0.6) < 0.01, "Filtered soft shadow opacity blends after denoising")
	sun.shadow_opacity = 1
	sun.light_angular_distance = 4
	await snapshot()
	sun.light_angular_distance = 2
	check((await snapshot(1)).get_data() == first.get_data(), "Changing the sun radius resets the light history")
	environment.shadow_rt_samples = 4
	await snapshot()
	environment.shadow_rt_samples = 1
	check((await snapshot(1)).get_data() == first.get_data(), "Changing the sample count restarts the sequence")
	var original_basis := sun.basis
	sun.rotate_y(0.13)
	await snapshot()
	sun.basis = original_basis
	check((await snapshot(1)).get_data() == first.get_data(), "Rotating the light resets its history")
	await test_cutout()

	# Shadow and then uncover the very last work texel / output pixel.
	var pillar := rig.get_node("Pillar") as MeshInstance3D
	var previous := pillar.position
	var edge := view.size - Vector2i.ONE
	var world_edge := camera.project_position(Vector2(edge) + Vector2(0.5, 0.5), 10)
	pillar.position = Vector3(world_edge.x - 1, world_edge.y, previous.z)
	check((await snapshot()).get_pixelv(edge).r < lit * 0.01, "The bottom-right pixel receives a shadow")
	pillar.position = previous
	check((await snapshot(1)).get_pixelv(edge).r > lit * 0.98, "The bottom-right pixel resolves immediately after the blocker moves")
	return hard


func test_two_lights() -> void:
	var second := sun.duplicate() as DirectionalLight3D
	rig.add_child(second)
	sun.light_color = Color.RED
	second.light_color = Color.GREEN
	second.rotation.y = -sun.rotation.y
	second.light_angular_distance = 2.0
	sun.light_angular_distance = 2.0
	var image := await snapshot(24)
	var split := image.get_pixelv(pixel(0, 0))
	check(split.r < 0.001 and split.g > 0.12, "Each soft light samples its own shadow mask")
	second.free()
	check((await snapshot()).get_pixelv(pixel(0, 1)).g < 0.001, "Removing a soft light retires its contribution")
	sun.light_color = Color.WHITE


func test_transparent_shadows(digest: HashingContext) -> void:
	var receiver := rig.get_node("Receiver") as MeshInstance3D
	var pillar := rig.get_node("Pillar") as MeshInstance3D
	var glass := rig.get_node("Glass") as MeshInstance3D
	receiver.material_override = glass.mesh.surface_get_material(0)
	sun.light_angular_distance = 2.0
	for transparent_only in [false, true]:
		pillar.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY if transparent_only else GeometryInstance3D.SHADOW_CASTING_SETTING_ON
		var image := await snapshot(8)
		var stages := await snapshot_profile()
		check(stages.count("Yutrel RT Scene Update") == 1 and stages.has("Yutrel Transparent") and not stages.has("Yutrel CSM Shadows"),
				"Mixed and transparent-only RT scenes share one TLAS update and skip CSM")
		check(stages.count("Yutrel RT Shadow Trace") == (0 if transparent_only else 1), "Only opaque receivers dispatch the shadow-mask trace")
		var dark := value(image, 0, 0)
		var lit := value(image, 0, 1)
		print("RT_SHADOW TRANSPARENT only=%s dark=%.4f lit=%.4f" % [transparent_only, dark, lit])
		check(dark < 0.001 and lit > 0.12, "The forward receiver sees the pillar shadow and adjacent lit region")
		if transparent_only:
			digest.update(image.get_data())
			sun.light_angular_distance = 0
			check((await snapshot()).get_data() == image.get_data(), "Soft lights use the same stable center ray on transparency")
			sun.shadow_opacity = 0.4
			check(absf(value(await snapshot(), 0, 0) / lit - 0.6) < 0.01, "Forward shadow opacity blends visibility")
			sun.shadow_opacity = 1
			# Keep RT active while a coincident second light has shadows disabled.
			var fill := sun.duplicate() as DirectionalLight3D
			fill.shadow_enabled = false
			fill.light_color = Color.GREEN
			sun.light_color = Color.RED
			rig.add_child(fill)
			var split := (await snapshot()).get_pixelv(pixel(0, 0))
			check(split.r < 0.001 and split.g > lit * 0.98, "Forward RT honors each light's shadow flag")
			fill.free()
			sun.light_color = Color.WHITE
	await test_cutout()


func run() -> void:
	root.disable_3d = true
	view = SubViewport.new()
	view.size = Vector2i(193, 129)
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)
	var scene := preload("res://editor_scene.tscn").instantiate()
	view.add_child(scene)
	rig = scene.get_node("Rig")
	rig.position.z = -10
	sun = rig.get_node("Sun")
	environment = scene.get_node("Environment")
	var defaults := YutrelEnvironment.new()
	check(defaults.shadow_mode == YutrelEnvironment.SHADOW_MODE_CSM and defaults.shadow_rt_half_resolution and defaults.shadow_rt_samples == 1, "CSM and RT environment defaults")
	defaults.free()
	environment.shadow_rt_samples = -5
	check(environment.shadow_rt_samples == 1, "Minimum sample count")
	environment.shadow_rt_samples = 42
	check(environment.shadow_rt_samples == 8, "Maximum sample count")
	camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 4
	camera.far = 30
	view.add_child(camera)
	camera.make_current()
	await snapshot()
	if OS.has_environment("YUTREL_RT_SHADOW_RELOAD_MARKER"):
		print("RT_SHADOW READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_RT_SHADOW_RELOAD_MARKER")):
			await create_timer(0.1).timeout

	rig.get_node("Glass").hide()
	environment.shadow_mode = YutrelEnvironment.SHADOW_MODE_CSM
	var csm := await snapshot()
	print("RT_SHADOW CSM dark=%.4f lit=%.4f" % [value(csm, 0, 0), value(csm, 0, 1)])
	check(value(csm, 0, 0) < 0.01 and value(csm, 0, 1) > 0.12, "CSM still renders the pillar shadow")
	environment.shadow_mode = YutrelEnvironment.SHADOW_MODE_RAY_TRACED
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	var half := await test_resolution(true, digest)
	var full := await test_resolution(false, digest)
	check(full.get_data() != half.get_data() and penumbra_width(full, 1.32, value(full, 0, 1.5)) == 0,
			"Full resolution preserves the binary edge and half resolution interpolates it")
	sun.shadow_enabled = false
	check(value(await snapshot(), 0, 0) > 0.12, "A disabled directional shadow leaves the receiver lit")
	sun.shadow_enabled = true
	view.size = Vector2i(2213, 1314)
	for half_res in [false, true]:
		environment.shadow_rt_half_resolution = half_res
		sun.light_angular_distance = 2.0
		var large := await snapshot(3)
		check(large.get_size() == view.size and value(large, 0, 0) < 0.001 and value(large, 0, 1) > 0.12,
				"The soft mask survives a resize to a large odd-width viewport")
	view.size = Vector2i(193, 129)
	await test_two_lights()
	await test_transparent_shadows(digest)
	print("RT_SHADOW IMAGE ", digest.finish().hex_encode())
	print("RT_SHADOW RUNTIME PASS" if failures == 0 else "RT_SHADOW RUNTIME FAIL")
	quit(failures)
