program TestRecarrDce;
{ Regression test (same class of bug fixed earlier for plain array/string-
  index assignments - see docs/ROADMAP.md's "Three-or-more-dimensional
  arrays" entry): an out-of-range field write's runtime bounds check must
  never be silently dropped by dead-code elimination just because the
  array is otherwise unread. 'bad' is never read anywhere else in the
  program, so without the NODE_ARRAY_RECORD_FIELD_ASSIGN DCE guard this
  assignment would be wrongly swept away as dead, and the program would
  exit 0 instead of aborting with a Runtime Error. }
type
    TPoint = record
        x, y: integer;
    end;
var
    bad: array[1..3] of TPoint;
begin
    bad[999].x := 1;
    writeln('unreachable');
end.
