module={}
module.constant="����һ������"
function module.fun1()
print("����һ�����к���")
end
local function fun2()
io.write("����һ��˽�к���\n")
end
function module.fun3()
fun2()
end
return module
