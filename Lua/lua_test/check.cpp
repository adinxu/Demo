#include"check.h"
#include<iostream>
using std::cout;
bool pGetValue(lua_State *L, int index,string typestr,void* save)
{
    if(!L) {cout<<"lua_state is valid\n";return false;}

    if(!typestr.compare("boolean")) {if(lua_isboolean(L,index)) {*(bool*)save=lua_toboolean(L,index);return true;}}
    else if(!typestr.compare("integer")) {if(lua_isinteger(L,index)) {*(int*)save=lua_tointeger(L,index);return true;}}
    else if(!typestr.compare("string")) {if(lua_isstring(L,index)) {const char*str=(char*)lua_tostring(L,index);return true;}}
    else if(!typestr.compare("number")) {if(lua_isnumber(L,index)) {*(double*)save=lua_tonumber(L,index);return true;}}
    else {cout<<"undefine type\n";return false;}

    cout<<"the type is not "<<typestr<<"\n";
    return false;
}
#warning FIXME (13345#1#10/11/17): 写一个lua版本的check
