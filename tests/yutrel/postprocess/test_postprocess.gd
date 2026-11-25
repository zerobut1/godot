extends SceneTree

const FLOAT_PROPERTIES := {
	"auto_exposure_min_ev": [-6, -12, 12],
	"auto_exposure_max_ev": [6, -12, 12],
	"auto_exposure_compensation": [0, -8, 8],
	"auto_exposure_speed_up": [3, 0.01, 20],
	"auto_exposure_speed_down": [1.5, 0.01, 20],
	"bloom_intensity": [0.15, 0, 4],
	"bloom_threshold": [1, 0, 64],
	"bloom_soft_knee": [0.5, 0, 1],
	"bloom_scatter": [0.65, 0.05, 0.95],
	"grading_temperature": [0, -1, 1],
	"grading_tint": [0, -1, 1],
	"grading_contrast": [1, 0, 2],
	"grading_saturation": [1, 0, 2],
	"grading_lut_intensity": [1, 0, 1],
	"vignette_intensity": [0, 0, 1],
	"vignette_smoothness": [0.5, 0.05, 1],
}
const BOOL_PROPERTIES := {"auto_exposure_enabled": false, "bloom_enabled": false, "debanding_enabled": true}
const COLOR_PROPERTIES := ["grading_shadows", "grading_midtones", "grading_highlights"]

var view: SubViewport
var env: YutrelEnvironment
var camera: Camera3D
var failures := 0
var digest := HashingContext.new()


func _initialize() -> void:
	run.call_deferred()


func check(condition: bool, message: String) -> void:
	if not condition:
		failures += 1
		push_error(message)


func snapshot(frames := 2) -> Image:
	for i in frames:
		await RenderingServer.frame_post_draw
	return view.get_texture().get_image()


func center(frames := 2) -> Color:
	var image := await snapshot(frames)
	return image.get_pixelv(view.size / 2)


func close_rgb(a: Color, b: Color, tolerance: float, label: String) -> void:
	var error := maxf(absf(a.r - b.r), maxf(absf(a.g - b.g), absf(a.b - b.b)))
	check(error <= tolerance, "%s: measured %s, expected %s (error %.7f)" % [label, a, b, error])


func make_strip(swap: bool) -> Image:
	var image := Image.create_empty(256, 16, false, Image.FORMAT_RGBA8)
	for b in 16:
		for g in 16:
			for r in 16:
				image.set_pixel(b * 16 + r, g, Color((b if swap else r) / 15.0, g / 15.0, (r if swap else b) / 15.0))
	return image


func properties() -> void:
	var node := YutrelEnvironment.new()
	for key in FLOAT_PROPERTIES:
		var spec: Array = FLOAT_PROPERTIES[key]
		check(is_equal_approx(node.get(key), spec[0]), key + " default")
		node.set(key, -1000.0)
		check(is_equal_approx(node.get(key), spec[1]), key + " lower bound")
		node.set(key, 1000.0)
		check(is_equal_approx(node.get(key), spec[2]), key + " upper bound")
		node.set(key, NAN)
		check(is_equal_approx(node.get(key), spec[2]), key + " rejects NaN")
		node.set(key, lerpf(spec[1], spec[2], 0.37))
	for key in BOOL_PROPERTIES:
		check(node.get(key) == BOOL_PROPERTIES[key], key + " default")
		node.set(key, not BOOL_PROPERTIES[key])
	for key in COLOR_PROPERTIES:
		check(node.get(key) == Color.WHITE, key + " default")
		node.set(key, Color(0.3, 0.55, 0.7))
	check(node.grading_lut == null, "Optional LUT defaults to null")
	node.grading_lut = ImageTexture.create_from_image(make_strip(true))
	var packed := PackedScene.new()
	check(packed.pack(node) == OK, "Pack environment")
	const PATH := "user://postprocess_environment.tscn"
	check(ResourceSaver.save(packed, PATH) == OK, "Save environment")
	var loaded := ResourceLoader.load(PATH, "PackedScene", ResourceLoader.CACHE_MODE_IGNORE) as PackedScene
	var copy := loaded.instantiate() as YutrelEnvironment
	for key in FLOAT_PROPERTIES.keys() + BOOL_PROPERTIES.keys() + COLOR_PROPERTIES:
		check(property_matches(copy.get(key), node.get(key)), key + " serialization")
	check(copy.grading_lut.get_image().get_data() == node.grading_lut.get_image().get_data(), "Serialize strip LUT")
	copy.free()
	node.free()


