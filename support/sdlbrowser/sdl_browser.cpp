#include "sdl_browser.h"

#include <SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace
{
	constexpr int button_width = 42;
	constexpr int toolbar_padding = 8;
	constexpr int address_left = 138;
	constexpr int scrollbar_size = 12;

	void set_draw_color(SDL_Renderer* renderer, Uint8 red, Uint8 green, Uint8 blue, Uint8 alpha = 255)
	{
		SDL_SetRenderDrawColor(renderer, red, green, blue, alpha);
	}
}

void sdl_browser::notifier::enqueue(notification item)
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		if(m_disconnected)
		{
			return;
		}
		m_queue.push(std::move(item));
	}

	SDL_Event event{};
	event.type = m_browser.m_user_event;
	if(SDL_PushEvent(&event) < 0)
	{
		std::cerr << "SDL_PushEvent failed: " << SDL_GetError() << '\n';
	}
}

void sdl_browser::notifier::disconnect()
{
	std::lock_guard<std::mutex> lock(m_mutex);
	m_disconnected = true;
	while(!m_queue.empty())
	{
		m_queue.pop();
	}
}

void sdl_browser::notifier::dispatch()
{
	std::queue<notification> pending;
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		pending.swap(m_queue);
	}

	while(!pending.empty())
	{
		const auto item = std::move(pending.front());
		pending.pop();
		switch(item.type)
		{
			case notification_type::redraw:
				m_browser.draw_frame();
				break;
			case notification_type::render:
				m_browser.render_page();
				break;
			case notification_type::update_state:
				m_browser.notify_state_changed();
				break;
			case notification_type::page_loaded:
				m_browser.on_page_loaded(item.page_id);
				break;
			case notification_type::set_caption:
				m_browser.m_caption = item.text;
				m_browser.update_window_title();
				break;
		}
	}
}

void sdl_browser::notifier::redraw()
{
	enqueue({notification_type::redraw, 0, {}});
}

void sdl_browser::notifier::render()
{
	enqueue({notification_type::render, 0, {}});
}

void sdl_browser::notifier::update_state()
{
	enqueue({notification_type::update_state, 0, {}});
}

void sdl_browser::notifier::on_set_caption(const std::string& caption)
{
	enqueue({notification_type::set_caption, 0, caption});
}

void sdl_browser::notifier::on_page_loaded(uint64_t page_id)
{
	enqueue({notification_type::page_loaded, page_id, {}});
}

sdl_browser::sdl_browser()
{
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0)
	{
		throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
	}

	m_user_event = SDL_RegisterEvents(1);
	if(m_user_event == static_cast<Uint32>(-1))
	{
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_RegisterEvents failed: ") + SDL_GetError());
	}

	m_window = SDL_CreateWindow(m_caption.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
								m_width, m_height, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if(!m_window)
	{
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
	}

	m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(!m_renderer)
	{
		m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_SOFTWARE);
	}
	if(!m_renderer)
	{
		SDL_DestroyWindow(m_window);
		m_window = nullptr;
		SDL_Quit();
		throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
	}

	if((IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG) & (IMG_INIT_PNG | IMG_INIT_JPG)) != (IMG_INIT_PNG | IMG_INIT_JPG))
	{
		std::cerr << "SDL_image initialization warning: " << IMG_GetError() << '\n';
	}

	m_notifier = std::make_shared<notifier>(*this);
	SDL_StartTextInput();
	resize(m_width, m_height);
}

sdl_browser::~sdl_browser()
{
	if(m_notifier)
	{
		m_notifier->disconnect();
	}
	{
		std::lock_guard<std::mutex> lock(m_page_mutex);
		if(m_current_page)
		{
			m_current_page->stop_loading();
		}
		if(m_next_page)
		{
			m_next_page->stop_loading();
		}
		m_current_page.reset();
		m_next_page.reset();
	}
	SDL_StopTextInput();
	if(m_cursor)
	{
		SDL_FreeCursor(m_cursor);
	}
	if(m_page_texture)
	{
		SDL_DestroyTexture(m_page_texture);
	}
	if(m_renderer)
	{
		SDL_DestroyRenderer(m_renderer);
	}
	if(m_window)
	{
		SDL_DestroyWindow(m_window);
	}
	IMG_Quit();
	SDL_Quit();
}

