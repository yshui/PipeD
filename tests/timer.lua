t = di.event.timer(1)
t.on("elapsed", function()
    di.quit()
end)
