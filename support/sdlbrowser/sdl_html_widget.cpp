#include "sdl_html_widget.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
	double clampd(double v, double lo, double hi)
	{
		if(hi < lo) return lo;
		return std::max(lo, std::min(v, hi));
	}
}

sdl_html_widget::sdl_html_widget()
{
	m_notifier = std::make_shared<sdl_notifier>();
	m_notifier->connect_redraw([this]() { on_redraw(); });
	m_notifier->connect_render([this]() { on_render(); });
	m_notifier->connect_on_page_loaded([this](uint64_t id) { on_page_loaded(id); });
	m_notifier->connect_on_set_caption([this](const std::string& c) { on_caption(c); });
	// system cursors are created in init(), after SDL_Init(VIDEO)
}

sdl_html_widget::~sdl_html_widget()
{
	if(m_notifier) m_notifier->disconnect();
	if(m_texture)  SDL_DestroyTexture(m_texture);
	if(m_renderer) SDL_DestroyRenderer(m_renderer);
	if(m_window)   SDL_DestroyWindow(m_window);
	if(m_cursor_arrow) SDL_FreeCursor(m_cursor_arrow);
	if(m_cursor_hand)  SDL_FreeCursor(m_cursor_hand);
	if(m_cursor_ibeam) SDL_FreeCursor(m_cursor_ibeam);
	SDL_Quit();
}

bool sdl_html_widget::init(const char* title, int width, int height)
{
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
	{
		std::cerr << "SDL_Init failed: " << SDL_GetError() << std::endl;
		return false;
	}

	m_cursor_arrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
	m_cursor_hand  = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
	m_cursor_ibeam = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_IBEAM);

	m_window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
								width, height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN);
	if(!m_window) { std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << std::endl; return false; }

	m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(!m_renderer)
	{
		// fall back to software rendering
		m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
	}
	if(!m_renderer) { std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << std::endl; return false; }

	SDL_SetRenderDrawColor(m_renderer, 255, 255, 255, 255);
	return true;
}

void sdl_html_widget::run(const std::string& url)
{
	int w = 0, h = 0;
	SDL_GetWindowSize(m_window, &w, &h);
	resize(w, h);
	open_url(url);

	m_running = true;
	while(m_running)
	{
		SDL_Event ev;
		while(SDL_PollEvent(&ev))
		{
			if(ev.type == SDL_QUIT)
			{
				m_running = false;
			}
			else if(m_notifier && ev.type == m_notifier->event_id())
			{
				m_notifier->pump();
			}
			else
			{
				handle_event(ev);
			}
		}
		SDL_Delay(1);
	}
}

// ---------------------------------------------------------------------------
// html_host_interface
// ---------------------------------------------------------------------------

double sdl_html_widget::get_dpi()
{
	return 96.0;
}

int sdl_html_widget::get_screen_width()
{
	SDL_DisplayMode dm;
	if(SDL_GetCurrentDisplayMode(0, &dm) == 0) return dm.w;
	return 1920;
}

int sdl_html_widget::get_screen_height()
{
	SDL_DisplayMode dm;
	if(SDL_GetCurrentDisplayMode(0, &dm) == 0) return dm.h;
	return 1080;
}

void sdl_html_widget::get_viewport(litehtml::position& viewport) const
{
	viewport.x      = m_draw_buffer.get_left();
	viewport.y      = m_draw_buffer.get_top();
	viewport.width  = m_draw_buffer.get_width();
	viewport.height = m_draw_buffer.get_height();
}

int sdl_html_widget::get_render_width()
{
	return m_draw_buffer.get_width();
}

cairo_surface_t* sdl_html_widget::load_image(const std::string& path)
{
	cairo_surface_t* surf = cairo_image_surface_create_from_png(path.c_str());
	if(cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS)
	{
		cairo_surface_destroy(surf);
		return nullptr;
	}
	return surf;
}

void sdl_html_widget::scroll_to(int x, int y)
{
	m_scroll_x = (double) x;
	m_scroll_y = (double) y;
	update_view_port(current_page());
	apply_scroll();
}

void sdl_html_widget::open_url(const std::string& url)
{
	std::string fragment;
	std::string s_url = url;

	auto p = s_url.find('#');
	if(p != std::string::npos)
	{
		fragment = s_url.substr(p + 1);
		s_url.erase(p);
	}

	std::string current = m_history.current();
	auto cp = current.find('#');
	if(cp != std::string::npos) current.erase(cp);

	bool open_hash_only = false;
	bool reload = false;

	if(!m_history.current().empty())
	{
		if(m_history.current() != url)
		{
			if(current == s_url) open_hash_only = true;
		}
		else reload = true;
	}

	if(!open_hash_only)
	{
		open_page(url, fragment);
	}
	else
	{
		auto page = current_page();
		if(page) page->show_fragment(fragment);
	}

	if(!reload) m_history.url_opened(url);
}

