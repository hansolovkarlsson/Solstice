program TestGotoForward;
procedure P; forward;

procedure P;
label 1;
var
    i: integer;
begin
    i := 0;
    1: writeln(i);
    i := i + 1;
    if i < 3 then goto 1;
end;

begin
    P;
end.
