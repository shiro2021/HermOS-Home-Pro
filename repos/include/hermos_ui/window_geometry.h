#ifndef _HERMOS_UI__WINDOW_GEOMETRY_H_
#define _HERMOS_UI__WINDOW_GEOMETRY_H_

#include <nitpicker_gfx/box_painter.h>

namespace Hermos::Ui {

	using namespace Genode;
	using Point = Surface_base::Point;
	using Area  = Surface_base::Area;
	using Rect  = Surface_base::Rect;

	/**
	 * Canonical corner inset of the HermOS stretched-horizontal hexagon.
	 *
	 * Decision D-006 fixes the outline: exactly six vertices
	 *   (C,0) (W-C,0) (W,H/2) (W-C,H) (C,H) (0,H/2)
	 * with a long straight top and bottom edge, short diagonal transitions
	 * and a single point at mid-height on each side.
	 *
	 * C is *derived* from the width. It is not a parameter: every hexagon in
	 * the system - window frame, hub, node, tile - uses this one definition,
	 * so two hexagons can never disagree about how sharp their corners are.
	 */
	constexpr int hex_corner_inset(int width) { return width / 4; }

	/**
	 * Horizontal inset of the hexagon outline at pixel row 'row'
	 *
	 * Linear from C at the top edge to 0 at mid-height and back to C at the
	 * bottom edge. The middle row is the left/right vertex, so the inset is
	 * exactly zero there.
	 */
	inline int hex_inset_at(int row, int width, int height)
	{
		int const corner = hex_corner_inset(width);
		int const half   = height / 2;

		if (corner <= 0 || half <= 1 || row < 0 || row >= height)
			return 0;

		/* mirror around the vertical middle: top and bottom are symmetric */
		int const r = row < half ? row : height - 1 - row;

		if (r >= half - 1)
			return 0;

		return corner - (corner * r) / (half - 1);
	}

	struct Window_geometry
	{
		Point origin;
		Area  size;

		/**
		 * Corner inset, always derived from the width (see hex_corner_inset).
		 */
		int corner_inset() const { return hex_corner_inset(int(size.w)); }

		int inset_at(int y) const
		{
			return hex_inset_at(y, int(size.w), int(size.h));
		}

		bool contains(Point p) const
		{
			int const x = p.x - origin.x;
			int const y = p.y - origin.y;
			if (y < 0 || y >= int(size.h)) return false;
			int const inset = inset_at(y);
			return x >= inset && x < int(size.w) - inset;
		}
	};

	/**
	 * Octagonal (chamfered-rectangle) geometry — eight sides.
	 *
	 * Adopted for better visuals: flat top/bottom AND vertical left/right
	 * sides, with a 45-degree chamfer at each corner. Corner inset is
	 * min(width,height)/6.
	 */
	constexpr int oct_corner_inset(int width, int height)
	{
		int const m = width < height ? width : height;
		return m / 6;
	}

	inline int oct_inset_at(int row, int height, int corner)
	{
		if (corner <= 0) return 0;
		if (row < corner)           return corner - row;               /* top chamfers */
		if (row >= height - corner) return row - (height - corner) + 1; /* bottom chamfers */
		return 0;                                                       /* vertical sides */
	}

	/* The shared six-vertex frame/title outline (kept for the shell). */
	template <typename PT>
	void paint_six_side_frame(Surface<PT> &surface, Window_geometry const &g,
	                          Color body, Color title, Color edge, int title_height)
	{
		for (int y = 0; y < int(g.size.h); ++y) {
			int const inset = g.inset_at(y);
			int const left  = g.origin.x + inset;
			int const width = int(g.size.w) - 2 * inset;
			if (width <= 0) continue;

			Color row = body;
			if (y < title_height) {
				unsigned const shade = unsigned((title_height - y) / 4);
				row = Color::rgb(uint8_t(min(255U, unsigned(title.r) + shade)),
				                 uint8_t(min(255U, unsigned(title.g) + shade)),
				                 uint8_t(min(255U, unsigned(title.b) + shade)));
			}
			Box_painter::paint(surface, Rect(Point(left, g.origin.y + y),
			                                 Area(width, 1)), row);

			/* Restrained carbon grain, clipped by this row of the hexagon. */
			if (y < title_height && (y % 4 == 1))
				for (int x = left + 10 + (y % 8); x < left + width - 10; x += 18)
					Box_painter::paint(surface,
					                   Rect(Point(x, g.origin.y + y), Area(5, 1)),
					                   Color::rgb(39, 43, 48));

			for (int t = 0; t < 2; ++t) {
				Box_painter::paint(surface,
				                   Rect(Point(left + t, g.origin.y + y), Area(1, 1)), edge);
				Box_painter::paint(surface,
				                   Rect(Point(left + width - 1 - t, g.origin.y + y),
				                        Area(1, 1)), edge);
			}
		}
	}

}

#endif
