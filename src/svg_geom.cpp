/*
 * This program source code file is part of KICAD, a free EDA CAD application.
 *
 * Copyright (C) 2021 Jan Sebastian Götte <kicad@jaseg.de>
 * Copyright (C) 2021 KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

#include "svg_geom.h"

#include <cmath>
#include <string>
#include <sstream>
#include <assert.h>
#include <cairo.h>
#include "svg_import_defs.h"

using namespace ClipperLib;
using namespace std;

/* Get bounding box of a Clipper Paths */
IntRect svg_plugin::get_paths_bounds(const Paths &paths) {
    if (paths.empty()) {
        return {0, 0, 0, 0};
    }

    if (paths[0].empty()) {
        return {0, 0, 0, 0};
    }

    IntPoint p0 = paths[0][0];
    cInt x0=p0.X, y0=p0.Y, x1=p0.X, y1=p0.Y;

    for (const Path &p : paths) {
        for (const IntPoint ip : p) {
            if (ip.X < x0)
                x0 = ip.X;

            if (ip.Y < y0)
                y0 = ip.Y;

            if (ip.X > x1)
                x1 = ip.X;

            if (ip.Y > y1)
                y1 = ip.Y;
        }
    }

    return {x0, y0, x1, y1};
}

enum ClipperLib::PolyFillType svg_plugin::clipper_fill_rule(const pugi::xml_node &node) {
    string val(node.attribute("fill-rule").value());
    if (val == "evenodd")
        return ClipperLib::pftEvenOdd;
    else
        return ClipperLib::pftNonZero; /* default */
}

enum ClipperLib::EndType svg_plugin::clipper_end_type(const pugi::xml_node &node) {
    string val(node.attribute("stroke-linecap").value());
    if (val == "round")
        return ClipperLib::etOpenRound;

    if (val == "square")
        return ClipperLib::etOpenSquare;

    return ClipperLib::etOpenButt;
}

enum ClipperLib::JoinType svg_plugin::clipper_join_type(const pugi::xml_node &node) {
    string val(node.attribute("stroke-linejoin").value());
    if (val == "round")
        return ClipperLib::jtRound;

    if (val == "bevel")
        return ClipperLib::jtSquare;

    return ClipperLib::jtMiter;
}

/* Take a Clipper polytree, i.e. a description of a set of polygons, their holes and their inner polygons, and remove
 * all holes from it. We remove holes by splitting each polygon that has a hole into two or more pieces so that the hole
 * is no more. These pieces perfectly fit each other so there is no visual or functional difference.
 */
void svg_plugin::dehole_polytree(PolyNode &ptree, Paths &out) {
    for (int i=0; i<ptree.ChildCount(); i++) {
        PolyNode *nod = ptree.Childs[i];
        assert(nod);
        assert(!nod->IsHole());

        /* First, recursively process inner polygons. */
        for (int j=0; j<nod->ChildCount(); j++) {
            PolyNode *child = nod->Childs[j];
            assert(child);
            assert(child->IsHole());

            if (child->ChildCount() > 0) {
                dehole_polytree(*child, out);
            }
        }

        if (nod->ChildCount() == 0) {
            out.push_back(nod->Contour);

        } else {

            /* Do not add children's children, those were handled in the recursive call above */
            Clipper c;
            c.AddPath(nod->Contour, ptSubject, /* closed= */ true);
            for (int k=0; k<nod->ChildCount(); k++) {
                c.AddPath(nod->Childs[k]->Contour, ptSubject, /* closed= */ true);
            }

            /* Find a viable cut: Cut from top-left bounding box corner, through two subsequent points on the hole
             * outline and to top-right bbox corner. */
            IntRect bbox = c.GetBounds();
            Path tri = { { bbox.left, bbox.top }, nod->Childs[0]->Contour[0], nod->Childs[0]->Contour[1], { bbox.right, bbox.top } };
            c.AddPath(tri, ptClip, true);

            PolyTree solution;
            c.StrictlySimple(true);
            /* Execute twice, once for intersection fragment and once for difference fragment. Note that this will yield
             * at least two, but possibly more polygons. */
            c.Execute(ctDifference, solution, pftNonZero);
            dehole_polytree(solution, out);

            c.Execute(ctIntersection, solution, pftNonZero);
            dehole_polytree(solution, out);
        }
    }
}

/* Intersect two clip paths. Both must share a coordinate system. */
void svg_plugin::combine_clip_paths(Paths &in_a, Paths &in_b, Paths &out) {
    Clipper c;
    c.StrictlySimple(true);
    c.AddPaths(in_a, ptClip, /* closed */ true);
    c.AddPaths(in_b, ptSubject, /* closed */ true);
    /* Nonzero fill since both input clip paths must already have been preprocessed by clipper. */
    c.Execute(ctIntersection, out, pftNonZero);
}

/* Transform given clipper paths under the given cairo transform. If no transform is given, use cairo's current
 * user-to-device transform. */
void svg_plugin::transform_paths(cairo_t *cr, Paths &paths, cairo_matrix_t *mat) {
    cairo_save(cr);
    if (mat != nullptr) {
        cairo_set_matrix(cr, mat);
    }
    
    for (Path &p : paths) {
        transform(p.begin(), p.end(), p.begin(),
                [cr](IntPoint p) -> IntPoint {
                    double x = p.X / clipper_scale, y = p.Y / clipper_scale;
                    cairo_user_to_device(cr, &x, &y);
                    return { (cInt)round(x * clipper_scale), (cInt)round(y * clipper_scale) };
                });
    }

    cairo_restore(cr);
}

