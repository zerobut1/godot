extends SceneTree

signal profile_ready(stages: PackedStringArray)

var view: SubViewport
var camera: Camera3D
var sky: YutrelEnvironment
var mesh: MeshInstance3D
var glass: MeshInstance3D
var failures := 0


func _initialize() -> void:
	run.call_deferred()


func check(condition: bool, description: String) -> void:
	if not condition:
		failures += 1
		push_error(description)


func snapshot(frames := 1) -> Image:
	for i in frames:
		await RenderingServer.frame_post_draw
	return view.get_texture().get_image()


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


func check_motion(image: Image, pixel: Vector2i, expected: Vector2, label: String) -> void:
	# The debug pass writes display-encoded HSV to our linear HDR viewport.
	var color := image.get_pixelv(pixel).linear_to_srgb()
	var actual := Vector2.from_angle(color.h * TAU) * color.v * 16
	print("UPSCALER MOTION %s pixel=%s measured=%s expected=%s" % [label, pixel, actual, expected])
	check(actual.distance_to(expected) < 0.08 if expected != Vector2.ZERO else actual == Vector2.ZERO, label)


func vectors() -> void:
	view.debug_draw = Viewport.DEBUG_DRAW_MOTION_VECTORS
	var center := Vector2i(camera.unproject_position(mesh.global_position))
	var background := Vector2i(25, 18)
	var still := await snapshot(3)
	check_motion(still, center, Vector2.ZERO, "Stationary mesh")
	check_motion(still, background, Vector2.ZERO, "Stationary sky")
	var before := camera.unproject_position(mesh.global_position)
	mesh.position += Vector3(0.28, 0.14, 0)
	var after := camera.unproject_position(mesh.global_position)
	check_motion(await snapshot(), Vector2i(after), before - after, "Moving mesh")
	mesh.position = Vector3.ZERO
	RenderingServer.instance_teleport(mesh.get_instance())
	check_motion(await snapshot(), center, Vector2.ZERO, "Teleported mesh")
	camera.position.x = 0.25
	check_motion(await snapshot(), background, Vector2.ZERO, "Translation-only sky")
	var old_camera := camera.global_transform
	camera.rotation.y = 0.035
	var ray := camera.project_ray_normal(Vector2(background) + Vector2(0.5, 0.5))
	var previous_ray := old_camera.basis.inverse() * ray
	var clip := camera.get_camera_projection() * Vector4(previous_ray.x, previous_ray.y, previous_ray.z, 0)
	var previous := (Vector2(clip.x, -clip.y) / clip.w * 0.5 + Vector2(0.5, 0.5)) * Vector2(view.size)
	check_motion(await snapshot(), background, previous - Vector2(background) - Vector2(0.5, 0.5), "Rotating sky")
	camera.transform = Transform3D.IDENTITY
	view.debug_draw = Viewport.DEBUG_DRAW_DISABLED


func add_cutout(parent: Node3D) -> void:
	var mask := Image.create_empty(8, 8, false, Image.FORMAT_RGBA8)
	mask.fill(Color.WHITE)
	mask.fill_rect(Rect2i(2, 0, 4, 8), Color.TRANSPARENT)
	var material := StandardMaterial3D.new()
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	material.albedo_texture = ImageTexture.create_from_image(mask)
	var quad := QuadMesh.new()
	quad.material = material
	var cutout := MeshInstance3D.new()
	cutout.mesh = quad
	cutout.position = Vector3(0, 1.3, 0)
	parent.add_child(cutout)


func check_image(image: Image, quality: int) -> void:
	check(image.get_size() == view.size, "Display dimensions at quality %d" % quality)
	var center := image.get_pixelv(Vector2i(camera.unproject_position(mesh.global_position)))
	var green := image.get_pixelv(Vector2i(camera.unproject_position(glass.global_position)))
	check(center.r > 0.4 and center.r > 4 * center.g and green.g > 2 * green.r, "Opaque and transparent HDR colors at quality %d" % quality)
	for y in image.get_height():
		for x in image.get_width():
			var c := image.get_pixel(x, y)
			if not (is_finite(c.r) and is_finite(c.g) and is_finite(c.b) and is_finite(c.a)):
				check(false, "Nonfinite display pixel at %d,%d" % [x, y])
				return


func run() -> void:
	root.disable_3d = true
	view = SubViewport.new()
	view.size = Vector2i(257, 159)
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)
	var scene := preload("res://editor_scene.tscn").instantiate()
	view.add_child(scene)
	mesh = scene.get_node("Rig/Opaque")
	glass = scene.get_node("Rig/Glass")
	sky = scene.get_node("Environment")
	add_cutout(scene.get_node("Rig"))
	camera = Camera3D.new()
	view.add_child(camera)
	camera.make_current()
	var defaults := YutrelEnvironment.new()
	check(defaults.upscaler_mode == 0 and defaults.upscaler_quality == 0 and is_equal_approx(defaults.upscaler_sharpness, 0.2), "Upscaler defaults")
	defaults.free()
	await vectors()
	await snapshot(8)
	if OS.has_environment("YUTREL_UPSCALER_RELOAD_MARKER"):
		check(preload("res://test_velocity.gd").new().run(OS.get_environment("YUTREL_VELOCITY_SPIRV_DIR")), "Signed RG16F geometry velocity readback")
		print("UPSCALER READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_UPSCALER_RELOAD_MARKER")):
			await create_timer(0.1).timeout
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	for quality in [0, 1, 2, 3]:
		sky.upscaler_quality = quality
		sky.upscaler_sharpness = -1 if quality == 0 else 2
		check(sky.upscaler_sharpness == (0.0 if quality == 0 else 1.0), "Sharpness clamps at both endpoints")
		sky.exposure = 1.0 + quality * 0.1
		mesh.position.x = 0.12 * quality
		var image := await snapshot(8)
		check_image(image, quality)
		digest.update(image.get_data())
		var stages := await profile()
		for stage in ["Yutrel Temporal Upscale", "Yutrel RTAO Temporal", "Yutrel RT Reflection Reproject", "Yutrel RT Shadow Temporal", "Yutrel Transparent"]:
			check(stages.has(stage), "%s ran at quality %d" % [stage, quality])
	sky.upscaler_mode = 0
	check(not (await profile()).has("Yutrel Temporal Upscale"), "Live Off releases temporal history")
	sky.upscaler_mode = 1
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	check(not (await profile()).has("Yutrel Temporal Upscale"), "Orthographic viewport bypasses jitter and upscaling")
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	view.size += Vector2i(2, 4)
	glass.hide()
	check((await profile()).has("Yutrel Temporal Upscale"), "Resized perspective view resumes FSR with no transparent draws")
	print("UPSCALER IMAGE ", digest.finish().hex_encode())
	print("UPSCALER RUNTIME PASS" if failures == 0 else "UPSCALER RUNTIME FAIL")
	quit(failures)
