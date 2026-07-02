-- wrk 登录压测脚本（多用户版）
-- 用法: wrk -t4 -c50 -d300s -s scripts/login.lua http://localhost:8083/api/v1/auth/login
-- 前置条件: 先执行 ./scripts/seed_users.sh 创建测试用户

wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

-- 用户池大小，需与 seed_users.sh 创建的数量一致
local user_count = 1000
local password = "Test@12345"

-- 预生成请求体模板，避免每次 request() 都做字符串拼接
local bodies = {}
for i = 1, user_count do
    bodies[i] = '{"username":"loadtest_user_' .. i .. '","password":"' .. password .. '"}'
end

function request()
    local idx = math.random(1, user_count)
    return wrk.format(nil, nil, nil, bodies[idx])
end

response = function(status, headers, body)
    if status ~= 200 then
        io.stderr:write("Error: status=" .. status .. " body=" .. body:sub(1, 200) .. "\n")
    end
end
