program TestSetDCE;
var
    s: set of 0..9;
    x: integer;
begin
    { s is never read after this assignment, but the set constructor's
      implicit '1 << x' bounds check is still a runtime side effect that
      dead-code elimination must not silently drop - matching the same
      class of bug already fixed for subrange range-checks. }
    x := 50;
    s := [x];
    writeln('unreachable');
end.
