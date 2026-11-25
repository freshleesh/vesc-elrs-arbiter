-- Copyright 2026 Sanghun Lee
--
-- This file is part of the VESC ELRS Arbiter.
--
-- This program is free software: you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version. See <http://www.gnu.org/licenses/>.

-- vesc.lua  (EdgeTX telemetry script names must be <= 6 chars)
-- Custom telemetry screen for the VESC-ELRS arbiter.
--
-- ELRS can only push a few telemetry frames per second, so anything the radio
-- already knows is computed LOCALLY (instant) and only the rest comes over the air:
--   local : mode switch (SA), steer stick (ch1), throttle stick (ch3), P2 dial (ch7)
--   telem : VSpd -> speed m/s (fast, 20 Hz from the VESC)
--           FM   -> "<actual arbiter mode> <CUR|SPD>" (catches "AUTO but computer dead -> stop",
--                   and the SD current/speed control toggle, which lives in the VESC)
--           Curr -> throttle %, Bat% -> steer %  (used in AUTO, where the computer drives)
-- getValue() already applies sensor precision.
--
-- Keep these in sync with firmware/app_arbiter.c
local MAXV_MIN = 0.5
local MAXV_MAX = 8.0
local DEADBAND = 0.05

local function clamp(v, lo, hi)
  if v < lo then return lo end
  if v > hi then return hi end
  return v
end

local function num(name)
  local v = getValue(name)
  if type(v) ~= "number" then return 0 end
  return v
end

local BAR_X = 70

local function bar(y, frac)
  local w = LCD_W - BAR_X - 1
  lcd.drawRectangle(BAR_X, y, w, 7)
  local fill = math.floor((w - 2) * clamp(frac, 0, 1))
  if fill > 0 then
    lcd.drawFilledRectangle(BAR_X + 1, y + 1, fill, 5)
  end
end

local function cbar(y, frac)
  local w = LCD_W - BAR_X - 1
  local cx = BAR_X + math.floor(w / 2)
  lcd.drawRectangle(BAR_X, y, w, 7)
  lcd.drawLine(cx, y - 1, cx, y + 7, SOLID, 0)
  local half = math.floor((w - 2) / 2)
  local len = math.floor(half * clamp(math.abs(frac), 0, 1))
  if len > 0 then
    if frac > 0 then
      lcd.drawFilledRectangle(cx + 1, y + 1, len, 5)
    else
      lcd.drawFilledRectangle(cx - len, y + 1, len, 5)
    end
  end
end

local function run(event)
  lcd.clear()

  -- local inputs, -1..1
  local sa    = num("sa") / 1024
  local st    = clamp(num("ch1") / 1024, -1, 1)
  local th    = clamp(num("ch3") / 1024, -1, 1)
  local dial  = clamp((num("ch7") + 1024) / 2048, 0, 1)
  if math.abs(th) < DEADBAND then th = 0 end

  local sw
  if sa < -0.5 then sw = "AUTO" elseif sa > 0.5 then sw = "MANUAL" else sw = "ESTOP" end

  local fm = getValue("FM")
  if type(fm) ~= "string" then fm = "" end
  local fm_mode, ctrl = string.match(fm, "^(%a+) (%a+)$")
  if ctrl == nil then ctrl = "---" end
  local mode = sw
  local thr, steer
  if sw == "AUTO" then
    if fm_mode == "ESTOP" then mode = "NOPC" end
    thr = num("Curr")
    steer = num("Bat%")
  else
    thr = (sw == "MANUAL") and th * 100 or 0
    steer = st * 100
  end

  local spd  = num("VSpd")
  local maxv = MAXV_MIN + dial * (MAXV_MAX - MAXV_MIN)

  lcd.drawText(0, 0, mode, MIDSIZE + INVERS)
  lcd.drawText(lcd.getLastRightPos() + 3, 4, ctrl, SMLSIZE + INVERS)
  lcd.drawText(LCD_W - 1, 2, string.format("%.1fm/s", spd), MIDSIZE + RIGHT)

  local y = 18
  lcd.drawText(0, y, "SPD", SMLSIZE)
  lcd.drawText(20, y, string.format("%5.2f", spd), SMLSIZE)
  bar(y, math.abs(spd) / maxv)

  y = y + 11
  lcd.drawText(0, y, "THR", SMLSIZE)
  lcd.drawText(20, y, string.format("%4d%%", math.floor(thr + 0.5)), SMLSIZE)
  cbar(y, thr / 100)

  y = y + 11
  lcd.drawText(0, y, "STR", SMLSIZE)
  lcd.drawText(20, y, string.format("%4d%%", math.floor(steer + 0.5)), SMLSIZE)
  cbar(y, steer / 100)

  y = y + 11
  lcd.drawText(0, y, "MAXV", SMLSIZE)
  lcd.drawText(20, y, string.format("%5.2f", maxv), SMLSIZE)
  bar(y, maxv / MAXV_MAX)

  return 0
end

return { run = run }
