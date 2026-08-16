program TestRecordParamBadDup;
type
    TPoint = record
        x, y: integer;
    end;

procedure Foo(p: TPoint; p: integer);
begin
end;

begin
end.
