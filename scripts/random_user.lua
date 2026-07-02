-- wrk 随机用户档案查询脚本
-- 用法: wrk -t4 -c50 -d300s -H "Authorization: Bearer TOKEN" -s scripts/random_user.lua http://localhost:8084

counter = 0

request = function()
    counter = counter + 1
    local user_id = "user_" .. math.random(1, 100000)
    return wrk.format(nil, "/api/v1/gamedata/profiles/" .. user_id)
end

response = function(status, headers, body)
    if status ~= 200 and status ~= 404 then
        io.stderr:write("Unexpected status: " .. status .. "\n")
    end
end
