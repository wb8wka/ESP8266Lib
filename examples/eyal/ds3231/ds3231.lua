--------------------------------------------------------------------------------
-- DS3231 I2C module for NODEMCU
-- NODEMCU TEAM
-- LICENCE: http://opensource.org/licenses/MIT
-- Tobie Booth <tbooth@hindbra.in>
--------------------------------------------------------------------------------

local moduleName = ...
local M = {}
_G[moduleName] = M

local bit = bit
local string = string
local i2c = i2c
local print = print
local tonumber = tonumber
setfenv(1,M)

local id = 0		-- always zero
local dev_addr = 0x68	-- DS3231 i2c id

local function log (msg)
	if false then print (msg) end
end

local function int (val)
	return bit.bor(val, 0)
end

local function decToBcd(val)
  return(int(val/10)*16 + (val%10))
end

local function bcdToDec(val)
  return(int(val/16)*10 + (val%16))
end

local function setup()
  i2c.setup(id, sda, scl, i2c.SLOW)
  i2c.start(id)
  local c = i2c.address(id, dev_addr, i2c.TRANSMITTER)
  i2c.stop(id)
  return c
end

function init(d, c)	-- io indexes: sda, scl
  if d == nil or c == nil or d < 0 or d > 12 or c < 0 or c > 12 or d == c then
    log("ds3231 init failed: bad arguments")
    return nil
  else
    sda = d
    scl = c
  end

  if not setup() then
    log("ds3231 init failed: no device")
    return nil
  end

  log("ds3231 init OK")
end

-- return data or nil on failure
local function readI2C (addr, len)
  i2c.start(id)
  local c = i2c.address(id, dev_addr, i2c.TRANSMITTER)
  if c then i2c.write(id, addr) end
  i2c.stop(id)
  if not c then return nil end

  i2c.start(id)
  c = i2c.address(id, dev_addr, i2c.RECEIVER)
  local data = nil
  if c then data = i2c.read(id, len) end
  i2c.stop(id)

  return data
end

-- return number of bytes read
local function writeI2C (addr, data)
  i2c.start(id)
  local c = i2c.address(id, dev_addr, i2c.TRANSMITTER)
  local len = 0
  if c then
    len = i2c.write(id, addr)
    if len > 0 then len = i2c.write(id, data) end
  end
  i2c.stop(id)

  return len
end

-- get time from DS3231
function getTime()
  local data = readI2C (0x00, 7)
  if nil == data then return nil end

  local month = tonumber(string.byte(data, 6))

  return 
    bcdToDec(tonumber(string.byte(data, 1))),
    bcdToDec(tonumber(string.byte(data, 2))),
    bcdToDec(tonumber(string.byte(data, 3))),
    bcdToDec(tonumber(string.byte(data, 4))),
    bcdToDec(tonumber(string.byte(data, 5))),
    bcdToDec(bit.band (month, 0x1F)),
    bcdToDec(tonumber(string.byte(data, 7))) + 2000 + 100*int(month/128)
end

-- set time for DS3231
function setTime(second, minute, hour, dow, day, month, year)
-- validate arguments?
  if year >= 2000 then
	year = year - 2000
  end
  if year >= 100 then
	year = year - 100
	month = month + 80
  end

  local len = writeI2C (0x00,
    string.char (
      decToBcd(second),
      decToBcd(minute),
      decToBcd(hour),
      decToBcd(dow),
      decToBcd(day),
      decToBcd(month),
      decToBcd(year)))

  return 8 == len
end

function getTemp()
  local data = readI2C (0x11, 2)
  if nil == data then return nil end

  return tonumber(string.byte(data, 1)) + tonumber(string.byte(data, 2))/256
end

return M
