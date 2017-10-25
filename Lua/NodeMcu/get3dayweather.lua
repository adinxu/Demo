weather={{},{},{}}
mt={} 
t={metatable=mt}
mt.__newindex=function(table, key, value)
if table['__json_path'][4]~=nil then 
    if key=="text_day" or key=="text_night" 
	or key=="high"or key=="low" then
    --print("Setting '" .. key .. "' = '" .. tostring(value) .."'") 
	rawset(weather[table['__json_path'][4]],key,value)
    end
	//what??this??
end
end

mt.checkpath=function(table,path)
rawset(table,'__json_path',path)
return true
end

decoder = sjson.decoder(t)

http.get("https://api.seniverse.com/v3/weather/daily.json?key=ldydccwkd6w1nmab&location=ip&language=zh-Hans&unit=c&start=0&days=5", nil, function(code, data)
if (code < 0) then
  print("HTTP request failed")
else
  decoder:write(data)
end
end)

