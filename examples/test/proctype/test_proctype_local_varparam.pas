program TestProctypeLocalVarparam;
type
    TIntFunc = function(x: integer): integer;
var
    gf: TIntFunc;

function Square(x: integer): integer;
begin
    Square := x * x;
end;

function Cube(x: integer): integer;
begin
    Cube := x * x * x;
end;

procedure UseLocal;
var
    lf: TIntFunc;
begin
    lf := Square;
    writeln('local square: ', lf(5));
end;

procedure Rebind(var pf: TIntFunc);
begin
    pf := Cube;
end;

begin
    UseLocal;
    gf := Square;
    Rebind(gf);
    writeln('after rebind: ', gf(3));
end.