void sdl_html_widget::update_cursor()
{
	auto page = current_page();
	std::string name = page ? page->get_cursor() : std::string("auto");

	SDL_Cursor* cur = m_cursor_arrow;
	if(name == "pointer" || name == "hand")      cur = m_cursor_hand;
	else if(name == "text" || name == "vertical-text") cur = m_cursor_ibeam;
	else if(name == "default" || name == "auto") cur = m_cursor_arrow;

	SDL_SetCursor(cur);
}

void sdl_html_widget::redraw_boxes(const litehtml::position::vector& boxes)
{
	if(boxes.empty()) return;

	litehtml::pixel_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	bool first = true;
	for(const auto& pos : boxes)
	{
		if(first) { x0 = pos.x; y0 = pos.y; x1 = pos.x + pos.width; y1 = pos.y + pos.height; first = false; }
		else
		{
			x0 = std::min(x0, pos.x);      y0 = std::min(y0, pos.y);
			x1 = std::max(x1, pos.x + pos.width); y1 = std::max(y1, pos.y + pos.height);
		}
	}
	if(x1 > x0 && y1 > y0)
	{
		m_draw_buffer.redraw_area(draw_fn(current_page()), x0, y0, x1 - x0, y1 - y0);
		present();
	}
}

// ---------------------------------------------------------------------------
// notifier dispatch
// ---------------------------------------------------------------------------

void sdl_html_widget::on_redraw()
{
	m_draw_buffer.redraw(draw_fn(current_page()));
	present();
}

void sdl_html_widget::on_render()
{
	auto page = current_page();
	if(page)
	{
		page->render(m_draw_buffer.get_width());
		update_view_port(page);
		m_draw_buffer.redraw(draw_fn(page));
		present();
	}
}

void sdl_html_widget::on_page_loaded(uint64_t web_page_id)
{
	std::string url;
	{
		std::lock_guard<std::mutex> lock(m_page_mutex);
		if(!m_next || m_next->id() != web_page_id) return;
		m_current = m_next;
		m_next.reset();
		url = m_current->url();
		update_view_port(m_current);   // m_page_mutex already held; do not re-lock
	}
	scroll_to(0, 0);
	on_redraw();
	SDL_SetWindowTitle(m_window, url.c_str());
}

void sdl_html_widget::on_caption(const std::string& caption)
{
	if(!caption.empty()) SDL_SetWindowTitle(m_window, caption.c_str());
}

// ---------------------------------------------------------------------------
// internal helpers
// ---------------------------------------------------------------------------

std::shared_ptr<litebrowser::web_page> sdl_html_widget::current_page()
{
	std::lock_guard<std::mutex> lock(m_page_mutex);
	return m_current;
}

void sdl_html_widget::open_page(const std::string& url, const std::string& fragment)
{
	{
		std::lock_guard<std::mutex> lock(m_page_mutex);
		if(m_current) m_current->stop_loading();
		m_next = std::make_shared<litebrowser::web_page>(this, m_notifier, 10);
		m_next->open(url, fragment);
	}
}

void sdl_html_widget::update_view_port(std::shared_ptr<litebrowser::web_page> page)
{
	if(page)
	{
		double max_x = (double) std::max(0, page->width()  - m_client_w);
		double max_y = (double) std::max(0, page->height() - m_client_h);
		m_scroll_x = clampd(m_scroll_x, 0, max_x);
		m_scroll_y = clampd(m_scroll_y, 0, max_y);
	}
	else
	{
		m_scroll_x = 0;
		m_scroll_y = 0;
	}
}

void sdl_html_widget::recreate_texture(int w, int h)
{
	if(w <= 0 || h <= 0) return;
	if(m_texture && m_tex_w == w && m_tex_h == h) return;

	if(m_texture) { SDL_DestroyTexture(m_texture); m_texture = nullptr; }

	m_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_ARGB8888,
								  SDL_TEXTUREACCESS_STREAMING, w, h);
	if(m_texture)
	{
		// cairo RGB24 stores opaque pixels; ignore the (zero) alpha channel.
		SDL_SetTextureBlendMode(m_texture, SDL_BLENDMODE_NONE);
		m_tex_w = w;
		m_tex_h = h;
	}
	else
	{
		std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << std::endl;
	}
}

