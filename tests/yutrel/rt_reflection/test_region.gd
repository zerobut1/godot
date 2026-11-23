extends RefCounted

# GDScript viewports expose a full render target. Dispatch the production upsampler
# with a padded G-buffer to exercise the render region normally supplied by XR.
var rd: RenderingDevice
var images: Array[RID] = []


func texture(image: Image, format: RenderingDevice.DataFormat, output := false) -> RID:
	var desc := RDTextureFormat.new()
	desc.width = image.get_width()
	desc.height = image.get_height()
	desc.format = format
	desc.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
	if output:
		desc.usage_bits |= RenderingDevice.TEXTURE_USAGE_STORAGE_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
	var rid := rd.texture_create(desc, RDTextureView.new(), [image.get_data()])
	images.append(rid)
	return rid


func filled(size: Vector2i, format: Image.Format, color: Color) -> Image:
	var image := Image.create_empty(size.x, size.y, false, format)
	image.fill(color)
	return image


func run(spirv_path: String) -> bool:
	rd = RenderingServer.create_local_rendering_device()
	var spirv := RDShaderSPIRV.new()
	spirv.bytecode_compute = FileAccess.get_file_as_bytes(spirv_path)
	var shader := rd.shader_create_from_spirv(spirv)
	var pipeline := rd.compute_pipeline_create(shader)
	var origin := Vector2i(3, 4)
	var full := Vector2i(5, 3)
	var half_size := Vector2i(3, 2)
	var padded := Vector2i(11, 9)
	var frame := PackedFloat32Array()
	frame.resize(44)
	for i in [0, 5, 10, 15, 16, 21, 26, 31]:
		frame[i] = 1
	frame[34] = 2
	frame[38] = 1
	frame[40] = origin.x
	frame[41] = origin.y
	frame[42] = 1.0 / full.x
	frame[43] = 1.0 / full.y
	var ubo := rd.uniform_buffer_create(176, frame.to_byte_array())
	var normal := Color(0.5, 0.5, 1, 0.02)
	var shaded := filled(padded, Image.FORMAT_RGBA8, Color.TRANSPARENT)
	shaded.fill_rect(Rect2i(origin, full), Color.WHITE)
	var material := filled(padded, Image.FORMAT_RGBA8, Color.BLACK)
	material.set_pixelv(origin + Vector2i(1, 1), Color.WHITE)
	var radiance := filled(half_size, Image.FORMAT_RGBAH, Color(2, 0.25, 0.5, 1))
	radiance.set_pixel(2, 1, Color(4, 0, 0, 1))
	var resources := [
		ubo,
		texture(filled(padded, Image.FORMAT_RGBAH, normal), RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT),
		texture(filled(padded, Image.FORMAT_RF, Color.WHITE), RenderingDevice.DATA_FORMAT_R32_SFLOAT),
		texture(radiance, RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT),
		texture(filled(half_size, Image.FORMAT_RGBAH, normal), RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT),
		texture(radiance, RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT),
		texture(filled(full, Image.FORMAT_RGBAH, Color.TRANSPARENT), RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT, true),
		texture(material, RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM),
		texture(shaded, RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM),
		ubo,
		texture(filled(full, Image.FORMAT_RGBAH, Color.TRANSPARENT), RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT),
		texture(filled(full, Image.FORMAT_RGBAH, Color.TRANSPARENT), RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT),
		texture(filled(full, Image.FORMAT_RGBAH, Color.TRANSPARENT), RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT, true),
	]
	var uniforms: Array[RDUniform] = []
	for i in resources.size():
		var u := RDUniform.new()
		u.binding = i
		u.uniform_type = RenderingDevice.UNIFORM_TYPE_TEXTURE
		if i == 0 or i == 9:
			u.uniform_type = RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER
		elif i == 6 or i == 12:
			u.uniform_type = RenderingDevice.UNIFORM_TYPE_IMAGE
		u.add_id(resources[i])
		uniforms.append(u)
	var bindings := rd.uniform_set_create(uniforms, shader, 0)
	var list := rd.compute_list_begin()
	rd.compute_list_bind_compute_pipeline(list, pipeline)
	rd.compute_list_bind_uniform_set(list, bindings, 0)
	var params := PackedInt32Array([0, 1, 0, 0, 0]).to_byte_array() # Phase (1,1) clamps at both odd edges.
	params.encode_float(0, 0.6)
	rd.compute_list_set_push_constant(list, params, params.size())
	rd.compute_list_dispatch(list, 1, 1, 1)
	rd.compute_list_end()
	rd.submit()
	rd.sync()
	var result := Image.create_from_data(full.x, full.y, false, Image.FORMAT_RGBAH, rd.texture_get_data(resources[6], 0))
	print("RT_REFLECTION REGION first=%s cut=%s last=%s" % [result.get_pixel(0, 0), result.get_pixel(1, 1), result.get_pixel(4, 2)])
	var correct := absf(result.get_pixel(0, 0).r - 2) < 0.002 and result.get_pixel(1, 1) == Color(0, 0, 0, 0) and absf(result.get_pixel(4, 2).r - 4) < 0.004
	rd.free_rid(bindings)
	rd.free_rid(pipeline)
	rd.free_rid(shader)
	rd.free_rid(ubo)
	for rid in images:
		rd.free_rid(rid)
	rd.free()
	return correct
