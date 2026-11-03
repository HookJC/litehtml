#include "container_cairo_toy.h"

#include <array>
#include <cctype>
#include <cmath>
#include <cstring>

namespace
{
	// Map a CSS font-family list (comma separated, possibly generic keywords) to
	// a concrete font that cairo/freetype can resolve on the target system.
	std::string resolve_family(const std::string& css_family_list)
	{
		std::string lower;
		lower.reserve(css_family_list.size());
		for(char ch : css_family_list) lower.push_back((char) std::tolower((unsigned char) ch));

		auto contains = [&](const char* needle)
		{
			return lower.find(needle) != std::string::npos;
		};

		// On Windows pick CJK-capable fonts so Chinese text renders. These ship
		// with the OS and are discoverable by fontconfig/freetype.
		if(contains("mono"))        return "NSimSun";        // 新宋体 (monospace CJK)
		if(contains("times"))       return "SimSun";         // 宋体
		if(contains("serif"))       return "SimSun";         // 宋体
		if(contains("courier"))     return "NSimSun";
		if(contains("arial"))       return "Microsoft YaHei"; // 微软雅黑
		if(contains("sans"))        return "Microsoft YaHei"; // 微软雅黑
		// default (cursive / fantasy / unknown) -> a readable CJK sans
		return "Microsoft YaHei";
	}

	cairo_toy_font* make_toy_font(const litehtml::font_description& descr)
	{
		auto* f = new cairo_toy_font;
		f->family  = resolve_family(descr.family);
		f->slant   = (descr.style == litehtml::font_style_italic) ? CAIRO_FONT_SLANT_ITALIC : CAIRO_FONT_SLANT_NORMAL;
		f->weight  = (descr.weight >= 600) ? CAIRO_FONT_WEIGHT_BOLD : CAIRO_FONT_WEIGHT_NORMAL;
		f->size    = (double) descr.size;

		cairo_font_face_t* face = cairo_toy_font_face_create(f->family.c_str(), f->slant, f->weight);

		cairo_matrix_t font_matrix;
		cairo_matrix_init_scale(&font_matrix, f->size, f->size);
		cairo_matrix_t ctm;
		cairo_matrix_init_identity(&ctm);

		cairo_font_options_t* opts = cairo_font_options_create();
		cairo_font_options_set_antialias(opts, CAIRO_ANTIALIAS_GRAY);

		f->scaled = cairo_scaled_font_create(face, &font_matrix, &ctm, opts);
		cairo_font_options_destroy(opts);
		cairo_font_face_destroy(face);

		cairo_scaled_font_status(f->scaled); // ensure it is realized

		return f;
	}

	double text_width_of(cairo_scaled_font_t* sf, const char* text)
	{
		cairo_text_extents_t te;
		cairo_scaled_font_text_extents(sf, text, &te);
		// Use the pen advance (not the ink bounding-box width): a space has zero
		// ink width but a non-zero x_advance, otherwise words run together.
		return te.x_advance;
	}
}

container_cairo_toy::container_cairo_toy() = default;

container_cairo_toy::~container_cairo_toy()
{
	clear_images();
}

litehtml::uint_ptr container_cairo_toy::create_font(const litehtml::font_description& descr,
													const litehtml::document* /*doc*/,
													litehtml::font_metrics* fm)
{
	cairo_toy_font* f = make_toy_font(descr);

	cairo_font_extents_t fe;
	cairo_scaled_font_extents(f->scaled, &fe);

	f->ascent  = fe.ascent;
	f->descent = fe.descent;
	f->height  = fe.height;

	// x-height: height of a lowercase 'x'
	f->x_height = text_width_of(f->scaled, "x");           // fallback
	cairo_text_extents_t xe;
	cairo_scaled_font_text_extents(f->scaled, "x", &xe);
	f->x_height = xe.height > 0 ? xe.height : fe.height * 0.5;

	// advance width of '0'
	cairo_text_extents_t ze;
	cairo_scaled_font_text_extents(f->scaled, "0", &ze);
	f->ch_width = ze.x_advance > 0 ? ze.x_advance : fe.height * 0.5;

	if(fm)
	{
		fm->font_size   = descr.size;
		fm->height      = (litehtml::pixel_t) std::round(f->height);
		fm->ascent      = (litehtml::pixel_t) std::round(f->ascent);
		fm->descent     = (litehtml::pixel_t) std::round(f->descent);
		fm->x_height    = (litehtml::pixel_t) std::round(f->x_height);
		fm->ch_width    = (litehtml::pixel_t) std::round(f->ch_width);
		fm->sub_shift   = (litehtml::pixel_t) std::round(descr.size / 5.0);
		fm->super_shift = (litehtml::pixel_t) std::round(descr.size / 3.0);
	}

	// decoration bookkeeping
	f->underline  = (descr.decoration_line & litehtml::text_decoration_line_underline)       != 0;
	f->strikeout  = (descr.decoration_line & litehtml::text_decoration_line_line_through)   != 0;
	f->overline   = (descr.decoration_line & litehtml::text_decoration_line_overline)       != 0;
	f->decoration_style   = descr.decoration_style;
	f->decoration_color   = descr.decoration_color;
	f->underline_thickness = (int) std::max(1.0, std::round(fe.height / 12.0));
	f->underline_position  = (int) std::round(fe.ascent * 0.12);   // below baseline
	f->strikethrough_thickness = f->underline_thickness;
	f->strikethrough_position  = (int) std::round(fe.ascent * 0.45); // above baseline
	f->overline_thickness  = f->underline_thickness;
	f->overline_position   = (int) std::round(fe.ascent);          // above baseline

	return (litehtml::uint_ptr) f;
}

