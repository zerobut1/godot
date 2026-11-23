extends SceneTree

var view: SubViewport
var camera: Camera3D
var sky: YutrelEnvironment
var corner: Node3D
var failures := 0


func _initialize() -> void:
	run.call_deferred()


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func snapshot(frames: int) -> Image:
	for i in frames:
		await RenderingServer.frame_post_draw
	return view.get_texture().get_image()


func area_mean(image: Image, region: Rect2i) -> float:
	var total := 0.0
	for y in range(region.position.y, region.end.y):
		for x in range(region.position.x, region.end.x):
			total += image.get_pixel(x, y).r
	return total / region.get_area()


func sample_at(image: Image, x: float, z := -5.0) -> float:
	var center := Vector2i(camera.unproject_position(Vector3(x, 0, z)))
	return area_mean(image, Rect2i(center - Vector2i(1, 10), Vector2i(3, 21)))


func mesh_instance(mesh: Mesh, position: Vector3, parent: Node3D) -> MeshInstance3D:
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	parent.add_child(instance)
	instance.position = position
	return instance


func furnace_and_normal_map() -> void:
	corner.hide()
	var material := StandardMaterial3D.new()
	material.roughness = 0.45
	var sphere := SphereMesh.new()
	sphere.material = material
	sphere.radius = 1
	sphere.height = 2
	var item := mesh_instance(sphere, Vector3(0, 0, -5), corner.get_parent())
	var lit := await snapshot(24)
	check(lit.get_format() in [Image.FORMAT_RGBH, Image.FORMAT_RGBAH, Image.FORMAT_RGBF, Image.FORMAT_RGBAF],
			"The receiver readback retains linear HDR")
	var minimum := 1.0
	for y in range(-27, 28):
		for x in range(-27, 28):
			if x * x + y * y <= 27 * 27:
				minimum = minf(minimum, lit.get_pixelv(view.size / 2 + Vector2i(x, y)).r)
	print("RTAO FURNACE minimum=%.7f center=%.7f" % [minimum, sample_at(lit, 0)])
	check(minimum > 0.985 and absf(sample_at(lit, 0) - 1) < 0.01, "An isolated convex white dielectric conserves sky radiance")
	item.free()

	# The back face is within the AO radius. Rays below the front face would hit it.
	var slab := BoxMesh.new()
	slab.size = Vector3(4, 3, 0.12)
	slab.material = material
	var normals := Image.create_empty(2, 2, false, Image.FORMAT_RGBA8)
	normals.fill(Color(0.99, 0.5, 1, 1))
	material.normal_texture = ImageTexture.create_from_image(normals)
	material.normal_enabled = true
	material.metallic_specular = 0
	item = mesh_instance(slab, Vector3(0, 0, -5), corner.get_parent())
	var previous_size := view.size
	view.size = Vector2i(3840, 2160)
	var wall_image := await snapshot(4)
	check(wall_image.get_size() == view.size, "The normal-mapped wall renders at 4K")
	var wall := area_mean(wall_image, Rect2i(view.size / 2 - Vector2i(48, 48), Vector2i(96, 96)))
	view.size = previous_size
	sky.sky_intensity = 0
	var sun := DirectionalLight3D.new()
	sun.light_energy = 3
	view.add_child(sun)
	var tilted := sample_at(await snapshot(2), 0)
	material.normal_enabled = false
	var flat := sample_at(await snapshot(2), 0)
	print("RTAO NORMAL WALL 3840x2160 mean=%.7f tilted_direct=%.7f flat_direct=%.7f" % [wall, tilted, flat])
	check(tilted < flat * 0.45 and flat > 0.2, "The normal map measurably tilts the shading normal")
	check(absf(wall - 1) < 0.01, "A steep normal map on a thin solid causes no self-occlusion")
	sun.free()
	item.free()
	sky.sky_intensity = 1
	corner.show()


