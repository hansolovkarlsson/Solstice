program TestRecordParamBadWith;
type
    TPoint = record
        x, y: integer;
    end;

procedure Foo(p: TPoint);
begin
    with p do
        writeln(x);
end;

begin
end.
