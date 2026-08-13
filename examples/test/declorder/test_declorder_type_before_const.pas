program TestDeclorderTypeBeforeConst;

{ A 'type' section, then a LATER 'const' section that doesn't need the
  type - just confirming multiple blocks in an unusual (for this
  compiler, until now) but legal order don't confuse anything. Prints
  the point then the constant. x=1 y=2 n=5 }

type
    TPoint = record
        x, y: integer;
    end;

const
    N = 5;

var
    p: TPoint;

begin
    p.x := 1;
    p.y := 2;
    writeln('x=', p.x, ' y=', p.y, ' n=', N);
end.
