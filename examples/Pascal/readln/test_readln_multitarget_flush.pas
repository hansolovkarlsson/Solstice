program TestReadlnMultitargetFlush;
{ A non-flushing numeric read (a, part of a multi-target readln) leaves
  the read position right before its own trailing newline, not at the
  start of the next line. The string target right after it must still
  correctly land on the REAL next line, not read an empty leftover
  line. Expected: a=1 b=2 s=third line }
var
    a, b: integer;
    s: string;
begin
    readln(a, b, s);
    writeln('a=', a, ' b=', b, ' s=', s);
end.
