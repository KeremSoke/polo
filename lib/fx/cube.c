/*
 * Copyright 2026 Kerem Soke
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * The geometry of Wayfire's cube plugin (plugins/cube/cube.cpp and the
 * shaders of shaders-3-2.tpp, commit e268d4e5), ported from glm and GLSL:
 *
 * Copyright (c) 2018 Ilia Bozhinov and the Wayfire contributors
 * SPDX-License-Identifier: MIT
 */

#include "polo-fx.h"

#include <math.h>
#include <string.h>

#define FOVY 45.0
#define DEFORM_RADIUS 0.5
#define AMBIENT 0.3
#define TWO_SIDES_GAP 1e-3
#define MAX_TESSELLATION 30.0

typedef struct {
	double x, y, z;
} Vec3;

static double
focal(void)
{
	return 1.0 / tan(FOVY / 2.0);
}

double
polo_fx_cube_side_angle(int sides)
{
	return 2.0 * G_PI / MAX(sides, 1);
}

static double
identity_z(int sides)
{
	if (sides <= 1)
		return 0.0;
	return 0.5 / tan(polo_fx_cube_side_angle(sides) / 2.0);
}

double
polo_fx_cube_rest_offset_z(int sides)
{
	return identity_z(sides) + 0.5 * focal();
}

static Vec3
model_point(const PoloFxCube *cube, int side, double mx, double my)
{
	double angle =
		side * polo_fx_cube_side_angle(cube->sides) + cube->rotation;
	double z = identity_z(cube->sides) +
		(cube->sides == 2 ? TWO_SIDES_GAP : 0.0);

	return (Vec3){ cos(angle) * mx + sin(angle) * z, my,
		       -sin(angle) * mx + cos(angle) * z };
}

static Vec3
deformed(const PoloFxCube *cube, Vec3 p)
{
	double d = hypot(p.x, p.z), scale;

	if (cube->deform == POLO_FX_CUBE_DEFORM_NONE || d <= 0.0)
		return p;
	scale = cube->deform == POLO_FX_CUBE_DEFORM_CYLINDER
		? DEFORM_RADIUS / d
		: d / DEFORM_RADIUS;
	scale = pow(scale, cube->ease);
	return (Vec3){ p.x * scale, p.y, p.z * scale };
}

static Vec3
world_point(const PoloFxCube *cube, int side, PoloFxVec2 local)
{
	double mx = local.x / cube->size.x - 0.5;
	double my = 0.5 - local.y / cube->size.y;

	return deformed(cube, model_point(cube, side, mx, my));
}

static Vec3
eye_point(const PoloFxCube *cube, Vec3 p)
{
	double zoom = cube->zoom > 0.0 ? cube->zoom : 1.0;
	double c = cos(cube->offset_y), s = sin(cube->offset_y);
	Vec3 q = { p.x / zoom, p.y / zoom, p.z / zoom };

	return (Vec3){ q.x, c * q.y - s * q.z,
		       s * q.y + c * q.z - cube->offset_z };
}

gboolean
polo_fx_cube_point(const PoloFxCube *cube, int side, PoloFxVec2 local,
		   PoloFxVec2 *screen)
{
	Vec3 e;
	double w;

	g_return_val_if_fail(cube != NULL && screen != NULL, FALSE);
	e = eye_point(cube, world_point(cube, side, local));
	w = -e.z;
	if (w <= 1e-6) {
		screen->x = screen->y = 0.0;
		return FALSE;
	}
	screen->x =
		cube->origin.x + (focal() * e.x / w + 1.0) / 2.0 * cube->size.x;
	screen->y =
		cube->origin.y + (1.0 - focal() * e.y / w) / 2.0 * cube->size.y;
	return TRUE;
}

