program TestConstTypedRecordInterleave;

{ The actual motivating example from the roadmap's own "record typed
  constants" bullet, now end to end: a type declared BEFORE a const
  that's typed AS it - only possible because const/type/var section
  interleaving shipped first. x=3 y=4 dist_sq=25 }

type
    TPoint = record
        x, y: integer;
    end;

const
    Origin: TPoint = (x: 3; y: 4);

var
    distSq: integer;

begin
    distSq := Origin.x * Origin.x + Origin.y * Origin.y;
    writeln('x=', Origin.x, ' y=', Origin.y, ' dist_sq=', distSq);
end.
