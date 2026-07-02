-- wrk Token 刷新压测脚本
-- 用法: wrk -t4 -c100 -d300s -s scripts/token_refresh.lua http://localhost:8083/api/v1/auth/token/refresh
-- 使用前需要先获取 refresh_token 并替换下方 body

wrk.method = "POST"
wrk.headers["Content-Type"] = "application/json"

-- TODO: 替换为实际的 refresh_token
wrk.body = '{"refresh_token":"YOUR_REFRESH_TOKEN_HERE"}'

init = function(args)
    -- 如果需要动态获取 token，可以在这里处理
end

response = function(status, headers, body)
    if status ~= 200 then
        io.stderr:write("Refresh failed: status=" .. status .. " body=" .. body:sub(1, 200) .. "\n")
    end
end
