/**************************************************************************/
/*  yutrel_environment.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "scene/main/node.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/environment.h"
#include "scene/resources/texture.h"

class YutrelEnvironment : public Node {
	GDCLASS(YutrelEnvironment, Node);

public:
	enum SkyMode {
		SKY_MODE_CONSTANT_COLOR,
		SKY_MODE_HDRI,
	};

	enum ToneMapping {
		TONE_MAPPING_AGX,
		TONE_MAPPING_AGX_PUNCHY,
		TONE_MAPPING_ACES,
		TONE_MAPPING_NONE,
	};

	void set_shadow_resolution(int p_resolution);
	int get_shadow_resolution() const { return shadow_resolution; }

	void set_sky_mode(SkyMode p_mode);
	SkyMode get_sky_mode() const { return sky_mode; }

	void set_sky_color(const Color &p_color);
	Color get_sky_color() const { return sky_color; }

	void set_sky_texture(const Ref<Texture2D> &p_texture);
	Ref<Texture2D> get_sky_texture() const { return sky_texture; }

	void set_sky_intensity(float p_intensity);
	float get_sky_intensity() const { return sky_intensity; }

	void set_sky_rotation(float p_degrees);
	float get_sky_rotation() const { return sky_rotation; }

	void set_tone_mapping(ToneMapping p_tone_mapping);
	ToneMapping get_tone_mapping() const { return tone_mapping; }

	void set_exposure(float p_exposure);
	float get_exposure() const { return exposure; }

	void set_rtao_enabled(bool p_enabled);
	bool get_rtao_enabled() const { return rtao_enabled; }

	void set_rtao_half_resolution(bool p_enabled);
	bool get_rtao_half_resolution() const { return rtao_half_resolution; }

	void set_rtao_radius(float p_radius);
	float get_rtao_radius() const { return rtao_radius; }

	void set_rtao_intensity(float p_intensity);
	float get_rtao_intensity() const { return rtao_intensity; }

	void set_rtao_samples(int p_samples);
	int get_rtao_samples() const { return rtao_samples; }

	PackedStringArray get_configuration_warnings() const override;

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	int shadow_resolution = 2048;
	SkyMode sky_mode = SKY_MODE_CONSTANT_COLOR;
	Color sky_color = Color(0.22f, 0.25f, 0.30f);
	Ref<Texture2D> sky_texture;
	float sky_intensity = 1.0f;
	float sky_rotation = 0.0f;
	ToneMapping tone_mapping = TONE_MAPPING_AGX_PUNCHY;
	float exposure = 1.0f;
	bool rtao_enabled = true;
	bool rtao_half_resolution = true;
	float rtao_radius = 1.0f;
	float rtao_intensity = 1.0f;
	int rtao_samples = 1;
	Ref<Environment> internal_environment;
	Ref<Environment> previous_environment;
	Ref<World3D> world;

	void _publish_settings();
	void _world_changed();
};

VARIANT_ENUM_CAST(YutrelEnvironment::ToneMapping);
VARIANT_ENUM_CAST(YutrelEnvironment::SkyMode);
