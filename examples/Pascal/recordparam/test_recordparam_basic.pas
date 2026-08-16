program TestRecordParamBasic;
type
    TPoint = record
        x, y: integer;
    end;

var
    g: TPoint;

function SumPlusOne(p: TPoint): integer;
var
    local: TPoint;
begin
    local.x := p.x + 1;
    local.y := p.y + 1;
    SumPlusOne := local.x + local.y;
end;

procedure Bump(amount: integer);
var
    q: TPoint;
begin
    q.x := amount;
    q.y := amount * 2;
    writeln('Bump: q.x=', q.x, ' q.y=', q.y, ' sum=', SumPlusOne(q));
end;

begin
    g.x := 10;
    g.y := 20;
    writeln('g.x=', g.x, ' g.y=', g.y);
    writeln('SumPlusOne(g)=', SumPlusOne(g));

    { by value: SumPlusOne's own 'local' never touches the caller's record }
    Bump(5);
    Bump(100);
    writeln('g.x still=', g.x, ' g.y still=', g.y);
end.
