program TestDeclorderVarTypeVar;

{ A 'var', then a 'type' used by a SECOND 'var' block, confirming 'var'
  also repeats/interleaves, not just 'const'/'type'. count=3 x=7 y=9 }

var
    count: integer;

type
    TPoint = record
        x, y: integer;
    end;

var
    p: TPoint;

begin
    count := 3;
    p.x := 7;
    p.y := 9;
    writeln('count=', count, ' x=', p.x, ' y=', p.y);
end.
