di:load_plugin("./plugins/dbus/di_dbus.so")
di.os.env.DBUS_SESSION_BUS_PID = nil
di.os.env.DBUS_SESSION_BUS_ADDRESS = nil
di.os.env.DISPLAY = nil
local dbusl = di.spawn:run({"dbus-daemon", "--print-address=1", "--print-pid=2", "--session", "--fork"}, false)
dbusl:on("stdout_line", function(l)
    -- remove new line
    if l == "" then
        return
    end
    print(l)
    di.os.env.DBUS_SESSION_BUS_ADDRESS = l
end)
dbusl:on("stderr_line", function(l)
    if l == "" then
        return
    end
    print(l)
    di.os.env.DBUS_SESSION_BUS_PID = l
end)
function call_with_error(o, name, ...)
    t = o[name](o, ...)
    if t.errmsg then
        print(t.errmsg)
        di:exit(1)
    end
    t:on("error", function(e)
        print(e)
    end)
end
dbusl:on("exit", function()
    b = di.dbus.session_bus
    if b.errmsg then
        print(b.errmsg)
        di:exit(1)
    end
    o = di.dbus.session_bus:get("org.freedesktop.DBus", "/org/freedesktop/DBus")
    o:Introspect():on("reply", function(s)
        print(s)
    end)
    o:ListNames():on("reply", function(s)
        for _, i in pairs(s) do
            print(i)
        end
    end)
    o:GetAllMatchRules():on("reply", function(s)
        print(s)
    end)

    -- Use non-existent method to test message serialization
    call_with_error(o, "org.dummy.Dummy", {1,2,3})
    call_with_error(o, "org.dummy.Dummy", {"asdf","qwer"})
    call_with_error(o, "org.dummy.Dummy", 1)
    call_with_error(o, "org.dummy.Dummy", "asdf")
end)
