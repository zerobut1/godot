extends SceneTree

signal profile_ready(stages: PackedStringArray)

var view: SubViewport
var camera: Camera3D
var sky: YutrelEnvironment
var rig: Node3D
var wall: MeshInstance3D
var panel: MeshInstance3D
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


func probe(image: Image, x := 0.0, y := 0.0) -> Color:
	return image.get_pixelv(Vector2i(camera.unproject_position(rig.to_global(Vector3(x, y, 0)))))


func resolution(half_res: bool) -> void:
	sky.rt_reflection_half_resolution = half_res
	var mirror := await snapshot(5)
	var stages := await profile()
	check(stages.has("Yutrel RT Reflection Denoise") and stages.has("Yutrel RT Reflection Upsample") == half_res, "Only half resolution dispatches the joint upsampler")
	var red := probe(mirror)
	print("RT_REFLECTION MIRROR half=%s radiance=%s" % [half_res, red])
	check(camera.is_position_behind(panel.global_position) and red.r > 2.95 and red.g < 0.005, "Mirror sees the HDR panel behind the camera")
	var clear := probe(mirror, -2)
	check(clear.r > 0.1 and absf(clear.r - clear.g) < 0.001, "Mirror sees the sky beside the panel")
	sky.rt_reflection_samples = 4
	check((await snapshot()).get_data() == mirror.get_data(), "Mirror is deterministic across sample counts")
	sky.rt_reflection_enabled = false
	var baseline := await snapshot()
	check(absf(probe(baseline).r - clear.r) < 0.002, "Disabling RT restores sky IBL")
	sky.rt_reflection_enabled = true
	# In orthographic projection the panel coordinates map directly onto the wall.
	# A single step edge and finite background give the two glossy lobes the
	# same hit distance. Both panels face the mirror and are culled by raster.
	panel.mesh.size = Vector2(20, 10)
	panel.position = Vector3(0, -5, 2)
	var backing := MeshInstance3D.new()
	var backing_mesh := QuadMesh.new()
	backing_mesh.size = Vector2(30, 30)
	var gray := StandardMaterial3D.new()
	gray.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	gray.albedo_color = sky.sky_color
	backing_mesh.material = gray
	backing.mesh = backing_mesh
	backing.rotation = panel.rotation
	backing.position.z = 2.1
	rig.add_child(backing)
	wall.hide()
	var strips: Array[MeshInstance3D] = []
	var roughness := [0.02, 0.25, 0.50, 0.85]
	for i in 4:
		var strip := MeshInstance3D.new()
		var mesh := QuadMesh.new()
		mesh.size = Vector2(1.5, 4)
		var mat := StandardMaterial3D.new()
		mat.metallic = 1
		mat.roughness = roughness[i]
		mesh.material = mat
		strip.mesh = mesh
		strip.position.x = (i - 1.5) * 1.5
		rig.add_child(strip)
		strips.append(strip)
	var bands := await snapshot(24)
	var widths: Array[int] = []
	for i in 3:
		var x := strips[i].position.x
		var peak := probe(bands, x, -1.7).r
		var width := 0
		for y in range(view.size.y):
			var column := int(camera.unproject_position(strips[i].global_position).x)
			var v := bands.get_pixel(column, y).r
			check(is_finite(v), "Finite HDR reflection")
			if v > clear.r + 0.08 * (peak - clear.r) and v < peak - 0.08 * (peak - clear.r):
				width += 1
		widths.append(width)
	print("RT_REFLECTION EDGE half=%s transition widths=%s" % [half_res, widths])
	check(widths[0] <= 2 and widths[1] > widths[0] and widths[2] > widths[1], "Glossy reflections broaden with roughness")
	var rough := probe(bands, strips[3].position.x)
	sky.rt_reflection_enabled = false
	var fallback := probe(await snapshot(), strips[3].position.x)
	check(rough.is_equal_approx(fallback) and rough.r > 0.1, "The rough band uses prefiltered IBL")
	sky.rt_reflection_enabled = true
	strips[3].mesh.material.roughness = 0.59
	var fade := probe(await snapshot(8), strips[3].position.x, -1)
	check(absf(fade.r - fallback.r) < 0.06, "The fade approaches the IBL value continuously at the cutoff")
	digest.update(bands.get_data())
	for strip in strips:
		strip.free()
	backing.free()
	panel.position = Vector3(0, 0, 6)
	panel.mesh.size = Vector2(1.5, 1.2)
	wall.show()
	sky.rt_reflection_samples = 1