func property_matches(a: Variant, b: Variant) -> bool:
	if a is float:
		return is_equal_approx(a, b)
	if a is Color:
		return a.is_equal_approx(b)
	return a == b


func reset_meter() -> void:
	env.auto_exposure_enabled = false
	await snapshot()
	env.auto_exposure_enabled = true


func metering() -> void:
	env.sky_intensity = 0
	env.auto_exposure_enabled = true
	check((await center()).r == 0, "Black-only histogram is finite")
	env.sky_intensity = 0.01
	var first := (await center(1)).r
	check(first > 0.48 and first <= 0.65, "Black-only histogram adapts from maximum EV")
	env.sky_intensity = 0.125
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 2
	var dark_patch := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(1.3, 3.0)
	dark_patch.mesh = quad
	dark_patch.position = Vector3(-1.0, 0, -4)
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color(0.035, 0.035, 0.035)
	dark_patch.material_override = material
	view.add_child(dark_patch)
	var specular := MeshInstance3D.new()
	specular.mesh = BoxMesh.new()
	specular.position = Vector3(0.6, 0, -4)
	specular.scale = Vector3.ONE * 0.2
	var emitter := StandardMaterial3D.new()
	emitter.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	emitter.emission_enabled = true
	emitter.emission = Color.WHITE
	emitter.emission_energy_multiplier = 128
	specular.material_override = emitter
	view.add_child(specular)
	await reset_meter()
	var trimmed := await snapshot()
	var sky_pixel := trimmed.get_pixelv(view.size / 2).r
	check(sky_pixel > 0.17 and sky_pixel < 0.20 and trimmed.get_pixel(12, 40).r < sky_pixel * 0.2 and trimmed.get_pixel(89, 40).r > 50,
			"Trimmed histogram rejects dark foreground and bright specular outliers")
	dark_patch.free()
	specular.free()
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	await reset_meter()
	env.sky_intensity = 8
	env.auto_exposure_enabled = true
	var bright := await center()
	check(bright.r > 0.17 and bright.r < 0.20, "Meter highlights to middle gray: %s" % bright)
	env.sky_intensity = 0.025
	var last := (await center(1)).r
	for i in 5:
		var next := (await center(6)).r
		check(next > last and next < 0.20, "Monotonic dark adaptation: %f -> %f" % [last, next])
		last = next
	await reset_meter()
	var dark := await center()
	check(dark.r > 0.17 and dark.r < 0.20, "First dark frame snaps to target")
	env.auto_exposure_compensation = 1
	await reset_meter()
	check(absf((await center()).r / dark.r - 2.0) < 0.01, "+1 EV doubles radiance")
	env.auto_exposure_compensation = 0
	env.auto_exposure_max_ev = -1
	env.auto_exposure_min_ev = 1
	await reset_meter()
	check(absf((await center()).r - 0.05) < 0.001, "Reversed EV bounds cap brightening")
	env.sky_intensity = 4
	var previous := (await center(1)).r
	for i in 4:
		var current := (await center(4)).r
		check(current < previous and current >= 2, "Monotonic bright adaptation")
		previous = current
	await reset_meter()
	check(absf((await center()).r - 2.0) < 0.005, "Minimum EV caps darkening")
	env.auto_exposure_min_ev = -6
	env.auto_exposure_max_ev = 6
	await reset_meter()
	env.upscaler_mode = YutrelEnvironment.UPSCALER_MODE_FSR2
	for quality in [0, 1, 2, 3]:
		env.upscaler_quality = quality
		var image := await snapshot(8)
		check(image.get_size() == view.size, "FSR display size")
		for y in range(3, view.size.y, 11):
			for x in range(3, view.size.x, 13):
				close_rgb(image.get_pixel(x, y), Color(0.185, 0.185, 0.185), 0.016, "FSR and adapted exposure, preset %d" % quality)
	env.upscaler_mode = 0
	env.auto_exposure_enabled = false
	check(absf((await center()).r - 4.0) < 0.005, "Disabling metering immediately restores manual exposure")


