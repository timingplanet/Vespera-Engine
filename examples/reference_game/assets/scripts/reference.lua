local elapsed = 0.0

function Start()
    Vespera.Log.info("[Lua] reference entry started; C# and Lua are sharing the same runtime services")
end

function Update(dt)
    elapsed = elapsed + dt
    if Vespera.Input.pressed("probe") then
        local player = Vespera.Scene.find_tag("player")
        if player then
            local p = Vespera.Entity.position(player)
            Vespera.Log.info(string.format("[Lua] probe observed at %.2f, %.2f, %.2f", p.x, p.y, p.z))
        end
    end
end

function Stop()
    Vespera.Log.info("[Lua] reference entry stopped")
end
