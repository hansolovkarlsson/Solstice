program TestRecordParamBadTypeMismatch;
type
    TPoint = record
        x, y: integer;
    end;
    TPair = record
        a, b: integer;
    end;

var
    q: TPair;

procedure Foo(p: TPoint);
begin
end;

begin
    Foo(q);
end.
