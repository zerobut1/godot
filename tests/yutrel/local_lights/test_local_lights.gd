extends SceneTree

signal profile_ready(stages: PackedStringArray)

var view: SubViewport
var camera: Camera3D
var rig: Node3D
var sky: YutrelEnvironment
var wall: MeshInstance3D
var omni: OmniLight3D
var failures := 0
var digest := HashingContext.new()


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


func pixel(image: Image, x := 0.0, y := 0.0) -> Color:
	return image.get_pixelv(Vector2i(camera.unproject_position(rig.to_global(Vector3(x, y, 0)))))


func read_profile() -> void:
	var rd := RenderingServer.get_rendering_device()
	var stages := PackedStringArray()
	for i in rd.get_captured_timestamps_count():
		stages.append(rd.get_captured_timestamp_name(i))
	profile_ready.emit.call_deferred(stages)


func profile() -> PackedStringArray:
	for i in 6:
		await RenderingServer.frame_post_draw
	RenderingServer.call_on_render_thread(read_profile)
	return await profile_ready


func direct() -> void:
	var lit := await snapshot()
	var center := pixel(lit)
	var expected := pow(1.0 - pow(2.0 / 4.0, 4), 2) / PI
	check(absf(center.r - expected) < 0.005 and absf(center.g / center.r - Color(0.5, 0, 0).srgb_to_linear().r) < 0.015 and center.b < 0.001,
			"Omni HDR diffuse color, energy, inverse-square decay and quartic range window")
	check(pixel(lit, 2).r < center.r * 0.4 and pixel(lit, 3.7).r < 0.001, "Omni cuts off at range")
	digest.update(lit.get_data())
	var stages := await profile()
	check(stages.count("Yutrel RT Scene Update") == 1 and stages.has("Yutrel Local Light Cluster") and stages.has("Yutrel Local Lights"),
			"A shadowed local light alone updates the TLAS once and dispatches clustering and lighting")
	omni.omni_attenuation = 0
	check(absf(pixel(await snapshot()).r - center.r * 4) < 0.01, "Inspector attenuation controls distance decay")
	omni.omni_attenuation = 2
	var subtract := omni.duplicate() as OmniLight3D
	subtract.light_negative = true
	subtract.light_energy *= 0.5
	rig.add_child(subtract)
	check(absf(pixel(await snapshot()).r / center.r - 0.5) < 0.01, "Negative local energy subtracts radiance")
	subtract.free()
	var metal := StandardMaterial3D.new()
	metal.metallic = 1
	metal.roughness = 0.26
	wall.material_override = metal
	var spec := await snapshot()
	check(pixel(spec).r > center.r * 2 and pixel(spec).r > 4 * pixel(spec, 1.2).r and pixel(spec).b < 0.001, "Metal shows a colored specular highlight")
	wall.material_override = null
	camera.cull_mask = 1
	omni.layers = 2
	check(pixel(await snapshot()).r < 0.001, "Camera layers exclude scenario lights")
	omni.layers = 1
	omni.hide()
	check(pixel(await snapshot()).r < 0.001 and not (await profile()).has("Yutrel Local Light Cluster"), "Hidden and empty light lists skip clustering")


func cone_and_shadow() -> void:
	var spot := SpotLight3D.new()
	spot.position = omni.position
	spot.light_energy = 4
	spot.light_color = Color.GREEN
	spot.spot_range = 4
	spot.spot_angle = 30
	spot.spot_attenuation = 2
	spot.spot_angle_attenuation = 1
	rig.add_child(spot)
	var cone := await snapshot()
	var core := pixel(cone).g
	var rim := pixel(cone, 0.9).g
	check(core > 0.2 and rim > 0.02 and rim < core * 0.6 and pixel(cone, 1.3).g < 0.001, "Spot cone has a bright axis, smooth rim and dark exterior")
	spot.spot_angle_attenuation = 2
	check(pixel(await snapshot(), 0.9).g < rim * 0.8, "Inspector cone attenuation uses the reciprocal exponent")
	spot.rotation.y = deg_to_rad(40)
	check(pixel(await snapshot()).g < 0.001, "Rotating the spot moves its cone")
	spot.rotation.y = 0
	digest.update(cone.get_data())

	var blocker := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3(0.38, 0.55, 0.1)
	blocker.mesh = box
	blocker.position.z = 1
	blocker.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
	rig.add_child(blocker)
	var glass := StandardMaterial3D.new()
	glass.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	glass.albedo_color.a = 0.75
	glass.metallic_specular = 0
	for light: Light3D in [spot, omni]:
		spot.visible = light == spot
		omni.visible = light == omni
		light.light_color = Color.WHITE
		for transparent in [false, true]:
			wall.material_override = glass if transparent else null
			light.shadow_enabled = false
			var clear := await snapshot()
			light.shadow_enabled = true
			light.shadow_opacity = 0.65
			var shadow := await snapshot()
			check(pixel(clear).r > 0.1 and absf(pixel(shadow).r / pixel(clear).r - 0.35) < 0.015,
					"%s shadow opacity on transparent=%s" % [light.get_class(), transparent])
			check(absf(pixel(clear, 0.65).r - pixel(shadow, 0.65).r) < 0.002, "Adjacent pixels outside the blocker stay lit")
			digest.update(shadow.get_data())
	wall.material_override = null
	blocker.free()
	spot.free()
	omni.hide()


