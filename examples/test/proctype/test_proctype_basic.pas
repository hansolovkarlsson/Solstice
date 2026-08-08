program TestProctypeBasic;
type
    TIntProc = procedure(x: integer);
    TIntFunc = function(x: integer): integer;
var
    p: TIntProc;
    f: TIntFunc;

procedure PrintDouble(x: integer);
begin
    writeln('double: ', x * 2);
end;

function Square(x: integer): integer;
begin
    Square := x * x;
end;

begin
    p := PrintDouble;
    p(21);

    f := Square;
    writeln('square: ', f(6));
end.
