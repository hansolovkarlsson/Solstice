program TestConstoutBadForwardToVar;
procedure Inner(var v: integer);
begin
    v := 1;
end;
procedure Outer(const c: integer);
begin
    Inner(c);
end;
begin
end.
