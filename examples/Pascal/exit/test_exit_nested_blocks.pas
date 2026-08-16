program TestExitNestedBlocks;
{ 'exit;' from inside a nested if/for unwinds the WHOLE enclosing function,
  not just the innermost block. Expected output: i=1, i=2, found 3, done
  (not 'unreached'). }
procedure Foo;
var i: integer;
begin
    for i := 1 to 5 do begin
        if i = 3 then begin
            writeln('found ', i);
            exit;
        end;
        writeln('i=', i);
    end;
    writeln('unreached');
end;
begin
    Foo;
    writeln('done');
end.
