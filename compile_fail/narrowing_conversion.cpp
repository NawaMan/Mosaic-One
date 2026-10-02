// expect-error: implicit conversion loses integer precision
//
// main.cpp turns -Wconversion into an error, so code using the library can't silently
// shrink an int into an int8_t before handing it over.
#include "../main.cpp"

int some_int();
std::int8_t narrowed = some_int();
