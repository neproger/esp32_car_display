#include "state.hpp"

AppState &app_state()
{
    static AppState state;
    return state;
}
