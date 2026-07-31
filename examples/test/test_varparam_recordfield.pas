program TestVarParamRecordField;
type
    TPoint = record
        x, y: integer;
    end;
var
    p: TPoint;

procedure Inc1(var v: integer);
begin
    v := v + 1;
end;

procedure TestLocalField;
var
    q: TPoint;
begin
    q.x := 10;
    Inc1(q.x);
    writeln('local q.x=', q.x);
end;

begin
    p.x := 10;
    Inc1(p.x);
    writeln('global p.x=', p.x);

    { a with-target's field can be a 'var' argument too }
    with p do
        Inc1(y);
    writeln('global p.y=', p.y);

    TestLocalField;
end.
