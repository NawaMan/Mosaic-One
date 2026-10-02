// expect-error: ignoring return value of type
//
// Nothing changes an OverflowableInt in place, so `total + item;` does nothing. main.cpp
// makes ignoring a result an error, so this mistake can't slip through.
#include "../main.cpp"

void add_to(OverflowableInt8 total, OverflowableInt8 item) {
    total + item;
}
