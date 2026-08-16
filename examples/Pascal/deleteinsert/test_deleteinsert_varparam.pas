program TestDeleteinsertVarparam;
var
    x: string;

procedure Trim(var s: string);
begin
    Delete(s, 1, 3);
    Insert('[', s, 1);
end;

begin
    x := 'xxxHello';
    Trim(x);
    writeln(x);
end.
