extern "C" {
#include "lua.hpp"
}

extern "C" int hello(lua_State* L) {
    printf("hello");
    return 0;
}

static const luaL_Reg myLib[] =
{
    { "hello", hello },
    { NULL, NULL }
};

#ifdef _WIN32
extern "C" __declspec(dllexport) int luaopen_MyLib(lua_State* L)
{
    #pragma message ("win32")
#else
extern "C"  int luaopen_MyLib(lua_State* L)
{
    #pragma message ("unix")
#endif // _WIN32
    luaL_newlib(L, myLib);
    return 1;
};
