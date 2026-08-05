program TestRecarrBadparam;
{ Array-of-record PARAMETERS are a deliberate, documented scope cut for
  this pass (see docs/LANGUAGE.md) - passing one by reference would need
  a second by-reference mechanism this feature doesn't build. Must be a
  clear Compile Error, not a crash or a silently wrong parameter. }
type
    TPoint = record
        x, y: integer;
    end;

procedure Foo(pts: array[1..2] of TPoint);
begin
end;

begin
end.
