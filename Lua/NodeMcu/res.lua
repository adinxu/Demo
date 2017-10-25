weather={}
mt={} 
t={metatable=mt}
mt.__newindex=function(table, key, value)
if
(key=="name") or
(key=="text") or
(key=="temperature") 
then
print("Setting '" .. key .. "' = '" .. tostring(value) .."'") rawset(weather,key,value)
end
end
http.get("http://api.seniverse.com/v3/weather/now.json?key=ldydccwkd6w1nmab&location=ip&language=zh-Hans&unit=c", nil, function(code, data)
if (code < 0) then
  print("HTTP request failed")
else
  print(data)
	sjson.decode(data,t)
	for k, v in pairs(weather) do  
	print(k, v)
	end 
end
end)

{"results":[
	{
		"location":{
		"id":"WTQFSTR6DMK4",
		"name":"鑸熷北",
		"country":"CN",
		"path":"鑸熷北,鑸熷��,娴欐睙,涓浗",
		"timezone":"Asia/Shanghai",
		"timezone_offset":"+08:00"
		},
		"now":{
		"text":"澶ч洦",
		"code":"15",
		"temperature":"20"
		},
		"last_update":"2017-10-15T13:35:00+08:00"
	}
  ]
}





{
    "results": [
        {
            "location": {
                "id": "WTQFSTR6DMK4",
                "name": "舟山",
                "country": "CN",
                "path": "舟山,舟山,浙江,中国",
                "timezone": "Asia/Shanghai",
                "timezone_offset": "+08:00"
            },
            "daily": [
                {
                    "date": "2017-10-17",
                    "text_day": "小雨",
                    "code_day": "13",
                    "text_night": "阴",
                    "code_night": "9",
                    "high": "21",
                    "low": "17",
                    "precip": "",
                    "wind_direction": "东北",
                    "wind_direction_degree": "45",
                    "wind_speed": "15",
                    "wind_scale": "3"
                },
                {
                    "date": "2017-10-18",
                    "text_day": "阴",
                    "code_day": "9",
                    "text_night": "多云",
                    "code_night": "4",
                    "high": "22",
                    "low": "17",
                    "precip": "",
                    "wind_direction": "东北",
                    "wind_direction_degree": "45",
                    "wind_speed": "15",
                    "wind_scale": "3"
                },
                {
                    "date": "2017-10-19",
                    "text_day": "多云",
                    "code_day": "4",
                    "text_night": "阴",
                    "code_night": "9",
                    "high": "22",
                    "low": "17",
                    "precip": "",
                    "wind_direction": "东北",
                    "wind_direction_degree": "45",
                    "wind_speed": "10",
                    "wind_scale": "2"
                }
            ],
            "last_update": "2017-10-17T18:00:00+08:00"
        }
    ]
}


