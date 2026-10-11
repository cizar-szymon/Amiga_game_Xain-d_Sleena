-- Xain'd Sleena: reference capture from MAME (loaded by tools/mame_capture.py).
-- The global CFG is set by the generated script before this file is run:
--   CFG.out      output directory (with a trailing slash)
--   CFG.shots    { [frame] = true }  frames to dump (PNG + state)
--   CFG.inputs   { [frame] = { {port, field, value}, ... } }
--   CFG.last     frame after which MAME exits
--   CFG.traces   list of { cpu = ":maincpu", from = F, to = F, file = "..." }
--
-- Frame N = the state after N complete frames (frame notifier, end of frame).
-- State file frame_NNNNN.bin: "XSNP" + chunks (16-byte name, u32 big endian size, data).

local m = manager.machine
local screen = m.screens[":screen"]
local cpus = { ":maincpu", ":sub", ":audiocpu", ":mcu:mcu" }

-- registers that cannot be read back: shadow copies from write taps
local io_main = {}    -- maincpu 3A00-3A0F last written values
local sub_bank = 0    -- subcpu 3000
local ym = { {}, {} } -- YM2203 register files
local ym_addr = { 0, 0 }
for i = 0, 255 do ym[1][i] = 0; ym[2][i] = 0 end
for i = 0, 15 do io_main[i] = 0 end

XS_TAPS = {}  -- global: taps and the frame notifier stop working when garbage collected
local taps = XS_TAPS
local mspace = m.devices[":maincpu"].spaces["program"]
taps[#taps + 1] = mspace:install_write_tap(0x3a00, 0x3a0f, "io_main", function(offset, data, mask)
  io_main[offset - 0x3a00] = data & 0xff
end)
local sspace = m.devices[":sub"].spaces["program"]
taps[#taps + 1] = sspace:install_write_tap(0x3000, 0x3000, "sub_bank", function(offset, data, mask)
  sub_bank = data & 0xff
end)
local aspace = m.devices[":audiocpu"].spaces["program"]
local function ym_tap(n, base)
  return aspace:install_write_tap(base, base + 1, "ym" .. n, function(offset, data, mask)
    if offset == base then ym_addr[n] = data & 0xff else ym[n][ym_addr[n]] = data & 0xff end
  end)
