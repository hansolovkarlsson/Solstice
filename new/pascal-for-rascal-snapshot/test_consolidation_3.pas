program TestConsolidation3;
type TPoint = record x, y: real; end;
var p1, p2: TPoint;
begin
    { write field-width directly on a record field }
    p1.x := 3.14159;
    writeln('[', p1.x:8:2, ']');    { [    3.14] }

    { whole-record assignment with real fields }
    p1.y := 2.71828;
    p2 := p1;
    writeln(p2.x:0:3, ' ', p2.y:0:3);  { 3.142 2.718 }

    { mutate p1 after copy, confirm p2 independent }
    p1.x := 0.0;
    writeln('p1.x=', p1.x:0:1, ' p2.x=', p2.x:0:3);
end.
