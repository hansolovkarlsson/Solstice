program TestRecarrBadindex;
{ Out-of-bounds index on an array-of-records must be a clean Runtime
  Error (via vm_record_array_offset() in vm.c), not a crash or silent
  memory corruption. }
type
    TPoint = record
        x, y: integer;
    end;
var
    pts: array[1..3] of TPoint;
    i: integer;
begin
    i := 5;
    pts[i].x := 1;
end.
