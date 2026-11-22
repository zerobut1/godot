extends SceneTree

var view: SubViewport
var camera: Camera3D
var sky: YutrelEnvironment
var geometry: Node3D
var volume: YutrelDDGIVolume
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


func probe(image: Image, point: Vector3, radius := 2) -> Color:
	var center := Vector2i(camera.unproject_position(point))
	var value := Color(0, 0, 0, 0)
	for y in range(-radius, radius + 1):
		for x in range(-radius, radius + 1):
			value += image.get_pixelv(center + Vector2i(x, y))
	return value / float((2 * radius + 1) ** 2)


func add_mesh(mesh: Mesh, position: Vector3, mat: StandardMaterial3D) -> MeshInstance3D:
	var item := MeshInstance3D.new()
	item.mesh = mesh
	item.material_override = mat
	item.position = position
	geometry.add_child(item)
	return item


func quad(position: Vector3, size: Vector2, mat: StandardMaterial3D) -> MeshInstance3D:
	var mesh := QuadMesh.new()
	mesh.size = size
	return add_mesh(mesh, position, mat)


func material(color: Color) -> StandardMaterial3D:
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.metallic_specular = 0
	return mat


func reset_geometry() -> void:
	geometry.free()
	geometry = Node3D.new()
	view.add_child(geometry)


func box(at: Vector3, size: Vector3, mat: StandardMaterial3D) -> MeshInstance3D:
	var mesh := BoxMesh.new()
	mesh.size = size
	return add_mesh(mesh, at, mat)


func furnace() -> void:
	sky.sky_color = Color.WHITE
	sky.sky_intensity = 1
	volume.position = Vector3(0, 0, -4)
	volume.size = Vector3(6, 4, 6)
	volume.probe_counts = Vector3i(4, 4, 4)
	volume.rays_per_probe = 257 # Exercises a partial shared-memory chunk.
	volume.hysteresis = 0.97
	var white := material(Color.WHITE)
	white.metallic_specular = 0.5
	white.roughness = 0.35
	var sphere := SphereMesh.new()
	sphere.radius = 0.35
	sphere.height = 0.7
	var item := add_mesh(sphere, volume.position, white)
	var converged := await snapshot(480)
	var color := probe(converged, item.position)
	print("DDGI FURNACE ", color)
	check(absf(color.r - 1.0) < 0.015 and absf(color.g - 1.0) < 0.015 and absf(color.b - 1.0) < 0.015, "White dielectric conserves unit sky radiance")
	check(converged.get_format() in [Image.FORMAT_RGBAH, Image.FORMAT_RGBH, Image.FORMAT_RGBAF, Image.FORMAT_RGBF], "Linear HDR readback")
	# All probes are farther than 0.1 from the sphere. Isolate an exact constant
	# radiance field and exercise both the 16-ray minimum and 1024-ray maximum.
	volume.hysteresis = 0.0
	volume.max_ray_distance = 0.1
	volume.rays_per_probe = 16
	var constant_low := probe(await snapshot(32), item.position)
	volume.rays_per_probe = 1024
	var constant_high := probe(await snapshot(16), item.position)
	print("DDGI CONSTANT WHITE 16=%s 1024=%s" % [constant_low, constant_high])
	check(absf(constant_low.r - 1) < 0.004 and absf(constant_high.r - 1) < 0.004, "Constant sky decodes to unit E / PI at both ray-count limits")
	# With gamma one, the stored value 0.5 is exactly representable in FP16.
	volume.irradiance_gamma = 1
	var exact := probe(await snapshot(16), item.position)
	print("DDGI LINEAR ENCODING ", exact)
	check(absf(exact.r - 1) < 0.002, "Linear probe encoding recovers unit radiance")
	volume.irradiance_gamma = 5
	volume.hysteresis = 0.5
	volume.max_ray_distance = 100
	volume.rays_per_probe = 257
	# Suppressing the active volume proves the furnace measured its contribution.
	volume.intensity = 0
	check(probe(await snapshot(), item.position).r < 0.005, "Zero volume intensity suppresses both diffuse and sky specular")
	volume.intensity = 1
	if OS.has_environment("YUTREL_DDGI_RELOAD_MARKER"):
		print("DDGI READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_DDGI_RELOAD_MARKER")):
			await create_timer(0.1).timeout
	check(absf(probe(await snapshot(40), item.position).r - 1) < 0.015, "The restored furnace converges to unit sky radiance")
	# An inactive volume can be edited without stealing ownership. Hiding the active
	# volume promotes the next visible one, and removing it restores the first.
	var standby := YutrelDDGIVolume.new()
	standby.position = volume.position
	standby.intensity = 0
	view.add_child(standby)
	standby.normal_bias = 0.3
	check(probe(await snapshot(12), item.position).r > 0.97, "Editing a second volume preserves the active one")
	volume.hide()
	check(probe(await snapshot(3), item.position).r < 0.005, "Hidden volume yields to the next visible volume")
	volume.show()
	standby.free()
	check(probe(await snapshot(32), item.position).r > 0.97, "Removing the active volume restores its successor")

	# Disable adaptive darkening to make any stale data from the old grid visible.
	volume.hysteresis = 0.99
	volume.irradiance_threshold = 10
	volume.infinite_bounces = false
	sky.sky_intensity = 0
	volume.position.x += 0.15
	check(probe(await snapshot(2), item.position).r < 0.001, "Moving the grid discards illuminated history")
	sky.sky_intensity = 1
	volume.hysteresis = 0
	await snapshot(12)
	volume.hysteresis = 0.99
	sky.sky_intensity = 0
	volume.size += Vector3.ONE * 0.2
	check(probe(await snapshot(2), item.position).r < 0.001, "Resizing the grid clears the old atlas")
	volume.irradiance_threshold = 0.25
	volume.hysteresis = 0.5
	reset_geometry()


