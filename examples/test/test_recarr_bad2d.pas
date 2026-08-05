program TestRecarrBad2d;
{ 2D (and N-D) arrays of records are a deliberate, documented scope cut
  for this pass (see docs/LANGUAGE.md) - only 1D is supported so far.
  Must be a clear Compile Error. }
type
    TPoint = record
        x, y: integer;
    end;
var
    grid: array[1..2, 1..2] of TPoint;
begin
end.
