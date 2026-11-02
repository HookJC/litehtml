#include "sdl_browser.h"

#include <iostream>
#include <string>

int main(int argc, char** argv)
{
	const std::string initial_url = argc > 1 ? argv[1] : "http://www.litehtml.com/";
	try
	{
		sdl_browser browser;
		return browser.run(initial_url);
	}
	catch(const std::exception& error)
	{
		std::cerr << "sdlbrowser: " << error.what() << '\n';
		return 1;
	}
}
