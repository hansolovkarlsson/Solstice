program TestIsasNilAs;
type
    TFoo = class
    public
        x: integer;
    end;
var
    p, q: TFoo;
begin
    p := nil;
    q := p as TFoo;
    if q = nil then
        writeln('as on nil yields nil, no exception')
    else
        writeln('BUG: expected nil');
end.
