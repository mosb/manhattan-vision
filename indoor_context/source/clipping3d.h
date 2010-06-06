#pragma once

#include "common_types.h"
#include "camera.h"

#include "range_utils.tpp"
#include "polygon.tpp"

namespace indoor_context {
	using namespace toon;
	// Plucker coordinates are P=[V,X] where V is the direction of the
	// line and X is a point on the line.

	typedef toon::Matrix<4> Mat4;

	// Get the line through two points, in Plucker coordinates
	Vec6 LineThrough(const Vec3& x, const Vec3& y) {
		Vec6 m;
		m.slice<0,3>() = x-y;
		m.slice<3,3>() = x^y;
		return m;
	}

	// Get the line through two points, in Plucker coordinates
	Vec6 LineThrough(const Vec4& x, const Vec4& y) {
		Vec6 m;
		m.slice<0,3>() = y[3]*x.slice<0,3>() - x[3]*y.slice<0,3>();
		m.slice<3,3>() = x.slice<0,3>() ^ y.slice<0,3>();
		return m;
	}

	// Get the intersection of two planes, in Plucker coordinates
	Vec6 LineFromPlanes(const Vec4& w, const Vec4& v) {
		Vec6 m;
		m.slice<0,3>() = w.slice<0,3>()^v.slice<0,3>();
		m.slice<3,3>() = v[3]*w.slice<0,3>() - w[3]*v.slice<0,3>();
		return m;
	}

	// Get the intersection between a plane and a line (in Plucker coordinates)
	Vec4 PlaneLineIsct(const Vec6& m, const Vec4& w) {
		Vec4 p;
		p.slice<0,3>() = (m.slice<3,3>()^w.slice<0,3>()) - w[3]*m.slice<0,3>();
		p[3] = w.slice<0,3>() * m.slice<0,3>();
		return p;
	}

	// Clip a polygon to the positive side of a plane
	template <typename Range, typename OutputIterator>
	int ClipAgainstPlane(const Range& poly,
											 const Vec4& plane,
											 OutputIterator out) {
		BOOST_STATIC_ASSERT((is_convertible<
												 typename range_value<Range>::type,
												 toon::Vector<3> >::value));
		int n = size(poly);
		bool prev = false;
		for (int i = 0; i < n; i++) {
			const Vec3& a = poly[i];
			const Vec3& b = poly[(i+1)%n];
			bool aa = a*plane.slice<0,3>() + plane[3] >= 0;
			bool bb = b*plane.slice<0,3>() + plane[3] >= 0;
			if (aa || bb) {
				Vec3 isct = aa && bb ? Zeros : project(PlaneLineIsct(LineThrough(a,b), plane));
				if (!aa) {
					*out++ = isct;
				} else if (!prev) {
					*out++ = a;
				}
				if (!bb) {
					*out++ = isct;
				} else if (i+1 < n) {
					*out++ = b;
				}
			}
			prev = bb;
		}
	}

	Vec4 AppendZero(const Vec3& image_line) {
		return makeVector(image_line[0], image_line[1], image_line[2], 0.0);
	}

	// Clip a polygon against a frustrum
	template <typename Range, typename OutputIterator>
	int ClipAgainstFrustrum(const Range& poly,
													const PosedCamera& pc,
													OutputIterator out) {
		static const double kZNear = 1e-6;
		static const double kZFar = 1e+6;
		int n = size(poly);

		// Create the frustrum in camera coords
		Vec4 frustrum[] = {
			AppendZero(pc.ret_bounds().left_eqn()),
			AppendZero(pc.ret_bounds().right_eqn()),
			AppendZero(pc.ret_bounds().top_eqn()),
			AppendZero(pc.ret_bounds().bottom_eqn()),
			makeVector(1.0, 0, 0, -kZNear),
			makeVector(-1.0, 0, 0, kZFar)
		};

		// Construct the camera matrix
		Mat4 m = Identity;
		m.slice<0,0,3,3>() = pc.pose.get_rotation().get_matrix();
		m.slice<0,3,3,1>() = pc.pose.get_translation().as_col();

		// Transfer planes _from camera to world_ using the _forwards_ camera matrix
		vector<Vec3> temp1, temp2;
		copy_all(poly, back_inserter(temp1));
		int ii = 0;
		BOOST_FOREACH(Vec4& w, array_range(frustrum, 6)) {
			ClipAgainstPlane(temp1, m*w, back_inserter(temp2));
			swap(temp1,temp2);
			temp2.clear();
		}
		copy_all(temp1, out);
	}
}