int sdl_browser::run(const std::string& initial_url)
{
	open_url(initial_url);
	while(m_running)
	{
		SDL_Event event{};
		if(SDL_WaitEventTimeout(&event, 16))
		{
			dispatch_event(event);
			while(SDL_PollEvent(&event))
			{
				dispatch_event(event);
			}
		}
	}
	return 0;
}

void sdl_browser::dispatch_event(const SDL_Event& event)
{
	if(event.type == m_user_event)
	{
		m_notifier->dispatch();
		return;
	}

	switch(event.type)
	{
		case SDL_QUIT:
			m_running = false;
			break;
		case SDL_WINDOWEVENT:
			if(event.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
			{
				resize(event.window.data1, event.window.data2);
			}
			break;
		case SDL_MOUSEBUTTONDOWN:
			if(event.button.button == SDL_BUTTON_LEFT)
			{
				const int x = event.button.x;
				const int y = event.button.y;
				if(y < toolbar_height)
				{
					if(x < address_left)
					{
						m_address_focused = false;
						if(x < toolbar_padding + button_width)
						{
							go_back();
						}
						else if(x < toolbar_padding + 2 * button_width)
						{
							go_forward();
						}
						else
						{
							reload_or_stop();
						}
					}
					else
					{
						m_address_focused = true;
						m_address_select_all = false;
						const SDL_Rect input_rect{address_left, 8, m_width - address_left - 10, 32};
						SDL_SetTextInputRect(&input_rect);
					}
				}
				else
				{
					m_address_focused = false;
					if(x >= m_view_width - scrollbar_size && m_view_height > 0)
					{
						m_dragging_vscroll = true;
					}
					else if(y >= m_height - scrollbar_size && m_view_width > 0)
					{
						m_dragging_hscroll = true;
					}
					else if(auto page = current_page())
					{
						const int page_x = x + m_draw_buffer.get_left();
						const int page_y = y - toolbar_height + m_draw_buffer.get_top();
						page->on_lbutton_down(page_x, page_y, x, y - toolbar_height);
						m_page_pressed = true;
					}
				}
				draw_frame();
			}
			break;
		case SDL_MOUSEBUTTONUP:
			if(event.button.button == SDL_BUTTON_LEFT && m_page_pressed)
			{
				m_page_pressed = false;
				if(auto page = current_page())
				{
					const int x = event.button.x;
					const int y = event.button.y;
					page->on_lbutton_up(x + m_draw_buffer.get_left(),
										y - toolbar_height + m_draw_buffer.get_top(),
										x, y - toolbar_height);
				}
			}
			m_dragging_vscroll = false;
			m_dragging_hscroll = false;
			break;
		case SDL_MOUSEMOTION:
			m_mouse_x = event.motion.x;
			m_mouse_y = event.motion.y;
			if(m_dragging_vscroll && m_height > toolbar_height)
			{
				auto page = current_page();
				const int max_scroll = page ? std::max(0, page->height() - m_view_height) : 0;
				const int track = std::max(1, m_view_height - scrollbar_size);
				set_scroll_position(m_scroll_x, max_scroll * (m_mouse_y - toolbar_height) / track);
			}
			else if(m_dragging_hscroll && m_view_width > 0)
			{
				auto page = current_page();
				const int max_scroll = page ? std::max(0, page->width() - m_view_width) : 0;
				const int track = std::max(1, m_view_width - scrollbar_size);
				set_scroll_position(max_scroll * m_mouse_x / track, m_scroll_y);
			}
			else if(auto page = current_page())
			{
				page->on_mouse_over(m_mouse_x + m_draw_buffer.get_left(),
									m_mouse_y - toolbar_height + m_draw_buffer.get_top(),
									m_mouse_x, m_mouse_y - toolbar_height);
			}
			update_cursor_icon();
			break;
		case SDL_MOUSEWHEEL:
			scroll_by(-event.wheel.x * 60, -event.wheel.y * 60);
			break;
		case SDL_KEYDOWN:
			handle_key(event.key);
			break;
		case SDL_TEXTINPUT:
			handle_text(event.text);
			break;
		default:
			break;
	}
}

void sdl_browser::handle_key(const SDL_KeyboardEvent& event)
{
	if(m_address_focused)
	{
		if(event.keysym.sym == SDLK_RETURN || event.keysym.sym == SDLK_KP_ENTER)
		{
			submit_address();
		}
		else if(event.keysym.sym == SDLK_ESCAPE)
		{
			m_address_focused = false;
			m_address_select_all = false;
		}
		else if(event.keysym.sym == SDLK_BACKSPACE && !m_address.empty())
		{
			if(m_address_select_all)
			{
				m_address.clear();
				m_address_select_all = false;
			}
			else
			{
				const auto codepoint_start = m_address.find_last_not_of(
					"\x80\x81\x82\x83\x84\x85\x86\x87\x88\x89\x8a\x8b\x8c\x8d\x8e\x8f"
					"\x90\x91\x92\x93\x94\x95\x96\x97\x98\x99\x9a\x9b\x9c\x9d\x9e\x9f");
				if(codepoint_start == std::string::npos)
				{
					m_address.clear();
				}
				else
				{
					m_address.erase(codepoint_start);
				}
			}
		}
		else if((event.keysym.mod & KMOD_CTRL) && event.keysym.sym == SDLK_a)
		{
			m_address_select_all = true;
		}
		draw_frame();
		return;
	}

	switch(event.keysym.sym)
	{
		case SDLK_PAGEUP:
			scroll_by(0, -m_view_height);
			break;
		case SDLK_PAGEDOWN:
			scroll_by(0, m_view_height);
			break;
		case SDLK_HOME:
			if(event.keysym.mod & KMOD_CTRL) set_scroll_position(0, 0);
			else scroll_by(0, -60);
			break;
		case SDLK_END:
			if(event.keysym.mod & KMOD_CTRL)
			{
				if(auto page = current_page()) set_scroll_position(m_scroll_x, page->height());
			}
			else scroll_by(0, 60);
			break;
		case SDLK_LEFT:
			if(event.keysym.mod & KMOD_ALT) go_back();
			else scroll_by(-60, 0);
			break;
		case SDLK_RIGHT:
			if(event.keysym.mod & KMOD_ALT) go_forward();
			else scroll_by(60, 0);
			break;
		case SDLK_UP:
			scroll_by(0, -60);
			break;
		case SDLK_DOWN:
			scroll_by(0, 60);
			break;
		case SDLK_F5:
			reload_or_stop();
			break;
		default:
			break;
	}
}

void sdl_browser::handle_text(const SDL_TextInputEvent& event)
{
	if(m_address_focused)
	{
		if(m_address_select_all)
		{
			m_address.clear();
			m_address_select_all = false;
		}
		m_address.append(event.text);
		draw_frame();
	}
}

void sdl_browser::resize(int width, int height)
{
	m_width = std::max(1, width);
	m_height = std::max(toolbar_height + 1, height);
	m_view_width = m_width;
	m_view_height = m_height - toolbar_height;
	m_render_width.store(m_view_width);
	m_render_height.store(m_view_height);
	if(auto page = current_page())
	{
		page->media_changed();
		page->render(m_view_width);
	}
	m_draw_buffer.on_size_allocate(get_draw_function(current_page()), m_view_width, m_view_height);
	if(auto page = current_page())
	{
		set_scroll_position(m_scroll_x, m_scroll_y);
		m_draw_buffer.redraw(get_draw_function(page));
	}
	draw_frame();
}

void sdl_browser::scroll_by(int dx, int dy)
{
	auto page = current_page();
	if(!page)
	{
		return;
	}
	auto values = page->on_scroll(dx, dy, m_mouse_x + m_draw_buffer.get_left(),
								  m_mouse_y - toolbar_height + m_draw_buffer.get_top(),
								  m_mouse_x, m_mouse_y - toolbar_height);
	bool used_x = false;
	bool used_y = false;
	for(const auto& value : values)
	{
		used_x = used_x || value.dx != 0;
		used_y = used_y || value.dy != 0;
		m_draw_buffer.redraw_area(get_draw_function(page), value.scroll_box.left(),
								  value.scroll_box.top(), value.scroll_box.width, value.scroll_box.height);
	}
	set_scroll_position(m_scroll_x + (used_x ? 0 : dx), m_scroll_y + (used_y ? 0 : dy));
}

void sdl_browser::set_scroll_position(int x, int y)
{
	auto page = current_page();
	if(!page)
	{
		m_scroll_x = 0;
		m_scroll_y = 0;
		return;
	}
	m_scroll_x = std::clamp(x, 0, std::max(0, page->width() - m_view_width));
	m_scroll_y = std::clamp(y, 0, std::max(0, page->height() - m_view_height));
	m_draw_buffer.on_scroll(get_draw_function(page), m_scroll_x, m_scroll_y, page->get_fixed_boxes());
	draw_frame();
}

void sdl_browser::draw_frame()
{
	if(!m_renderer)
	{
		return;
	}
	set_draw_color(m_renderer, 244, 246, 248);
	SDL_RenderClear(m_renderer);
	draw_toolbar();
	update_page_texture();

	if(m_page_texture)
	{
		SDL_Rect destination{0, toolbar_height, m_view_width, m_view_height};
		SDL_RenderCopy(m_renderer, m_page_texture, nullptr, &destination);
	}

	if(auto page = current_page())
	{
		if(page->height() > m_view_height)
		{
			const int track_height = std::max(1, m_view_height - 4);
			const int thumb_height = std::max(28, track_height * m_view_height / std::max(page->height(), m_view_height));
			const int thumb_y = toolbar_height + 2 +
				(m_view_height - thumb_height - 4) * m_scroll_y / std::max(1, page->height() - m_view_height);
			set_draw_color(m_renderer, 215, 220, 226, 190);
			SDL_Rect track{m_width - scrollbar_size, toolbar_height, scrollbar_size, m_view_height};
			SDL_RenderFillRect(m_renderer, &track);
			set_draw_color(m_renderer, 120, 130, 142, 230);
			SDL_Rect thumb{m_width - scrollbar_size + 2, thumb_y, scrollbar_size - 4, thumb_height};
			SDL_RenderFillRect(m_renderer, &thumb);
		}
	}
	SDL_RenderPresent(m_renderer);
}

void sdl_browser::draw_toolbar()
{
	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, m_width, toolbar_height);
	cairo_t* context = cairo_create(surface);
	cairo_set_source_rgb(context, 0.96, 0.97, 0.98);
	cairo_paint(context);
	cairo_set_source_rgb(context, 0.83, 0.85, 0.88);
	cairo_rectangle(context, 0, toolbar_height - 1, m_width, 1);
	cairo_fill(context);

	const char* labels[] = {"<", ">", m_loading ? "Stop" : "Reload"};
	for(int i = 0; i < 3; ++i)
	{
		const int x = toolbar_padding + i * button_width;
		cairo_set_source_rgb(context, 0.91, 0.93, 0.95);
		cairo_rectangle(context, x, 7, button_width - 4, 34);
		cairo_fill(context);
		cairo_set_source_rgb(context, 0.16, 0.19, 0.23);
		draw_label(context, labels[i], x + (i == 2 ? 1 : 11), 29);
	}

	cairo_set_source_rgb(context, 1, 1, 1);
	cairo_rectangle(context, address_left, 7, std::max(0, m_width - address_left - 10), 34);
	cairo_fill(context);
	cairo_set_source_rgb(context, m_address_focused ? 0.20 : 0.78,
						 m_address_focused ? 0.48 : 0.81,
						 m_address_focused ? 0.88 : 0.84);
	cairo_set_line_width(context, m_address_focused ? 2 : 1);
	cairo_rectangle(context, address_left, 7, std::max(0, m_width - address_left - 10), 34);
	cairo_stroke(context);
	cairo_save(context);
	cairo_rectangle(context, address_left + 6, 8, std::max(0, m_width - address_left - 22), 32);
	cairo_clip(context);
	cairo_set_source_rgb(context, 0.12, 0.15, 0.19);
	draw_label(context, m_address.c_str(), address_left + 10, 29);
	cairo_restore(context);

	cairo_surface_flush(surface);
	auto* data = cairo_image_surface_get_data(surface);
	const int stride = cairo_image_surface_get_stride(surface);
	SDL_Surface* image = SDL_CreateRGBSurfaceWithFormatFrom(data, m_width, toolbar_height,
														   32, stride, SDL_PIXELFORMAT_ARGB8888);
	if(image)
	{
		SDL_Texture* texture = SDL_CreateTextureFromSurface(m_renderer, image);
		if(texture)
		{
			SDL_Rect destination{0, 0, m_width, toolbar_height};
			SDL_RenderCopy(m_renderer, texture, nullptr, &destination);
			SDL_DestroyTexture(texture);
		}
		else
		{
			std::cerr << "SDL_CreateTextureFromSurface failed: " << SDL_GetError() << '\n';
		}
		SDL_FreeSurface(image);
	}
	else
	{
		std::cerr << "SDL_CreateRGBSurfaceWithFormatFrom failed: " << SDL_GetError() << '\n';
	}
	cairo_destroy(context);
	cairo_surface_destroy(surface);
}

