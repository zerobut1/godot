extends SceneTree

var view: SubViewport
var camera: Camera3D
var sky: YutrelEnvironment
var geometry: Node3D
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


func add_mesh(mesh: Mesh, position: Vector3, mat: Material) -> MeshInstance3D:
	var item := MeshInstance3D.new()
	item.mesh = mesh
	item.material_override = mat
	item.position = position
	geometry.add_child(item)
	return item


func quad(position: Vector3, size: Vector2, mat: Material) -> MeshInstance3D:
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


func index_and_color_formats() -> void:
	# One BLAS containing a non-indexed triangle and a surface whose indices exceed 16 bits.
	var mesh := ArrayMesh.new()
	var mat := material(Color.WHITE)
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.vertex_color_use_as_albedo = true
	mat.vertex_color_is_srgb = true
	for surface in 2:
		var count := 3 if surface == 0 else 65539
		var vertices := PackedVector3Array()
		var normals := PackedVector3Array()
		var colors := PackedColorArray()
		vertices.resize(count)
		normals.resize(count)
		colors.resize(count)
		normals.fill(Vector3.BACK)
		colors.fill(Color(0.5, 0.25, 0.75))
		var x := -1.5 if surface == 0 else 1.5
		vertices[count - 3] = Vector3(x - 1, -1, 0)
		vertices[count - 2] = Vector3(x, 1, 0)
		vertices[count - 1] = Vector3(x + 1, -1, 0)
		var arrays := []
		arrays.resize(Mesh.ARRAY_MAX)
		arrays[Mesh.ARRAY_VERTEX] = vertices
		arrays[Mesh.ARRAY_NORMAL] = normals
		arrays[Mesh.ARRAY_COLOR] = colors
		if surface == 1:
			arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([count - 3, count - 2, count - 1])
		mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	var item := add_mesh(mesh, Vector3(0, 0, -4), mat)
	var linear := await snapshot()
	for x in [-1.5, 1.5]:
		var value := probe(linear, item.position + Vector3(x, 0, 0))
		check(absf(value.r - 0.214) < 0.005 and absf(value.b - 0.523) < 0.008,
				"Indexed and non-indexed triangles decode sRGB vertex colors at x=%s: %s" % [x, value])
	mat.vertex_color_is_srgb = false
	var raw := probe(await snapshot(), Vector3(1.5, 0, -4))
	check(absf(raw.r - 0.5) < 0.005, "Raw vertex colors follow the material flag")
	item.rotation.y = PI
	check(probe(await snapshot(), Vector3(1.5, 0, -4)).r < 0.001, "Opaque single-sided back faces are culled")
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	check(probe(await snapshot(), Vector3(1.5, 0, -4)).r > 0.49, "Disabling culling reveals the opaque back faces")
	reset_geometry()


