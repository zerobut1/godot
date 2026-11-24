extends SceneTree

var view: SubViewport
var camera: Camera3D
var sky: YutrelEnvironment
var failures := 0


func _initialize() -> void:
	run.call_deferred()


func check(ok: bool, message: String) -> void:
	if not ok:
		failures += 1
		push_error(message)


func close_rgb(actual: Color, expected: Color, message: String, epsilon := 0.004) -> void:
	var delta := (Vector3(actual.r, actual.g, actual.b) - Vector3(expected.r, expected.g, expected.b)).abs()
	check(delta[delta.max_axis_index()] <= epsilon, "%s: got %s, expected %s" % [message, actual, expected])


func snapshot() -> Image:
	for i in 3:
		await process_frame
		await RenderingServer.frame_post_draw
	return view.get_texture().get_image()


func center() -> Color:
	return (await snapshot()).get_pixelv(view.size / 2)


func panel(z: float, color: Color, transparent: bool) -> MeshInstance3D:
	var mesh := QuadMesh.new()
	mesh.size = Vector2(4, 3)
	var item := MeshInstance3D.new()
	item.mesh = mesh
	var mat := StandardMaterial3D.new()
	mat.albedo_color = color
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	if transparent:
		mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	item.material_override = mat
	item.position.z = z
	view.add_child(item)
	return item


func blending() -> void:
	var wall := panel(-6, Color.BLUE, false)
	var red := panel(-7, Color(1, 0, 0, 0.35), true)
	close_rgb(await center(), Color.BLUE, "Opaque wall occludes transparent geometry")
	red.position.z = -6
	var red_mix := Color.BLUE.lerp(Color.RED, 0.35)
	close_rgb(await center(), red_mix, "Coplanar decal passes Reverse-Z depth test")
	red.position.z = -4
	var green := panel(-5, Color(0, 1, 0, 0.5), true)
	var green_mix := Color.BLUE.lerp(Color.GREEN, 0.5)
	close_rgb(await center(), green_mix.lerp(Color.RED, 0.35), "Far green then near red")
	# Move only the mesh center in front of red. The instance origin is still farther away.
	green.mesh.center_offset.z = 2
	close_rgb(await center(), red_mix.lerp(Color.GREEN, 0.5), "Swapping depth reverses order")
	red.material_override.render_priority = 1
	close_rgb(await center(), green_mix.lerp(Color.RED, 0.35), "Priority lets a farther surface draw last without depth writes")
	red.material_override.render_priority = 0
	var legacy := red.material_override
	var native := YutrelMaterial.new()
	native.shading_model = YutrelMaterial.SHADING_MODEL_UNLIT
	native.alpha_mode = YutrelMaterial.ALPHA_MODE_BLEND
	native.base_color = Color(1, 0, 0, 0.35)
	red.material_override = native
	close_rgb(await center(), red_mix.lerp(Color.GREEN, 0.5), "Native blend follows depth at default priority")
	native.render_priority = 1
	close_rgb(await center(), green_mix.lerp(Color.RED, 0.35), "Native priority edit takes effect on the next draw")
	red.material_override = legacy
	green.mesh.center_offset.z = 1
	var tied := await center()
	for i in 3:
		close_rgb(await center(), tied, "Coplanar draws retain submission order")
	green.free()

	var texels := Image.create_empty(1, 1, false, Image.FORMAT_RGBA8)
	texels.fill(Color(1, 1, 1, 0.5))
	red.material_override.albedo_texture = ImageTexture.create_from_image(texels)
	close_rgb(await center(), Color.BLUE.lerp(Color.RED, 0.35 * texels.get_pixel(0, 0).a), "Albedo texture multiplies linear alpha")
	red.material_override.albedo_texture = null

	var mat := red.material_override as StandardMaterial3D
	var bg := Color(0.7, 0.65, 0.8)
	var rgb := Color(0.8, 0.4, 0.65)
	wall.material_override.albedo_color = bg
	bg = bg.srgb_to_linear()
	var src := rgb.srgb_to_linear()
	# Exercise classification by blend mode even when TRANSPARENCY_ALPHA is disabled.
	mat.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
	for mode in [BaseMaterial3D.BLEND_MODE_ADD, BaseMaterial3D.BLEND_MODE_SUB, BaseMaterial3D.BLEND_MODE_MUL]:
		mat.blend_mode = mode
		for alpha: float in [0.0, 0.35, 1.0]:
			mat.albedo_color = Color(rgb, alpha)
			var expected := bg
			match mode:
				BaseMaterial3D.BLEND_MODE_ADD:
					expected = bg + src * alpha
				BaseMaterial3D.BLEND_MODE_SUB:
					expected = (bg - src * alpha).clamp()
				BaseMaterial3D.BLEND_MODE_MUL:
					expected = bg * Color.WHITE.lerp(src, alpha)
			close_rgb(await center(), expected, "Mode %d at alpha %s" % [mode, alpha])
	red.free()
	wall.free()