void sdl_browser::draw_label(cairo_t* context, const char* text, double x, double y)
{
	cairo_select_font_face(context, "Sans", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_NORMAL);
	cairo_set_font_size(context, 14);
	cairo_move_to(context, x, y);
	cairo_show_text(context, text);
}

void sdl_browser::update_page_texture()
{
	cairo_surface_t* surface = m_draw_buffer.get_cairo_surface();
	if(!surface)
	{
		return;
	}
	cairo_surface_flush(surface);
	const int width = m_draw_buffer.get_width();
	const int height = m_draw_buffer.get_height();
	if(width <= 0 || height <= 0)
	{
		return;
	}
	if(m_page_texture)
	{
		int texture_width = 0;
		int texture_height = 0;
		SDL_QueryTexture(m_page_texture, nullptr, nullptr, &texture_width, &texture_height);
		if(texture_width != width || texture_height != height)
		{
			SDL_DestroyTexture(m_page_texture);
			m_page_texture = nullptr;
		}
	}
	if(!m_page_texture)
	{
		m_page_texture = SDL_CreateTexture(m_renderer, SDL_PIXELFORMAT_RGB888,
										   SDL_TEXTUREACCESS_STREAMING, width, height);
		if(!m_page_texture)
		{
			std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
			return;
		}
	}
	if(SDL_UpdateTexture(m_page_texture, nullptr, cairo_image_surface_get_data(surface),
						 cairo_image_surface_get_stride(surface)) != 0)
	{
		std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
	}
}

