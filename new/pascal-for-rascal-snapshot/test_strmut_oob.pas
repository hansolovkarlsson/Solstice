program TestStrMutOob;
var s: string;
begin
    s := 'hi';
    s[5] := 'x';
    writeln(s);
end.
