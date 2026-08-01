program TestGotoIntoLoop;
{ Documents a deliberate simplification: this compiler doesn't reject a
  goto that jumps into the middle of a structured statement (a while/for/
  if/case body) from outside it, unlike standard Pascal - see
  docs/LANGUAGE.md#goto-and-label. Here the first iteration's 'i := i + 1'
  is skipped entirely (the jump lands past it, straight on the label),
  so the loop prints 0 once, then 1..10 normally. }
label 1;
var
    i: integer;
begin
    i := 0;
    goto 1;
    while i < 10 do begin
        i := i + 1;
        1: writeln(i);
    end;
end.