std::shared_ptr<litebrowser::web_page> sdl_browser::current_page()
{
	std::lock_guard<std::mutex> lock(m_page_mutex);
	return m_current_page;
}

void sdl_browser::open_url(const std::string& url)
{
	if(url.empty())
	{
		return;
	}
	std::string fragment;
	std::string page_url = url;
	const auto hash = page_url.find('#');
	if(hash != std::string::npos)
	{
		fragment = page_url.substr(hash + 1);
		page_url.erase(hash);
	}

	std::string current = m_history.current();
	const auto current_hash = current.find('#');
	if(current_hash != std::string::npos)
	{
		current.erase(current_hash);
	}
	const bool hash_only = !current.empty() && current == page_url && m_history.current() != url;
	const bool reload = m_history.current() == url;

	if(hash_only)
	{
		if(auto page = current_page())
		{
			page->show_fragment(fragment);
		}
		m_address = url;
		draw_frame();
	}
	else
	{
		open_page(url, fragment);
	}
	if(!reload)
	{
		m_history.url_opened(url);
	}
	notify_state_changed();
}

void sdl_browser::open_page(const std::string& url, const std::string& fragment)
{
	{
		std::lock_guard<std::mutex> lock(m_page_mutex);
		if(m_current_page)
		{
			m_current_page->stop_loading();
		}
		m_next_page = std::make_shared<litebrowser::web_page>(this, m_notifier, 10);
		m_next_page->open(url, fragment);
	}
	m_address = url;
	draw_frame();
	notify_state_changed();
}