static Vec3
sub(Vec3 a, Vec3 b)
{
	return (Vec3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static Vec3
cross(Vec3 a, Vec3 b)
{
	return (Vec3){ a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
		       a.x * b.y - a.y * b.x };
}

static double
dot(Vec3 a, Vec3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static Vec3
centre(const PoloFxCube *cube, int side)
{
	return eye_point(cube,
			 world_point(cube, side,
				     (PoloFxVec2){ cube->size.x / 2,
						   cube->size.y / 2 }));
}

gboolean
polo_fx_cube_side_facing(const PoloFxCube *cube, int side)
{
	PoloFxVec2 c, right, down;
	Vec3 o, a, b;

	g_return_val_if_fail(cube != NULL, FALSE);
	c = (PoloFxVec2){ cube->size.x / 2, cube->size.y / 2 };
	right = (PoloFxVec2){ c.x + 1.0, c.y };
	down = (PoloFxVec2){ c.x, c.y + 1.0 };
	o = eye_point(cube, world_point(cube, side, c));
	a = sub(eye_point(cube, world_point(cube, side, right)), o);
	b = sub(eye_point(cube, world_point(cube, side, down)), o);
	return dot(cross(b, a), o) < 0.0;
}

double
polo_fx_cube_side_depth(const PoloFxCube *cube, int side)
{
	Vec3 c;

	g_return_val_if_fail(cube != NULL, 0.0);
	c = centre(cube, side);
	return sqrt(dot(c, c));
}

void
polo_fx_cube_side_order(const PoloFxCube *cube, int *order)
{
	int n = 0;

	g_return_if_fail(cube != NULL && order != NULL);
	for (int pass = 0; pass < 2; pass++) {
		int first = n;

		for (int side = 0; side < cube->sides; side++)
			if (polo_fx_cube_side_facing(cube, side) == pass)
				order[n++] = side;
		for (int i = first + 1; i < n; i++) {
			int side = order[i], j = i;
			double depth = polo_fx_cube_side_depth(cube, side);

			while (j > first &&
			       polo_fx_cube_side_depth(cube, order[j - 1]) <
				       depth) {
				order[j] = order[j - 1];
				j--;
			}
			order[j] = side;
		}
	}
}

double
polo_fx_cube_shade(const PoloFxCube *cube, int side, PoloFxVec2 local)
{
	const Vec3 light = { 0.0, 0.0, 2.0 };
	Vec3 p, a, b, n, to_light;
	double length, d, ambient, value;

	g_return_val_if_fail(cube != NULL, 1.0);
	p = world_point(cube, side, local);
	a = sub(world_point(cube, side, (PoloFxVec2){ local.x + 1.0, local.y }),
		p);
	b = sub(world_point(cube, side, (PoloFxVec2){ local.x, local.y + 1.0 }),
		p);
	n = cross(a, b);
	length = sqrt(dot(n, n));
	if (length <= 0.0)
		return 1.0;
	to_light = (Vec3){ 0.0, 0.0, 1.0 };
	d = sqrt(dot(sub(p, light), sub(p, light)));
	ambient = d > 0.0 ? pow(CLAMP(2.0 / d, 0.0, 1.0), 10.0) : 1.0;
	value = CLAMP(pow(fabs(dot(n, to_light)) / length, 1.5), 0.0, 1.0);
	return AMBIENT * ambient + (1.0 - AMBIENT) * value;
}

void
polo_fx_cube_grid_size(const PoloFxCube *cube, double width, double height,
		       double spacing, guint *cols, guint *rows)
{
	double across = ceil(width / MAX(spacing, 1.0));
	double down = ceil(height / MAX(spacing, 1.0));

	g_return_if_fail(cube != NULL && cols != NULL && rows != NULL);
	if (cube->deform != POLO_FX_CUBE_DEFORM_NONE) {
		across = MAX(across,
			     ceil(MAX_TESSELLATION * width / cube->size.x));
		down = MAX(down,
			   ceil(MAX_TESSELLATION * height / cube->size.y));
	}
	across = CLAMP(across, 1.0, 254.0);
	down = CLAMP(down, 1.0, 254.0);
	*cols = (guint)across + 1;
	*rows = (guint)down + 1;
}

gboolean
polo_fx_cube_fill_grid(const PoloFxCube *cube, int side, double view_x,
		       double view_y, double view_width, double view_height,
		       double clip_x, double clip_y, double clip_width,
		       double clip_height, PoloFxGrid *grid)
{
	gboolean in_front = TRUE;
	float *vertex;

	g_return_val_if_fail(cube != NULL && grid != NULL, FALSE);
	g_return_val_if_fail(view_width > 0.0 && view_height > 0.0, FALSE);
	vertex = grid->vertices;
	for (guint row = 0; row < grid->rows; row++) {
		double y = clip_y + clip_height * row / (grid->rows - 1);

		for (guint col = 0; col < grid->cols; col++) {
			double x = clip_x + clip_width * col / (grid->cols - 1);
			PoloFxVec2 screen;

			in_front &= polo_fx_cube_point(
				cube, side, (PoloFxVec2){ x, y }, &screen);
			*vertex++ = (float)screen.x;
			*vertex++ = (float)screen.y;
			*vertex++ = (float)((x - view_x) / view_width);
			*vertex++ = (float)((y - view_y) / view_height);
		}
	}
	return in_front;
}

void
polo_fx_cube_fill_shade(const PoloFxCube *cube, int side, guint8 *pixels,
			int width, int height, int stride)
{
	g_return_if_fail(cube != NULL && pixels != NULL);
	for (int y = 0; y < height; y++) {
		guint32 *row = (guint32 *)(pixels + (gsize)y * stride);

		for (int x = 0; x < width; x++) {
			PoloFxVec2 local = { (x + 0.5) * cube->size.x / width,
					     (y + 0.5) * cube->size.y /
						     height };
			double shade = polo_fx_cube_shade(cube, side, local);
			guint32 alpha = (guint32)lround(
				CLAMP(1.0 - shade, 0.0, 1.0) * 255.0);

			row[x] = alpha << 24;
		}
	}
}

static void
sky_direction(const PoloFxCube *cube, PoloFxVec2 screen, double *u, double *phi)
{
	double nx = 2.0 * (screen.x - cube->origin.x) / cube->size.x - 1.0;
	double ny = 1.0 - 2.0 * (screen.y - cube->origin.y) / cube->size.y;
	double ex = nx / focal(), ey = ny / focal(), ez = -1.0;
	double pitch = cube->offset_y * 0.5;
	double c = cos(pitch), s = sin(pitch);
	double wx = ex, wy = c * ey + s * ez, wz = -s * ey + c * ez;
	double cr = cos(cube->rotation), sr = sin(cube->rotation);
	double x = cr * wx - sr * wz, z = sr * wx + cr * wz;
	double length = sqrt(x * x + wy * wy + z * z);

	*u = atan2(z, x) / (2.0 * G_PI);
	if (*u < 0.0)
		*u += 1.0;
	if (*u > 1.0 - 1e-12)
		*u = 0.0;
	*phi = acos(CLAMP(wy / length, -1.0, 1.0));
}

static double
dome_v(double phi)
{
	return CLAMP((phi / G_PI * 128.0 - 1.0) / 126.0, 0.0, 1.0);
}

PoloFxVec2
polo_fx_cube_sky_uv(const PoloFxCube *cube, PoloFxVec2 screen, gboolean mirror)
{
	double u, phi;

	sky_direction(cube, screen, &u, &phi);
	if (mirror)
		u = 1.0 - fabs(2.0 * u - 1.0);
	return (PoloFxVec2){ u, dome_v(phi) };
}

void
polo_fx_cube_fill_sky_grid(const PoloFxCube *cube, gboolean mirror,
			   gboolean dome, PoloFxGrid *grid)
{
	PoloFxVec2 centre;
	double centre_u, phi;
	float *vertex;

	g_return_if_fail(cube != NULL && grid != NULL);
	centre = (PoloFxVec2){ cube->origin.x + cube->size.x / 2,
			       cube->origin.y + cube->size.y / 2 };
	sky_direction(cube, centre, &centre_u, &phi);
	if (centre_u < 0.5)
		centre_u += 1.0;
	vertex = grid->vertices;
	for (guint row = 0; row < grid->rows; row++) {
		for (guint col = 0; col < grid->cols; col++) {
			PoloFxVec2 screen = {
				cube->origin.x +
					cube->size.x * col / (grid->cols - 1),
				cube->origin.y +
					cube->size.y * row / (grid->rows - 1)
			};
			double u;

			sky_direction(cube, screen, &u, &phi);
			if (mirror)
				u = 1.0 - fabs(2.0 * u - 1.0);
			else
				u += round(centre_u - u);
			*vertex++ = screen.x;
			*vertex++ = screen.y;
			*vertex++ = (float)CLAMP(u / 2.0, 0.0, 1.0);
			*vertex++ = (float)(dome ? dome_v(phi) : phi / G_PI);
		}
	}
}

void
polo_fx_cube_sky_texture(const guint8 *src, int width, int height, int stride,
			 guint8 *dst, int dst_stride)
{
	g_return_if_fail(src != NULL && dst != NULL);
	for (int row = 0; row < height; row++) {
		const guint8 *in = src + (gsize)row * stride;
		guint8 *out = dst + (gsize)row * dst_stride;

		memcpy(out, in, (gsize)width * 4);
		memcpy(out + (gsize)width * 4, in, (gsize)width * 4);
	}
}

static PoloFxVec2
cubemap_uv(double x, double y, double z, int *face)
{
	double ax = fabs(x), ay = fabs(y), az = fabs(z);
	double sc, tc, ma;
	int tile_x, tile_y;

	if (ax == 0.0 && ay == 0.0 && az == 0.0) {
		if (face)
			*face = 5;
		return (PoloFxVec2){ 0.875, 0.5 };
	}

	if (ax >= ay && ax >= az) {
		if (face)
			*face = x >= 0 ? 0 : 1;
		ma = ax;
		sc = x >= 0 ? -z : z;
		tc = -y;
		tile_x = x >= 0 ? 2 : 0;
		tile_y = 1;
	} else if (ay >= az) {
		if (face)
			*face = y >= 0 ? 2 : 3;
		ma = ay;
		sc = x;
		tc = y >= 0 ? z : -z;
		tile_x = 1;
		tile_y = y >= 0 ? 0 : 2;
	} else {
		if (face)
			*face = z >= 0 ? 4 : 5;
		ma = az;
		sc = z >= 0 ? x : -x;
		tc = -y;
		tile_x = z >= 0 ? 1 : 3;
		tile_y = 1;
	}
	return (PoloFxVec2){ (tile_x + (sc / ma + 1.0) / 2.0) / 4.0,
			     (tile_y + (tc / ma + 1.0) / 2.0) / 3.0 };
}

PoloFxVec2
polo_fx_cube_cubemap_uv(double x, double y, double z)
{
	return cubemap_uv(x, y, z, NULL);
}

gboolean
polo_fx_cube_cubemap_equirect(const guint8 *src, int width, int height,
			      int stride, guint8 *dst, int dst_width,
			      int dst_height, int dst_stride)
{
	if (!src || !dst || width < 4 || height < 3 || width % 4 ||
	    height % 3 || width / 4 != height / 3 || dst_width < 1 ||
	    dst_height < 1 || stride < width * 4 || dst_stride < dst_width * 4)
		return FALSE;
	for (int row = 0; row < dst_height; row++) {
		double phi = G_PI * (row + 0.5) / dst_height;
		for (int col = 0; col < dst_width; col++) {
			double theta = 2.0 * G_PI * (col + 0.5) / dst_width;
			PoloFxVec2 uv =
				cubemap_uv(cos(theta) * sin(phi), cos(phi),
					   sin(theta) * sin(phi), NULL);
			int sx = CLAMP((int)(uv.x * width), 0, width - 1);
			int sy = CLAMP((int)(uv.y * height), 0, height - 1);
			const guint32 *in =
				(const guint32 *)(src + sy * stride);
			guint32 *out = (guint32 *)(dst + row * dst_stride);

			out[col] = in[sx];
		}
	}
	return TRUE;
}
