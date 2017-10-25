weather={}
mt={} 
t={metatable=mt}
mt.__newindex=function(table, key, value)
if key=="text" then
print("Setting '" .. key .. "' = '" .. tostring(value) .."'") rawset(weather,key,value)
end
end

decoder = sjson.decoder(t)

http.get("http://api.seniverse.com/v3/weather/now.json?key=ldydccwkd6w1nmab&location=ip&language=zh-Hans&unit=c", nil, function(code, data)
if (code < 0) then
  print("HTTP request failed")
else
  decoder:write(data)
end
end)