func moving_camera() -> void:
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	var virtual_panel := rig.to_global(Vector3(0, 0, -panel.position.z))
	var old_center := Vector2i(camera.unproject_position(virtual_panel))
	await snapshot(16)
	camera.position.x += 1.5
	var moved := await snapshot(1)
	var new_center := Vector2i(camera.unproject_position(virtual_panel))
	check(moved.get_pixelv(new_center).r > 2.9 and moved.get_pixelv(old_center).r < 0.2, "Mirror tracks the virtual panel after a camera translation")
	camera.position.x = 0
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	await snapshot(16)
	panel.position.x = 1.5
	var uncovered := await snapshot(1)
	check(probe(uncovered).r < 0.2 and probe(uncovered, 1.5).r > 2.9, "A moving off-screen emitter uncovers the previous mirror image immediately")
	panel.position.x = 0


func furnace() -> void:
	wall.hide()
	panel.hide()
	sky.sky_color = Color.WHITE
	var sphere := MeshInstance3D.new()
	sphere.mesh = SphereMesh.new()
	var mat := StandardMaterial3D.new()
	mat.metallic = 1
	mat.roughness = 0.35
	sphere.material_override = mat
	rig.add_child(sphere)
	var white := await snapshot(20)
	sky.sky_intensity = 0
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	var mask := await snapshot()
	var samples := 0
	var error := 0.0
	for y in view.size.y:
		for x in view.size.x:
			if mask.get_pixel(x, y).r > 0.5:
				var c := white.get_pixel(x, y)
				samples += 1
				error = maxf(error, maxf(absf(1 - c.r), maxf(absf(1 - c.g), absf(1 - c.b))))
	print("RT_REFLECTION FURNACE pixels=%d max_linear_error=%.6f" % [samples, error])
	check(samples > 500 and error < 0.02, "Isolated metallic sphere conserves white-furnace energy with RT enabled")
	sphere.free()
	panel.show()
	wall.show()


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


func check_shared_scene() -> void:
	var stages := await profile()
	check(stages.count("Yutrel RT Scene Update") == 1 and stages.has("Yutrel RTAO Trace") and stages.has("Yutrel RT Shadow Trace"), "All four ray effects share one TLAS update")
	var probes := stages.find("< Yutrel DDGI")
	var reflection := stages.find("Yutrel RT Reflection Trace")
	check(probes >= 0 and probes < reflection and reflection < stages.find("Yutrel IBL / Indirect"), "Reflections read updated probes before deferred IBL")


