// sdl_html_widget.h
//
// SDL2 replacement for support/gtkmm4/html_widget. Owns the SDL window /
// renderer / texture, runs the event loop, and implements
// litebrowser::html_host_interface so it can drive litebrowser::web_page.
// The page is rendered off-screen into a cairo image surface (draw_buffer) and
// then uploaded to an SDL_Texture for presentation. No GTK/Pango involved.
#pragma once

#include "html_host.h"
#include "web_page.h"
#include "web_history.h"
#include "../draw_buffer/draw_buffer.h"
#include "sdl_notifier.h"

#include <SDL.h>

#include <memory>
#include <mutex>
#include <string>

class sdl_html_widget : public litebrowser::html_host_interface
{
public:
	sdl_html_widget();
	~sdl_html_widget() override;

	bool init(const char* title, int width, int height);
	void run(const std::string& url);   // blocks until the window is closed

	// ---- litebrowser::html_host_interface ----
	double get_dpi() override;
	int get_screen_width() override;
	int get_screen_height() override;
	void open_url(const std::string& url) override;
	void update_cursor() override;
	void scroll_to(int x, int y) override;
	void get_viewport(litehtml::position& viewport) const override;
	void redraw_boxes(const litehtml::position::vector& boxes) override;
	int get_render_width() override;
	cairo_surface_t* load_image(const std::string& path) override;

private:
	// ---- notifier dispatch (GUI thread) ----
	void on_redraw();
	void on_render();
	void on_page_loaded(uint64_t web_page_id);
	void on_caption(const std::string& caption);

	// ---- helpers ----
	std::shared_ptr<litebrowser::web_page> current_page();
	void open_page(const std::string& url, const std::string& fragment);
	void resize(int w, int h);
	void recreate_texture(int w, int h);
	void present();
	void draw_scrollbars();
	void apply_scroll();
	void update_view_port(std::shared_ptr<litebrowser::web_page> page);
	void handle_event(const SDL_Event& ev);
	void on_mouse_move(int x, int y);
	void on_button(Uint8 button, bool down, int x, int y);
	void on_wheel(int mouse_x, int mouse_y);
	void on_key(SDL_Keycode key);

	litebrowser::draw_buffer::draw_page_function_t draw_fn(const std::shared_ptr<litebrowser::web_page>& page)
	{
		return [page](cairo_t* cr, int x, int y, const litehtml::position* clip)
		{
			if(page) page->draw((litehtml::uint_ptr) cr, x, y, clip);
		};
	}

private:
	SDL_Window*   m_window   = nullptr;
	SDL_Renderer* m_renderer = nullptr;
	SDL_Texture*  m_texture  = nullptr;
	int           m_tex_w = 0;
	int           m_tex_h = 0;

	std::shared_ptr<sdl_notifier> m_notifier;
	std::mutex                    m_page_mutex;
	std::shared_ptr<litebrowser::web_page> m_current;
	std::shared_ptr<litebrowser::web_page> m_next;

	litebrowser::draw_buffer m_draw_buffer;
	web_history              m_history;

	int    m_client_w = 0;
	int    m_client_h = 0;
	double m_scroll_x = 0;
	double m_scroll_y = 0;

	bool   m_running = false;

	SDL_Cursor* m_cursor_arrow  = nullptr;
	SDL_Cursor* m_cursor_hand   = nullptr;
	SDL_Cursor* m_cursor_ibeam  = nullptr;
};
