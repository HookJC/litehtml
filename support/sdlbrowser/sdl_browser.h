#pragma once

#include "html_host.h"
#include "web_history.h"
#include "web_page.h"
#include "../draw_buffer/draw_buffer.h"

#include <SDL.h>
#include <cairo.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>

class sdl_browser final : public litebrowser::html_host_interface
{
	enum class notification_type
	{
		redraw,
		render,
		update_state,
		page_loaded,
		set_caption
	};

	struct notification
	{
		notification_type type;
		uint64_t page_id = 0;
		std::string text;
	};

	class notifier final : public litebrowser::browser_notify_interface
	{
		sdl_browser& m_browser;
		std::mutex m_mutex;
		std::queue<notification> m_queue;
		bool m_disconnected = false;

		void enqueue(notification item);

	public:
		explicit notifier(sdl_browser& browser) : m_browser(browser) {}
		void disconnect();
		void dispatch();

		void redraw() override;
		void render() override;
		void update_state() override;
		void on_set_caption(const std::string& caption) override;
		void on_page_loaded(uint64_t page_id) override;
	};

	static constexpr int toolbar_height = 48;
	SDL_Window* m_window = nullptr;
	SDL_Renderer* m_renderer = nullptr;
	SDL_Texture* m_page_texture = nullptr;
	SDL_Cursor* m_cursor = nullptr;
	SDL_SystemCursor m_cursor_type = SDL_SYSTEM_CURSOR_ARROW;
	Uint32 m_user_event = static_cast<Uint32>(-1);
	int m_width = 1100;
	int m_height = 760;
	int m_view_width = 1100;
	int m_view_height = 712;
	std::atomic<int> m_render_width{1100};
	std::atomic<int> m_render_height{712};
	std::mutex m_page_mutex;
	std::shared_ptr<litebrowser::web_page> m_current_page;
	std::shared_ptr<litebrowser::web_page> m_next_page;
	std::shared_ptr<notifier> m_notifier;
	litebrowser::draw_buffer m_draw_buffer;
	web_history m_history;
	std::string m_address;
	std::string m_caption = "litehtml SDL Browser";
	bool m_address_focused = false;
	bool m_running = true;
	bool m_dragging_vscroll = false;
	bool m_dragging_hscroll = false;
	bool m_page_pressed = false;
	bool m_loading = false;
	bool m_address_select_all = false;
	int m_mouse_x = 0;
	int m_mouse_y = 0;
	int m_scroll_x = 0;
	int m_scroll_y = 0;

public:
	sdl_browser();
	~sdl_browser() override;

	int run(const std::string& initial_url);

	void open_url(const std::string& url) override;
	void update_cursor() override;
	void scroll_to(int x, int y) override;
	void get_viewport(litehtml::position& viewport) const override;
	void redraw_boxes(const litehtml::position::vector& boxes) override;
	int get_render_width() override;
	double get_dpi() override;
	int get_screen_width() override;
	int get_screen_height() override;
	cairo_surface_t* load_image(const std::string& path) override;

private:
	std::shared_ptr<litebrowser::web_page> current_page();
	void open_page(const std::string& url, const std::string& fragment);
	void on_page_loaded(uint64_t page_id);
	void render_page();
	void resize(int width, int height);
	void scroll_by(int dx, int dy);
	void dispatch_event(const SDL_Event& event);
	void handle_key(const SDL_KeyboardEvent& event);
	void handle_text(const SDL_TextInputEvent& event);
	void draw_frame();
	void draw_toolbar();
	void update_window_title();
	void update_page_texture();
	void draw_label(cairo_t* context, const char* text, double x, double y);
	void update_cursor_icon();
	void go_back();
	void go_forward();
	void reload_or_stop();
	void submit_address();
	void stop_download();
	void notify_state_changed();
	void set_scroll_position(int x, int y);
	litebrowser::draw_buffer::draw_page_function_t get_draw_function(
		const std::shared_ptr<litebrowser::web_page>& page);
};
