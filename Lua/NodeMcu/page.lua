print("Connected, IP is "..wifi.sta.getip())
sv=net.createServer(net.TCP)
sv:listen(80,function(conn)
conn:on("receive",function(conn,payload)
print("Heap = "..node.heap().." Bytes")
print("Print payload:\n"..payload)
page=[[
<html> 
<head>
<title>default web</title>
</head>
<body bgcolor="#70000">
<table width=100% height=100%>
<tr><td>
<center>
<span style="font-size:100px;color:A0A0A0;font-family:century">TEST</span>
<p><font size="5" face="arial">The size of the memory available:]]
..tostring(node.heap())..
[[ 
</font></p>
<hr>
</center>
</td></tr>
</table>
</body>
</html>
]]
payloadLen = string.len(page)
conn:send("HTTP/1.1 200 OK\r\n")
conn:send("Content-Length:" .. tostring(payloadLen) .. "\r\n")
conn:send("Connection:close\r\n\r\n")
conn:send(page)
collectgarbage()
end)
conn:on("sent",function(conn)
conn:close() 
end)
end)

