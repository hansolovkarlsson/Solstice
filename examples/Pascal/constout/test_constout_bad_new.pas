program TestConstoutBadNew;
type
    TFoo = class
        v: integer;
    end;
procedure P(const c: TFoo);
begin
    new(c);
end;
begin
end.
