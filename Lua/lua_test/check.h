#ifndef CHECK_H_INCLUDED
#define CHECK_H_INCLUDED
#include<lua.hpp>
#include<string>
using std::string;
extern bool pGetValue(string typestr,lua_State *L, int index);

#endif // CHECK_H_INCLUDED