func bloom_test() -> void:
	env.sky_intensity = 0
	var box := MeshInstance3D.new()
	box.mesh = BoxMesh.new()
	box.position.z = -4
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.albedo_color = Color.BLACK
	material.emission_enabled = true
	material.emission = Color.WHITE
	material.emission_energy_multiplier = 8
	box.material_override = material
	view.add_child(box)
	view.size = Vector2i(333, 211)
	var base := await snapshot(3)
	var y := view.size.y / 2
	var edge := view.size.x / 2
	check(base.get_pixel(edge, y).r > 7, "HDR emitter")
	while edge < view.size.x - 26 and base.get_pixel(edge, y).r > 0:
		edge += 1
	var near := Vector2i(edge + 2, y)
	var far := Vector2i(edge + 20, y)
	check(base.get_pixelv(near).r == 0, "No bloom when disabled")
	env.bloom_enabled = true
	env.bloom_intensity = 1
	env.bloom_threshold = 32
	check((await snapshot()).get_pixelv(near).r == 0, "Emitter below threshold has no halo")
	env.bloom_threshold = 9
	env.bloom_soft_knee = 0
	check((await snapshot()).get_pixelv(near).r == 0, "Hard knee rejects emitter below threshold")
	env.bloom_soft_knee = 0.5
	check((await snapshot()).get_pixelv(near).r > 0.001, "Soft knee admits near-threshold highlights")
	env.bloom_threshold = 1
	env.bloom_scatter = 0.3
	var low := await snapshot()
	env.bloom_intensity = 2
	var strong := await snapshot()
	check(low.get_pixelv(near).r > 0.01 and absf(strong.get_pixelv(near).r / low.get_pixelv(near).r - 2) < 0.02, "Bloom scales halo energy")
	env.bloom_scatter = 0.9
	var wide := await snapshot()
	check(wide.get_pixelv(far).r > strong.get_pixelv(far).r + 0.01, "Scatter widens halo on odd-sized target")
	digest.update(wide.get_data())
	env.auto_exposure_min_ev = -4
	env.auto_exposure_max_ev = -4
	env.auto_exposure_enabled = true
	check((await snapshot()).get_pixelv(near).r == 0, "Bloom thresholds adapted HDR exposure")
	env.auto_exposure_enabled = false
	env.auto_exposure_min_ev = -6
	env.auto_exposure_max_ev = 6
	env.bloom_intensity = 0
	check((await snapshot()).get_data() == base.get_data(), "Zero intensity restores unfiltered HDR")
	env.bloom_enabled = false
	box.free()


# A 65x5 viewport has a single 32x2 bloom level. Read its source radiance
# from the viewport and independently reconstruct the bilinear taps on CPU.
func bilinear_red(image: Image, uv: Vector2) -> float:
	var p := uv * Vector2(image.get_size()) - Vector2(0.5, 0.5)
	var lo := Vector2i(p.floor())
	var f := p - p.floor()
	var limit := image.get_size() - Vector2i.ONE
	var a := image.get_pixelv(lo.clamp(Vector2i.ZERO, limit)).r
	var b := image.get_pixelv((lo + Vector2i.RIGHT).clamp(Vector2i.ZERO, limit)).r
	var c := image.get_pixelv((lo + Vector2i.DOWN).clamp(Vector2i.ZERO, limit)).r
	var d := image.get_pixelv((lo + Vector2i.ONE).clamp(Vector2i.ZERO, limit)).r
	return lerpf(lerpf(a, b, f.x), lerpf(c, d, f.x), f.y)


