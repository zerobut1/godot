extends RefCounted

# Render the production BasePass into an RG16F attachment for an exact signed readback.
var rd: RenderingDevice
var owned: Array[RID] = []


func keep(rid: RID) -> RID:
	owned.append(rid)
	return rid


func uniform_at(binding: int, type: RenderingDevice.UniformType, rid: RID) -> RDUniform:
	var u := RDUniform.new()
	u.binding = binding
	u.uniform_type = type
	u.add_id(rid)
	return u


func load_shader(shader_dir: String, name: String) -> RID:
	var spirv := RDShaderSPIRV.new()
	spirv.bytecode_vertex = FileAccess.get_file_as_bytes(shader_dir.path_join(name + "-vertex.spv"))
	spirv.bytecode_fragment = FileAccess.get_file_as_bytes(shader_dir.path_join(name + "-fragment.spv"))
	return keep(rd.shader_create_from_spirv(spirv))


func run(shader_dir: String) -> bool:
	rd = RenderingServer.create_local_rendering_device()
	var shader := load_shader(shader_dir, "base_pass")
	var colors: Array[RID] = []
	var blend := RDPipelineColorBlendState.new()
	var attachments: Array[RDPipelineColorBlendStateAttachment] = []
	for i in 5:
		var fmt := RDTextureFormat.new()
		fmt.width = 8
		fmt.height = 8
		fmt.format = RenderingDevice.DATA_FORMAT_R16G16_SFLOAT if i == 4 else RenderingDevice.DATA_FORMAT_R16G16B16A16_SFLOAT
		fmt.usage_bits = RenderingDevice.TEXTURE_USAGE_COLOR_ATTACHMENT_BIT | RenderingDevice.TEXTURE_USAGE_CAN_COPY_FROM_BIT
		colors.append(keep(rd.texture_create(fmt, RDTextureView.new())))
		attachments.append(RDPipelineColorBlendStateAttachment.new())
	blend.attachments = attachments
	var fb := keep(rd.framebuffer_create(colors))
	var attributes: Array[RDVertexAttribute] = []
	var buffers: Array[RID] = []
	var positions := PackedFloat32Array([-1, -1, 0.5, 1, 3, -1, 0.5, 1, -1, 3, 0.5, 1])
	for location in [0, 1, 3, 4]:
		var attr := RDVertexAttribute.new()
		attr.location = location
		attr.stride = 16
		attr.format = RenderingDevice.DATA_FORMAT_R32G32_SFLOAT if location == 4 else RenderingDevice.DATA_FORMAT_R32G32B32A32_SFLOAT
		attributes.append(attr)
		var values := positions.duplicate()
		if location != 0:
			values.fill(0.5)
		buffers.append(keep(rd.vertex_buffer_create(48, values.to_byte_array())))
	var layout := rd.vertex_format_create(attributes)
	var vertices := keep(rd.vertex_array_create(3, layout, buffers))
	var pipeline := keep(rd.render_pipeline_create(shader, rd.framebuffer_get_format(fb), layout, RenderingDevice.RENDER_PRIMITIVE_TRIANGLES,
			RDPipelineRasterizationState.new(), RDPipelineMultisampleState.new(), RDPipelineDepthStencilState.new(), blend))
	var frame := PackedFloat32Array()
	frame.resize(80)
	for offset in [0, 16, 44, 60]:
		for i in 4:
			frame[offset + 5 * i] = 1
	frame[3] = 0.1 # Jittered raster projection differs from both velocity projections.
	var frame_buffer := keep(rd.uniform_buffer_create(320, frame.to_byte_array()))
	var instance := PackedFloat32Array()
	instance.resize(48)
	for offset in [0, 12, 36]:
		for i in 3:
			instance[offset + 5 * i] = 1
	var instance_buffer := keep(rd.storage_buffer_create(192, instance.to_byte_array()))
	var material := PackedFloat32Array()
	material.resize(32)
	material[3] = 1
	var material_buffer := keep(rd.storage_buffer_create(128, material.to_byte_array()))
	var scene_set := keep(rd.uniform_set_create([
		uniform_at(0, RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER, frame_buffer),
		uniform_at(1, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, instance_buffer),
		uniform_at(2, RenderingDevice.UNIFORM_TYPE_STORAGE_BUFFER, material_buffer)], shader, 0))
	var fmt := RDTextureFormat.new()
	fmt.format = RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM
	fmt.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
	var white := keep(rd.texture_create(fmt, RDTextureView.new(), [PackedByteArray([255, 255, 255, 255])]))
	var textures: Array[RDUniform] = []
	for i in 6:
		textures.append(uniform_at(i, RenderingDevice.UNIFORM_TYPE_TEXTURE, white))
	textures.append(uniform_at(6, RenderingDevice.UNIFORM_TYPE_SAMPLER, keep(rd.sampler_create(RDSamplerState.new()))))
	var material_set := keep(rd.uniform_set_create(textures, shader, 1))
	var clear := PackedColorArray()
	clear.resize(5)
	clear.fill(Color.WHITE)
	var valid := true
	for expected: Vector2 in [Vector2.ZERO, Vector2(0.125, -0.0625), Vector2(10, 10)]:
		instance[39] = 2 * expected.x
		instance[43] = 2 * expected.y
		rd.buffer_update(instance_buffer, 0, 192, instance.to_byte_array())
		if expected == Vector2(10, 10):
			frame[75] = 0 # Singular previous clip w uses the positive offscreen sentinel.
			rd.buffer_update(frame_buffer, 0, 320, frame.to_byte_array())
		var list := rd.draw_list_begin(fb, RenderingDevice.DRAW_CLEAR_COLOR_ALL, clear)
		rd.draw_list_bind_render_pipeline(list, pipeline)
		rd.draw_list_bind_uniform_set(list, scene_set, 0)
		rd.draw_list_bind_uniform_set(list, material_set, 1)
		rd.draw_list_bind_vertex_array(list, vertices)
		rd.draw_list_set_push_constant(list, PackedByteArray([0, 0, 0, 0]), 4)
		rd.draw_list_draw(list, false, 1)
		rd.draw_list_end()
		rd.submit()
		rd.sync()
		var raw := rd.texture_get_data(colors[4], 0)
		var offset := (4 * 8 + 4) * 4
		var measured := Vector2(raw.decode_half(offset), raw.decode_half(offset + 2))
		print("UPSCALER RG16F expected=%s measured=%s" % [expected, measured])
		valid = valid and measured == expected
	valid = check_sky(shader_dir, frame_buffer, colors) and valid
	owned.reverse()
	for rid in owned:
		rd.free_rid(rid)
	rd.free()
	return valid

