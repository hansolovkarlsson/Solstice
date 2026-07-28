program ProcRecursive;
var
    n: integer;

procedure countdown;
begin
    writeln(n);
    if n > 0 then begin
        n := n - 1;
        countdown;
    end;
end;

begin
    n := 5;
    countdown;
end.