func firefly_filter() -> void:
	view.size = Vector2i(65, 5)
	camera.projection = Camera3D.PROJECTION_ORTHOGONAL
	camera.size = 5
	var box := MeshInstance3D.new()
	box.mesh = BoxMesh.new()
	box.position.z = -4
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.emission_enabled = true
	mat.emission = Color.WHITE
	mat.emission_energy_multiplier = 48
	box.material_override = mat
	view.add_child(box)
	env.sky_intensity = 0
	env.bloom_enabled = false
	var source := await snapshot(3)
	check(source.get_pixelv(view.size / 2).r > 47, "One-pixel HDR firefly")
	env.bloom_enabled = true
	env.bloom_intensity = 1
	env.bloom_threshold = 1
	env.bloom_soft_knee = 0.5
	var actual := await snapshot()
	var half_size := view.size / 2
	var level := Image.create_empty(half_size.x, half_size.y, false, Image.FORMAT_RF)
	const OFFSETS := [Vector2(-2,-2), Vector2(0,-2), Vector2(2,-2), Vector2(-2,0), Vector2(0,0), Vector2(2,0),
			Vector2(-2,2), Vector2(0,2), Vector2(2,2), Vector2(-1,-1), Vector2(1,-1), Vector2(-1,1), Vector2(1,1)]
	const GROUPS := [[0,1,3,4], [1,2,4,5], [3,4,6,7], [4,5,7,8], [9,10,11,12]]
	var largest_rejection := 0.0
	for y in half_size.y:
		for x in half_size.x:
			var uv := (Vector2(x, y) + Vector2(0.5, 0.5)) / Vector2(half_size)
			var taps := PackedFloat32Array()
			for offset: Vector2 in OFFSETS:
				var radiance := bilinear_red(source, uv + offset / Vector2(view.size))
				var knee := 0.50001
				var soft := clampf(radiance - 1.0 + knee, 0, 2 * knee)
				taps.append(maxf(soft * soft / (4 * knee), radiance - 1.0))
			var weighted := 0.0
			var weights := 0.0
			var unweighted := 0.0
			for i in GROUPS.size():
				var mean := 0.0
				for tap in GROUPS[i]:
					mean += taps[tap] / 4
				var w := 0.5 if i == 4 else 0.125
				unweighted += mean * w
				w /= 1 + mean
				weighted += mean * w
				weights += w
			var filtered := weighted / weights
			level.set_pixel(x, y, Color(filtered, 0, 0))
			largest_rejection = maxf(largest_rejection, unweighted - filtered)
	var max_error := 0.0
	for y in view.size.y:
		for x in view.size.x:
			var uv := (Vector2(x, y) + Vector2(0.5, 0.5)) / Vector2(view.size)
			var expected := source.get_pixel(x, y).r + bilinear_red(level, uv)
			max_error = maxf(max_error, absf(expected - actual.get_pixel(x, y).r))
	check(largest_rejection > 0.1 and max_error < 0.04, "13-tap Karis reference: rejection %.3f max error %.5f" % [largest_rejection, max_error])
	box.free()
	view.size = Vector2i(333, 211)
	camera.projection = Camera3D.PROJECTION_PERSPECTIVE
	env.bloom_enabled = false


# CPU reference on the neutral axis, where the Rec.709 transforms preserve gray.
func tonemap_gray(c: float, mode: int) -> float:
	if mode == YutrelEnvironment.TONE_MAPPING_ACES:
		return clampf((c * (c + 0.0245786) - 0.000090537) / (c * (0.983729 * c + 0.4329510) + 0.238081), 0, 1)
	var x := clampf((log(maxf(c, pow(2, -12.47393))) / log(2) + 12.47393) / 16.499999, 0, 1)
	var v := x * (x * (x * (x * (x * (15.5 * x - 40.14) + 31.96) - 6.868) + 0.4298) + 0.1191) - 0.00232
	return pow(clampf(v, 0, 1), 2.2 * (1.35 if mode == 1 else 1.0))