func color_bleeding() -> void:
	sky.sky_intensity = 0
	volume.position = Vector3(0, 0, -4)
	volume.size = Vector3(4.6, 3.5, 3.5)
	volume.probe_counts = Vector3i(7, 4, 6)
	volume.rays_per_probe = 256
	var white := material(Color.WHITE)
	quad(Vector3(0, 0, -6), Vector2(8, 5), white)
	box(Vector3(2.15, 0, -4), Vector3(0.3, 5, 4.5), material(Color.RED))
	box(Vector3(0, -2.05, -4), Vector3(5, 0.2, 5), white)
	box(Vector3(0, 2.05, -4), Vector3(5, 0.2, 5), white)
	var sun := DirectionalLight3D.new()
	sun.light_energy = 4.0
	geometry.add_child(sun)
	sun.look_at(Vector3.RIGHT) # Parallel to the white receiver. Only the red wall faces the sun.
	var point := Vector3(1.3, -1.3, -6)
	volume.infinite_bounces = false
	var single := probe(await snapshot(36), point)
	volume.infinite_bounces = true
	var multi := probe(await snapshot(56), point)
	print("DDGI COLOR BLEED single=%s multi=%s" % [single, multi])
	check(single.r > 0.04 and single.r > 3 * maxf(single.g, single.b), "Sunlit red wall illuminates shadowed white receiver in Final")
	check(multi.r > single.r * 1.06, "Previous-frame probe sampling adds multiple diffuse bounces in the cavity")
	reset_geometry()


func visibility_and_decal() -> void:
	sky.sky_intensity = 1
	volume.size = Vector3(6.4, 3.6, 6.4)
	volume.position = Vector3(0, 0, -3)
	volume.probe_counts = Vector3i(8, 4, 7)
	volume.rays_per_probe = 128
	var white := material(Color.WHITE)
	var black := material(Color.BLACK)
	quad(Vector3(0, 0, -6), Vector2(8, 6), white)
	# A sealed left room and open right receiver share a rear wall. The camera looks out from inside.
	box(Vector3(0, 0, -2), Vector3(0.35, 5, 9), black)
	box(Vector3(-3.5, 0, -2), Vector3(0.35, 5, 9), black)
	box(Vector3(-1.75, 0, 1.5), Vector3(3.5, 5, 0.35), black)
	for y: float in [-2.15, 2.15]:
		box(Vector3(-1.75, y, -2), Vector3(3.5, 0.35, 9), black)
	quad(Vector3(-1.6, 0, -5.95), Vector2(1, 1), black)
	var decal := material(Color(1, 1, 1, 0.5))
	decal.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	quad(Vector3(-1.6, 0, -5.90), Vector2(0.9, 0.9), decal)
	var dark_point := Vector3(-0.42, 0, -6)
	var bright_point := Vector3(2.5, 0, -6)
	var decal_point := Vector3(-1.6, 0, -5.9)
	var active := await snapshot(60)
	var dark := probe(active, dark_point)
	var bright := probe(active, bright_point)
	var shaded_decal := probe(active, decal_point)
	var sun := DirectionalLight3D.new()
	sun.shadow_enabled = true
	sun.light_energy = 2
	geometry.add_child(sun)
	sun.look_at(Vector3(-1, -0.2, -1))
	var sunlit := await snapshot(32)
	var exterior := probe(sunlit, bright_point)
	var interior := probe(sunlit, dark_point)
	print("DDGI SUNLIT exterior=%s interior=%s" % [exterior, interior])
	check(exterior.r > bright.r + 0.15 and interior.r < 0.15 * exterior.r, "A sunlit exterior stays separated from the enclosed room")
	sun.hide()
	volume.hide()
	var baseline := await snapshot()
	var sky_decal := probe(baseline, decal_point)
	print("DDGI VISIBILITY dark=%s bright=%s decal=%s sky_decal=%s" % [dark, bright, shaded_decal, sky_decal])
	check(bright.r > 0.5 and dark.r < bright.r * 0.15 and dark.r < probe(baseline, dark_point).r * 0.15, "Wall separates enclosed and sky-lit receivers")
	check(sky_decal.r > 0.4 and sky_decal.r < 0.6 and shaded_decal.r < sky_decal.r * 0.15, "Half-alpha white decal receives the room's indirect light")
	# Re-entering the world starts a deterministic history; hash a non-uniform image with DDGI enabled.
	volume.show()
	var image := await snapshot(24)
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(image.get_data())
	print("DDGI IMAGE ", digest.finish().hex_encode())


func run() -> void:
	root.disable_3d = true
	view = SubViewport.new()
	view.size = Vector2i(192, 128)
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)
	camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 6
	view.add_child(camera)
	camera.make_current()
	sky = YutrelEnvironment.new()
	sky.sky_intensity = 0
	sky.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	view.add_child(sky)
	geometry = Node3D.new()
	view.add_child(geometry)
	volume = YutrelDDGIVolume.new()
	view.add_child(volume)
	volume.size = Vector3(-1, 2, 0)
	volume.probe_counts = Vector3i(1, 5, 80)
	volume.rays_per_probe = 3
	check(volume.size.is_equal_approx(Vector3(0.01, 2, 0.01)) and volume.probe_counts == Vector3i(2, 5, 64) and volume.rays_per_probe == 16, "Volume setter bounds")
	await furnace()
	await color_bleeding()
	await visibility_and_decal()
	print("DDGI RUNTIME PASS" if failures == 0 else "DDGI RUNTIME FAIL")
	quit(failures)
