extends SceneTree

const SIZE := Vector2i(600, 320)
const ROUGHNESSES := [0.05, 0.25, 0.5, 0.75, 1.0]
const SUN := Vector3.BACK

var view: SubViewport
var camera: Camera3D
var sky: YutrelEnvironment
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


func error_rgb(a: Color, b: Color) -> float:
	return maxf(maxf(absf(a.r - b.r), absf(a.g - b.g)), absf(a.b - b.b))


func print_image_hash(label: String, image: Image) -> void:
	var hash := HashingContext.new()
	hash.start(HashingContext.HASH_SHA256)
	hash.update(image.get_data())
	print("IMAGE %s %s" % [label, hash.finish().hex_encode()])


func mesh_instance(mesh: Mesh, location: Vector3, metallic: float, roughness: float) -> MeshInstance3D:
	var material := StandardMaterial3D.new()
	material.metallic = metallic
	material.roughness = roughness
	var instance := MeshInstance3D.new()
	instance.mesh = mesh
	instance.material_override = material
	instance.position = location
	view.add_child(instance)
	return instance


func white_furnace() -> void:
	# Isolate the BRDF furnace from occlusion between neighboring spheres.
	sky.rtao_enabled = false
	var spheres: Array[MeshInstance3D] = []
	var sphere := SphereMesh.new()
	sphere.radius = 0.44
	sphere.height = 0.88
	sphere.radial_segments = 64
	sphere.rings = 32
	for row in 2:
		for col in ROUGHNESSES.size():
			spheres.push_back(mesh_instance(sphere, Vector3((col - 2) * 1.2, (1 - 2 * row) * 0.8, 0.0), 1.0 - row, ROUGHNESSES[col]))

	var furnace := await snapshot()
	print_image_hash("furnace", furnace)
	var background := furnace.get_pixel(0, 0)
	check(error_rgb(background, Color.WHITE) < 0.001, "White Furnace sky has unit linear radiance")
	var format := furnace.get_format()
	check(format in [Image.FORMAT_RGBH, Image.FORMAT_RGBAH, Image.FORMAT_RGBF, Image.FORMAT_RGBAF],
			"Readback retains linear HDR, got format %d" % format)

	# Generate a geometry mask using the same sphere coverage at zero IBL intensity.
	sky.sky_intensity = 0.0
	for instance in spheres:
		instance.material_override.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	var mask := await snapshot()
	sky.sky_intensity = 1.0
	var counts := PackedInt32Array()
	var maxima := PackedFloat32Array()
	counts.resize(spheres.size())
	maxima.resize(spheres.size())
	for y in SIZE.y:
		for x in SIZE.x:
			var point := Vector2i(x, y)
			if mask.get_pixelv(point).r > 0.5:
				var index := floori(float(y) / 160.0) * 5 + floori(float(x) / 120.0)
				counts[index] += 1
				maxima[index] = maxf(maxima[index], error_rgb(furnace.get_pixelv(point), background))
	for i in spheres.size():
		print("FURNACE metallic=%d roughness=%.2f samples=%d max_linear_error=%.7f" % [
			1 - floori(float(i) / 5.0), ROUGHNESSES[i % 5], counts[i], maxima[i]])
		check(counts[i] > 5000 and maxima[i] < 0.02, "White Furnace sphere %d" % i)
		spheres[i].free()
	sky.rtao_enabled = true


func radiance(dir: Vector3) -> Color:
	var sun := 6.0 * pow(maxf(dir.dot(SUN), 0.0), 128.0)
	return Color(0.2 + 0.12 * dir.y + sun, 0.32 + 0.14 * dir.y + 0.7 * sun, 0.45 + 0.2 * dir.y + 0.4 * sun)


func make_panorama() -> ImageTexture:
	var image := Image.create_empty(512, 256, false, Image.FORMAT_RGBAH)
	for y in image.get_height():
		var latitude := PI * (y + 0.5) / image.get_height()
		for x in image.get_width():
			var longitude := TAU * ((x + 0.5) / image.get_width() - 0.5)
			var dir := Vector3(sin(latitude) * sin(longitude), cos(latitude), -sin(latitude) * cos(longitude))
			image.set_pixel(x, y, radiance(dir))
	return ImageTexture.create_from_image(image)


func check_sky_rays(image: Image, rotation: float) -> float:
	var max_error := 0.0
	for y in range(2, SIZE.y, 5):
		for x in range(2, SIZE.x, 5):
			var ray := camera.project_ray_normal(Vector2(x + 0.5, y + 0.5))
			var expected := radiance(ray.rotated(Vector3.UP, -rotation))
			max_error = maxf(max_error, error_rgb(image.get_pixel(x, y), expected))
	check(max_error < 0.055, "Sky matches analytic HDRI across cube edges, with +Y at zenith")
	return max_error


