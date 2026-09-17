# P-730: name whatever rewrites the trophy list camera's GX link mask.
#
# The row text is submitted on GX link 62.  Its camera is given a mask that
# includes bit 62, and something then rewrites the mask with every bit moved
# down by 32 (links 62/57/52 -> 30/25/20, i.e. `1 << link` in 32 bits), so
# nothing renders the rows and the names vanish.  This finds the writer.
#
#   gdb -x native/tools/trophy_watch.gdb --args ./build/native/melee
#
# Then play to Trophies -> Trophy List.  When it stops, type:  bt 15
#
# Watches the TOP four bytes only, at +4: bits 32..63 live there on a
# little-endian host, bit 62 among them.  That keeps it a *hardware*
# watchpoint -- watching all 8 bytes on a 32-bit target falls back to a
# software watchpoint, which single-steps and makes the game unplayable.

set pagination off
set confirm off
set breakpoint pending on

break tylist.c:981
commands
  silent
  printf "\n[trophy_watch] camera gobj = %p\n", entry->x4
  printf "[trophy_watch] mask field   = %p\n", &entry->x4->gxlink_prios
  watch -l *(unsigned int *) ((char *) &entry->x4->gxlink_prios + 4)
  printf "[trophy_watch] armed -- play on; when it stops, type:  bt 15\n\n"
  delete breakpoints 1
  continue
end

run
