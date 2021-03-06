-- init.lua: measure the time it takes for wifi connection to be established using event monitor
local start_time = tmr.now()
local eventmon = wifi.eventmon
local stop = 1	-- gpio5, D1
local time_set_ip = start_time
local time_connected = start_time
local start_status = wifi.sta.status()
local connected_status = -1

local function msg(t, reason)
	print (("start     status %d"):format(start_status))
	print (("connected status %d"):format(connected_status))
	print (("end       status %d"):format(wifi.sta.status()))
	print (("%8.3fms %s"):format((time_set_ip-start_time)/1000, "set IP"))
	print (("%8.3fms %s"):format((time_connected-start_time)/1000, "connected"))
	print (("%8.3fms %s"):format((t-start_time)/1000, reason))
	eventmon.unregister(eventmon.STA_CONNECTED)
	eventmon.unregister(eventmon.STA_GOT_IP)
	eventmon.unregister(eventmon.STA_DISCONNECTED)
	tmr.alarm(3, 100, 0, function()		-- 100ms, time for message to print
		rtctime.dsleep(1*1000*1000)	-- sleep for 1s
	end)
end

local function test()
	gpio.mode (stop, gpio.INPUT, gpio.PULLUP)
	if 0 == gpio.read (stop) then
		print ("stopped")
		return
	end

	local t = 0

	eventmon.register(eventmon.STA_GOT_IP, function(T)
		t = tmr.now() msg(t, "GOT_IP") end)
	eventmon.register(eventmon.STA_CONNECTED, function(T)
		time_connected = tmr.now() connected_status = wifi.sta.status() end)
	eventmon.register(eventmon.STA_DISCONNECTED, function(T)
		t = tmr.now() msg(t, "DISCONNECTED") end)

	tmr.alarm(1, 20, 0, function()	-- set IP before connection
		wifi.sta.setip({ip="192.168.2.47",netmask="255.255.255.0",gateway="192.168.2.7"})
		time_set_ip = tmr.now()
	end)

	tmr.alarm(2, 2*1000, 0, function()	-- 2s timeout
		msg(tmr.now(), "Timeout")
	end)
end

test()

