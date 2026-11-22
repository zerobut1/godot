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

class YutrelEnvironment : public Node {
	GDCLASS(YutrelEnvironment, Node);

public:
	enum ToneMapping {
		TONE_MAPPING_AGX,
		TONE_MAPPING_ACES,
		TONE_MAPPING_NONE,
	};

	void set_tone_mapping(ToneMapping p_tone_mapping);
	ToneMapping get_tone_mapping() const { return tone_mapping; }

	void set_exposure(float p_exposure);
	float get_exposure() const { return exposure; }

	PackedStringArray get_configuration_warnings() const override;

protected:
	static void _bind_methods();
	void _notification(int p_what);

private:
	ToneMapping tone_mapping = TONE_MAPPING_AGX;
	float exposure = 1.0f;
	Ref<Environment> internal_environment;
	Ref<World3D> world;

	void _publish_settings();
	void _world_changed();
};

VARIANT_ENUM_CAST(YutrelEnvironment::ToneMapping);
