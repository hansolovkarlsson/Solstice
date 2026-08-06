program TestRecarrLocal;
{ A procedure-local array of records reuses the same "hidden global"
  trick a local scalar array already does - shared/persistent across
  every call, including recursive ones. Expected output (both calls
  identical, proving the second call's writes started from the SAME
  storage the first call left behind - and correctly overwrote it):
  call1: 1,2 3,4
  call2: 1,2 3,4 }
type
    TPoint = record
        x, y: integer;
    end;

procedure Fill(tag: string);
var
    pts: array[1..2] of TPoint;
begin
    pts[1].x := 1;
    pts[1].y := 2;
    pts[2].x := 3;
    pts[2].y := 4;
    writeln(tag, ': ', pts[1].x, ',', pts[1].y, ' ', pts[2].x, ',', pts[2].y);
end;

begin
    Fill('call1');
    Fill('call2');
end.