func grading() -> void:
	for mode in [0, 1, 2]:
		env.tone_mapping = mode
		for gray in [0.0, 0.01, 0.04, 0.18, 0.5, 1.0, 4.0, 16.0]:
			env.sky_intensity = gray
			var expected := tonemap_gray(gray, mode)
			close_rgb(await center(), Color(expected, expected, expected), 0.015, "32^3 LUT mode %d at %.3f" % [mode, gray])
		# At a texel center only half-float storage and the gray-axis fit contribute error.
		for cell in [12, 17, 21]:
			var gray := pow(2.0, cell * 22.0 / 31.0 - 12.0) - 1.0 / 4096.0
			env.sky_intensity = gray
			var expected := tonemap_gray(gray, mode)
			close_rgb(await center(), Color(expected, expected, expected), 0.002, "Tone curve at LUT grid point")
	env.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	env.sky_intensity = 0.3
	for mode in [3, 2]:
		env.tone_mapping = mode
		for direction in [1.0, -1.0]:
			env.grading_temperature = 0.8 * direction
			var temp := await center()
			check((temp.r - temp.b) * direction > 0.04, "Bradford temperature %s" % temp)
			env.grading_temperature = 0
			env.grading_tint = 0.8 * direction
			var tint := await center()
			check(((tint.r + tint.b) * 0.5 - tint.g) * direction > 0.04, "Bradford tint %s" % tint)
			env.grading_tint = 0
	env.tone_mapping = 3
	env.grading_saturation = 0
	for primary in [Color.RED, Color.GREEN]:
		env.sky_color = primary
		var neutral := await center()
		check(neutral.r > 0.04 and absf(neutral.r - neutral.g) < 0.01 and absf(neutral.g - neutral.b) < 0.01, "Desaturate %s" % primary)
	env.sky_color = Color.WHITE
	env.grading_saturation = 1
	env.grading_contrast = 1.5
	env.sky_intensity = 0.04
	var low := (await center()).r
	env.sky_intensity = 0.8
	check((await center()).r / low > 30.0, "Log contrast increases bright-to-dark ratio")
	env.grading_contrast = 1
	env.grading_shadows = Color.RED
	env.grading_highlights = Color.BLUE
	for gray in [0.01, 2.0]:
		env.sky_intensity = gray
		var tinted := await center()
		check(tinted.r > 10 * tinted.b if gray < 0.1 else tinted.b > 10 * tinted.r, "Luminance-selective grading: %s" % tinted)
	env.sky_intensity = 0.45
	env.grading_midtones = Color.GREEN
	close_rgb(await center(), Color(0, 0.45, 0), 0.002, "Midtones receive the middle luminance band")
	env.grading_midtones = Color.WHITE
	env.grading_shadows = Color.WHITE
	env.grading_highlights = Color.WHITE
	env.sky_color = Color(0.82, 0.44, 0.23)
	env.sky_intensity = 1
	var strip := ImageTexture.create_from_image(make_strip(true))
	env.grading_lut = strip
	var original: Color
	for mode in [3, 2]:
		env.tone_mapping = mode
		env.grading_lut_intensity = 0
		original = await center()
		if mode == 3:
			close_rgb(original, env.sky_color.srgb_to_linear(), 0.002, "Zero LUT intensity")
		env.grading_lut_intensity = 1
		var swapped := await center()
		close_rgb(swapped, Color(original.b, original.g, original.r), 0.006, "sRGB strip swaps intermediate R/B in mode %d" % mode)
		if mode == 2:
			strip.update(make_strip(false))
			close_rgb(await center(), original, 0.006, "In-place LUT update invalidates the bake")
	env.grading_lut = ImageTexture.create_from_image(Image.create_empty(7, 3, false, Image.FORMAT_RGBA8))
	close_rgb(await center(), original, 0.001, "Invalid strip dimensions remain finite")
	env.grading_lut = null
	env.sky_color = Color.WHITE


func transfer() -> void:
	env.tone_mapping = 2
	env.sky_intensity = 0.4
	env.debanding_enabled = false
	var clean := await snapshot()
	env.vignette_intensity = 0.7
	var vignette := await snapshot()
	check(vignette.get_pixel(1, 1).r < clean.get_pixel(1, 1).r * 0.36, "Vignette darkens corners")
	close_rgb(vignette.get_pixelv(view.size / 2), clean.get_pixelv(view.size / 2), 0.0001, "Vignette preserves center")
	env.vignette_intensity = 0
	env.debanding_enabled = true
	check((await snapshot()).get_data() == clean.get_data(), "HDR bypasses dither bit-for-bit")
	view.use_hdr_2d = false
	env.tone_mapping = 3
	env.sky_intensity = 4.0
	env.vignette_intensity = 0.7
	var clipped := (await snapshot()).get_pixel(0, 0)
	check(absf(clipped.r - Color(0.3, 0.3, 0.3).linear_to_srgb().r) < 0.012, "None clips display output before vignette")
	env.vignette_intensity = 0
	env.sky_intensity = 0.4
	env.debanding_enabled = false
	var none := await snapshot()
	env.debanding_enabled = true
	check((await snapshot()).get_data() == none.get_data(), "Tone Mapping None bypasses dither on 8-bit target")

	# Render a smooth horizontal ramp. Adjacent rows have identical undithered radiance.
	var ramp := Image.create_empty(256, 2, false, Image.FORMAT_RGBA8)
	for x in 256:
		var v := lerpf(0.3, 0.75, float(x) / 255)
		for y in 2:
			ramp.set_pixel(x, y, Color(v, v, v))
	var mat := StandardMaterial3D.new()
	mat.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	mat.albedo_texture = ImageTexture.create_from_image(ramp)
	var screen := MeshInstance3D.new()
	var quad := QuadMesh.new()
	quad.size = Vector2(12, 8)
	screen.mesh = quad
	screen.material_override = mat
	screen.position.z = -4
	view.add_child(screen)
	env.tone_mapping = 2
	env.debanding_enabled = false
	var plain := await snapshot(3)
	env.debanding_enabled = true
	var dither := await snapshot()
	var count := 0
	var total := 0.0
	var maximum := 0.0
	for y in view.size.y:
		for x in view.size.x:
			var diff := dither.get_pixel(x, y).r - plain.get_pixel(x, y).r
			total += diff
			maximum = maxf(maximum, absf(diff))
			if absf(diff) > 0.001:
				count += 1
	check(absf(plain.get_pixel(10, 50).r - plain.get_pixel(280, 50).r) > 0.1, "8-bit ramp spans multiple levels")
	check(count > 100 and maximum < 1.1 / 255 and absf(total / (view.size.x * view.size.y)) < 0.1 / 255, "Zero-mean sub-LSB dither (changed %d, max %.6f)" % [count, maximum])
	digest.update(dither.get_data())
	screen.free()


