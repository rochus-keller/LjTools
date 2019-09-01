local i=-12
local j=0
local k={}
local function b()
	local function c()
		i = i + 1
		j = i
	end
	c()
end
b()
print(i)
b()
print(i)
k.x="test"
print(k)
print(k.x)
print(_VERSION)

-- TestVM outputs the following
--> -11
--> -10
--> table: 0x97b4200
--> test
--> TestVM