func frame_variance(a: Image, b: Image, region: Rect2i) -> float:
	var total := 0.0
	for y in range(region.position.y, region.end.y):
		for x in range(region.position.x, region.end.x):
			total += pow(a.get_pixel(x, y).r - b.get_pixel(x, y).r, 2)
	return total / region.get_area()


func contact_and_history(digest: HashingContext) -> void:
	sky.rtao_radius = 1.5
	sky.rtao_enabled = false
	var white := await snapshot(2)
	var baseline := sample_at(white, -0.1)
	sky.rtao_enabled = true
	var previous := await snapshot(1)
	var first := previous
	var region := Rect2i(Vector2i(camera.unproject_position(Vector3(-0.7, 0.8, -5))), Vector2i(18, 50))
	var early := 0.0
	var late := 0.0
	for i in 32:
		var image := await snapshot(1)
		if i < 4:
			early += frame_variance(image, previous, region) / 4.0
		if i >= 28:
			late += frame_variance(image, previous, region) / 4.0
		previous = image
	var crease := sample_at(previous, -0.1)
	var mid := sample_at(previous, -0.57)
	var open := sample_at(previous, -2)
	print("RTAO CONTACT disabled=%.7f crease=%.7f mid=%.7f open=%.7f" % [baseline, crease, mid, open])
	print("RTAO TEMPORAL early_ms_delta=%.9f late_ms_delta=%.9f" % [early, late])
	check(baseline > 0.99 and crease < baseline - 0.22 and mid > crease + 0.08 and open > 0.985, "Contact occlusion fades smoothly into the open receiver")
	check(early > 0 and late < 0.35 * early, "Temporal accumulation reduces static frame-to-frame variance")
	digest.update(previous.get_data())
	if sky.rtao_half_resolution:
		sky.rtao_half_resolution = false
		var full := await snapshot(1)
		sky.rtao_half_resolution = true
		var restarted := await snapshot(1)
		check(full.get_data() != first.get_data() and restarted.get_data() == first.get_data(),
				"Switching resolutions in place restarts the half-resolution sampling sequence")

	sky.rtao_radius = 0.2
	var narrow := sample_at(await snapshot(20), -0.57)
	sky.rtao_radius = 1.5
	sky.rtao_intensity = 0
	var neutral := sample_at(await snapshot(4), -0.1)
	sky.rtao_intensity = 2
	var squared := sample_at(await snapshot(24), -0.1)
	print("RTAO CONTROLS short_radius=%.7f zero_intensity=%.7f double_intensity=%.7f" % [narrow, neutral, squared])
	check(narrow > 0.985 and narrow > mid + 0.06, "Radius changes the reach of the occluder")
	check(absf(neutral - 1) < 0.01 and absf(squared - crease * crease) < 0.04, "Intensity controls the final filtered visibility")
	sky.rtao_intensity = 1

	camera.position.x += 1.4
	var moved := await snapshot(1)
	var new_crease := sample_at(moved, -0.1)
	var vacated := sample_at(moved, 1.30)
	check(new_crease < 0.8 and vacated > 0.93, "A camera pan keeps the crease attached to its world position")
	# Reveal a deeper plate under the old crease in a single rendered frame.
	var plate := QuadMesh.new()
	plate.size = Vector2(10, 6)
	plate.material = (corner.get_node("Receiver") as MeshInstance3D).mesh.surface_get_material(0)
	var back := mesh_instance(plate, Vector3(0, 0, -6), corner.get_parent())
	await snapshot(16)
	corner.position.x -= 10
	var revealed := sample_at(await snapshot(1), -0.1, -6)
	print("RTAO DISOCCLUSION tracked_crease=%.7f vacated_pixel=%.7f revealed=%.7f" % [new_crease, vacated, revealed])
	check(revealed > 0.985, "The newly exposed plane rejects the previous dark crease immediately")
	back.free()
	corner.position.x += 10
	camera.position.x = 0


