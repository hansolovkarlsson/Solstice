program TestVarParamBadRecord;
type
    TPoint = record
        x, y: integer;
    end;

procedure Foo(var p: TPoint);
begin
end;

begin
end.
