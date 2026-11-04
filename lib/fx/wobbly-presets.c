/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Based on picom wobbly branch 5ab12b3142be247034568976c88c40a9dac424dd.
 */

#include "polo-fx.h"

PoloFxWobblyParameters
polo_fx_wobbly_parameters_for_level(int level)
{
	static const double presets[POLO_FX_WOBBLY_PRESET_COUNT][3] = {
		{ 0.15, 0.80, 0.10 }, { 0.10, 0.85, 0.10 },
		{ 0.06, 0.90, 0.10 }, { 0.03, 0.92, 0.20 },
		{ 0.01, 0.97, 0.25 },
	};
	if (level < 0 || level >= POLO_FX_WOBBLY_PRESET_COUNT)
		level = 0;
	return (PoloFxWobblyParameters){
		.stiffness = presets[level][0],
		.drag = presets[level][1],
		.move_factor = presets[level][2],
		.min_velocity = 0.0,
		.max_velocity = 1000.0,
		.stop_velocity = 0.5,
		.min_acceleration = 0.0,
		.max_acceleration = 1000.0,
		.stop_acceleration = 0.5,
		.x_tessellation = 20,
		.y_tessellation = 20,
	};
}
