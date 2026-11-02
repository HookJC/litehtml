#include "sdl_notifier.h"

sdl_notifier::sdl_notifier()
{
	m_event_id = SDL_RegisterEvents(1);
}

void sdl_notifier::disconnect()
{
	std::lock_guard<std::mutex> lock(m_lock);
	m_disconnect = true;
	m_queue.clear();
}

void sdl_notifier::push(func_type t, uint64_t param, const std::string& str)
{
	{
		std::lock_guard<std::mutex> lock(m_lock);
		if(m_disconnect) return;
		m_queue.push_back({t, param, str});
	}
	SDL_Event ev;
	SDL_zero(ev);
	ev.type = m_event_id;
	SDL_PushEvent(&ev);
}

void sdl_notifier::redraw()            { push(func_redraw, 0, {}); }
void sdl_notifier::render()            { push(func_render, 0, {}); }
void sdl_notifier::update_state()      { push(func_update_state, 0, {}); }
void sdl_notifier::on_page_loaded(uint64_t id) { push(func_on_page_loaded, id, {}); }
void sdl_notifier::on_set_caption(const std::string& caption) { push(func_on_set_caption, 0, caption); }

void sdl_notifier::pump()
{
	std::deque<item> local;
	{
		std::lock_guard<std::mutex> lock(m_lock);
		if(m_disconnect) return;
		local.swap(m_queue);
	}

	func_type prev = func_none;
	while(!local.empty())
	{
		const item it = local.front();
		local.pop_front();

		// collapse consecutive duplicate notifications (same idea as the gtkmm version)
		if(it.type == prev) continue;
		prev = it.type;

		switch(it.type)
		{
		case func_redraw:        if(m_redraw)        m_redraw();        break;
		case func_render:        if(m_render)        m_render();        break;
		case func_update_state:  if(m_update_state)  m_update_state();  break;
		case func_on_page_loaded:if(m_on_page_loaded) m_on_page_loaded(it.param); break;
		case func_on_set_caption:if(m_on_set_caption) m_on_set_caption(it.str); break;
		default: break;
		}
	}
}