func lighting() -> void:
	var bg := Color(0.12, 0.22, 0.35)
	var wall := panel(-8, bg, false)
	var receiver := panel(-6, Color.WHITE, true)
	var mat := ORMMaterial3D.new()
	mat.albedo_color = Color(0.8, 0.55, 0.3)
	mat.cull_mode = BaseMaterial3D.CULL_DISABLED
	receiver.material_override = mat
	receiver.rotate_y(PI)
	var sun := DirectionalLight3D.new()
	view.add_child(sun)
	sky.sky_color = Color.WHITE
	sky.sky_intensity = 0.4
	var opaque := await center()
	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	close_rgb(await center(), opaque, "Double-sided forward ORM matches deferred direct and IBL", 0.008)
	mat.albedo_color.a = 0.35
	close_rgb(await center(), bg.srgb_to_linear().lerp(opaque, 0.35), "Lit decal with linear alpha 0.35", 0.005)
	mat.albedo_color.a = 1.0
	wall.free()
	sun.hide()
	var indirect := await center()
	check(indirect.r > 0.1, "Transparent-only scene receives IBL")
	var native := YutrelMaterial.new()
	native.alpha_mode = YutrelMaterial.ALPHA_MODE_BLEND
	native.base_color = mat.albedo_color
	native.cull_mode = YutrelMaterial.CULL_MODE_DISABLED
	receiver.material_override = native
	close_rgb(await center(), indirect, "Native forward material matches legacy indirect light")
	var ao := Image.create_empty(1, 1, false, Image.FORMAT_RGBA8)
	ao.fill(Color(0, 1, 1))
	native.occlusion_texture = ImageTexture.create_from_image(ao)
	close_rgb(await center(), Color.BLACK, "Native AO uses the packed red channel")
	native.occlusion_strength = 0.0
	close_rgb(await center(), indirect, "Zero AO strength restores indirect light")
	receiver.material_override = mat
	view.transparent_bg = true
	var no_sky := await center()
	close_rgb(no_sky, indirect, "Forward IBL with opaque lighting and skybox both disabled")
	receiver.hide()
	var clear := await center()
	check(absf(no_sky.a - clear.a) < 0.001, "Transparent pass preserves the cleared SceneColor alpha")
	view.transparent_bg = false
	close_rgb(await center(), Color.WHITE * 0.4, "Sky in transparent-only scene")
	receiver.show()
	mat.albedo_color.a = 0.35
	close_rgb(await center(), (Color.WHITE * 0.4).lerp(indirect, 0.35), "Transparent surface blends over the sky")
	mat.albedo_color.a = 1.0
	sky.sky_intensity = 0
	sun.show()
	var direct := await center()
	check(direct.r > 0.1, "Back-facing normal flipped for direct lighting")
	close_rgb(indirect + direct, opaque, "Direct and indirect radiance add", 0.009)
	var fills: Array[DirectionalLight3D] = []
	for i in 4:
		var fill := DirectionalLight3D.new()
		view.add_child(fill)
		fills.append(fill)
	close_rgb(await center(), direct * 5, "All five directional lights contribute")
	for fill in fills:
		fill.free()

	# Shadow-only geometry isolates the cast shadow from camera depth occlusion.
	var caster := panel(-3, Color.WHITE, false)
	caster.scale.x = 0.5
	caster.position.x = -1
	caster.cast_shadow = GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY
	sun.shadow_enabled = true
	sun.directional_shadow_max_distance = 15
	sun.directional_shadow_blend_splits = true
	var left := Vector2i(camera.unproject_position(Vector3(-1, 0, -6)))
	var right := Vector2i(camera.unproject_position(Vector3(1, 0, -6)))
	var shadow := await snapshot()
	check(shadow.get_pixelv(left).r < direct.r * 0.05, "Opaque caster shadows transparent receiver")
	close_rgb(shadow.get_pixelv(right), direct, "Other half remains lit")
	var second := DirectionalLight3D.new()
	second.light_color = Color.GREEN
	second.shadow_enabled = true
	second.directional_shadow_mode = DirectionalLight3D.SHADOW_PARALLEL_2_SPLITS
	second.directional_shadow_max_distance = 15
	view.add_child(second)
	second.look_at(Vector3(0.55, 0, -1))
	sun.light_color = Color.RED
	var both := await snapshot()
	var a := both.get_pixelv(left)
	var b := both.get_pixelv(right)
	check(a.r < 0.01 and a.g > 0.03 and b.r > 0.1 and b.g < 0.01, "Two shadowed lights use independent cascade layers: %s %s" % [a, b])
	second.free()
	sun.light_color = Color.WHITE
	mat.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
	caster.material_override.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	caster.material_override.albedo_color.a = 0.8
	var no_cast := await snapshot()
	close_rgb(no_cast.get_pixelv(left), no_cast.get_pixelv(right), "Transparent caster leaves opaque receiver lit")
	check(no_cast.get_pixelv(left).r > 0.1, "Opaque receiver receives light")

	# Both surfaces share an instance mask. The shadow query must filter each surface.
	var caster_mesh := caster.mesh as QuadMesh
	var caster_material := caster.material_override
	var mixed := ArrayMesh.new()
	var arrays := caster_mesh.get_mesh_arrays()
	mixed.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mixed.surface_set_material(0, native)
	var positions: PackedVector3Array = arrays[Mesh.ARRAY_VERTEX]
	for i in positions.size():
		positions[i].x += caster_mesh.size.x
	arrays[Mesh.ARRAY_VERTEX] = positions
	mixed.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mixed.surface_set_material(1, mat)
	caster.material_override = null
	caster.mesh = mixed
	sky.shadow_mode = YutrelEnvironment.SHADOW_MODE_RAY_TRACED
	var rt_shadow := await snapshot()
	check(rt_shadow.get_pixelv(left).r > 0.1 and rt_shadow.get_pixelv(right).r < 0.01,
			"RT shadow skips the translucent half of a mixed mesh and blocks on its opaque half")
	sky.shadow_mode = YutrelEnvironment.SHADOW_MODE_CSM
	caster.mesh = caster_mesh
	caster.material_override = caster_material

	mat.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	mat.albedo_color.a = 0.35
	caster.material_override.transparency = BaseMaterial3D.TRANSPARENCY_DISABLED
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	var before := await snapshot()
	var left_p := Vector2i(camera.unproject_position(Vector3(-1, 0, -6)))
	var right_p := Vector2i(camera.unproject_position(Vector3(1, 0, -6)))
	check(before.get_pixelv(left_p).r < 0.01 and before.get_pixelv(right_p).r > 0.03, "Perspective transparent receiver retains its shadow")
	var digest := HashingContext.new()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(before.get_data())
	print("TRANSPARENT IMAGE ", digest.finish().hex_encode())
	if OS.has_environment("YUTREL_TRANSPARENT_RELOAD_MARKER"):
		print("TRANSPARENT READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_TRANSPARENT_RELOAD_MARKER")):
			await create_timer(0.1).timeout
		check(before.get_data() == (await snapshot()).get_data(), "Hot reload preserves the shadowed transparent image")


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
	camera.far = 20
	camera.size = 4.0
	view.add_child(camera)
	camera.make_current()
	sky = YutrelEnvironment.new()
	sky.shadow_mode = YutrelEnvironment.SHADOW_MODE_CSM
	sky.shadow_resolution = 1024
	sky.sky_intensity = 0
	sky.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	view.add_child(sky)
	await blending()
	await lighting()
	print("TRANSPARENT RUNTIME PASS" if failures == 0 else "TRANSPARENT RUNTIME FAIL")
	quit(failures)