func save_and_reload_environment() -> void:
	var container := Node.new()
	var saved := YutrelEnvironment.new()
	saved.name = "Sky"
	saved.sky_mode = sky.sky_mode
	saved.sky_color = Color(0.2, 0.3, 0.4)
	saved.sky_texture = sky.sky_texture
	saved.sky_rotation = 47.5
	saved.sky_intensity = 1.5
	saved.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	saved.exposure = 1.25
	container.add_child(saved)
	saved.owner = container
	var scene := PackedScene.new()
	check(scene.pack(container) == OK, "Pack YutrelEnvironment")
	var path := "user://ibl_environment.tscn"
	check(ResourceSaver.save(scene, path) == OK, "Save the environment as tscn")
	var readback := ResourceLoader.load(path, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	check(readback != null, "Load the environment from tscn")
	if readback:
		var instance := readback.instantiate()
		var loaded: YutrelEnvironment = instance.get_node("Sky")
		check(loaded.sky_mode == saved.sky_mode and loaded.sky_color == saved.sky_color
				and loaded.sky_rotation == 47.5 and loaded.sky_intensity == 1.5
				and loaded.tone_mapping == saved.tone_mapping and loaded.exposure == 1.25,
				"Serialized sky and tone mapping properties")
		check(loaded.sky_texture != null and loaded.sky_texture.get_image().get_data() == saved.sky_texture.get_image().get_data(),
				"Serialized HDR panorama")
		instance.free()
	container.free()


func hdri() -> void:
	sky.sky_mode = YutrelEnvironment.SKY_MODE_HDRI
	var fallback := await snapshot()
	check(error_rgb(fallback.get_pixel(0, 0), Color.WHITE) < 0.001, "Missing HDRI falls back to sky color")
	sky.sky_texture = make_panorama()

	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	camera.fov = 70.0
	camera.look_at(camera.position + Vector3(1.0, 0.8, 1.0))
	var unrotated := await snapshot()
	print_image_hash("hdri", unrotated)
	var max_error := check_sky_rays(unrotated, 0.0)
	check(unrotated.get_pixel(300, 15).b > unrotated.get_pixel(300, 305).b, "Zenith is up")

	camera.rotation = Vector3.ZERO
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	var quad := QuadMesh.new()
	var reflective := mesh_instance(quad, Vector3(-1.0, 0.0, 0.0), 1.0, 0.05)
	var diffuse := mesh_instance(quad, Vector3(1.0, 0.0, 0.0), 0.0, 1.0)
	diffuse.material_override.metallic_specular = 0.0
	var lit := await snapshot()
	var lit_specular := lit.get_pixel(200, 160)
	var lit_diffuse := lit.get_pixel(400, 160)
	var dark_sky := lit.get_pixel(0, 0)
	check(error_rgb(lit.get_pixel(0, 319), dark_sky) < 0.001, "Orthographic sky rays are parallel")
	print("LIVE SKY CHANGES BEGIN")
	sky.sky_rotation = 180.0
	var rotated := await snapshot()
	print_image_hash("rotated", rotated)
	var rotated_specular := rotated.get_pixel(200, 160)
	var rotated_diffuse := rotated.get_pixel(400, 160)
	var bright_sky := rotated.get_pixel(0, 0)
	check(bright_sky.r > 5.9 and dark_sky.r < 0.25, "Rotation places the HDR sun in the camera direction")
	check(lit_specular.r > 5.0 and rotated_specular.r < 0.25, "Rotation moves the same sun out of the mirror direction")
	# For a sun radiance 6 * cos^128, the exact Lambert integral is 12/130.
	# The L2 SH projection approximates this at the pole with about 0.098.
	check(absf(lit_diffuse.r - (0.2 + 12.0 / 130.0)) < 0.02 and absf(rotated_diffuse.r - 0.2) < 0.02,
			"SH diffuse agrees with the HDR sun directions")
	sky.sky_intensity = 0.5
	var dimmed := await snapshot()
	check(error_rgb(dimmed.get_pixel(0, 0), 0.5 * bright_sky) < 0.005
			and error_rgb(dimmed.get_pixel(200, 160), 0.5 * rotated_specular) < 0.005
			and error_rgb(dimmed.get_pixel(400, 160), 0.5 * rotated_diffuse) < 0.005,
			"Intensity scales skybox, specular, and diffuse together")
	sky.sky_intensity = 1.0
	reflective.free()
	diffuse.free()
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	camera.look_at(camera.position + Vector3(-1.0, -0.8, -1.0))
	max_error = maxf(max_error, check_sky_rays(await snapshot(), PI))
	sky.sky_rotation = 90.0
	camera.look_at(camera.position + Vector3.RIGHT)
	max_error = maxf(max_error, check_sky_rays(await snapshot(), PI / 2.0))
	print("LIVE SKY CHANGES END")
	print("HDRI seam/grid max_error=%.6f sky_sun=%.6f->%.6f specular=%.6f->%.6f diffuse=%.6f->%.6f" % [
		max_error, dark_sky.r, bright_sky.r, lit_specular.r, rotated_specular.r, lit_diffuse.r, rotated_diffuse.r])
	save_and_reload_environment()


func run() -> void:
	root.disable_3d = true
	RenderingServer.viewport_set_disable_2d(root.get_viewport_rid(), true)
	view = SubViewport.new()
	view.size = SIZE
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)

	camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 3.2
	camera.position.z = 5.0
	view.add_child(camera)
	camera.make_current()

	sky = YutrelEnvironment.new()
	sky.sky_color = Color.WHITE
	sky.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	view.add_child(sky)
	await white_furnace()

	# A native WorldEnvironment added later must not replace the active Yutrel sky.
	var native := WorldEnvironment.new()
	native.environment = Environment.new()
	native.environment.background_mode = Environment.BG_COLOR
	native.environment.background_color = Color.MAGENTA
	view.add_child(native)
	await hdri()
	native.free()

	if OS.has_environment("YUTREL_IBL_RELOAD_MARKER"):
		var before := await snapshot()
		print("IBL READY FOR RELOAD")
		var marker := OS.get_environment("YUTREL_IBL_RELOAD_MARKER")
		while not FileAccess.file_exists(marker):
			await create_timer(0.1).timeout
		check((await snapshot()).get_data() == before.get_data(), "Hot reload preserves identical rendered output")

	print("IBL PASS" if failures == 0 else "IBL FAIL")
	quit(failures)