func cutout() -> void:
	var receiver := material(Color.WHITE)
	quad(Vector3(0, 0, -6), Vector2(12, 8), receiver)
	var image := Image.create_empty(32, 32, false, Image.FORMAT_RGBA8)
	image.fill(Color.RED)
	image.fill_rect(Rect2i(10, 10, 12, 12), Color.TRANSPARENT)
	var mat := material(Color.WHITE)
	mat.albedo_texture = ImageTexture.create_from_image(image)
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	var mesh := QuadMesh.new()
	mesh.size = Vector2(2, 2)
	var packed := ArrayMesh.new()
	packed.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, mesh.get_mesh_arrays(), [], {}, Mesh.ARRAY_FLAG_COMPRESS_ATTRIBUTES)
	packed.surface_set_material(0, mat)
	# Put a completely transparent surface in front of both the rim and hole of the same BLAS.
	var overlay := material(Color(0, 1, 0, 0.0))
	overlay.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	overlay.cull_mode = BaseMaterial3D.CULL_DISABLED
	var arrays := mesh.get_mesh_arrays()
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	for i in positions.size():
		positions[i].z -= 0.1 # The panel is rotated 180 degrees below.
	arrays[Mesh.ARRAY_VERTEX] = positions
	packed.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	packed.surface_set_material(1, overlay)
	var panel := add_mesh(packed, Vector3(1, 0, -4), null)
	panel.scale.x = -1
	panel.rotate_y(PI) # View the mirrored, double-sided back face.
	var sun := DirectionalLight3D.new()
	geometry.add_child(sun)
	sun.look_at(Vector3(-1.4, 0, -1))
	var lit := await snapshot(32)
	var rim := probe(lit, Vector3(1.7, 0, -4))
	var hole := probe(lit, panel.position)
	var shadow_hole := probe(lit, Vector3(-1.8, 0, -6))
	var shadow_rim := probe(lit, Vector3(-1.1, 0, -6))
	print("CUTOUT rim=%s hole=%s shadow hole=%s rim=%s" % [rim, hole, shadow_hole, shadow_rim])
	check(rim.r > 0.1 and rim.g < 0.01, "Mirrored compressed cutout shows its red texture through the transparent overlay")
	check(hole.g > 0.12, "Primary ray passes through the alpha hole")
	check(shadow_hole.g > 0.12 and shadow_rim.g < shadow_hole.g * 0.1, "Cutout rim blocks the sun while its hole passes light")
	mat.cull_mode = BaseMaterial3D.CULL_BACK
	var back := probe(await snapshot(), Vector3(1.7, 0, -4))
	check(back.g > 0.12, "A one-sided back face reveals the receiver")
	mat.cull_mode = BaseMaterial3D.CULL_FRONT
	var front_culled := probe(await snapshot(), Vector3(1.7, 0, -4))
	check(front_culled.r > 0.1 and front_culled.g < 0.01, "Front culling exposes the mirrored back face")
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	panel.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
	var shadow_only := await snapshot()
	check(probe(shadow_only, Vector3(1.7, 0, -4)).g > 0.12 and probe(shadow_only, Vector3(-1.1, 0, -6)).g < 0.01,
			"Shadow-only geometry stays in occlusion queries while primary rays pass")
	var normal := Image.create_empty(4, 4, false, Image.FORMAT_RGBA8)
	normal.fill(Color(0.01, 0.5, 0.5))
	receiver.normal_enabled = true
	receiver.normal_texture = ImageTexture.create_from_image(normal)
	var mapped := probe(await snapshot(4), Vector3(3, 1, -6))
	check(mapped.g < hole.g * 0.25, "Tangent-space normal map changes directional illumination")
	reset_geometry()


func check_rgb(actual: Color, expected: Color, message: String, tolerance := 0.005) -> void:
	var delta := (Vector3(actual.r, actual.g, actual.b) - Vector3(expected.r, expected.g, expected.b)).abs()
	check(delta[delta.max_axis_index()] <= tolerance, "%s: got %s, expected %s" % [message, actual, expected])


func native_layers() -> void:
	var point := Vector3(0, 0, -4)
	var size := Vector2(3, 3)
	var back := YutrelMaterial.new()
	back.shading_model = YutrelMaterial.SHADING_MODEL_UNLIT
	back.base_color = Color.BLUE
	quad(point + Vector3.FORWARD, size, back)
	var layer := YutrelMaterial.new()
	layer.base_color = Color(0, 0, 0, 0.5)
	layer.emissive_color = Color.GREEN
	layer.emissive_strength = 0.75
	var pane := quad(point, size, layer)
	check_rgb(probe(await snapshot(), point), Color.GREEN * 0.75, "Opaque ignores base alpha and preserves emission")
	layer.alpha_mode = YutrelMaterial.ALPHA_MODE_MASK
	layer.alpha_cutoff = 0.6
	check_rgb(probe(await snapshot(), point), Color.BLUE, "Native alpha mask rejects the pane")
	layer.alpha_cutoff = 0.4
	check_rgb(probe(await snapshot(), point), Color.GREEN * 0.75, "Native alpha mask accepts the pane")
	layer.alpha_mode = YutrelMaterial.ALPHA_MODE_BLEND
	check_rgb(probe(await snapshot(48), point, 5), Color(0, 0.375, 0.5), "Half-alpha green emission over opaque blue", 0.035)
	var front := YutrelMaterial.new()
	front.shading_model = YutrelMaterial.SHADING_MODEL_UNLIT
	front.base_color = Color(1, 0, 0, 0.5)
	var top := quad(point + Vector3.BACK, size, front)
	front.alpha_mode = YutrelMaterial.ALPHA_MODE_BLEND
	check_rgb(probe(await snapshot(48), point, 5), Color(0.5, 0.1875, 0.25), "Stacked translucent layers", 0.035)
	top.free()

	# Default-lit emission exercises the reflected and straight-through branches.
	back.base_color = Color(0.7, 0.7, 0.7)
	var bg := back.base_color.srgb_to_linear()
	layer.alpha_mode = YutrelMaterial.ALPHA_MODE_OPAQUE
	for mode in [YutrelMaterial.BLEND_MODE_ADD, YutrelMaterial.BLEND_MODE_SUB, YutrelMaterial.BLEND_MODE_MUL]:
		layer.blend_mode = mode
		var expected := bg
		match mode:
			YutrelMaterial.BLEND_MODE_ADD:
				expected.g += 0.375
			YutrelMaterial.BLEND_MODE_SUB:
				expected.g -= 0.375
			YutrelMaterial.BLEND_MODE_MUL:
				expected *= Color.WHITE.lerp(Color.GREEN * 0.75, 0.5)
		check_rgb(probe(await snapshot(16), point, 5), expected, "Native path blend mode %d" % mode)
	pane.free()
	reset_geometry()


