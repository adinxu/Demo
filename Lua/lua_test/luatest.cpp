#include<iostream>
#include<lua.hpp>
using namespace std;

#define execute
//#define load
//#define registercfun

#ifdef registercfun
static int sub(lua_State* L)
{
    int a=luaL_checknumber(L,1);
    int b=luaL_checknumber(L,2);
    lua_pushnumber(L,a-b);
    return 1;
}
const char* testcfun="print(c_sub(1,1))";
#endif // registercfun


int main()
{
    lua_State *L=luaL_newstate();
    if(L)
    {
        luaL_openlibs(L);
    }
    else
    {
        cout<<"creat luastate falied!\n";
        return 0;
    }

    lua_newtable(L);//ÐÂ½¨±í
    lua_pushstring(L,"name");
    lua_setfield(L,-2,"lisi");

#ifdef execute

    if(luaL_loadfile(L,"exectest.lua")||lua_pcall(L,0,0,0))
    {
        cout<<"file load err!\n";
        lua_close(L);
        return -1;
    }
    if(luaL_dofile(L,"exectest.lua"))
    {
        cout<<"file execute err!\n";
    }
#endif // execute

#ifdef load
    if(luaL_loadfile(L,"test.lua")||lua_pcall(L,0,0,0))
    {
        cout<<"file load err!\n";
        lua_close(L);
        return -1;
    }

    lua_getglobal(L,"str");
    cout<<"the str is: "<<luaL_checkstring(L,-1)<<"\n";


    lua_getglobal(L,"tbl");
    lua_getfield(L,-1,"name");
    lua_getfield(L,-2,"id");

    cout<<"name: "<<luaL_checkstring(L,-2)<<"\n";
    cout<<"id: "<<luaL_checknumber(L,-1)<<"\n";

    lua_getglobal(L,"add");
    lua_pushnumber(L,1);
    lua_pushnumber(L,1);
    if(lua_pcall(L,2,1,0))
    {
        const char* errmsg=lua_tostring(L,-1);
        cout<<errmsg<<"\n";
        lua_close(L);
        return -1;
    }
    cout<<"1+1="<<luaL_checknumber(L,-1)<<"\n";
#endif // load


#ifdef registercfun
    lua_pushcfunction(L,sub);
    lua_setglobal(L,"c_sub");
    if (luaL_dostring(L,testcfun))
    printf("Failed to invoke.\n");

#endif // registercfun


    lua_close(L);
    return 0;
}