void container_cairo_toy::delete_font(litehtml::uint_ptr hFont)
{
	auto* f = (cairo_toy_font*) hFont;
	if(f)
	{
		if(f->scaled) cairo_scaled_font_destroy(f->scaled);
		delete f;
	}
}

litehtml::pixel_t container_cairo_toy::text_width(const char* text, litehtml::uint_ptr hFont)
{
	auto* f = (cairo_toy_font*) hFont;
	if(!f || !text) return 0;
	return (litehtml::pixel_t) std::round(text_width_of(f->scaled, text));
}

namespace
{
	enum class draw_type { overline, strikethrough, underline };

	void draw_decoration_line(cairo_t* cr, litehtml::pixel_t x, litehtml::pixel_t y, litehtml::pixel_t width,
							 int thickness, draw_type type, int style, const litehtml::web_color& color)
	{
		double top;
		switch(type)
		{
		case draw_type::underline:    top = y + (double) thickness / 2.0; break;
		case draw_type::overline:     top = y - (double) thickness / 2.0; break;
		case draw_type::strikethrough: top = y + 0.5; break;
		default: top = y; break;
		}

		cairo_save(cr);
		cairo_set_source_rgba(cr, color.red / 255.0, color.green / 255.0, color.blue / 255.0, color.alpha / 255.0);
		cairo_set_line_width(cr, (double) thickness);

		if(style == litehtml::text_decoration_style_dotted)
		{
			std::array<double, 2> dashes{0.0, thickness * 2.0};
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_set_dash(cr, dashes.data(), 2, (double) x);
		}
		else if(style == litehtml::text_decoration_style_dashed)
		{
			std::array<double, 2> dashes{thickness * 2.0, thickness * 3.0};
			cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
			cairo_set_dash(cr, dashes.data(), 2, (double) x);
		}
		else if(style == litehtml::text_decoration_style_double && type == draw_type::underline)
		{
			cairo_move_to(cr, x, top);
			cairo_line_to(cr, x + width, top);
			cairo_stroke(cr);
			cairo_move_to(cr, x, top + thickness + 1.0);
			cairo_line_to(cr, x + width, top + thickness + 1.0);
			cairo_restore(cr);
			return;
		}

		cairo_move_to(cr, x, top);
		cairo_line_to(cr, x + width, top);
		cairo_stroke(cr);
		cairo_restore(cr);
	}
}

void container_cairo_toy::draw_text(litehtml::uint_ptr hdc, const char* text, litehtml::uint_ptr hFont,
									litehtml::web_color color, const litehtml::position& pos)
{
	auto* f  = (cairo_toy_font*) hFont;
	auto* cr = (cairo_t*) hdc;
	if(!f || !cr || !text) return;

	cairo_save(cr);
	apply_clip(cr);

	// baseline = top + (box_height - descent); mirrors container_cairo_pango
	litehtml::pixel_t baseline_local = pos.height - (litehtml::pixel_t) std::round(f->descent);
	litehtml::pixel_t x = pos.left();
	litehtml::pixel_t y = pos.top() + baseline_local;

	cairo_set_scaled_font(cr, f->scaled);
	cairo_set_source_rgba(cr, color.red / 255.0, color.green / 255.0, color.blue / 255.0, color.alpha / 255.0);
	cairo_move_to(cr, (double) x, (double) y);
	cairo_show_text(cr, text);
	// The cairo toy font / freetype back-end does not reliably synthesize a bold
	// face for the CJK fonts we resolve, so embolden by drawing the glyphs a
	// second time with a sub-pixel horizontal offset.
	if(f->weight == CAIRO_FONT_WEIGHT_BOLD)
	{
		cairo_move_to(cr, (double) x + 0.6, (double) y);
		cairo_show_text(cr, text);
	}

	litehtml::web_color deco_color = f->decoration_color.is_current_color ? color : f->decoration_color;
	litehtml::pixel_t tw = (litehtml::pixel_t) std::round(text_width_of(f->scaled, text));

	if(f->underline)
	{
		draw_decoration_line(cr, x, y + f->underline_position, tw, f->underline_thickness,
							 draw_type::underline, f->decoration_style, deco_color);
	}
	if(f->strikeout)
	{
		draw_decoration_line(cr, x, y - f->strikethrough_position, tw, f->strikethrough_thickness,
							 draw_type::strikethrough, f->decoration_style, deco_color);
	}
	if(f->overline)
	{
		draw_decoration_line(cr, x, y - f->overline_position, tw, f->overline_thickness,
							 draw_type::overline, f->decoration_style, deco_color);
	}

	cairo_restore(cr);
}
