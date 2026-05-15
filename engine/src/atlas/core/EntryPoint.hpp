#pragma once

#include "core/Application.hpp"
#include "core/Log.hpp"

#include <exception>
#include <iostream>

int main(int argc, char **argv) {
    try {
        Atlas::Log::init();

        Atlas::Application *application = Atlas::CreateApplication({argc, argv});
        application->run();
        delete application;
    } catch (const std::exception &error) {
        std::cerr << "Atlas failed: " << error.what() << '\n';
        return 1;
    }

    return 0;
}
