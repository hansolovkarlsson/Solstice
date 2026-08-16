program TestProctypeNilCopy;
type
    TIntProc = procedure(x: integer);
var
    p1, p2: TIntProc;

procedure PrintDouble(x: integer);
begin
    writeln('double: ', x * 2);
end;

begin
    p1 := nil;
    if p1 = nil then
        writeln('p1 is nil')
    else
        writeln('unexpected: p1 not nil');

    p1 := PrintDouble;
    p2 := p1;   { copy, not a call - bare, no '(' }
    p2(10);

    if p1 <> nil then
        writeln('p1 is not nil')
    else
        writeln('unexpected: p1 is nil');
end.
