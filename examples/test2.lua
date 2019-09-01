function a()
	local i=-12
	local k = "test"
	local j=0
	local function b()
		local function c(x)
			i = i + 1
			j = i + x
		end
		c(23)
		print(j)
	end
	local function d()
		print(k)
	end
	b()
	print(i)
	b()
	print(i)
	b()
	print(i)
end
a()

-- TestVM outputs the following
--> 12
--> -11
--> 13
--> -10
--> 14
--> -9


