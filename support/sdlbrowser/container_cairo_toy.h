// container_cairo_toy.h
//
// A drop-in replacement for container_cairo_pango that renders text with the
// cairo "toy" font API (freetype/fontconfig under the hood) instead of Pango.
// It implements exactly the four text methods of litehtml::document_container
// that container_cairo leaves abstract, so it inherits all of the drawing /
// borders / gradients / images support from container_cairo.
//
// This keeps the SDL2 browser free of any GTK/Pango/Glib dependency.
#pragma once

#include "container_cairo.h"
#include <string>

struct cairo_toy_font
{
	std::string				family;
	cairo_font_slant_t		slant  = CAIRO_FONT_SLANT_NORMAL;
	cairo_font_weight_t		weight = CAIRO_FONT_WEIGHT_NORMAL;
	double					size = 0;

	cairo_scaled_font_t*	scaled = nullptr;	// owned

	// metrics (pixels)
	double					ascent = 0;
	double					descent = 0;
	double					height = 0;
	double					x_height = 0;
	double					ch_width = 0;

	// text decoration
	bool					underline = false;
	bool					strikeout = false;
	bool					overline = false;
	int						decoration_style = 0;
	litehtml::web_color		decoration_color;
	int						underline_thickness = 1;
	int						underline_position  = 0;
	int						strikethrough_thickness = 1;
	int						strikethrough_position  = 0;
	int						overline_thickness = 1;
	int						overline_position  = 0;
};

class container_cairo_toy : public container_cairo
{
public:
	container_cairo_toy();
	~container_cairo_toy() override;

	litehtml::uint_ptr create_font(const litehtml::font_description& descr, const litehtml::document* doc, litehtml::font_metrics* fm) override;
	void               delete_font(litehtml::uint_ptr hFont) override;
	litehtml::pixel_t  text_width(const char* text, litehtml::uint_ptr hFont) override;
	void               draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
								 litehtml::web_color color, const litehtml::position& pos) override;
};
