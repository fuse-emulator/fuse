-- boot.lua: example Lua automation scenario
-- Copyright (c) 2026 Fredrick Meunier
--
-- This program is free software; you can redistribute it and/or modify
-- it under the terms of the GNU General Public License as published by
-- the Free Software Foundation; either version 2 of the License, or
-- (at your option) any later version.
--
-- This program is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
-- GNU General Public License for more details.
--
-- You should have received a copy of the GNU General Public License along
-- with this program; if not, write to the Free Software Foundation, Inc.,
-- 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

-- Run with: fuse-lua --script examples/lua/boot.lua --output /tmp/evidence -- /path/to/roms
fuse.start {
  machine = "48",
  rom_directory = arg[1] or "roms",
  capture = { screen = true, audio = true },
}
fuse.expect(fuse.run { frames = 100 }.termination == "frames")
fuse.key("space", true)
fuse.run { frames = 2 }
fuse.key("space", false)
if fuse.registers().pc < 0x4000 then
  fuse.run { frames = 50 }
else
  fuse.run { frames = 100 }
end
fuse.expect(#fuse.read_memory(0x4000, 16) == 16)