end
taps[#taps + 1] = ym_tap(1, 0x2800)
taps[#taps + 1] = ym_tap(2, 0x3000)

-- CFG.watch: list of { cpu = ":maincpu", addr = N }: log every write as "frame cpu pc addr=value".
-- Installed at the first frame: taps on RAM installed before the machine runs see no writes.
local function watch_start()
  if not (CFG.watch and #CFG.watch > 0) or XS_WATCH_FILE then return end
  local wf = assert(io.open(CFG.out .. "watch.txt", "w"))
  XS_WATCH_FILE = wf
  for i, w in ipairs(CFG.watch) do
    local dev = m.devices[w.cpu]
    local st = dev.state["CURPC"]
    taps[#taps + 1] = dev.spaces["program"]:install_write_tap(w.addr, w.addr, "watch" .. i, function(offset, data, mask)
      wf:write(string.format("%d %s %04X %04X=%02X\n", screen:frame_number(), w.cpu, st.value, offset, data & 0xff))
      wf:flush()
    end)
  end
end

local function share_bytes(tag)
  local sh = m.memory.shares[tag]
  local t = {}
  for i = 0, sh.size - 1 do t[#t + 1] = string.char(sh:read_u8(i)) end
  return table.concat(t)
end

local function space_bytes(space, from, to)
  local t = {}
  for a = from, to do t[#t + 1] = string.char(space:read_u8(a)) end
  return table.concat(t)
end

local function regs_text()
  local lines = {}
  for _, tag in ipairs(cpus) do
    local st = m.devices[tag].state
    local names = {}
    for name, _ in pairs(st) do names[#names + 1] = name end
    table.sort(names)
    local parts = {}
    for _, name in ipairs(names) do parts[#parts + 1] = string.format("%s=%X", name, st[name].value) end
    lines[#lines + 1] = tag .. " " .. table.concat(parts, " ")
  end
  return table.concat(lines, "\n") .. "\n"
end

local function table_bytes(tbl, n)
  local t = {}
  for i = 0, n - 1 do t[#t + 1] = string.char(tbl[i]) end
  return table.concat(t)
end

local function chunk(f, name, data)
  f:write(name .. string.rep("\0", 16 - #name))
  f:write(string.pack(">I4", #data))
  f:write(data)
end

local function dump(frame)
  local base = string.format("%sframe_%05d", CFG.out, frame)
  screen:snapshot(base .. ".png")
  local f = assert(io.open(base .. ".bin", "wb"))
  f:write("XSNP")
  chunk(f, "share1", share_bytes(":share1"))
  chunk(f, "charram", share_bytes(":charram"))
  chunk(f, "bgram0", share_bytes(":bgram0"))
  chunk(f, "bgram1", share_bytes(":bgram1"))
  chunk(f, "spriteram", share_bytes(":spriteram"))
  chunk(f, "palette", share_bytes(":palette"))
  chunk(f, "palette_ext", share_bytes(":palette_ext"))
  chunk(f, "sound_ram", space_bytes(aspace, 0x0000, 0x07ff))
  chunk(f, "mcu_ram", space_bytes(m.devices[":mcu:mcu"].spaces["program"], 0x0000, 0x007f))
  chunk(f, "io_main", table_bytes(io_main, 16))
  chunk(f, "sub_bank", string.char(sub_bank))
  chunk(f, "ym1", table_bytes(ym[1], 256))
  chunk(f, "ym2", table_bytes(ym[2], 256))
  chunk(f, "regs", regs_text())
  f:close()
end

local fields = {}
local function field(port, name)
  local key = port .. "/" .. name
  if not fields[key] then
    local p = assert(m.ioport.ports[port], "no port " .. port)
    fields[key] = assert(p.fields[name], "no field " .. name .. " in " .. port)
  end
  return fields[key]
end

-- CFG.traces: list of { cpu = ":maincpu", from = F, to = F, file = "..." }
local TRACELOG = {
  m6809 = '{tracelog "A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X T=%d ",a,b,x,y,u,s,dp,cc,totalcycles}',
  m6805 = '{tracelog "A=%02X X=%02X S=%02X CC=%02X T=%d ",a,x,s,cc,totalcycles}',
}
local tracing = {}
-- memory log of the traced CPU: "R|W addr data curpc" per data access, in order
local memlog = nil
local function memlog_start(tr)
  local dev = m.devices[tr.cpu]
  local space = dev.spaces["program"]
  local top = tr.cpu == ":mcu:mcu" and 0x7ff or 0xffff
  local f = assert(io.open(tr.file:gsub("%.txt$", "_mem.txt"), "w"))
  local st = dev.state["CURPC"]
  memlog = { f = f, taps = {} }
  memlog.taps[1] = space:install_read_tap(0, top, "memlog_r", function(offset, data, mask)
    f:write(string.format("R %04X %02X %04X\n", offset, data & 0xff, st.value))
  end)
  memlog.taps[2] = space:install_write_tap(0, top, "memlog_w", function(offset, data, mask)
    f:write(string.format("W %04X %02X %04X\n", offset, data & 0xff, st.value))
  end)
end
local function memlog_stop()
  if memlog then
    for _, t in ipairs(memlog.taps) do t:remove() end
    memlog.f:close()
    memlog = nil
  end
end

local function trace(frame)
  for i, tr in ipairs(CFG.traces) do
    local cpu = tr.cpu:sub(2)  -- debugger tags have no leading colon
    if frame == tr.from and not tracing[i] then
      local kind = tr.cpu == ":mcu:mcu" and "m6805" or "m6809"
      -- tracelog evaluates registers of the visible CPU and writes to its trace file,
      -- so only one CPU can be traced per run (mame_capture.py runs MAME once per trace)
      m.debugger.visible_cpu = m.devices[tr.cpu]
      m.debugger:command(string.format("trace %s,%s,noloop,%s", tr.file, cpu, TRACELOG[kind]))
      memlog_start(tr)
      tracing[i] = true
    elseif frame == tr.to and tracing[i] then
      m.debugger:command("trace off," .. cpu)
      memlog_stop()
      tracing[i] = nil
    end
  end
end

local function trace_stop()
  for i, tr in ipairs(CFG.traces) do
    if tracing[i] then m.debugger:command("trace off," .. tr.cpu:sub(2)) end
  end
  memlog_stop()
end

local function frame_input(frame)
  local list = CFG.inputs[frame]
  if list then
    for _, e in ipairs(list) do field(e[1], e[2]):set_value(e[3]) end
  end
end

frame_input(0)
trace(0)
XS_FRAME_SUB = emu.add_machine_frame_notifier(function()
  local frame = screen:frame_number()
  watch_start()
  if CFG.shots[frame] then dump(frame) end
  frame_input(frame)
  trace(frame)
  if frame >= CFG.last then
    trace_stop()
    m:exit()
  end
end)
