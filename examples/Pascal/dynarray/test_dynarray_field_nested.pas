program TestDynArrayFieldNested;
type
    TInner = record
        data: array of integer;
    end;
    TOuter = record
        inner: TInner;
        tag: integer;
    end;
var
    o: TOuter;
begin
    o.tag := 5;
    o.inner.data := [1, 2, 3];
    writeln(o.tag, ' ', Length(o.inner.data));   { 5 3 }
    writeln(o.inner.data[0], ' ', o.inner.data[2]);  { 1 3 }
end.