func run() -> void:
	properties()
	root.disable_3d = true
	view = SubViewport.new()
	view.size = Vector2i(129, 81)
	view.own_world_3d = true
	view.use_hdr_2d = true
	view.render_target_update_mode = SubViewport.UPDATE_ALWAYS
	root.add_child(view)
	RenderingServer.viewport_set_disable_2d(view.get_viewport_rid(), true)
	camera = Camera3D.new()
	view.add_child(camera)
	camera.make_current()
	env = YutrelEnvironment.new()
	env.sky_color = Color.WHITE
	env.rtao_enabled = false
	env.rt_reflection_enabled = false
	env.auto_exposure_enabled = true
	env.bloom_enabled = true
	view.add_child(env)
	var test_scene := preload("res://editor_scene.tscn").instantiate()
	var emitter: MeshInstance3D = test_scene.get_node("Box")
	test_scene.remove_child(emitter)
	test_scene.free()
	view.add_child(emitter)
	env.auto_exposure_min_ev = -0.25
	env.auto_exposure_max_ev = -0.25
	env.grading_lut = ImageTexture.create_from_image(make_strip(true))
	env.grading_temperature = 0.4
	env.vignette_intensity = 0.65
	var combined := await snapshot(4)
	if OS.has_environment("YUTREL_POSTPROCESS_RELOAD_MARKER"):
		print("POSTPROCESS READY FOR RELOAD")
		while not FileAccess.file_exists(OS.get_environment("YUTREL_POSTPROCESS_RELOAD_MARKER")):
			await create_timer(0.1).timeout
	check((await snapshot(3)).get_data() == combined.get_data(), "Combined image survives shader reload")
	print("POSTPROCESS CACHE BEGIN")
	var same_strip := env.grading_lut
	env.grading_lut = same_strip
	env.bloom_intensity = 0.22
	env.vignette_smoothness = 0.32
	await snapshot(3)
	env.grading_tint = 0.1
	await snapshot(4)
	print("POSTPROCESS CACHE END")
	env.grading_tint = 0
	env.vignette_smoothness = 0.5
	emitter.free()
	env.auto_exposure_min_ev = -6
	env.auto_exposure_max_ev = 6
	env.grading_lut = null
	env.grading_temperature = 0
	env.vignette_intensity = 0
	env.auto_exposure_enabled = false
	env.bloom_enabled = false
	env.tone_mapping = YutrelEnvironment.TONE_MAPPING_NONE
	await snapshot()
	digest.start(HashingContext.HASH_SHA256)
	digest.update(combined.get_data())
	await metering()
	await bloom_test()
	await firefly_filter()
	await grading()
	await transfer()
	view.free()
	print("POSTPROCESS IMAGE ", digest.finish().hex_encode())
	print("POSTPROCESS RUNTIME PASS" if failures == 0 else "POSTPROCESS RUNTIME FAIL")
	quit(failures)
