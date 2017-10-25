print("Connected, IP is "..wifi.sta.getip())
sv=net.createServer(net.TCP)
sv:listen(80,function(conn)
con_std = conn
function s_output(str)
  if(con_std~=nil)
	 then con_std:send(str)
  end
end
node.output(s_output, 1) 
conn:on("connection",function(conn)
local _,ip=conn:getpeer()
print("connection successed!the client ip is:"..ip)
end)
conn:on("disconnection",function(conn,errcode)
con_std=nil
node.output(nil) 
print("connection close!")
if(errcode~=0) then 
print("an err occor,the errcode is:"..errcode)
end
print("waiting for next connection!")
end)
conn:on("receive",function(conn, payload)
node.input(payload)
end)
end)
