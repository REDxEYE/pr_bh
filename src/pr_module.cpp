#include "pr_module.hpp"
#include <pragma/lua/luaapi.h>
#include <pragma/console/conout.h>
#include <luainterface.hpp>

extern "C"
{
    // Called after the module has been loaded
    DLLEXPORT bool pragma_attach(std::string &outErr)
    {
        // Return true to indicate that the module has been loaded successfully.
        // If the module could not be loaded properly, return false and populate 'outErr' with a descriptive error message.
        Con::cout<<"Custom module \"pr_bh\" has been loaded!"<<Con::endl;
        return true;
    }

    // Called when the module is about to be unloaded
    DLLEXPORT void pragma_detach()
    {
        Con::cout<<"Custom module \"pr_bh\" is about to be unloaded!"<<Con::endl;
    }

    // Lua bindings can be initialized here
    DLLEXPORT void pragma_initialize_lua(Lua::Interface &lua)
    {
        auto &libDemo = lua.RegisterLibrary("pr_bh");
        libDemo[luabind::def("print",+[]() {
            Con::cout<<"Hello World"<<Con::endl;
        })];

        struct DemoClass
        {
            DemoClass() {}
            void PrintWarning(const std::string &msg)
            {
                Con::cwar<<msg<<Con::endl;
            }
        };

        auto classDef = luabind::class_<DemoClass>("DemoClass");
        classDef.def(luabind::constructor<>());
        classDef.def("PrintWarning",&DemoClass::PrintWarning);
        libDemo[classDef];
    }

    // Called when the Lua state is about to be closed.
    DLLEXPORT void pragma_terminate_lua(Lua::Interface &lua)
    {
        
    }
};
