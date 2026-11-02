// sdl_notifier.h
//
// Replaces gtkmm4's html_widget_notifier. Background download threads (curl /
// web_page) call the browser_notify_interface methods; those must be executed on
// the SDL GUI thread. We marshal them through a mutex queue + a custom SDL event
// (SDL_PushEvent wakes SDL_WaitEvent in the main loop).
#pragma once

#include "html_host.h"

#include <SDL.h>

#include <functional>
#include <mutex>
#include <deque>
#include <string>
#include <cstdint>

class sdl_notifier : public litebrowser::browser_notify_interface
{
public:
	enum func_type
	{
		func_none = 0,
		func_redraw,
		func_render,
		func_update_state,
		func_on_page_loaded,
		func_on_set_caption
	};

private:
	struct item
	{
		func_type   type;
		uint64_t    param;
		std::string str;
	};

	std::mutex           m_lock;
	std::deque<item>     m_queue;
	bool                 m_disconnect = false;
	Uint32               m_event_id   = 0;

	std::function<void()>                 m_redraw;
	std::function<void()>                 m_render;
	std::function<void()>                 m_update_state;
	std::function<void(uint64_t)>         m_on_page_loaded;
	std::function<void(const std::string&)> m_on_set_caption;

public:
	sdl_notifier();
	~sdl_notifier() override = default;

	Uint32 event_id() const { return m_event_id; }
	void disconnect();

	void connect_redraw(std::function<void()> f)             { m_redraw = std::move(f); }
	void connect_render(std::function<void()> f)             { m_render = std::move(f); }
	void connect_update_state(std::function<void()> f)       { m_update_state = std::move(f); }
	void connect_on_page_loaded(std::function<void(uint64_t)> f) { m_on_page_loaded = std::move(f); }
	void connect_on_set_caption(std::function<void(const std::string&)> f) { m_on_set_caption = std::move(f); }

	// Drain + dispatch queued calls. Called on the GUI thread when m_event_id fires.
	void pump();

	// browser_notify_interface
	void redraw() override;
	void render() override;
	void update_state() override;
	void on_set_caption(const std::string& caption_text) override;
	void on_page_loaded(uint64_t web_page_id) override;

private:
	void push(func_type t, uint64_t param, const std::string& str);
};
