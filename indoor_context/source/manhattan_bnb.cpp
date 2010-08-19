#include "manhattan_bnb.h"

#include <limits>

#include <VW/Image/imagecopy.tpp>

#include "common_types.h"
#include "clipping.h"
#include "geom_utils.h"
#include "image_utils.h"
#include "bld_helpers.h"
#include "timer.h"

#include "counted_foreach.tpp"
#include "fill_polygon.tpp"
#include "range_utils.tpp"
#include "image_utils.tpp"
#include "io_utils.tpp"
#include "math_utils.tpp"
#include "vector_utils.tpp"

namespace indoor_context {
using namespace toon;

lazyvar<int> gvMaxCorners("ManhattanRecovery.MaxCorners");
lazyvar<double> gvOcclThresh("ManhattanRecovery.CnrOcclusionThresh");
lazyvar<double> gvMinCornerMargin("ManhattanRecovery.MinCornerMargin");
lazyvar<int> gvOrientRes("ManhattanRecovery.OrientRes");

void ClipToFront(Vec3& a, Vec3& b) {
	const double kClipDist = 0.1;
	if (b[2] < 0 && a[2] > 0) {
		double t = (a[2] - kClipDist) / (a[2] - b[2]);
		b = t * b + (1 - t) * a;
	} else if (a[2] < 0 && b[2] > 0) {
		double t = (b[2] - kClipDist) / (b[2] - a[2]);
		a = t * a + (1 - t) * b;
	}
}

ManhattanEdge::ManhattanEdge() :
			id(-1) {
}

ManhattanEdge::ManhattanEdge(const Vec3& a, const Vec3& b, int ax, int i) :
			start(pve_unit(a)), end(pve_unit(b)), eqn(start ^ end), axis(ax), id(i) {
}

bool ManhattanEdge::Contains(const Vec3& p) const {
	// This notion of distance only makes sense for homogeneous
	// coordinates when z coordinates are positive
	Vec3 x = pve_unit(p);
	return min(x * start, x * end) > start * end;
}

bool ManhattanBuilding::ContainsEdge(int id) const {
	return edge_ids.find(id) != edge_ids.end();
}




bool MonocularManhattanBnb::Compute(const PosedCamera& pcam,
                                    const vector<ManhattanEdge> edges[],
                                    const MatI& orients) {
	pc = &pcam;

	vert_axis = 0;
	for (int i = 1; i < 3; i++) {
		if (abs(pc->GetRetinaVpt(i)[1]) > abs(pc->GetRetinaVpt(vert_axis)[1])) {
			vert_axis = i;
		}
	}

	enumerator.Configure(pcam, vert_axis);
	renderer.Configure(pcam, vert_axis);

	DownsampleOrients(orients, est_orients, makeVector(*gvOrientRes, *gvOrientRes));

	soln_score = numeric_limits<int>::min();
	hypothesis_count = 0;
	if (enumerator.Compute(edges,
			bind(&MonocularManhattanBnb::EvaluateHypothesis, this, _1))) {
		renderer.PredictImOrientations(soln, soln_orients);
		DLOG << "Searched " << hypothesis_count << " hypotheses";
		return true;
	} else {
		return false;
	}
}

void MonocularManhattanBnb::EvaluateHypothesis(const ManhattanBuilding& bld) {
	renderer.PredictGridOrientations(bld, predict_buffer);
	int score = ComputeAgreement(predict_buffer, est_orients);
	if (score > soln_score) {
		soln_score = score;
		soln = bld;
	}
	hypothesis_count++;
}

int MonocularManhattanBnb::OtherHorizAxis(int a) const {
	return a == (vert_axis+1)%3 ? (vert_axis+2)%3 : (vert_axis+1)%3;
}

Vec3 MonocularManhattanBnb::FloorPoint(const Vec3& p, double z) {
	// Note that p appear in the numerator _and_ the denominator
	// below, so linear scaling has no effect (as expected for
	// homogeneous coordinates)
	return -z*p / (p*horizon);
}

Vec3 MonocularManhattanBnb::CeilPoint(const Vec3& p,
                                   const Vec3& floor_pt) {
	Matrix<3,2> m;
	Vec3 u = unit(p);
	// unit() below is important because it affects the weighting of the least-squares problem
	m.slice<0,0,3,1>() = u.as_col();
	m.slice<0,1,3,1>() = -horizon.as_col();
	Vec2 soln = SVD<3,2>(m).backsub(floor_pt);
	return soln[0] * u;
}

void MonocularManhattanBnb::TransferBuilding(const ManhattanBuilding& bld,
                                             const SE3<>& new_pose,
                                             double floor_z,
                                             MatI& orients) {
	// Compute scaling (TODO: actually use this)
	DiagonalMatrix<3> Mscale(makeVector(1.0*orients.Cols()/pc->image_size().x,
			1.0*orients.Rows()/pc->image_size().y,
			1.0));

	// Invert the pose
	const SE3<> orig_inv = pc->pose().inverse();

	// Draw each wall segment
	//Viewer3D v;

	orients.Fill(vert_axis);
	for (ManhattanBuilding::ConstCnrIt left_cnr = bld.cnrs.begin();
			successor(left_cnr) != bld.cnrs.end();
			left_cnr++) {
		ManhattanBuilding::ConstCnrIt right_cnr = successor(left_cnr);

		//TITLE("Next corner");
		int axis = OtherHorizAxis(left_cnr->right_axis);

		// Compute vertices in the 3D camera frame
		Vec3 bl = FloorPoint(left_cnr->right_floor, floor_z);
		Vec3 br = FloorPoint(right_cnr->left_floor, floor_z);
		Vec3 tl = CeilPoint(left_cnr->right_ceil, bl);
		Vec3 tr = CeilPoint(right_cnr->left_ceil, br);
		if (bl[2] < 0) {
			bl = -bl;
			br = -br;
			tl = -tl;
			tr = -tr;
		}
		//DREPORT(project(left_cnr->right_ceil));
		//DREPORT(bl,br,tl,tr);

		// Compute vertices in the 3D global frame
		Vec3 world_tl = orig_inv * tl;
		Vec3 world_tr = orig_inv * tr;
		Vec3 world_bl = orig_inv * bl;
		Vec3 world_br = orig_inv * br;

		// Project into the viewer
		/*Vec3 floor_c = orig_inv.get_translation();
			 Vec3 ceil_c = orig_inv.get_translation();
			 floor_c[2] = floor_z;
			 ceil_c[2] = world_tl[2];
			 QuadWidget* w;
			 v.AddOwned(w = new QuadWidget(world_tl, world_tr, world_br, world_bl));
			 w->color = Colors::primary(axis);
			 v.AddOwned(w = new QuadWidget(world_bl, world_br, floor_c, floor_c));
			 w->color = Colors::blue();
			 v.AddOwned(w = new QuadWidget(world_tl, world_tr, ceil_c, ceil_c));
			 w->color = Colors::white();
			 v.AddOwned(new MapWidget(&map));*/

		// Compute the wall corners in the other camera
		Vec3 ret_tl = new_pose * world_tl;
		Vec3 ret_tr = new_pose * world_tr;
		Vec3 ret_bl = new_pose * world_bl;
		Vec3 ret_br = new_pose * world_br;
		ClipToFront(ret_tr, ret_tl);
		ClipToFront(ret_br, ret_bl);

		// Compute the wall corners in the other image
		Vec3 im_tl = pc->RetToIm(ret_tl);
		Vec3 im_tr = pc->RetToIm(ret_tr);
		Vec3 im_bl = pc->RetToIm(ret_bl);
		Vec3 im_br = pc->RetToIm(ret_br);

		// Build the polygon and fill
		vector<Vec3 > poly;
		poly.push_back(im_tl);
		poly.push_back(im_bl);
		poly.push_back(im_br);
		poly.push_back(im_tr);
		FillPolygon(poly, orients, axis);
	}

	//v.Run();
}






ManhattanBranchAndBound::ManhattanBranchAndBound() :
			max_corners(*gvMaxCorners) {
}

bool ManhattanBranchAndBound::Compute(const vector<ManhattanEdge> edges[],
                                      function<void(const ManhattanBuilding&)> f) {
	visitor = f;
	Initialize(edges);
	if (init_hypotheses.empty()) {
		DLOG << "Warning: ManhattanBranchAndBound failed to generate initial hypotheses, aborting.";
		return false;
	}
	Enumerate();
}

void ManhattanBranchAndBound::Configure(const PosedCamera& pcam, int v_axis) {
	pc = &pcam;

	// Compute size of a pixel
	Vec2 a = pc->RetToIm(makeVector(0.0, 0.0));
	Vec2 b = pc->RetToIm(makeVector(1.0, 1.0));
	px_diam = 1 / norm(a-b);

	// Cache the vanishing points for efficiency
	for (int i = 0; i < 3; i++) {
		vpts[i] = pcam.GetRetinaVpt(i);
	}
	vert_axis = v_axis;
	h1_axis = (vert_axis + 1) % 3;
	h2_axis = (vert_axis + 2) % 3;
	vert_vpt = vpts[vert_axis];

	// Ensure the horizon line has its positive side at the top of the image
	horizon = vpts[h1_axis] ^ vpts[h2_axis];
	if (horizon[1] < 0) horizon = -horizon;
}


void ManhattanBranchAndBound::Initialize(const vector<ManhattanEdge> edges[]) {
	// Init axis is the axis we'll use to generate initial hypotheses
	int init_axis;
	if (abs(vpts[h1_axis][0]) > abs(vpts[h2_axis][0])) {
		init_axis = h1_axis;
	} else {
		init_axis = h2_axis;
	}
	init_hypotheses.clear();

	// Find the left/right bounds
	// TODO: do this from the image bounds instead
	double xmin = INFINITY, xmax = -INFINITY;
	for (int i = 0; i < 3; i++) {
		BOOST_FOREACH(const ManhattanEdge& e, edges[i]) {
			double x1 = e.start[0] / e.start[2];
			double x2 = e.end[0] / e.end[2];
			if (x1 > x2)
				swap(x1, x2);
			if (x1 < xmin)
				xmin = x1;
			if (x2 > xmax)
				xmax = x2;
		}
	}
	Vec3 left_div = DivVec(unit(vert_vpt ^ makeVector(xmin - 1, 0, 1)));
	Vec3 right_div = DivVec(unit(vert_vpt ^ makeVector(xmax + 1, 0, 1)));

	// Identify edges above and below horizon
	for (int a = vert_axis + 1; a <= vert_axis + 2; a++) {
		int axis = a % 3;
		ceil_edges[axis].clear();
		floor_edges[axis].clear();

		const vector<ManhattanEdge>& es = edges[axis];
		BOOST_FOREACH(const ManhattanEdge& e, es) {
			if (AboveHorizon(e.start) && AboveHorizon(e.end)) {
				ceil_edges[axis].push_back(&e);
			} else if (BelowHorizon(e.start) && BelowHorizon(e.end)) {
				floor_edges[axis].push_back(&e);
			}
			// note that we discard edges that cross the horizon
		}
	}

	// Make a list of vertical edges
	vert_edges.clear();
	BOOST_FOREACH(const ManhattanEdge& e, edges[vert_axis]) {
		vert_edges.push_back(&e);
	}

	// Enumerate initial candidates. We have already decided which axis they will come from
	COUNTED_FOREACH(int ii, const ManhattanEdge* ceil_e, ceil_edges[init_axis]) {
		Vec3 div1 = ceil_e->start ^ vert_vpt;
		Vec3 div2 = ceil_e->end ^ vert_vpt;
		COUNTED_FOREACH(int jj, const ManhattanEdge* floor_e, floor_edges[init_axis]) {
			Vec3 div3 = floor_e->start ^ vert_vpt;
			Vec3 div4 = floor_e->end ^ vert_vpt;
			Vec3 p1 = div1 ^ floor_e->eqn;
			Vec3 p2 = div2 ^ floor_e->eqn;
			Vec3 p3 = div3 ^ ceil_e->eqn;
			Vec3 p4 = div4 ^ ceil_e->eqn;
			if (ceil_e->Contains(p3) || ceil_e->Contains(p4)
					|| floor_e->Contains(p1) || floor_e->Contains(p2)) {
				// There is overlap, we can create an initial hypothesis
				ManhattanCorner left;
				left.div_eqn = left_div;
				left.right_ceil = left_div ^ ceil_e->eqn;
				left.right_floor = left_div ^ floor_e->eqn;
				left.right_axis = init_axis;
				left.leftmost = true;

				ManhattanCorner right;
				right.div_eqn = right_div;
				right.left_ceil = right_div ^ ceil_e->eqn;
				right.left_floor = right_div ^ floor_e->eqn;
				right.left_axis = init_axis;
				right.rightmost = true;

				ManhattanBuilding * bld = new ManhattanBuilding;
				bld->cnrs.push_back(left);
				bld->cnrs.push_back(right);
				init_hypotheses.push_back(bld); // memory now managed by the ptr_vector
			}
		}
	}
}

void ManhattanBranchAndBound::Enumerate() {
	CHECK(!init_hypotheses.empty());
	DREPORT(init_hypotheses.size());

	BOOST_FOREACH(const ManhattanBuilding& bld, init_hypotheses) {
		BranchFrom(bld);
	}
}

void ManhattanBranchAndBound::BranchFrom(const ManhattanBuilding& bld) {
	//DLOG << "Creating building " << hypothesis_count;
	visitor(bld);

	// Branch from here
	if (bld.cnrs.size() < max_corners + 2) { // +2 is for the initial two corners
		VertBranchFrom(bld);
		HorizBranchFrom(bld);
	}
}

// Generate all building cnrss reachable by adding a single
// horizontal edge to the specified building
int ManhattanBranchAndBound::HorizBranchFrom(const ManhattanBuilding& bld) {
	//ptr_vector<ManhattanBuilding>& out) {
	int count = 0;

	for (int a = 1; a <= 2; a++) {
		int new_axis = (vert_axis + a) % 3;
		for (int surf = 0; surf <= 1; surf++) {
			const vector<const ManhattanEdge*>& es =
					surf ? ceil_edges[new_axis] : floor_edges[new_axis];

			BOOST_FOREACH(const ManhattanEdge* e, es)
			{
				// Don't add the same edge twice
				if (bld.ContainsEdge(e->id)) {
					//DLOG << "culled because edge already present";
					continue;
				}

				ManhattanBuilding::ConstCnrIt l_cnr, r_cnr;
				LocateStripe(bld, e->start, l_cnr, r_cnr);

				// Check that the stripe fully contains the edge
				if (!StripeContains(l_cnr->div_eqn, r_cnr->div_eqn,
						e->end)) {
					// Horizontal segment spans multiple stripes
					// TODO: make this a threshold rather than a hard rule
					// TODO: maybe even remove this rule?
					//DLOG << "culled because end point outside stripe";
					continue;
				}

				// Check that the stripe is generated from the opposite axis
				if (l_cnr->right_axis == e->axis) {
					//DLOG << "culled because axis already matches";
					continue;
				}

				Vec3 seg = surf ? l_cnr->right_ceil
						^ r_cnr->left_ceil : l_cnr->right_floor
						^ r_cnr->left_floor;
				Vec3 isct = e->eqn ^ seg;
				Vec3 new_div = DivVec(isct ^ vert_vpt);

				// Check that the intersection is within the stripe
				if (!StripeContains(l_cnr->div_eqn, r_cnr->div_eqn,
						isct)) {
					//DLOG << "culled because intersection outside stripe";
					continue;
				}

				// Don't add the div if too close to an existing edge
				double margin = GetStripeMargin(*l_cnr, *r_cnr,
						isct);
				if (margin < *gvMinCornerMargin * px_diam) {
					//DLOG << "culled because margin is " << margin;
					continue;
				}

				// case 1: change the stripe on the left
				if (!StripeContains(l_cnr->div_eqn, new_div,
						vpts[e->axis])) {
					//DLOG << "Adding with change to left";
					// TODO: avoid all this copying by adding the corner and then removing it
					ManhattanBuilding new_bld = bld;
					if (AddCorner(new_bld, e->id, new_div, e->axis,
							true)) {
						BranchFrom(new_bld);
						count++;
					}
				} else {
					//DLOG << "culled left due to vpt";
				}

				// case 2: change the stripe on the right
				if (!StripeContains(new_div, r_cnr->div_eqn,
						vpts[e->axis])) {
					//DLOG << "Adding with change to right";
					// TODO: avoid all this copying by adding the corner and then removing it
					ManhattanBuilding new_bld = bld;
					if (AddCorner(new_bld, e->id, new_div, e->axis,
							false)) {
						BranchFrom(new_bld);
						count++;
					}
				} else {
					//DLOG << "culled right due to vpt";
				}
			}
		}
	}
	return count;
}

// Generate all building cnrss reachable by adding a single
// vertical edge to the specified building
int ManhattanBranchAndBound::VertBranchFrom(const ManhattanBuilding& bld) {
	//ptr_vector<ManhattanBuilding>& out) {
	int count = 0;

	BOOST_FOREACH(const ManhattanEdge* e, vert_edges)
	{
		// Don't add the same edge twice
		if (bld.ContainsEdge(e->id))
			continue;

		Vec3 midp = HMidpoint(e->start, e->end);
		Vec3 new_div = DivVec(midp ^ vert_vpt);

		// Find the stripe this edge is in
		ManhattanBuilding::ConstCnrIt l_cnr, r_cnr;
		LocateStripe(bld, midp, l_cnr, r_cnr);

		// Don't add the div if too close to an existing edge
		double margin = GetStripeMargin(*l_cnr, *r_cnr, midp);
		if (margin < *gvMinCornerMargin * px_diam) {
			continue;
		}

		// Configure the new corner
		Vec3 ceil = l_cnr->right_ceil ^ r_cnr->left_ceil;
		Vec3 floor = l_cnr->right_floor ^ r_cnr->left_floor;

		// Don't add the corner if it crosses the existing floor or ceiling lines
		if ((Sign(ceil * e->start) == Sign(floor * e->start))
				|| (Sign(ceil * e->end) == Sign(floor * e->end))) {
			continue;
		}

		int axis = l_cnr->right_axis;
		int new_axis = (axis == h1_axis) ? h2_axis : h1_axis;

		// case 1: change the stripe on the left
		if (!StripeContains(l_cnr->div_eqn, new_div, vpts[new_axis])) {
			// TODO: avoid all this copying by adding the corner and then removing it
			ManhattanBuilding new_bld = bld;
			if (AddCorner(new_bld, e->id, new_div, new_axis, true)) {
				BranchFrom(new_bld);
				count++;
			}
		}

		// case 2: change the stripe on the right
		if (!StripeContains(new_div, r_cnr->div_eqn, vpts[new_axis])) {
			// TODO: avoid all this copying by adding the corner and then removing it
			ManhattanBuilding new_bld = bld;
			if (AddCorner(new_bld, e->id, new_div, new_axis, false)) {
				BranchFrom(new_bld);
				count++;
			}
		}
	}
	return count;
}

// Add a corner to bld. Return true if the corner is valid
bool ManhattanBranchAndBound::AddCorner(ManhattanBuilding& bld, int edge_id,
                                        const Vec3& div_eqn, int new_axis,
                                        bool update_left) {
	ManhattanBuilding::CnrIt l_cnr, r_cnr;
	Vec3 horizon_pt = DivVec(horizon ^ div_eqn);
	LocateStripe(bld, horizon_pt, l_cnr, r_cnr);

	// Create the new corner
	ManhattanCorner new_cnr;
	new_cnr.div_eqn = DivVec(div_eqn);

	// Check that it doesn't contain the vpt
	Vec3 ceil = l_cnr->right_ceil ^ r_cnr->left_ceil;
	Vec3 floor = l_cnr->right_floor ^ r_cnr->left_floor;
	new_cnr.left_ceil = new_cnr.right_ceil = new_cnr.div_eqn ^ ceil;
	new_cnr.left_floor = new_cnr.right_floor = new_cnr.div_eqn ^ floor;
	new_cnr.left_axis = new_cnr.right_axis = l_cnr->right_axis;

	bld.edge_ids.insert(edge_id);

	ManhattanBuilding::CnrIt new_cnr_it = bld.cnrs.insert(r_cnr, new_cnr);
	Vec3 new_floor = vpts[new_axis] ^ new_cnr.left_floor;
	Vec3 new_ceil = vpts[new_axis] ^ new_cnr.left_ceil;

	//DLOG << "updating " << (update_left ? "left" : "right") << " stripe";

	if (update_left) {
		return UpdateStripe(*l_cnr, *new_cnr_it, new_floor, new_ceil, new_axis);
	} else {
		return UpdateStripe(*new_cnr_it, *r_cnr, new_floor, new_ceil, new_axis);
	}
}

bool ManhattanBranchAndBound::AboveHorizon(const Vec3& v) const {
	// the horizon always has y>0
	return PointSign(v, horizon) < 0;
}

bool ManhattanBranchAndBound::BelowHorizon(const Vec3& v) const {
	// the horizon always has y>0
	return PointSign(v, horizon) > 0;
}

bool ManhattanBranchAndBound::OcclusionValid(ManhattanCorner& cnr) {
	CHECK_LE(abs(cnr.occl_side), 1); // check for uninitialized

	if (cnr.occl_side == 0) {
		// No occlusion, nothing to check
		return true;
	}

	// the axis of the occluding side
	int occl_axis = cnr.occl_side < 0 ? cnr.left_axis : cnr.right_axis;

	// the other horizontal axis (irrespective of cnr.left_axis, cnr.right_axis!)
	int opp_axis = (occl_axis == h1_axis) ? h2_axis : h1_axis;

	CHECK(abs(norm_sq(vpts[occl_axis]) - 1.0) < 1e-5); // should be guaranteed of this
	CHECK(abs(norm_sq(vpts[opp_axis]) - 1.0) < 1e-5); // should be guaranteed of this

	// which side is the associated vpt on?
	// -1 for left, 1 for right since div_eqn[0]>0
	int vpt_side = PointSign(vpts[occl_axis], cnr.div_eqn);

	// is the vpt on the same side as the occluding segment?
	bool vpt_behind = vpt_side == cnr.occl_side;

	// is the other vpt between the div and the vpt
	bool opp_vpt_between = PointSign(vpts[opp_axis], cnr.div_eqn) == vpt_side
			&& abs(cnr.div_eqn * vpts[opp_axis]) < abs(cnr.div_eqn
					* vpts[occl_axis]);

	// the occlusion is valid iff vpt_behind <=> opp_vpt_between
	return vpt_behind == opp_vpt_between;
}

// Update the occlusion member
bool ManhattanBranchAndBound::UpdateOcclusion(ManhattanCorner& cnr) {
	if (cnr.leftmost || cnr.rightmost) {
		// The bounding corners cannot be occluded and should not be
		// checked since they have meaningless floor and ceiling points
		// on their outer sides.
		cnr.occl_side = 0;
		return true;
	}

	if (!BelowHorizon(cnr.left_floor) || !BelowHorizon(cnr.right_floor)
			|| !AboveHorizon(cnr.left_ceil) || !AboveHorizon(cnr.right_ceil)) {
		//DLOG << "culled because intersection on wrong side of horizon";
		return false;
	}

	const double kThresh = *gvOcclThresh * px_diam;
	const double kThreshSq = kThresh * kThresh;
	double floor_diff = HNormSq(cnr.left_floor, cnr.right_floor);
	double ceil_diff = HNormSq(cnr.left_ceil, cnr.right_ceil);
	if (floor_diff <= kThreshSq || ceil_diff <= kThreshSq) {
		// Below threshold, no occlusion
		cnr.occl_side = 0;
	} else {
		if (!BelowHorizon(cnr.left_floor)) {
			WITH_DLOG			DREPORT(cnr.left_floor, horizon,
			         			        cnr.left_floor*horizon,
			         			        PointSign(cnr.left_floor, horizon));
		}
		CHECK_PRED1(BelowHorizon, cnr.left_floor);
		CHECK_PRED1(BelowHorizon, cnr.right_floor);
		// TODO: I think we should use pve_unit below since if
		// cnr.left_floor[2]<0 or cnr.right_floor[2]<0 then the
		// inequality will be swapped
		double l_dp = unit(cnr.left_floor) * horizon;
		double r_dp = unit(cnr.right_floor) * horizon;
		cnr.occl_side = l_dp > r_dp ? -1 : 1; // -1 means left occludes right
	}

	return true;
}

bool ManhattanBranchAndBound::UpdateStripe(ManhattanCorner& left_cnr,
                                           ManhattanCorner& right_cnr,
                                           const Vec3& floor_line,
                                           const Vec3& ceil_line,
                                           int new_axis) {

	left_cnr.right_floor = left_cnr.div_eqn ^ floor_line;
	left_cnr.right_ceil = left_cnr.div_eqn ^ ceil_line;
	left_cnr.right_axis = new_axis;

	right_cnr.left_floor = right_cnr.div_eqn ^ floor_line;
	right_cnr.left_ceil = right_cnr.div_eqn ^ ceil_line;
	right_cnr.left_axis = new_axis;

	if (!UpdateOcclusion(left_cnr)) {
		return false;
	}
	if (!UpdateOcclusion(right_cnr)) {
		return false;
	}

	if (!OcclusionValid(left_cnr) || !OcclusionValid(right_cnr)) {
		//DLOG << "culling due to invalid occlusion";
		return false;
	}

	return true;
}

int ManhattanBranchAndBound::LocateStripe(const ManhattanBuilding& bld,
                                          const Vec3& p,
                                          ManhattanBuilding::ConstCnrIt& l_cnr,
                                          ManhattanBuilding::ConstCnrIt& r_cnr) const {
	int index = -1;
	for (r_cnr = bld.cnrs.begin(); r_cnr != bld.cnrs.end(); r_cnr++) {
		if (PointSign(p, r_cnr->div_eqn) < 0) break;
		l_cnr = r_cnr;
		index++;
	}
	CHECK(r_cnr != bld.cnrs.begin());
	CHECK(r_cnr != bld.cnrs.end());
	return index;
}

int ManhattanBranchAndBound::LocateStripe(ManhattanBuilding& bld,
                                          const Vec3& p,
                                          ManhattanBuilding::CnrIt& l_cnr,
                                          ManhattanBuilding::CnrIt& r_cnr) {
	int index = -1;
	for (r_cnr = bld.cnrs.begin(); r_cnr != bld.cnrs.end(); r_cnr++) {
		if (PointSign(p, r_cnr->div_eqn) < 0) break;
		l_cnr = r_cnr;
		index++;
	}
	CHECK(r_cnr != bld.cnrs.begin());
	CHECK(r_cnr != bld.cnrs.end());
	return index;
}

bool ManhattanBranchAndBound::StripeContains(const Vec3& left_div,
                                             const Vec3& right_div,
                                             const Vec3& p) {
	CHECK_GE(left_div[0], 0);
	CHECK_GE(right_div[0], 0);
	return PointSign(p, left_div) > 0 && PointSign(p, right_div) < 0;
}

bool ManhattanBranchAndBound::StripeContains(const ManhattanCorner& left,
                                             const ManhattanCorner& right,
                                             const Vec3& p) {
	return StripeContains(left.div_eqn, right.div_eqn, p);
}

double ManhattanBranchAndBound::GetStripeMargin(const ManhattanCorner& left,
                                                const ManhattanCorner& right,
                                                const Vec3& p) {
	double d1 = EuclPointLineDist(p, left.div_eqn);
	double d2 = EuclPointLineDist(p, left.div_eqn);
	return min(d1, d2);
}

Vec3 ManhattanBranchAndBound::DivVec(const Vec3& v) {
	return v[0] > 0 ? v : -v;
}







void ManhattanEvaluator::Configure(const PosedCamera& pcam, int v_axis) {
	pc = &pcam;
	vert_axis = v_axis;
	h1_axis = (vert_axis + 1) % 3;
	h2_axis = (vert_axis + 2) % 3;

	// Find the horizon line
	horizon = pcam.GetRetinaVpt(h1_axis) ^ pcam.GetRetinaVpt(h2_axis);
	if (horizon[1] < 0) horizon = -horizon;
}

void ManhattanEvaluator::PredictOrientations(const ManhattanBuilding& bld,
                                             MatI& orients) const {
	CHECK_GT(orients.Rows(), 0);
	CHECK_GT(orients.Cols(), 0);
	orients.Fill(vert_axis);
	Polygon<4> wall;
	DiagonalMatrix<3> Mscale(makeVector(1.0*orients.Cols()/pc->image_size().x,
			1.0*orients.Rows()/pc->image_size().y,
			1.0));
	for (ManhattanBuilding::ConstCnrIt left_cnr = bld.cnrs.begin();
			successor(left_cnr) != bld.cnrs.end();
			left_cnr++) {
		ManhattanBuilding::ConstCnrIt right_cnr = successor(left_cnr);
		wall.verts[0] = Mscale * pc->RetToIm(left_cnr->right_ceil);
		wall.verts[1] = Mscale * pc->RetToIm(right_cnr->left_ceil);
		wall.verts[2] = Mscale * pc->RetToIm(right_cnr->left_floor);
		wall.verts[3] = Mscale * pc->RetToIm(left_cnr->right_floor);
		FillPolygon(array_range(wall.verts, 4),
		            orients,
		            OtherHorizAxis(left_cnr->right_axis));
	}
}

void ManhattanEvaluator::PredictGridOrientations(const ManhattanBuilding& bld,
                                                 MatI& orients) const {
	orients.Resize(*gvOrientRes, *gvOrientRes);
	PredictOrientations(bld, orients);
}
void ManhattanEvaluator::PredictImOrientations(const ManhattanBuilding& bld,
                                               MatI& orients) const {
	orients.Resize(pc->image_size().y, pc->image_size().x);
	PredictOrientations(bld, orients);
}

int ManhattanEvaluator::OtherHorizAxis(int a) const {
	return a == h1_axis ? h2_axis : h1_axis;
}

void ManhattanEvaluator::DrawBuilding(const ManhattanBuilding& bld,
                                      ImageRGB<byte>& canvas) {
	PixelRGB<byte> colors[3];
	colors[vert_axis] = Colors::primary(vert_axis);
	colors[h1_axis] = Colors::primary(h1_axis);
	colors[h2_axis] = Colors::primary(h2_axis);
	ManhattanBuilding::ConstCnrIt a = bld.cnrs.begin();
	ManhattanBuilding::ConstCnrIt b = successor(a);
	while (b != bld.cnrs.end()) {
		Vec2 tl = project(pc->RetToIm(a->right_ceil));
		Vec2 bl = project(pc->RetToIm(a->right_floor));
		Vec2 tr = project(pc->RetToIm(b->left_ceil));
		Vec2 br = project(pc->RetToIm(b->left_floor));
		PixelRGB<byte> floor_color = colors[a->right_axis];
		floor_color.r /= 2;
		floor_color.g /= 2;
		floor_color.b /= 2;
		DrawLineClipped(canvas, tl, bl, colors[vert_axis]);
		DrawLineClipped(canvas, tr, br, colors[vert_axis]);
		DrawLineClipped(canvas, tl, tr, colors[a->right_axis]);
		DrawLineClipped(canvas, bl, br, floor_color);
		a++;
		b++;
	}
}

void ManhattanEvaluator::DrawPrediction(const ManhattanBuilding& bld,
                                        ImageRGB<byte>& canvas) {
	MatI predicted;
	PredictImOrientations(bld, predicted);
	DrawOrientations(predicted, canvas);
}

void ManhattanEvaluator::DrawOrientations(const MatI& orients,
                                          ImageRGB<byte>& canvas) {
	ResizeImage(canvas, pc->image_size());
	for (int y = 0; y < pc->image_size().y; y++) {
		PixelRGB<byte>* row = canvas[y];
		const int* orient_row = orients[y*orients.Rows()/pc->image_size().y];
		for (int x = 0; x < pc->image_size().x; x++) {
			int orient = orient_row[x*orients.Cols()/pc->image_size().x];
			if (orient == -1) {
				row[x] = PixelRGB<byte>(255,255,255);
			} else {
				row[x] = Colors::primary(orient);
			}
		}
	}
}

void ManhattanEvaluator::WriteBuilding(const ManhattanBuilding& bld,
                                       const string& filename) {
	ofstream output(filename.c_str());
	output << "num_corners: " << bld.cnrs.size() << endl;
	output << "edges: " << iowrap(bld.edge_ids) << endl;
	COUNTED_FOREACH (int i, const ManhattanCorner& cnr, bld.cnrs) {
		output << "corner " << i << endl;
		output << "  left_axis: " << cnr.left_axis << endl;
		output << "  right_axis: " << cnr.right_axis << endl;
		output << "  occl_side: " << cnr.occl_side << endl;
		output << "  left_floor: " << pc->RetToIm(cnr.left_floor) << endl;
		output << "  right_floor: " << pc->RetToIm(cnr.right_floor) << endl;
		output << "  left_ceil: " << pc->RetToIm(cnr.left_ceil) << endl;
		output << "  right_ceil: " << pc->RetToIm(cnr.right_ceil) << endl;
	}
	output.close();
}

void ManhattanEvaluator::OutputBuildingViz(const ImageRGB<byte>& image,
                                           const ManhattanBuilding& bld,
                                           const string& filename) {
	ImageRGB<byte> canvas;
	ImageCopy(image, canvas);
	DrawBuilding(bld, canvas);
	WriteImage(filename, canvas);
}

void ManhattanEvaluator::OutputPredictionViz(const ManhattanBuilding& bld,
                                             const string& filename) {
	ImageRGB<byte> canvas;
	DrawPrediction(bld, canvas);
	WriteImage(filename, canvas);
}

void ManhattanEvaluator::OutputAllViz(const ImageRGB<byte>& image,
                                      const ManhattanBuilding& bld,
                                      const string& basename) {
	OutputBuildingViz(image, bld, basename+"bld.png");
	OutputPredictionViz(bld, basename+"prediction.png");
	WriteBuilding(bld, basename+"info.txt");
}

}