func scaling(perspective: bool) -> void:
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE if perspective else Camera3D.PROJECTION_ORTHOGONAL
	var group := Node3D.new()
	view.add_child(group)
	wall.hide()
	var samples: Array[Vector2i] = []
	var lamps: Array[OmniLight3D] = []
	for i in 8:
		var sample := Vector2i(20 + i * 22, 20 + (i * 29) % 95)
		var z := 0.6 * pow(1.8, i)
		var pos := camera.project_position(Vector2(sample) + Vector2(0.5, 0.5), z)
		var width := pos.distance_to(camera.project_position(Vector2(sample) + Vector2(8.5, 0.5), z))
		var quad := MeshInstance3D.new()
		var mesh := QuadMesh.new()
		mesh.size = Vector2.ONE * width
		quad.mesh = mesh
		quad.position = pos
		group.add_child(quad)
		var lamp := OmniLight3D.new()
		lamp.position = pos + Vector3.BACK * width
		lamp.omni_range = width * 1.8
		lamp.omni_attenuation = 0
		lamp.light_energy = 0.1
		group.add_child(lamp)
		samples.append(sample)
		lamps.append(lamp)
	var baseline := await snapshot()
	for lamp in lamps:
		for j in 8:
			group.add_child(lamp.duplicate())
	var many := await snapshot()
	for sample in samples:
		var first := baseline.get_pixelv(sample).r
		check(first > 0.015 and absf(many.get_pixelv(sample).r / first - 9) < 0.08,
				"72 lights sum correctly at pixel %s, perspective=%s" % [sample, perspective])
	digest.update(many.get_data())
	group.free()
	wall.show()


func secondary() -> void:
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.position.z = rig.position.z + 1
	var panel := MeshInstance3D.new()
	var mesh := QuadMesh.new()
	mesh.size = Vector2(5, 5)
	panel.mesh = mesh
	panel.rotation.y = PI
	panel.position.z = 4
	rig.add_child(panel)
	omni.position.z = 3
	omni.omni_range = 1.7
	omni.light_color = Color.RED
	omni.light_energy = 12
	omni.shadow_enabled = true
	omni.show()
	check(camera.is_position_behind(omni.global_position - Vector3.BACK * omni.omni_range), "Entire light sphere is behind the camera")
	var mirror := StandardMaterial3D.new()
	mirror.metallic = 1
	mirror.roughness = 0.02
	wall.material_override = mirror
	sky.rt_reflection_enabled = true
	sky.rt_reflection_half_resolution = false
	var reflected := await snapshot(5)
	check(pixel(reflected).r > 0.5 and pixel(reflected).g < 0.001, "Mirror sees the locally illuminated rear panel")
	omni.light_color = Color.GREEN
	var changed := pixel(await snapshot(1))
	check(changed.g > 0.5 and changed.r < 0.001, "Changing off-screen radiance invalidates reflection history")
	digest.update(reflected.get_data())
	sky.rt_reflection_enabled = false
	wall.material_override = null
	check(pixel(await snapshot()).g < 0.001, "Diffuse receiver is outside the direct light sphere")
	var volume := YutrelDDGIVolume.new()
	volume.position.z = 1.8
	volume.size = Vector3(5, 5, 5)
	volume.probe_counts = Vector3i(5, 5, 5)
	volume.rays_per_probe = 256
	volume.hysteresis = 0.4
	volume.probe_classification = false
	volume.probe_relocation = false
	rig.add_child(volume)
	var bounce := pixel(await snapshot(24))
	check(bounce.g > 0.006 and bounce.r < 0.001, "DDGI carries green light from the rear panel onto the out-of-range receiver")


func run() -> void:
	root.disable_3d = true
	view = SubViewport.new()
	view.size = Vector2i(193, 137)
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)
	var scene := preload("res://editor_scene.tscn").instantiate()
	view.add_child(scene)
	rig = scene.get_node("Rig")
	rig.position.z = -4
	wall = rig.get_node("Receiver")
	omni = rig.get_node("Omni")
	sky = scene.get_node("Environment")
	camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 6
	camera.far = 50
	view.add_child(camera)
	camera.make_current()
	digest.start(HashingContext.HASH_SHA256)
	await snapshot()
	if OS.has_environment("YUTREL_LOCAL_LIGHTS_RELOAD_MARKER"):
		print("LOCAL_LIGHTS READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_LOCAL_LIGHTS_RELOAD_MARKER")):
			await create_timer(0.1).timeout
	await direct()
	await cone_and_shadow()
	await scaling(false)
	await scaling(true)
	await secondary()
	print("LOCAL_LIGHTS IMAGE ", digest.finish().hex_encode())
	print("LOCAL_LIGHTS RUNTIME PASS" if failures == 0 else "LOCAL_LIGHTS RUNTIME FAIL")
	quit(failures)