func offscreen_cutout() -> void:
	# Keep the wall completely outside the right clip plane, within one AO radius.
	camera.position.x = -0.5 * camera.size * view.size.aspect() - 0.05
	var solid_image := await snapshot(24)
	var solid := sample_at(solid_image, -0.21)
	# The last row and column of this odd viewport both have half-resolution sample centers.
	var edge := Vector2i(view.size.x - 1, view.size.y - 1)
	var edge_solid := solid_image.get_pixelv(edge).r
	var wall: MeshInstance3D = corner.get_node("Wall")
	var cutout := StandardMaterial3D.new()
	cutout.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	var holes := Image.create_empty(2, 2, false, Image.FORMAT_RGBA8)
	holes.fill(Color(1, 1, 1, 0))
	cutout.albedo_texture = ImageTexture.create_from_image(holes)
	wall.material_override = cutout
	var cut_image := await snapshot(32)
	var cut := sample_at(cut_image, -0.21)
	var edge_cut := cut_image.get_pixelv(edge).r
	print("RTAO OFFSCREEN solid=%.7f alpha_cutout=%.7f edge_solid=%.7f edge_cut=%.7f" % [solid, cut, edge_solid, edge_cut])
	check(solid < 0.83 and cut > 0.985, "The off-frustum wall occludes the visible plane only at opaque ray intersections")
	check(edge_solid < 0.85 and edge_cut > 0.985, "The bottom-right pixel of the odd viewport follows the occluder")
	wall.material_override = null
	camera.position.x = 0


func direct_isolation() -> void:
	sky.sky_intensity = 0
	var sun := DirectionalLight3D.new()
	sun.light_energy = 4
	view.add_child(sun)
	sky.rtao_enabled = false
	var direct := await snapshot(3)
	sky.rtao_enabled = true
	var combined := await snapshot(24)
	var without := sample_at(direct, -0.1)
	var with_ao := sample_at(combined, -0.1)
	print("RTAO DIRECT disabled=%.7f enabled=%.7f" % [without, with_ao])
	check(without > 0.5 and direct.get_data() == combined.get_data(), "Direct-only HDR output is bit-identical with RTAO enabled")
	sun.free()
	sky.sky_intensity = 1


func run() -> void:
	root.disable_3d = true
	view = SubViewport.new()
	view.size = Vector2i(192, 128)
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)
	var scene := preload("res://editor_scene.tscn").instantiate()
	view.add_child(scene)
	corner = scene.get_node("Corner")
	corner.position.z = -5
	sky = scene.get_node("Sky")
	check(sky.rtao_enabled and sky.rtao_half_resolution and sky.rtao_radius == 1 and sky.rtao_intensity == 1 and sky.rtao_samples == 1, "RTAO defaults")
	sky.rtao_radius = -1
	sky.rtao_samples = 30
	sky.rtao_intensity = 6
	check(is_equal_approx(sky.rtao_radius, 0.05) and sky.rtao_samples == 8 and sky.rtao_intensity == 4, "RTAO property bounds")
	sky.rtao_intensity = 1
	camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 4
	view.add_child(camera)
	camera.make_current()
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	for half_res in [true, false]:
		print("RTAO MODE ", "half" if half_res else "full")
		sky.rtao_half_resolution = half_res
		sky.rtao_radius = 1
		sky.rtao_samples = 8
		await furnace_and_normal_map()
		if half_res and OS.has_environment("YUTREL_RTAO_RELOAD_MARKER"):
			print("RTAO READY FOR RELOAD")
			while not FileAccess.file_exists(OS.get_environment("YUTREL_RTAO_RELOAD_MARKER")):
				await create_timer(0.1).timeout
		view.size = Vector2i(193, 129)
		sky.rtao_samples = 1
		await contact_and_history(digest)
		await offscreen_cutout()
		await direct_isolation()
	print("RTAO IMAGE ", digest.finish().hex_encode())
	print("RTAO RUNTIME PASS" if failures == 0 else "RTAO RUNTIME FAIL")
	quit(failures)
