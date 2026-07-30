program TestStrMutDce;
var used, dead: string;
begin
    used := 'hello';
    used[1] := 'H';
    dead := 'world';
    dead[1] := 'W';
    writeln(used);
end.