void sdl_browser::on_page_loaded(uint64_t page_id)
{
	std::shared_ptr<litebrowser::web_page> page;
	{
		std::lock_guard<std::mutex> lock(m_page_mutex);
		if(!m_next_page || m_next_page->id() != page_id)
		{
			return;
		}
		m_current_page = std::move(m_next_page);
		page = m_current_page;
		m_address = page->url();
	}
	m_scroll_x = 0;
	m_scroll_y = 0;
	page->render(m_view_width);
	m_draw_buffer.on_size_allocate(get_draw_function(page), m_view_width, m_view_height);
	m_draw_buffer.on_scroll(get_draw_function(page), 0, 0, page->get_fixed_boxes());
	m_draw_buffer.redraw(get_draw_function(page));
	update_window_title();
	draw_frame();
	notify_state_changed();
}

void sdl_browser::render_page()
{
	auto page = current_page();
	if(!page)
	{
		return;
	}
	page->render(m_view_width);
	set_scroll_position(m_scroll_x, m_scroll_y);
	m_draw_buffer.redraw(get_draw_function(page));
	draw_frame();
}

void sdl_browser::go_back()
{
	std::string url;
	if(m_history.back(url))
	{
		open_url(url);
	}
}

void sdl_browser::go_forward()
{
	std::string url;
	if(m_history.forward(url))
	{
		open_url(url);
	}
}

