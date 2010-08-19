/*
 * simple_render.h
 *
 *  Created on: 6 Jul 2010
 *      Author: alexf
 */

#include "simple_renderer.h"
#include "common_types.h"
#include "geom_utils.h"

#include "fill_polygon.tpp"
#include "clipping3d.tpp"

namespace indoor_context {

SimpleRenderer::SimpleRenderer() {
}

SimpleRenderer::SimpleRenderer(const toon::Matrix<3,4>& camera, Vec2I viewport) {
	Configure(camera, viewport);
}

void SimpleRenderer::Configure(const toon::Matrix<3,4>& camera, Vec2I viewport) {
	viewport_ = viewport;
	camera_ = camera;
	framebuffer_.Resize(viewport[1], viewport[0]);
	depthbuffer_.Resize(viewport[1], viewport[0]);
	Clear(0);
}

void SimpleRenderer::Render(Vec3 p, Vec3 q, Vec3 r, int label) {
	// Do 3D clipping
	Vec3 vs[] = {p,q,r};
	vector<Vec3> clipped;
	ClipToFrustrum(array_range(vs,3), camera_, viewport_, back_inserter(clipped));

	// Project into the camera
	vector<Vec3> projected;
	BOOST_FOREACH(const Vec3& v, clipped) {
		projected.push_back(camera_ * unproject(v));
	}

	// Compute the triangle scanlines
	int y0;
	vector<pair<int, int> > scanlines;
	ComputeFillScanlines(projected, viewport_, y0, scanlines);

	// Set up the depth equation
	Vec3 nrm = (p-q)^(p-r);
	Vec4 plane = concat(nrm, -nrm*p);
	Vec3 depth_eqn = PlaneToDepthEqn(camera_, plane);

	// Do the rendering
	for (int i = 0; i < scanlines.size(); i++) {
		// Pre-compute the first bit of the depth equation
		double depth_base = depth_eqn * makeVector(0, y0+i, 1);
		double depth_coef = depth_eqn[0];

		// Fill the row
		double* depth_row = depthbuffer_[y0+i];
		int* label_row = framebuffer_[y0+i];
		//DREPORT(i, y0+i, scanlines[i].first, scanlines[i].second);
		for (int x = scanlines[i].first; x < scanlines[i].second; x++) {
			double depth = 1.0 / (depth_base + depth_coef*x);  // reciprocal is due to PlaneToDepthEqn
			if (depth < depth_row[x]) {
				depth_row[x] = depth;
				label_row[x] = label;
			}
		}
	}
}

void SimpleRenderer::Clear(int bg) {
	framebuffer_.Fill(bg);
	depthbuffer_.Fill(INFINITY);
}

}