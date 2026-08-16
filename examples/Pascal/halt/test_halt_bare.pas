program TestHaltBare;
{ Bare 'halt;' terminates the WHOLE PROGRAM immediately, from anywhere -
  including out of a loop, unlike 'exit'/'break'. Expected output: i=1,
  i=2 (nothing after, including 'unreached'). }
var i: integer;
begin
    for i := 1 to 5 do begin
        if i = 3 then halt;
        writeln('i=', i);
    end;
    writeln('unreached');
end.