void sdl_browser::reload_or_stop()
{
	if(m_next_page || (m_current_page && m_current_page->is_downloading()))
	{
		stop_download();
	}
	else if(m_current_page)
	{
		open_url(m_current_page->url());
	}
}

void sdl_browser::submit_address()
{
	m_address_focused = false;
	std::string url = m_address;
	if(url.find("://") == std::string::npos && url.rfind("file:", 0) != 0)
	{
		url.insert(0, "http://");
	}
	open_url(url);
}

void sdl_browser::stop_download()
{
	{
		std::lock_guard<std::mutex> lock(m_page_mutex);
		if(m_next_page)
		{
			m_next_page->stop_loading();
		}
		else if(m_current_page)
		{
			m_current_page->stop_loading();
		}
	}
	notify_state_changed();
}

void sdl_browser::notify_state_changed()
{
	{
		std::lock_guard<std::mutex> lock(m_page_mutex);
		m_loading = m_next_page != nullptr || (m_current_page && m_current_page->is_downloading());
	}
	if(m_renderer)
	{
		draw_frame();
	}
}

void sdl_browser::update_window_title()
{
	if(m_window)
	{
		SDL_SetWindowTitle(m_window, m_caption.c_str());
	}
}

void sdl_browser::update_cursor_icon()
{
	auto page = current_page();
	const std::string cursor_name = page ? page->get_cursor() : "auto";
	SDL_SystemCursor cursor = SDL_SYSTEM_CURSOR_ARROW;
	if(cursor_name == "pointer") cursor = SDL_SYSTEM_CURSOR_HAND;
	else if(cursor_name == "text") cursor = SDL_SYSTEM_CURSOR_IBEAM;
	else if(cursor_name == "wait" || cursor_name == "progress") cursor = SDL_SYSTEM_CURSOR_WAIT;
	else if(cursor_name == "crosshair") cursor = SDL_SYSTEM_CURSOR_CROSSHAIR;
	if(cursor != m_cursor_type || !m_cursor)
	{
		SDL_Cursor* handle = SDL_CreateSystemCursor(cursor);
		if(handle)
		{
			if(m_cursor)
			{
				SDL_FreeCursor(m_cursor);
			}
			m_cursor = handle;
			m_cursor_type = cursor;
			SDL_SetCursor(m_cursor);
		}
	}
}