func check_sky(shader_dir: String, frame_buffer: RID, colors: Array[RID]) -> bool:
	var shader := load_shader(shader_dir, "skybox")
	var cube := RDTextureFormat.new()
	cube.texture_type = RenderingDevice.TEXTURE_TYPE_CUBE
	cube.array_layers = 6
	cube.usage_bits = RenderingDevice.TEXTURE_USAGE_SAMPLING_BIT
	cube.format = RenderingDevice.DATA_FORMAT_R8G8B8A8_UNORM
	var set0 := keep(rd.uniform_set_create([
		uniform_at(0, RenderingDevice.UNIFORM_TYPE_UNIFORM_BUFFER, frame_buffer),
		uniform_at(1, RenderingDevice.UNIFORM_TYPE_TEXTURE, keep(rd.texture_create(cube, RDTextureView.new()))),
		uniform_at(2, RenderingDevice.UNIFORM_TYPE_SAMPLER, keep(rd.sampler_create(RDSamplerState.new())))], shader, 0))
	var fb := keep(rd.framebuffer_create([colors[0], colors[4]]))
	var blend := RDPipelineColorBlendState.new()
	blend.attachments = [RDPipelineColorBlendStateAttachment.new(), RDPipelineColorBlendStateAttachment.new()]
	var pipeline := keep(rd.render_pipeline_create(shader, rd.framebuffer_get_format(fb), RenderingDevice.INVALID_ID, RenderingDevice.RENDER_PRIMITIVE_TRIANGLES,
			RDPipelineRasterizationState.new(), RDPipelineMultisampleState.new(), RDPipelineDepthStencilState.new(), blend))
	var frame := PackedFloat32Array()
	frame.resize(80)
	# The sampled center (4.5, 4.5) faces -Z in a seven-pixel region at (1,1).
	frame[16] = 1
	frame[21] = 1
	frame[27] = -1
	frame[30] = 1
	frame[38] = 1
	frame[40] = 1
	frame[41] = 1
	frame[42] = 1.0 / 7
	frame[43] = 1.0 / 7
	for offset in [44, 60]:
		frame[offset] = 1
		frame[offset + 5] = 1
		frame[offset + 11] = 1
		frame[offset + 14] = -1
	var valid := true
	for mode in 4:
		if mode == 1:
			frame[63] = 5 # Previous view translation must drop out of a direction projection.
		if mode == 2:
			frame[60] = 0.8
			frame[62] = 0.6
			frame[72] = 0.6
			frame[74] = -0.8
		frame[39] = 1 if mode == 3 else 0
		rd.buffer_update(frame_buffer, 0, 320, frame.to_byte_array())
		var list := rd.draw_list_begin(fb, RenderingDevice.DRAW_CLEAR_COLOR_ALL, [Color.WHITE, Color.WHITE])
		rd.draw_list_bind_render_pipeline(list, pipeline)
		rd.draw_list_bind_uniform_set(list, set0, 0)
		rd.draw_list_set_push_constant(list, PackedFloat32Array([1, 0, 1]).to_byte_array(), 12)
		rd.draw_list_draw(list, false, 1, 3)
		rd.draw_list_end()
		rd.submit()
		rd.sync()
		var raw := rd.texture_get_data(colors[4], 0)
		var offset := (4 * 8 + 4) * 4
		var measured := Vector2(raw.decode_half(offset), raw.decode_half(offset + 2))
		var expected := Vector2(-0.375, 0) if mode == 2 else Vector2.ZERO
		print("UPSCALER SKY RG16F expected=%s measured=%s" % [expected, measured])
		valid = valid and measured == expected
	return valid