func native_orm() -> void:
	var texels := Image.create_empty(1, 1, false, Image.FORMAT_RGBA8)
	texels.fill(Color(0.1, 0.65, 0.85))
	var packed := ImageTexture.create_from_image(texels)
	var legacy := ORMMaterial3D.new()
	legacy.orm_texture = packed
	legacy.ao_enabled = true
	legacy.albedo_color = Color(0.7, 0.3, 0.1)
	var point := Vector3(0, 0, -4)
	var pane := quad(point, Vector2(3, 3), legacy)
	var sun := DirectionalLight3D.new()
	geometry.add_child(sun)
	sun.look_at(Vector3(0.9, 0, -1))
	var reference := probe(await snapshot(), point)
	check(reference.r > 0.01, "Packed ORM reference receives directional light")
	var native := YutrelMaterial.new()
	native.base_color = legacy.albedo_color
	native.metallic_texture = packed
	check(native.metallic == 0.0, "Scripted texture assignment preserves a zero metallic factor")
	native.metallic = 1.0
	native.roughness_texture = packed
	native.occlusion_texture = packed
	pane.material_override = native
	check_rgb(probe(await snapshot(), point), reference, "Native packed ORM matches the legacy bridge")
	native.metallic_channel = YutrelMaterial.TEXTURE_CHANNEL_RED
	var red_metallic := probe(await snapshot(), point)
	check(absf(red_metallic.r - reference.r) > 0.01, "Metallic channel selection changes the BRDF")
	native.roughness_channel = YutrelMaterial.TEXTURE_CHANNEL_RED
	var red_roughness := probe(await snapshot(), point)
	check(absf(red_roughness.r - red_metallic.r) > 0.01, "Roughness channel selection changes the BRDF")
	var channels := texels.get_pixel(0, 0)
	var scalar := StandardMaterial3D.new()
	scalar.albedo_color = legacy.albedo_color
	scalar.metallic = channels.r
	scalar.roughness = channels.g
	pane.material_override = scalar
	check_rgb(red_metallic, probe(await snapshot(), point), "Red metallic and green roughness match scalar factors", 0.001)
	scalar.roughness = channels.r
	check_rgb(red_roughness, probe(await snapshot(), point), "Red roughness matches its scalar factor", 0.001)
	reset_geometry()


func indirect() -> void:
	quad(Vector3(0, 0, -6), Vector2(9, 6), material(Color.WHITE))
	var red := material(Color.RED)
	var wall := quad(Vector3(2, 0, -4.6), Vector2(2.8, 6), red)
	wall.rotation.y = -PI / 2
	var sun := DirectionalLight3D.new()
	geometry.add_child(sun)
	sun.look_at(Vector3(1, 0, -0.3))
	var blocker := quad(Vector3(0, 0, -5.55), Vector2(0.7, 6), material(Color.BLACK))
	blocker.rotation.y = PI / 2
	var point := Vector3(1.5, 0, -6)
	var bounce := probe(await snapshot(96), point)
	print("RED BOUNCE ", bounce)
	check(bounce.r > 0.03 and bounce.g < bounce.r * 0.12 and bounce.b < bounce.r * 0.12,
			"Sunlit red wall illuminates the shadowed white receiver")
	wall.hide()
	check(probe(await snapshot(), point).r < 0.001, "Removing the red reflector eliminates indirect light")
	blocker.hide()
	check(probe(await snapshot(), point).g > 0.05, "Removing the blocker exposes the receiver to direct white light")
	wall.show()
	sun.free()
	var dark := probe(await snapshot(3), point)
	check(dark.r < 0.001, "Unlit room is dark before enabling emission")
	red.emission_enabled = true
	red.emission = Color.BLUE
	var glow := probe(await snapshot(64), point)
	check(glow.b > 0.08 and glow.r < glow.b * 0.1, "Blue emitter illuminates the receiver without a sun")
	wall.position = Vector3(0, 0, 1)
	wall.rotation.y = PI
	check(camera.is_position_behind(wall.position), "Emitter is entirely behind the camera")
	var offscreen := probe(await snapshot(96), Vector3(0, 0, -6))
	check(offscreen.b > 0.025 and offscreen.r < 0.001, "Off-camera emitter remains in the RT scene")
	reset_geometry()