void sdl_browser::scroll_to(int x, int y)
{
	set_scroll_position(x, y);
}

void sdl_browser::get_viewport(litehtml::position& viewport) const
{
	viewport.x = m_draw_buffer.get_left();
	viewport.y = m_draw_buffer.get_top();
	viewport.width = m_draw_buffer.get_width();
	viewport.height = m_draw_buffer.get_height();
}

void sdl_browser::redraw_boxes(const litehtml::position::vector& boxes)
{
	if(boxes.empty())
	{
		return;
	}
	auto page = current_page();
	for(const auto& box : boxes)
	{
		m_draw_buffer.redraw_area(get_draw_function(page), box.x, box.y, box.width, box.height);
	}
	draw_frame();
}

int sdl_browser::get_render_width()
{
	return m_render_width.load();
}

double sdl_browser::get_dpi()
{
	return 96.0;
}

int sdl_browser::get_screen_width()
{
	SDL_DisplayMode mode{};
	return SDL_GetCurrentDisplayMode(0, &mode) == 0 ? mode.w : m_width;
}

int sdl_browser::get_screen_height()
{
	SDL_DisplayMode mode{};
	return SDL_GetCurrentDisplayMode(0, &mode) == 0 ? mode.h : m_height;
}

cairo_surface_t* sdl_browser::load_image(const std::string& path)
{
	SDL_Surface* decoded = IMG_Load(path.c_str());
	if(!decoded)
	{
		std::cerr << "Unable to load image " << path << ": " << IMG_GetError() << '\n';
		return nullptr;
	}
	SDL_Surface* converted = SDL_ConvertSurfaceFormat(decoded, SDL_PIXELFORMAT_ARGB8888, 0);
	SDL_FreeSurface(decoded);
	if(!converted)
	{
		std::cerr << "Unable to convert image " << path << ": " << SDL_GetError() << '\n';
		return nullptr;
	}

	cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, converted->w, converted->h);
	if(cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS)
	{
		SDL_FreeSurface(converted);
		cairo_surface_destroy(surface);
		return nullptr;
	}
	cairo_surface_flush(surface);
	unsigned char* destination = cairo_image_surface_get_data(surface);
	const int destination_stride = cairo_image_surface_get_stride(surface);
	for(int row = 0; row < converted->h; ++row)
	{
		const auto* source_row = static_cast<const unsigned char*>(converted->pixels) + row * converted->pitch;
		auto* destination_row = destination + row * destination_stride;
		for(int column = 0; column < converted->w; ++column)
		{
			std::uint32_t pixel = 0;
			std::memcpy(&pixel, source_row + column * 4, sizeof(pixel));
			const std::uint32_t alpha = pixel >> 24;
			const std::uint32_t red = ((pixel >> 16) & 0xff) * alpha / 255;
			const std::uint32_t green = ((pixel >> 8) & 0xff) * alpha / 255;
			const std::uint32_t blue = (pixel & 0xff) * alpha / 255;
			const std::uint32_t premultiplied = (alpha << 24) | (red << 16) | (green << 8) | blue;
			std::memcpy(destination_row + column * 4, &premultiplied, sizeof(premultiplied));
		}
	}
	cairo_surface_mark_dirty(surface);
	SDL_FreeSurface(converted);
	return surface;
}

litebrowser::draw_buffer::draw_page_function_t sdl_browser::get_draw_function(
	const std::shared_ptr<litebrowser::web_page>& page)
{
	return [page](cairo_t* context, int x, int y, const litehtml::position* clip)
	{
		if(page)
		{
			page->draw(reinterpret_cast<litehtml::uint_ptr>(context), x, y, clip);
		}
	};
}

void sdl_browser::update_cursor()
{
	update_cursor_icon();
}
