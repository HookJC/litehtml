// main.cpp - entry point for the SDL2-based litehtml browser.
//
// Usage: sdlbrowser [url]
// If no url is given, http://172.16.231.18:1080/ is opened.

#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "sdl_html_widget.h"

#include <iostream>
#include <string>
#include <exception>

int main(int argc, char* argv[])
{
	SDL_SetMainReady();

	std::string url = (argc > 1) ? argv[1] : "http://www.litehtml.com/";

	try
	{
		std::cerr << "[main] creating widget" << std::endl;
		sdl_html_widget widget;
		std::cerr << "[main] widget created" << std::endl;

		if(!widget.init("SDL Browser - litehtml", 1024, 768))
		{
			std::cerr << "Failed to initialize the browser window." << std::endl;
			return 1;
		}
		std::cerr << "[main] initialized, opening " << url << std::endl;

		widget.run(url);
		std::cerr << "[main] run returned" << std::endl;
	}
	catch(const std::exception& ex)
	{
		std::cerr << "[main] FATAL std::exception: " << ex.what() << std::endl;
		return 2;
	}
	catch(...)
	{
		std::cerr << "[main] FATAL unknown exception" << std::endl;
		return 3;
	}
	return 0;
}
