program TestLocalReadln;

procedure readAll;
var i: integer; b: boolean; r: real; s: string; c: char;
begin
    readln(i);
    readln(b);
    readln(r);
    readln(s);
    readln(c);
    writeln('i=', i, ' b=', b, ' r=', r, ' s=', s, ' c=', c);
end;

begin
    readAll;
end.
