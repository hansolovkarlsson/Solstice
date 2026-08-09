unit MathUtils;

interface

const
  Factor = 3;

var
  CallCount: integer;

function Square(x: integer): integer;
procedure Bump;

implementation

function Square;
begin
  CallCount := CallCount + 1;
  Square := x * x * Factor;
end;

procedure Bump;
begin
  CallCount := CallCount + 1;
end;

end.
