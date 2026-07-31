program TestRecordParamLocal;
type
    TPoint = record
        x, y: integer;
    end;

var
    g: TPoint;

procedure LocalVsGlobal;
var
    p, r: TPoint;
    total: integer;
begin
    { a local record field as a 'for' loop counter }
    for p.x := 1 to 3 do
        total := total + p.x;
    writeln('total=', total);

    g.x := 7;
    g.y := 8;
    p.x := 7;
    p.y := 8;

    { comparing a local record against a global one }
    if p = g then writeln('p = g') else writeln('p <> g');
    p.y := 9;
    if p = g then writeln('p = g') else writeln('p <> g');

    { whole-record assignment: global -> local, local -> local, local -> global }
    p := g;
    writeln('after p := g: p.x=', p.x, ' p.y=', p.y);

    r := p;
    writeln('after r := p: r.x=', r.x, ' r.y=', r.y);

    r.x := 99;
    g := r;
    writeln('after g := r: g.x=', g.x, ' g.y=', g.y);
end;

begin
    LocalVsGlobal;
end.
