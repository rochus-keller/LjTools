----------------------------------------------------------------------------
-- Verbose mode of the LuaJIT compiler.
--
-- Copyright (C) 2005-2020 Mike Pall. All rights reserved.
-- Released under the MIT license. See Copyright Notice in luajit.h
----------------------------------------------------------------------------
-- modified version 2020 by me@rochus-keller.ch
----------------------------------------------------------------------------

-- Cache some library functions and objects.
local jit = require("jit")
assert(jit.version_num == 20005, "LuaJIT core/library version mismatch")
local jutil = require("jit.util")
local vmdef = require("jit.vmdef")
local funcinfo, traceinfo = jutil.funcinfo, jutil.traceinfo
local type, format = type, string.format
local stdout, stderr = io.stdout, io.stderr

-- Active flag and output file handle.
local active, out

------------------------------------------------------------------------------

local startloc, startex
local prettyTraceLoc = _prettyTraceLoc

local function fmtfunc(func, pc)
  local fi = funcinfo(func, pc)
  if fi.loc then
    return prettyTraceLoc(fi.loc, fi.source, fi.linedefined )
  elseif fi.ffid then
    return vmdef.ffnames[fi.ffid]
  elseif fi.addr then
    return format("C:%x", fi.addr)
  else
    return "(?)"
  end
end

-- Format trace error message.
local function fmterr(err, info)
  if type(err) == "number" then
    if type(info) == "function" then info = fmtfunc(info) end
    err = format(vmdef.traceerr[err], info)
  end
  return err
end

-- Dump trace states.
local function dump_trace(what, tr, func, pc, otr, oex)
  if what == "start" then
    startloc = fmtfunc(func, pc)
    startex = otr and "("..otr.."/"..oex..") " or ""
  else
    if what == "abort" then
      local loc = fmtfunc(func, pc)
      if loc ~= startloc then
		out:write(format("[TRACE fail --- %s%s -- %s at %s]\n", startex, startloc, fmterr(otr, oex), loc))
      else
		out:write(format("[TRACE fail --- %s%s -- %s]\n", startex, startloc, fmterr(otr, oex)))
      end
    elseif what == "stop" then
      local info = traceinfo(tr)
      local link, ltype = info.link, info.linktype
      if ltype == "interpreter" then
		out:write(format("[TRACE %3s %s%s -- fallback to interpreter]\n", tr, startex, startloc))
      elseif link == tr or link == 0 then
		out:write(format("[TRACE %3s %s%s %s]\n", tr, startex, startloc, ltype))
      elseif ltype == "root" then
		out:write(format("[TRACE %3s %s%s -> %d]\n", tr, startex, startloc, link))
      else
		out:write(format("[TRACE %3s %s%s -> %d %s]\n", tr, startex, startloc, link, ltype))
      end
    else
      out:write(format("[TRACE %s]\n", what))
    end
    out:flush()
  end
end

------------------------------------------------------------------------------

out = stderr
jit.attach(dump_trace, "trace")
active = true