func hit_lighting() -> void:
	# The reflected wall is inside the room, while the mirror stays outside the volume.
	var volume := YutrelDDGIVolume.new()
	volume.position = panel.position
	volume.size = Vector3(4, 4, 4)
	volume.probe_counts = Vector3i(5, 5, 5)
	volume.rays_per_probe = 144
	volume.hysteresis = 0
	volume.probe_classification = false
	rig.add_child(volume)
	var white := StandardMaterial3D.new()
	white.metallic_specular = 0
	panel.material_override = white
	var sides: Array[MeshInstance3D] = []
	for x in [-1.5, 1.5]:
		var side := MeshInstance3D.new()
		var box := BoxMesh.new()
		box.size = Vector3(0.1, 3, 3)
		side.mesh = box
		side.material_override = panel.mesh.material
		side.position = panel.position + Vector3(x, 0, -1)
		rig.add_child(side)
		sides.append(side)
	var bounced := probe(await snapshot(40))
	volume.hide()
	var dark := probe(await snapshot(4))
	print("RT_REFLECTION BOUNCE active=%s inactive=%s" % [bounced, dark])
	check(bounced.r > 0.05 and bounced.g < 0.005 and dark.r < 0.005, "Probes light the reflected diffuse wall in a black sky")
	for side in sides:
		side.free()
	# An oblique sun illuminates only the back-facing panel. A small box on
	# the secondary shadow ray stays clear of the camera-to-mirror light path.
	var sun := DirectionalLight3D.new()
	sun.rotation.y = deg_to_rad(135)
	sun.shadow_enabled = true
	rig.add_child(sun)
	volume.intensity = 0
	volume.show()
	sky.rtao_enabled = true
	sky.shadow_mode = YutrelEnvironment.SHADOW_MODE_RAY_TRACED
	await check_shared_scene()
	var sunlit := probe(await snapshot())
	var blocker := MeshInstance3D.new()
	var box := BoxMesh.new()
	box.size = Vector3.ONE * 0.3
	blocker.mesh = box
	blocker.position = panel.position + Vector3(0.6, 0, -0.6)
	rig.add_child(blocker)
	var shadowed := probe(await snapshot())
	check(sunlit.r > 0.15 and shadowed.r < 0.01, "The reflected panel receives shadowed direct illumination")
	sun.free()
	blocker.free()
	volume.hide()
	sky.rtao_enabled = false
	white.metallic = 1
	white.roughness = 0.3
	sky.sky_intensity = 1
	var metal := probe(await snapshot(8))
	check(metal.r > 0.98 and metal.g > 0.98, "A reflected metal receives prefiltered environment specular")
	volume.show()
	volume.intensity = 0
	var occluded := probe(await snapshot(20))
	check(occluded.r < 0.005, "DDGI sky visibility attenuates specular at the metallic hit")
	volume.free()


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
	rig.position.z = -4
	wall = rig.get_node("Wall")
	panel = rig.get_node("Panel")
	sky = scene.get_node("Environment")
	check(sky.rt_reflection_enabled and sky.rt_reflection_half_resolution and is_equal_approx(sky.rt_reflection_max_roughness, 0.6) and sky.rt_reflection_samples == 1, "Reflection defaults")
	sky.rt_reflection_max_roughness = -1
	sky.rt_reflection_samples = 8
	check(is_equal_approx(sky.rt_reflection_max_roughness, 0.1) and sky.rt_reflection_samples == 4, "Reflection settings bounds")
	sky.rt_reflection_max_roughness = 0.6
	sky.rt_reflection_samples = 1
	camera = Camera3D.new()
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 4
	view.add_child(camera)
	camera.make_current()
	await snapshot()
	if OS.has_environment("YUTREL_RT_REFLECTION_RELOAD_MARKER"):
		check(preload("res://test_region.gd").new().run(OS.get_environment("YUTREL_REFLECTION_REGION_SPIRV")), "Non-zero render-region origin and odd bottom-right texel")
		var before := await snapshot()
		print("RT_REFLECTION READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_RT_REFLECTION_RELOAD_MARKER")):
			await create_timer(0.1).timeout
		check((await snapshot()).get_data() == before.get_data(), "Shader hot reload preserves the mirror")
	digest.start(HashingContext.HASH_SHA256)
	await resolution(true)
	await resolution(false)
	await moving_camera()
	await furnace()
	await hit_lighting()
	print("RT_REFLECTION IMAGE ", digest.finish().hex_encode())
	print("RT_REFLECTION RUNTIME PASS" if failures == 0 else "RT_REFLECTION RUNTIME FAIL")
	quit(failures)
