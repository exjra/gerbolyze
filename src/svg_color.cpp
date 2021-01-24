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

#include "svg_color.h"

#include <assert.h>
#include <string>
#include <cmath>

using namespace svg_plugin;
using namespace std;

/* Map an SVG fill or stroke definition (color, but may also be a pattern) to a gerber color.
 *
 * This function handles transparency: Transparent SVG colors are mapped such that no gerber output is generated for
 * them.
 */
enum gerber_color svg_plugin::svg_color_to_gerber(string color, string opacity, enum gerber_color default_val) {
    float alpha = 1.0;
    if (!opacity.empty() && opacity[0] != '\0') {
        char *endptr = nullptr;
        alpha = strtof(opacity.data(), &endptr);
        assert(endptr);
        assert(*endptr == '\0');
    }

    if (alpha < 0.5f) {
        return GRB_NONE;
    }

    if (color.empty()) {
        return default_val;
    }

    if (color == "none") {
        return GRB_NONE;
    }

    if (color.rfind("url(#", 0) != string::npos) {
        return GRB_PATTERN_FILL;
    }

    if (color.length() == 7 && color[0] == '#') {
        HSVColor hsv(color);
        if (hsv.v >= 0.5) {
            return GRB_CLEAR;
        }
    }

    return GRB_DARK;
}

svg_plugin::RGBColor::RGBColor(string hex) {
    assert(hex[0] == '#');
    char *endptr = nullptr;
    const char *c = hex.data();
    int rgb = strtol(c + 1, &endptr, 16);
    assert(endptr);
    assert(endptr == c + 7);
    assert(*endptr == '\0');
    r = ((rgb >> 16) & 0xff) / 255.0f;
    g = ((rgb >>  8) & 0xff) / 255.0f;
    b = ((rgb >>  0) & 0xff) / 255.0f;
};

svg_plugin::HSVColor::HSVColor(const RGBColor &color) {
    float xmax = fmax(color.r, fmax(color.g, color.b));
    float xmin = fmin(color.r, fmin(color.g, color.b));
    float c = xmax - xmin;

    v = xmax;

    if (c == 0)
        h = 0;
    else if (v == color.r)
        h = 1/3 * (0 + (color.g - color.b) / c);
    else if (v == color.g)
        h = 1/3 * (2 + (color.b - color.r) / c);
    else //  v == color.b
        h = 1/3 * (4 + (color.r - color.g) / c);

    s = (v == 0) ? 0 : (c/v);
}

/* Invert gerber color */
enum gerber_color svg_plugin::gerber_color_invert(enum gerber_color color) {
    switch (color) {
        case GRB_CLEAR: return GRB_DARK;
        case GRB_DARK: return GRB_CLEAR;
        default: return color; /* none, pattern */
    }
}

/* Read node's fill attribute and convert it to a gerber color */
enum gerber_color svg_plugin::gerber_fill_color(const pugi::xml_node &node) {
    return svg_color_to_gerber(node.attribute("fill").value(), node.attribute("fill-opacity").value(), GRB_DARK);
}

/* Read node's stroke attribute and convert it to a gerber color */
enum gerber_color svg_plugin::gerber_stroke_color(const pugi::xml_node &node) {
    return svg_color_to_gerber(node.attribute("stroke").value(), node.attribute("stroke-opacity").value(), GRB_NONE);
}

