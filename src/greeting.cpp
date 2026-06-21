#include "greeting.h"

std::string make_greeting(const std::string& name) {
    const std::string who = name.empty() ? "World" : name;
    return "Hello, " + who + "!";
}