func furnace() -> void:
	sky.sky_color = Color.WHITE
	sky.sky_intensity = 1
	var empty := (await snapshot()).get_pixel(64, 48)
	check(absf(empty.r - 1.0) < 0.002, "Empty TLAS returns the white sky")
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	var mesh := SphereMesh.new()
	mesh.radius = 0.85
	mesh.height = 1.7
	var sphere := ArrayMesh.new()
	sphere.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, mesh.get_mesh_arrays(), [], {}, Mesh.ARRAY_FLAG_COMPRESS_ATTRIBUTES)
	var metal := material(Color.WHITE)
	metal.metallic = 1
	metal.roughness = 0.08
	var dielectric := material(Color.WHITE)
	dielectric.metallic_specular = 0.5
	dielectric.roughness = 0.3
	var left := add_mesh(sphere, Vector3(-1.4, 0, -4), metal)
	var right := add_mesh(sphere, Vector3(1.4, 0, -4), dielectric)
	var early := await snapshot(4)
	var intermediate := await snapshot(32)
	var converged := await snapshot(260)
	var early_error := 0.0
	var intermediate_error := 0.0
	for y in range(view.size.y):
		for x in range(view.size.x):
			var reference := converged.get_pixel(x, y).r
			early_error += (early.get_pixel(x, y).r - reference) ** 2
			intermediate_error += (intermediate.get_pixel(x, y).r - reference) ** 2
	check(intermediate_error < early_error * 0.5, "Additional samples reduce mean squared image error")
	for item in [left, right]:
		var color := probe(converged, item.position, 4)
		print("WHITE FURNACE ", color)
		check(color.r >= 0.97 and color.r <= 1.02, "White furnace conserves energy for sphere at %s" % item.position)
	check(early.get_data() != converged.get_data(), "Successive samples refine the image")
	check(converged.get_data() == (await snapshot(4)).get_data(), "Converged image stays fixed after 256 samples")
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(converged.get_data())
	print("PATHTRACER IMAGE ", digest.finish().hex_encode())
	if OS.has_environment("YUTREL_PATHTRACER_RELOAD_MARKER"):
		print("PATHTRACER READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_PATHTRACER_RELOAD_MARKER")):
			await create_timer(0.1).timeout
		check(converged.get_data() == (await snapshot(258)).get_data(), "Both shader reloads reproduce the converged image")
	# Single-scatter GGX loses energy as masking increases. The smooth limit is unity.
	for roughness: float in [0.05, 0.25, 0.5]:
		metal.roughness = roughness
		var reflectance := probe(await snapshot(64), left.position, 4).r
		print("METAL FURNACE roughness=%.2f reflectance=%.5f" % [roughness, reflectance])
		var minimum := 0.88 if roughness == 0.5 else 0.97
		check(reflectance >= minimum and reflectance <= 1.02, "Single-scatter metal furnace at roughness %s" % roughness)
	dielectric.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	dielectric.albedo_color = Color.BLUE
	var changed := probe(await snapshot(2), right.position)
	check(changed.b > 0.99 and changed.r < 0.001, "Material edit immediately discards converged samples")
	var previous_pixel := Vector2i(camera.unproject_position(right.position))
	camera.position.x += 3.5
	var moved := (await snapshot(2)).get_pixelv(previous_pixel)
	check(moved.r > 0.99, "Camera move immediately replaces the old blue silhouette")
	right.position.x += 3.5
	var translated := (await snapshot(2)).get_pixelv(previous_pixel)
	check(translated.r < 0.001 and translated.b > 0.99, "Instance transform resets history and moves its TLAS hit")


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
	await index_and_color_formats()
	await cutout()
	await native_layers()
	await native_orm()
	await indirect()
	await furnace()
	print("PATHTRACER RUNTIME PASS" if failures == 0 else "PATHTRACER RUNTIME FAIL")
	quit(failures)