void sdl_html_widget::present()
{
	cairo_surface_t* surf = m_draw_buffer.get_cairo_surface();
	if(!surf || !m_texture) return;
	if(cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) return;

	int sw = cairo_image_surface_get_width(surf);
	int sh = cairo_image_surface_get_height(surf);
	if(sw <= 0 || sh <= 0) return;
	if(sw != m_tex_w || sh != m_tex_h) recreate_texture(sw, sh);

	void* data  = cairo_image_surface_get_data(surf);
	int   stride = cairo_image_surface_get_stride(surf);
	if(data && stride > 0)
	{
		SDL_UpdateTexture(m_texture, nullptr, data, stride);
	}

	SDL_RenderClear(m_renderer);
	SDL_RenderCopy(m_renderer, m_texture, nullptr, nullptr);
	SDL_RenderPresent(m_renderer);
}

void sdl_html_widget::apply_scroll()
{
	auto page = current_page();
	m_draw_buffer.on_scroll(draw_fn(page),
							(int) m_scroll_x, (int) m_scroll_y,
							page ? page->get_fixed_boxes() : litehtml::position::vector{});
	present();
}

void sdl_html_widget::resize(int w, int h)
{
	if(w <= 0 || h <= 0) return;
	m_client_w = w;
	m_client_h = h;

	auto page = current_page();
	m_draw_buffer.on_size_allocate(draw_fn(page), w, h);
	if(page)
	{
		page->media_changed();
		page->render(w);
		update_view_port(page);
		m_draw_buffer.redraw(draw_fn(page));
	}
	recreate_texture(w, h);
	present();
}

// ---------------------------------------------------------------------------
// event handling
// ---------------------------------------------------------------------------

void sdl_html_widget::handle_event(const SDL_Event& ev)
{
	switch(ev.type)
	{
	case SDL_WINDOWEVENT:
		if(ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_RESIZED)
		{
			resize(ev.window.data1, ev.window.data2);
		}
		break;

	case SDL_MOUSEMOTION:
		on_mouse_move(ev.motion.x, ev.motion.y);
		break;

	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		on_button(ev.button.button, ev.button.state == SDL_PRESSED, ev.button.x, ev.button.y);
		break;

	case SDL_MOUSEWHEEL:
	{
		int mx = 0, my = 0;
		SDL_GetMouseState(&mx, &my);
		auto page = current_page();
		if(!page) break;

		int docx = mx + m_draw_buffer.get_left();
		int docy = my + m_draw_buffer.get_top();
		int dxpx = ev.wheel.x * 60;
		int dypx = ev.wheel.y * 60;
		if(ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) { dxpx = -dxpx; dypx = -dypx; }

		auto vals = page->on_scroll((litehtml::pixel_t) dxpx, (litehtml::pixel_t) dypx, docx, docy, mx, my);
		bool dx_used = false, dy_used = false;
		for(const auto& v : vals)
		{
			if(v.dx != 0) dx_used = true;
			if(v.dy != 0) dy_used = true;
			m_draw_buffer.redraw_area(draw_fn(current_page()),
									  v.scroll_box.left(), v.scroll_box.top(),
									  v.scroll_box.width, v.scroll_box.height);
		}
		if(!dx_used) m_scroll_x += dxpx;
		if(!dy_used) m_scroll_y += dypx;
		update_view_port(current_page());
		apply_scroll();
		break;
	}

	case SDL_KEYDOWN:
		on_key(ev.key.keysym.sym);
		break;

	default:
		break;
	}
}

void sdl_html_widget::on_mouse_move(int x, int y)
{
	auto page = current_page();
	if(page)
	{
		page->on_mouse_over(x + m_draw_buffer.get_left(),
							y + m_draw_buffer.get_top(),
							x, y);
	}
}

void sdl_html_widget::on_button(Uint8 button, bool down, int x, int y)
{
	if(button != SDL_BUTTON_LEFT) return;
	auto page = current_page();
	if(!page) return;

	int docx = x + m_draw_buffer.get_left();
	int docy = y + m_draw_buffer.get_top();
	if(down)
		page->on_lbutton_down(docx, docy, x, y);
	else
		page->on_lbutton_up(docx, docy, x, y);
}

void sdl_html_widget::on_key(SDL_Keycode key)
{
	auto page = current_page();
	int step = 40;
	int page_step = m_client_h > 0 ? m_client_h - 20 : 600;

	switch(key)
	{
	case SDLK_PAGEDOWN: m_scroll_y += page_step; break;
	case SDLK_PAGEUP:   m_scroll_y -= page_step; break;
	case SDLK_DOWN:     m_scroll_y += step; break;
	case SDLK_UP:       m_scroll_y -= step; break;
	case SDLK_HOME:     m_scroll_y = 0; break;
	case SDLK_END:      if(page) m_scroll_y = (double) std::max(0, page->height() - m_client_h); break;
	case SDLK_LEFT:     m_scroll_x -= step; break;
	case SDLK_RIGHT:    m_scroll_x += step; break;
	default: return;
	}
	update_view_port(current_page());
	apply_scroll();
}
