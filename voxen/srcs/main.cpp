#define STB_IMAGE_IMPLEMENTATION

#include <iostream>
#include "App.h"

int main(void) {
	App app;
	
	if (!app.Initialize()) {
		std::cout << "Failed App Initialized" << std::endl;
		return -1;
	}
	app.Run();
	
	return 0;
}